#include "workspacecontroller.h"
#include "gitqueries.h"
#include "repositorycache.h"
#include "repositorypaths.h"
using RepositoryPaths::gitMetadataDirectory;
#include "commitcommandbuilder.h"

#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QScopedValueRollback>
#include <QSettings>
#include <QTextStream>
#include <QUrl>
#include <algorithm>
#include <utility>

namespace {
QString normalizedPath(QString path)
{
    path = QDir::cleanPath(path.trimmed());
    while (path.startsWith(QStringLiteral("./")))
        path.remove(0, 2);
    return path;
}

// Parses `git log --format=%H%x00%B%x00 <target>..HEAD`.
QString untrackedAiPreview(const QString &repository, const QStringList &paths)
{
    QString preview;
    int remaining = 4096;
    for (const QString &path : paths) {
        if (remaining <= 0) break;
        const QFileInfo info(QDir(repository).filePath(path));
        if (!info.isFile() || info.isSymLink() || info.size() > 256 * 1024) continue;
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) continue;
        const QByteArray bytes = file.read(qMin<qint64>(2048, remaining));
        if (bytes.contains('\0')) continue;
        const QString content = QString::fromUtf8(bytes);
        preview += QStringLiteral("\n--- /dev/null\n+++ b/%1\n").arg(path);
        for (const QString &line : content.split(QLatin1Char('\n')))
            preview += QLatin1Char('+') + line + QLatin1Char('\n');
        if (file.size() > bytes.size()) preview += QStringLiteral("[新增文件内容已截断]\n");
        remaining -= bytes.size();
    }
    return preview;
}
}

WorkspaceController::WorkspaceController(QObject *parent)
    : QObject(parent)
    , m_projectManager(this, this)
    , m_batchPush(*this, m_gerrit)
    , m_revisions(*this)
    , m_aiWorkflow(*this, m_commitAi)
    , m_hooks(*this, m_gerrit)
    , m_reviewPush(*this, m_gerrit, m_hooks)
    , m_workingTree(*this)
    , m_sync(*this)
{
    m_proxy.setSourceModel(&m_model);
    m_proxy.sort(0);

    connect(&m_gitService, &GitService::busyChanged,
            this, &WorkspaceController::busyChanged);
    connect(&m_gitService, &GitService::activeTaskChanged,
            this, &WorkspaceController::busyChanged);
    connect(&m_gitService, &GitService::commandFinished,
            this, &WorkspaceController::onGitCommandFinished);
    connect(&m_gitService, &GitService::outputReceived,
            this, &WorkspaceController::onGitOutputReceived);
    connect(&m_gitService, &GitService::logMessage,
            this, &WorkspaceController::appendConsole);
    connect(&m_gitService, &GitService::dependentCommandsCanceled, this, [this](quint64) {
        // Update history pending state if a canceled command affected history
        m_historyPending = false;
    });

    connect(&m_commitAi, &CommitMessageAiService::candidateReady,
            this, [this](const QString &) {
        appendConsole(tr("✓ AI 提交说明已生成\n"));
    });
    connect(&m_commitAi, &CommitMessageAiService::errorOccurred,
            this, [this](const QString &err) {
        reportError(tr("AI 说明生成失败"), err, ErrorType::Ai);
    });

    connect(&m_repositoryWatcher, &RepositoryWatcher::repositoryChanged,
            this, [this] {
                if (!busy() && !m_refreshPending) {
                    requestActiveRefresh();
                }
            });

    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(250);
    connect(&m_refreshTimer, &QTimer::timeout, this, [this] {
        if (!m_refreshPending || busy() || m_activeIndex < 0) return;
        m_refreshPending = false;
        enqueueRepositoryRefresh(m_activeIndex, true);
    });
    connect(this, &WorkspaceController::busyChanged, this, [this] {
        m_repositoryWatcher.setSuspended(busy());
        if (m_refreshPending && !busy() && !m_refreshTimer.isActive())
            m_refreshTimer.start();
    });

    for (auto *workflow : QList<RepositoryWorkflow *>{&m_batchPush, &m_revisions, &m_aiWorkflow, &m_hooks, &m_reviewPush, &m_workingTree, &m_sync})
        bindWorkflow(*workflow);
    connect(&m_sync, &SynchronizationWorkflow::rescanRequested, this, &WorkspaceController::scanWorkspace);
    connect(&m_workingTree, &WorkingTreeWorkflow::stashChanged, this, &WorkspaceController::stashListChanged);
    connect(&m_batchPush, &BatchPushWorkflow::changed, this, &WorkspaceController::batchPushChanged);
    connect(&m_batchPush, &BatchPushWorkflow::targetConfirmed, this,
        [this](const QString &path, const QString &remote, const QString &branch) {
            if (path != selectedPath() || m_activeIndex < 0) return;
            m_repositoryConfig.remote = remote;
            m_repositoryConfig.targetBranch = branch;
            m_repositoryConfig.targetBranchConfirmed = true;
            emit settingsChanged();
        });
    connect(&m_revisions, &RevisionWorkflow::commitMessageReady, this, &WorkspaceController::commitMessageReady);
    connect(&m_hooks, &HookWorkflow::statusChanged, this, &WorkspaceController::hasCommitHookChanged);
    connect(&m_hooks, &HookWorkflow::busyChanged, this, &WorkspaceController::busyChanged);
    connect(&m_hooks, &HookWorkflow::resumeCommitRequested, this, &WorkspaceController::commitActive);
    connect(&m_reviewPush, &ReviewPushWorkflow::pushStatusChanged, this, &WorkspaceController::pushStatusChanged);
    connect(&m_reviewPush, &ReviewPushWorkflow::lastGerritReviewUrlChanged, this, &WorkspaceController::lastGerritReviewUrlChanged);

    loadSettings();
    m_projectManager.updateCurrentIndexForPath(m_workspacePath);
    connect(this, &WorkspaceController::workspacePathChanged, this, [this] {
        m_projectManager.updateCurrentIndexForPath(m_workspacePath);
    });
}

void WorkspaceController::setWorkspacePath(const QString &path)
{
    const QString cleanPath = QDir::cleanPath(path.trimmed());
    if (m_workspacePath == cleanPath)
        return;
    if (m_reviewPush.state() == 1 || m_batchPush.running()) {
        reportError(tr("无法切换项目"), tr("Gerrit 推送尚未结束，请等待结果或取消任务。"));
        return;
    }
    cancelScheduledRefresh();
    m_repositoryWatcher.setActiveRepository(QString());

    // Invalidate old results before changing the base path. In particular, a
    // completed discard preflight must never schedule writes in the new workspace.
    ++m_generation;
    m_aiWorkflow.reset();
    m_reviewPush.reset();
    m_batchPush.resetBatchPush();
    m_hooks.reset();
    m_reviewPush.clearRepair();
    m_gitService.setGeneration(m_generation);
    m_gitService.clearQueue();
    m_activeIndex = -1;
    m_model.resetRepositories({}, {});
    m_changedFiles.clear();
    m_groupedChanges.clear();
    m_detailsCached = false;
    m_selectedFile.clear();
    setDiffText({});
    m_historyEntries.clear();
    m_historyRequested = false;
    m_historyPending = false;
    m_historyLoaded = false;
    m_availableBranches.clear();
    m_remoteBranches.clear();
    m_remoteTrackingBranches.clear();
    m_remoteBranchesLoaded = false;
    m_revisionFiles.clear();
    m_revisionFile.clear();
    m_revisionDiff.clear();
    m_selectedRevision.clear();
    m_workingTree.reset();
    m_workspacePath = cleanPath;

    loadRepositorySettings();
    m_ignoredRepositories = WorkspaceSettings::loadIgnoredRepositories(m_workspacePath);

    emit workspacePathChanged();
    emit stashListChanged();
    emit ignoredRepositoriesChanged();
    emit selectedRepositoryChanged();
    emit detailsTextChanged();
    emit diffTextChanged();
    emit historyTextChanged();
    emit branchesChanged();
    emit revisionDiffChanged();
    emit summaryChanged();
    emit selectionChanged();
    emit busyChanged();
    saveSettings();
}

QString WorkspaceController::selectedName() const
{
    return m_model.nameAt(m_activeIndex);
}

QString WorkspaceController::selectedPath() const
{
    return m_model.pathAt(m_activeIndex);
}

QString WorkspaceController::selectedBranch() const
{
    return m_model.branchAt(m_activeIndex);
}

void WorkspaceController::setRemote(const QString &value)
{
    if (m_repositoryConfig.remote == value.trimmed()) return;
    m_repositoryConfig.remote = value.trimmed(); emit settingsChanged(); saveSettings();
}

void WorkspaceController::setTargetBranch(const QString &value)
{
    if (m_repositoryConfig.targetBranch == value.trimmed() && m_repositoryConfig.targetBranchConfirmed) return;
    m_repositoryConfig.targetBranch = value.trimmed();
    m_repositoryConfig.targetBranchConfirmed = true;
    emit settingsChanged();
    saveSettings();
}

void WorkspaceController::setTopic(const QString &value)
{
    if (m_repositoryConfig.topic == value.trimmed()) return;
    m_repositoryConfig.topic = value.trimmed(); emit settingsChanged(); saveSettings();
}

void WorkspaceController::setReviewers(const QString &value)
{
    const QString list = GerritService::splitList(value).join(QStringLiteral(", "));
    if (m_repositoryConfig.reviewers == list) return;
    m_repositoryConfig.reviewers = list; emit settingsChanged(); saveSettings();
}

void WorkspaceController::setCc(const QString &value)
{
    const QString list = GerritService::splitList(value).join(QStringLiteral(", "));
    if (m_repositoryConfig.cc == list) return;
    m_repositoryConfig.cc = list; emit settingsChanged(); saveSettings();
}

void WorkspaceController::setPullRebase(bool value)
{
    if (m_pullRebase == value) return;
    m_pullRebase = value;
    QSettings().setValue(QStringLiteral("pullRebase"), value);
    emit pullRebaseChanged();
}

void WorkspaceController::setRequireChangeIdHook(bool required)
{
    if (busy() || m_activeIndex < 0 || m_repositoryConfig.requireChangeIdHook == required) return;
    m_repositoryConfig.requireChangeIdHook = required;
    emit settingsChanged();
    saveSettings();
}

void WorkspaceController::setGerritHost(const QString &value)
{
    if (m_repositoryConfig.gerritHost == value.trimmed()) return;
    m_repositoryConfig.gerritHost = value.trimmed(); emit settingsChanged(); saveSettings();
}

void WorkspaceController::setGerritUser(const QString &value)
{
    if (m_repositoryConfig.gerritUser == value.trimmed()) return;
    m_repositoryConfig.gerritUser = value.trimmed(); emit settingsChanged(); saveSettings();
}

void WorkspaceController::setGerritPort(int value)
{
    if (m_repositoryConfig.gerritPort == value) return;
    m_repositoryConfig.gerritPort = value; emit settingsChanged(); saveSettings();
}

void WorkspaceController::setIgnoreWhitespace(bool value)
{
    if (m_ignoreWhitespace == value) return;
    m_ignoreWhitespace = value;
    emit ignoreWhitespaceChanged();
    refreshSelectedFile();
}

void WorkspaceController::loadSettings()
{
    QSettings settings;
    m_workspacePath = settings.value(QStringLiteral("workspace")).toString();
    m_pullRebase = settings.value(QStringLiteral("pullRebase"), false).toBool();

    WorkspaceSettings::migrateLegacySettings(m_workspacePath);
    loadRepositorySettings();
    m_ignoredRepositories = WorkspaceSettings::loadIgnoredRepositories(m_workspacePath);
}

void WorkspaceController::saveSettings() const
{
    QSettings settings;
    settings.setValue(QStringLiteral("workspace"), m_workspacePath);
    if (m_activeIndex >= 0) {
        WorkspaceSettings::saveRepositoryConfig(m_workspacePath, selectedPath(), m_repositoryConfig);
    }
    WorkspaceSettings::saveIgnoredRepositories(m_workspacePath, m_ignoredRepositories);
}

void WorkspaceController::loadRepositorySettings()
{
    m_lastCommitMessage.clear();
    m_branchQueryRemote.clear();
    m_pushRemoteUrl.clear();
    ++m_branchQuery;
    m_repositoryConfig = WorkspaceSettings::loadRepositoryConfig(m_workspacePath, selectedPath());
    emit settingsChanged();
}

void WorkspaceController::loadRepositoryCache()
{
    QSettings settings;
    m_cachedRepositoryPath = settings.value(QStringLiteral("cacheSelection/")
        + RepositoryCache::key(m_workspacePath)).toString();
    if (m_model.indexOfPath(m_cachedRepositoryPath) < 0) m_cachedRepositoryPath.clear();
    restoreRepositoryCache(m_cachedRepositoryPath);
    m_cachedWorkspacePath = m_model.indexOfPath(m_cachedRepositoryPath) >= 0 ? m_workspacePath : QString();
}

void WorkspaceController::restoreRepositoryCache(const QString &repositoryPath)
{
    const auto data = RepositoryCache::load(m_workspacePath, repositoryPath, RepositoryCache::Section::Files);
    const int repositoryIndex = m_model.indexOfPath(repositoryPath);
    // Cached or absent, no restored snapshot is authoritative until Git validates it.
    m_detailsCached = true;
    m_changedFiles = ownRepositoryFiles(repositoryIndex, data.value(QStringLiteral("files")).toList());
    rebuildGroupedChanges();
    if (repositoryIndex >= 0 && data.contains(QStringLiteral("files"))) {
        const auto summary = m_model.summaryAt(repositoryIndex);
        m_model.updateStatus(repositoryIndex, m_model.branchAt(repositoryIndex), m_changedFiles.size(),
            summary.value(QStringLiteral("ahead")).toInt(), summary.value(QStringLiteral("behind")).toInt(),
            QStringLiteral("cached"));
    }
    m_historyEntries.clear();
    const auto branches = RepositoryCache::load(m_workspacePath, repositoryPath, RepositoryCache::Section::Branches);
    m_availableBranches = branches.value(QStringLiteral("branches")).toStringList();
    m_remoteBranches.clear();
    m_remoteTrackingBranches = branches.value(QStringLiteral("remoteTrackingBranches")).toStringList();
    m_remoteBranchesLoaded = false;
    setDiffText({});
    m_selectedFile.clear();
}

void WorkspaceController::saveFileCache(int index, const QVariantList &files)
{
    if (index < 0) return;
    const QString path = m_model.pathAt(index);
    if (!RepositoryCache::save(m_workspacePath, path, RepositoryCache::Section::Files,
                               {{QStringLiteral("files"), files}})) {
        appendConsole(tr("文件缓存未保存（写入失败或超过大小限制）：%1").arg(path));
        return;
    }
    if (index == m_activeIndex) {
        QSettings settings;
        settings.setValue(QStringLiteral("cacheSelection/") + RepositoryCache::key(m_workspacePath), path);
    }
}

void WorkspaceController::saveBranchCache()
{
    if (m_activeIndex < 0) return;
    if (!RepositoryCache::save(m_workspacePath, selectedPath(), RepositoryCache::Section::Branches,
        {{QStringLiteral("branches"), m_availableBranches},
         {QStringLiteral("remoteTrackingBranches"), m_remoteTrackingBranches}}))
        appendConsole(tr("分支缓存未保存：%1").arg(selectedPath()));
}

void WorkspaceController::saveHistoryCache()
{
    if (m_activeIndex < 0) return;
    // Only the first page is useful for a quick preview; pagination stays live.
    if (!RepositoryCache::save(m_workspacePath, selectedPath(), RepositoryCache::Section::History,
        {{QStringLiteral("entries"), m_historyEntries.mid(0, 30)}}))
        appendConsole(tr("历史缓存未保存：%1").arg(selectedPath()));
}

QString WorkspaceController::loadCommitDraft(const QString &repoPath, const QString &workspacePath) const
{
    const QString ws = workspacePath.isEmpty() ? m_workspacePath : workspacePath;
    return WorkspaceSettings::loadDraft(ws, repoPath.isEmpty() ? selectedPath() : repoPath);
}

void WorkspaceController::saveCommitDraft(const QString &repoPath, const QString &text, const QString &workspacePath)
{
    const QString ws = workspacePath.isEmpty() ? m_workspacePath : workspacePath;
    WorkspaceSettings::saveDraft(ws, repoPath.isEmpty() ? selectedPath() : repoPath, text);
}

void WorkspaceController::setViewMode(int mode)
{
    if (m_proxy.filterMode() == mode)
        return;
    m_proxy.setFilterMode(mode);
    emit viewModeChanged();
}

void WorkspaceController::scanWorkspace()
{
    if (m_reviewPush.state() == 1 || m_batchPush.running()) {
        reportError(tr("无法重新载入项目"), tr("Gerrit 推送尚未结束，请等待结果或取消任务。"));
        return;
    }
    cancelScheduledRefresh();
    m_hooks.reset();
    m_reviewPush.clearRepair();
    m_aiWorkflow.reset();
    m_reviewPush.reset();
    const QFileInfo root(m_workspacePath);
    if (!root.isDir()) {
        reportError(tr("工作区不可用"), tr("目录不存在：%1").arg(m_workspacePath));
        return;
    }

    QStringList paths;
    QFile projectList(QDir(m_workspacePath).filePath(QStringLiteral(".repo/project.list")));
    if (projectList.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream stream(&projectList);
        while (!stream.atEnd()) {
            const QString path = normalizedPath(stream.readLine());
            if (!path.isEmpty() && QFileInfo(QDir(m_workspacePath).filePath(path)).isDir())
                paths.append(path);
        }
    } else if (QFileInfo(QDir(m_workspacePath).filePath(QStringLiteral(".git"))).exists()) {
        paths.append(QStringLiteral("."));
    } else if (QFileInfo(QDir(m_workspacePath).filePath(QStringLiteral(".repo"))).isDir()) {
        // repo init creates .repo before repo sync creates project.list.
        appendConsole(tr("项目已初始化，但尚未同步子仓库。\n"));
    } else {
        reportError(tr("无法识别工作区"),
                             tr("没有找到 .repo/project.list 或 .git。"));
        return;
    }

    ++m_generation;
    m_gitService.setGeneration(m_generation);
    m_gitService.clearQueue();
    m_batchPush.resetBatchPush();
    if (isRepoWorkspace()) {
        GitCommand command;
        command.program = QStringLiteral("repo");
        command.arguments = {QStringLiteral("list"), QStringLiteral("-p")};
        command.workingDirectory = m_workspacePath;
        command.workspacePath = m_workspacePath;
        command.paths = paths; // Local fallback when repo is unavailable or the Manifest is invalid.
        command.kind = GitCommandKind::RepoList;
        command.title = tr("读取 Repo 项目列表");
        command.timeoutMilliseconds = 15000;
        enqueue(command);
        return;
    }
    applyRepositoryPaths(paths);
}

void WorkspaceController::discoverManifestRepository(const QStringList &paths)
{
    const QString directory = QDir(m_workspacePath).filePath(QStringLiteral(".repo/manifests"));
    if (!QFileInfo(directory).isDir()) {
        applyRepositoryPaths(paths);
        return;
    }
    GitCommand command;
    command.arguments = {QStringLiteral("rev-parse"), QStringLiteral("--show-toplevel")};
    command.workingDirectory = directory;
    command.workspacePath = m_workspacePath;
    command.paths = paths;
    command.kind = GitCommandKind::ManifestRepository;
    command.title = tr("识别 Manifest Git 工作目录");
    command.timeoutMilliseconds = 5000;
    enqueue(command);
}

void WorkspaceController::applyRepositoryPaths(QStringList paths)
{
    const QString root = QFileInfo(m_workspacePath).canonicalFilePath();
    QStringList checked;
    for (const QString &rawPath : std::as_const(paths)) {
        const QString path = normalizedPath(rawPath);
        const QString directory = QDir(m_workspacePath).filePath(path);
        const QString canonical = QFileInfo(directory).canonicalFilePath();
        if ((canonical == root || canonical.startsWith(root + QLatin1Char('/')))
            && QFileInfo(QDir(directory).filePath(QStringLiteral(".git"))).exists())
            checked.append(path);
    }
    paths = checked;
    paths.removeDuplicates();
    paths.sort(Qt::CaseInsensitive);
    m_gitService.clearQueue();
    m_model.resetRepositories(paths, m_ignoredRepositories);
    for (int row = 0; row < m_model.rowCount(); ++row) {
        const auto summary = RepositoryCache::load(m_workspacePath, m_model.pathAt(row), RepositoryCache::Section::Summary);
        m_model.restoreSummary(row, summary);
    }
    m_historyRequested = false;
    m_historyPending = false;
    m_historyLoaded = false;
    m_unbornRepositories.clear();
    ++m_revisionFilesRequest;
    m_revisionFiles.clear();
    m_revisionFile.clear();
    ++m_revisionRequest;
    m_revisionDiff.clear();
    m_selectedRevision.clear();
    emit revisionDiffChanged();
    m_proxy.sort(0);
    loadRepositoryCache();
    const bool useCache = m_cachedWorkspacePath == m_workspacePath
        && !m_cachedRepositoryPath.isEmpty();
    m_activeIndex = useCache ? m_model.indexOfPath(m_cachedRepositoryPath) : -1;
    loadRepositorySettings();
    if (m_activeIndex < 0) {
        m_changedFiles.clear();
        m_groupedChanges.clear();
        setDiffText({});
        m_historyEntries.clear();
        m_availableBranches.clear();
        m_remoteBranches.clear();
        m_remoteTrackingBranches.clear();
        m_remoteBranchesLoaded = false;
        m_repositoryWatcher.setActiveRepository(QString());
    } else {
        m_repositoryWatcher.setActiveRepository(absoluteRepositoryPath(m_activeIndex));
    }
    emit selectedRepositoryChanged();
    emit detailsTextChanged();
    emit diffTextChanged();
    emit historyTextChanged();
    emit branchesChanged();
    emit summaryChanged();
    emit selectionChanged();
    appendConsole(tr("已载入 %1 个仓库 · %2").arg(paths.size()).arg(m_workspacePath));
    saveSettings();
    if (m_activeIndex >= 0) {
        appendConsole(tr("已恢复缓存 · %1").arg(m_model.nameAt(m_activeIndex)));
        updateCommitHookStatus();
        enqueueRepositoryRefresh(m_activeIndex, true);
    } else if (m_proxy.rowCount() > 0) {
        activateRepository(0);
    }
}

void WorkspaceController::refreshAll()
{
    for (int row = 0; row < m_model.rowCount(); ++row) {
        enqueueGit(row,
                   GitQueries::summary(),
                   tr("检查 %1").arg(m_model.nameAt(row)),
                   row == m_activeIndex ? GitCommandKind::Details : GitCommandKind::Status);
    }
}

void WorkspaceController::activateRepository(int index)
{
    const int sourceIndex = m_proxy.sourceRow(index);
    if (sourceIndex < 0 || sourceIndex == m_activeIndex)
        return;
    m_hooks.reset();
    m_reviewPush.clearRepair();
    cancelScheduledRefresh();
    m_activeIndex = sourceIndex;
    m_aiWorkflow.reset();
    m_repositoryWatcher.setActiveRepository(absoluteRepositoryPath(sourceIndex));
    ++m_historyRequest;
    loadRepositorySettings();
    m_historyRequested = false;
    m_historyPending = false;
    m_historyLoaded = false;
    m_detailsCached = false;
    ++m_revisionFilesRequest;
    m_revisionFiles.clear();
    m_revisionFile.clear();
    ++m_revisionRequest;
    m_revisionDiff.clear();
    m_selectedRevision.clear();
    emit revisionDiffChanged();
    m_selectedFile.clear();
    m_selectedFileStaged = false;
    m_workingTree.reset();
    emit stashListChanged();
    ++m_diffRequest;
    restoreRepositoryCache(m_model.pathAt(sourceIndex));
    emit detailsTextChanged();
    emit diffTextChanged();
    emit historyTextChanged();
    emit selectedRepositoryChanged();
    emit summaryChanged();
    emit branchesChanged();
    updateCommitHookStatus();
    refreshSelectedFile();
    enqueueGit(sourceIndex, GitQueries::summary(),
               tr("读取改动"), GitCommandKind::Details);
    enqueueGit(sourceIndex,
               GitQueries::branches(),
               tr("读取分支"), GitCommandKind::Branches);
}

void WorkspaceController::refreshActive()
{
    if (m_activeIndex >= 0 && !busy()) {
        cancelScheduledRefresh();
        enqueueRepositoryRefresh(m_activeIndex, true);
    }
}

void WorkspaceController::requestActiveRefresh()
{
    if (m_activeIndex < 0) return;
    m_refreshPending = true;
    m_detailsCached = true;
    emit detailsTextChanged();
    m_refreshTimer.start();
}

void WorkspaceController::cancelScheduledRefresh()
{
    m_refreshTimer.stop();
    m_refreshPending = false;
}

void WorkspaceController::loadHistory()
{
    if (m_activeIndex < 0) return;
    const bool restoreCache = !m_historyRequested;
    m_historyRequested = true;
    if (m_historyPending || m_historyLoaded) return;
    m_historyPending = true;
    if (restoreCache) {
        const auto cached = RepositoryCache::load(m_workspacePath, selectedPath(), RepositoryCache::Section::History);
        m_historyEntries = cached.value(QStringLiteral("entries")).toList().mid(0, 30);
        emit historyTextChanged();
    }
    ++m_historyRequest;
    m_historyAnchor.clear();
    enqueueGit(m_activeIndex, GitQueries::summary(), tr("检查历史状态"), GitCommandKind::Status);
    enqueueGit(m_activeIndex, GitQueries::history(), tr("读取历史"), GitCommandKind::History);
}

void WorkspaceController::loadMoreHistory()
{
    if (m_activeIndex < 0 || m_historyPending || !historyHasMore() || m_historyAnchor.isEmpty()) return;
    m_historyPending = true;
    GitCommand command;
    command.program = QStringLiteral("git");
    command.arguments = GitQueries::history(30, historyEntries().size(), m_historyAnchor);
    command.workingDirectory = absoluteRepositoryPath(m_activeIndex);
    command.repositoryIndex = m_activeIndex;
    command.repositoryPath = selectedPath();
    command.kind = GitCommandKind::History;
    command.appendHistory = true;
    command.title = tr("读取更多历史");
    enqueue(command);
}

void WorkspaceController::toggleRepository(int index)
{
    const int sourceIndex = m_proxy.sourceRow(index);
    if (sourceIndex >= 0) {
        m_model.setSelected(sourceIndex, !m_model.data(m_model.index(sourceIndex, 0), RepositoryModel::SelectedRole).toBool());
        emit selectionChanged();
    }
}

void WorkspaceController::selectAll(bool selected)
{
    m_model.selectAll(selected);
    emit selectionChanged();
}

void WorkspaceController::setActiveRepositoryIgnored(bool ignored)
{
    if (m_activeIndex >= 0)
        setRepositoryIgnored(m_model.pathAt(m_activeIndex), ignored);
}

void WorkspaceController::setRepositoryIgnored(const QString &path, bool ignored)
{
    const int index = m_model.indexOfPath(path);
    if (index >= 0)
        m_model.setIgnored(index, ignored);
    if (ignored) {
        if (!m_ignoredRepositories.contains(path))
            m_ignoredRepositories.append(path);
    } else {
        m_ignoredRepositories.removeAll(path);
    }
    emit ignoredRepositoriesChanged();
    saveSettings();
}

QStringList WorkspaceController::actionPaths(int scope) const
{
    if (scope == 0) {
        if (m_activeIndex >= 0) return {m_model.pathAt(m_activeIndex)};
        return {};
    }
    if (scope == 1) {
        return m_model.selectedPaths();
    }
    if (scope == 2) {
        QStringList all;
        const int count = m_model.rowCount();
        for (int i = 0; i < count; ++i) {
            all.append(m_model.pathAt(i));
        }
        return all;
    }
    const QStringList selected = m_model.selectedPaths();
    if (!selected.isEmpty()) return selected;
    if (m_activeIndex >= 0) return {m_model.pathAt(m_activeIndex)};
    return {};
}

int WorkspaceController::checkedRepositoryCount() const
{
    return m_model.selectedPaths().size();
}

int WorkspaceController::totalRepositoryCount() const
{
    return m_model.rowCount();
}

bool WorkspaceController::isRepoWorkspace() const
{
    const QString repoDir = QDir(m_workspacePath).filePath(QStringLiteral(".repo"));
    return QFileInfo(repoDir).isDir()
        && (QFileInfo::exists(repoDir + QStringLiteral("/repo"))
            || QFileInfo::exists(repoDir + QStringLiteral("/project.list")));
}

QString WorkspaceController::syncPreview(int scope, int method) const
{
    const QStringList paths = actionPaths(scope);
    if (paths.isEmpty() && scope != 2) {
        if (scope == 1) return tr("未勾选任何仓库");
        return tr("未选择任何仓库");
    }
    if (method == 1) {
        if (scope == 2) {
            return QStringLiteral("repo sync --no-manifest-update -j1 --fail-fast（整个项目）");
        }
        QStringList args{QStringLiteral("sync"), QStringLiteral("--no-manifest-update"),
                         QStringLiteral("-j1"), QStringLiteral("--fail-fast")};
        args.append(paths);
        return GitService::displayCommand(QStringLiteral("repo"), args);
    }
    const QString pullCommand = m_pullRebase ? QStringLiteral("git pull --rebase")
                                             : QStringLiteral("git pull --ff-only");
    if (scope == 0 || paths.size() == 1) {
        const int index = paths.isEmpty() ? -1 : m_model.indexOfPath(paths.first());
        const QString targetName = (index >= 0) ? m_model.nameAt(index) : QString();
        const QString branch = (index >= 0) ? m_model.branchAt(index) : QString();
        QString desc = pullCommand;
        if (!targetName.isEmpty()) {
            desc += QStringLiteral(" (%1%2)").arg(targetName, branch.isEmpty() ? QString() : QStringLiteral(" · %1").arg(branch));
        }
        return desc;
    }
    return pullCommand + tr("（共 %1 个仓库）").arg(paths.size());
}

void WorkspaceController::syncSelection(int scope)
{
    m_sync.syncSelection(actionPaths(scope), m_pullRebase, ++m_nextOperationId);
}

void WorkspaceController::syncRepoSelection(int scope)
{
    m_sync.syncRepoSelection(scope, actionPaths(scope), ++m_nextOperationId);
}

void WorkspaceController::stageFiles(const QStringList &paths)
{
    m_workingTree.stageFiles(paths, ++m_nextOperationId);
}

void WorkspaceController::unstageFiles(const QStringList &paths)
{
    m_workingTree.unstageFiles(paths, ++m_nextOperationId);
}

void WorkspaceController::discardFiles(const QStringList &paths)
{
    m_workingTree.discardFiles(paths, ++m_nextOperationId);
}

void WorkspaceController::commitActive(const QString &message, bool amend, bool stageAll)
{
    if (m_activeIndex < 0) return;
    m_hooks.clearPendingCommit();
    const QScopedValueRollback<quint64> operation(m_enqueueOperationId, ++m_nextOperationId);

    GitCommand stageCommand;
    GitCommand commitCommand;
    QString errorMessage;
    if (!CommitCommandBuilder::prepareCommit(m_activeIndex,
                                       absoluteRepositoryPath(m_activeIndex),
                                       selectedPath(),
                                       m_workspacePath,
                                       message, amend, stageAll,
                                       m_detailsCached, busy(),
                                       &stageCommand, &commitCommand,
                                       &errorMessage)) {
        reportError(tr("暂时无法提交"), errorMessage);
        return;
    }

    if (stageAll && !m_workingTree.prepareScopedStage(stageCommand)) return;
    if (requireChangeIdHook()) {
        m_hooks.rememberCommit(message, amend, stageAll);
        enqueue(m_gerrit.createQueryHookPathCommand(m_activeIndex, absoluteRepositoryPath(m_activeIndex),
                                                   selectedPath(), GitCommandKind::HookCommitPreflight));
    }
    if (stageAll)
        enqueue(stageCommand);

    enqueueGit(m_activeIndex, GitQueries::commitScope(), tr("检查提交仓库边界"), GitCommandKind::CommitScopePreflight, true);
    enqueue(commitCommand);
    enqueueRepositoryRefresh(m_activeIndex, true);
}

void WorkspaceController::generateCommitMessage(const QString &existingMessage, int scope,
                                                const QString &commitType, const QString &issueId,
                                                bool manualCompiled, const QString &compileNote,
                                                const QString &purpose, const QString &secondaryIssueId)
{
    m_aiWorkflow.generate(existingMessage, scope, commitType, issueId, manualCompiled, compileNote, purpose, secondaryIssueId);
}

QString WorkspaceController::pushPreview() const
{
    return m_reviewPush.pushPreview();
}

QString WorkspaceController::pushCommandPreview(const QString &remote, const QString &branch,
                                                const QVariantMap &options) const
{
    return m_reviewPush.pushCommandPreview(remote, branch, options);
}

QString WorkspaceController::pushOptionsError(const QVariantMap &options) const
{
    return m_reviewPush.pushOptionsError(options);
}

void WorkspaceController::pushActive(const QVariantMap &extraOptions)
{
    m_reviewPush.pushActive(extraOptions, ++m_nextOperationId);
}

void WorkspaceController::fetchChange(const QString &spec, int mode)
{
    m_revisions.fetchChange(spec, mode, ++m_nextOperationId);
}

void WorkspaceController::requestCommitMessage(const QString &revision)
{
    m_revisions.requestCommitMessage(revision);
}

void WorkspaceController::rewriteCommit(const QString &revision, int action, const QString &message)
{
    m_revisions.rewriteCommit(revision, action, message, ++m_nextOperationId);
}

QVariantList WorkspaceController::batchPushRows() const
{
    return m_batchPush.batchPushRows();
}

void WorkspaceController::prepareBatchPush()
{
    m_batchPush.prepareBatchPush();
}

void WorkspaceController::setBatchPushTarget(const QString &path, const QString &branch)
{
    m_batchPush.setBatchPushTarget(path, branch);
}

void WorkspaceController::setBatchPushIncluded(const QString &path, bool included)
{
    m_batchPush.setBatchPushIncluded(path, included);
}

void WorkspaceController::startBatchPush(const QVariantMap &options)
{
    m_batchPush.startBatchPush(options);
}

void WorkspaceController::repairLastCommitChangeId()
{
    m_reviewPush.repairLastCommitChangeId(++m_nextOperationId);
}

void WorkspaceController::checkoutBranch(const QString &branch)
{
    m_revisions.checkoutBranch(branch, ++m_nextOperationId);
}

void WorkspaceController::checkoutRevision(const QString &revision)
{
    m_revisions.checkoutRevision(revision, ++m_nextOperationId);
}

void WorkspaceController::showRevisionDiff(const QString &revision)
{
    const QString hash = revision.trimmed();
    const QRegularExpression hashPattern(QStringLiteral("^[0-9a-fA-F]{7,64}$"));
    if (m_activeIndex < 0 || !hashPattern.match(hash).hasMatch())
        return;

    m_selectedRevision = hash;
    ++m_revisionRequest;
    m_revisionFiles.clear();
    m_revisionFile.clear();
    m_revisionDiff.clear();
    emit revisionDiffChanged();
    enqueueGit(m_activeIndex,
               {QStringLiteral("show"), QStringLiteral("--format="),
                QStringLiteral("--name-only"), QStringLiteral("-z"),
                QStringLiteral("--first-parent"), QStringLiteral("--no-renames"), hash, QStringLiteral("--")},
               tr("读取提交文件 %1").arg(hash), GitCommandKind::RevisionFiles);
}

void WorkspaceController::showRevisionFileDiff(const QString &path)
{
    if (m_activeIndex < 0 || m_selectedRevision.isEmpty() || !m_revisionFiles.contains(path)) return;
    m_revisionFile = path;
    m_revisionDiff.clear();
    emit revisionDiffChanged();
    enqueueGit(m_activeIndex,
               {QStringLiteral("--literal-pathspecs"), QStringLiteral("show"), QStringLiteral("--format="),
                QStringLiteral("--first-parent"), QStringLiteral("--no-renames"), QStringLiteral("--no-textconv"),
                QStringLiteral("--no-ext-diff"), QStringLiteral("--no-color"),
                QStringLiteral("--unified=4"), m_selectedRevision, QStringLiteral("--"), path},
               tr("读取历史文件 Diff"), GitCommandKind::RevisionDiff);
}

void WorkspaceController::showWorkingTreeDiff()
{
    if (m_activeIndex < 0)
        return;

    m_selectedFile.clear();
    ++m_diffRequest;
    setDiffText({});
    emit diffTextChanged();
    QStringList paths;
    for (const auto &file : ownRepositoryFiles(m_activeIndex, m_changedFiles)) {
        const auto entry = file.toMap();
        paths.append(entry.value(QStringLiteral("path")).toString());
        const auto original = entry.value(QStringLiteral("originalPath")).toString();
        if (!original.isEmpty()) paths.append(original);
    }
    paths.removeDuplicates();
    // An empty pathspec means the entire repository to Git, not an empty diff.
    if (paths.isEmpty()) return;
    QStringList arguments{QStringLiteral("--literal-pathspecs"), QStringLiteral("diff"),
        QStringLiteral("HEAD"), QStringLiteral("--no-ext-diff"), QStringLiteral("--no-textconv"),
        QStringLiteral("--no-color"), QStringLiteral("--unified=4"), QStringLiteral("--")};
    arguments.append(paths);
    enqueueGit(m_activeIndex, arguments,
               tr("查看工作区 Diff"), GitCommandKind::Diff);
}

void WorkspaceController::rebuildGroupedChanges()
{
    QVariantList conflicts, staged, unstaged;
    for (const QVariant &value : m_changedFiles) {
        QVariantMap entry = value.toMap();
        const QString status = entry.value(QStringLiteral("status")).toString();
        if (status.size() != 2) continue;
        const bool conflict = GitStatusParser::isConflict(status);
        entry.insert(QStringLiteral("conflict"), conflict);
        if (conflict) {
            entry.insert(QStringLiteral("group"), QStringLiteral("冲突文件"));
            entry.insert(QStringLiteral("staged"), false);
            conflicts.append(entry);
            continue;
        }
        if (status == QStringLiteral("!!")) continue;
        if (status.at(0) != ' ' && status != QStringLiteral("??")) {
            entry.insert(QStringLiteral("group"), QStringLiteral("Staged Changes"));
            entry.insert(QStringLiteral("staged"), true);
            staged.append(entry);
        }
        if (status.at(1) != ' ') {
            entry.insert(QStringLiteral("group"), QStringLiteral("Changes"));
            entry.insert(QStringLiteral("staged"), false);
            unstaged.append(entry);
        }
    }
    m_groupedChanges = conflicts + staged + unstaged;
}

void WorkspaceController::showFileDiff(const QString &path, bool staged)
{
    if (m_activeIndex < 0 || path.isEmpty())
        return;
    if (!m_workingTree.validateOwnFiles({path})) return;
    const bool fileChanged = (m_selectedFile != path || m_selectedFileStaged != staged);
    m_selectedFile = path;
    m_selectedFileStaged = staged;
    ++m_diffRequest;
    if (fileChanged) {
        setDiffText({});
        emit diffTextChanged();
    }
    QStringList arguments{QStringLiteral("--literal-pathspecs"), QStringLiteral("diff"), QStringLiteral("--no-ext-diff"),
                          QStringLiteral("--no-textconv"), QStringLiteral("--no-color"), QStringLiteral("--unified=4")};
    if (m_ignoreWhitespace)
        arguments.append(QStringLiteral("--ignore-all-space"));
    bool untracked = false;
    for (const auto &value : m_changedFiles) {
        const auto entry = value.toMap();
        if (entry.value(QStringLiteral("path")).toString() == path) {
            untracked = entry.value(QStringLiteral("status")).toString() == QStringLiteral("??");
            break;
        }
    }
    if (untracked && !staged) {
        const QString clean = QDir::cleanPath(path);
        if (QDir::isAbsolutePath(clean) || clean == QStringLiteral("..")
            || clean.startsWith(QStringLiteral("../"))) {
            reportError(tr("无法读取 Diff"), tr("文件路径不在当前仓库内。"));
            return;
        }
        const QFileInfo file(QDir(absoluteRepositoryPath(m_activeIndex)).absoluteFilePath(path));
        if (!file.exists() || (!file.isFile() && !file.isSymLink())) {
            reportError(tr("无法读取 Diff"), tr("文件不存在或不是普通文件：%1").arg(path));
            return;
        }
        if (file.size() > 2 * 1024 * 1024) {
            reportError(tr("文件过大"), tr("新文件超过 2 MiB，请使用打开文件按钮查看：%1").arg(path));
            return;
        }
        arguments.append({QStringLiteral("--no-index"), QStringLiteral("--"), QStringLiteral("/dev/null"), path});
        GitCommand command;
        command.program = QStringLiteral("git");
        command.arguments = arguments;
        command.workingDirectory = absoluteRepositoryPath(m_activeIndex);
        command.title = tr("读取新文件 Diff");
        command.kind = GitCommandKind::Diff;
        command.repositoryIndex = m_activeIndex;
        command.repositoryPath = selectedPath();
        command.differenceExitCode = true;
        enqueue(command);
        return;
    }
    if (staged) arguments.append(QStringLiteral("--cached"));
    arguments.append({QStringLiteral("--"), path});
    enqueueGit(m_activeIndex, arguments,
               tr("读取文件 Diff"), GitCommandKind::Diff);
}

void WorkspaceController::openFile(const QString &path)
{
    if (m_activeIndex < 0 || path.isEmpty()) return;
    const QDir repository(absoluteRepositoryPath(m_activeIndex));
    const QString relative = QDir::cleanPath(path);
    if (QDir::isAbsolutePath(relative) || relative == QStringLiteral("..")
        || relative.startsWith(QStringLiteral("../"))) {
        reportError(tr("无法打开文件"), tr("文件路径不在当前仓库内。"));
        return;
    }
    const QFileInfo file(repository.absoluteFilePath(relative));
    if (!file.exists() || !file.isFile()) {
        reportError(tr("无法打开文件"), tr("工作区中不存在此文件：%1").arg(path));
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(file.absoluteFilePath())))
        reportError(tr("无法打开文件"), tr("没有可用的默认应用打开：%1").arg(path));
}

void WorkspaceController::refreshSelectedFile()
{
    const auto entries = groupedChanges();
    if (m_selectedFile.isEmpty()) {
        if (!entries.isEmpty()) {
            const auto first = entries.first().toMap();
            showFileDiff(first.value(QStringLiteral("path")).toString(),
                         first.value(QStringLiteral("staged")).toBool());
        }
        return;
    }
    QVariantMap fallback;
    for (const auto &value : entries) {
        const auto entry = value.toMap();
        if (entry.value(QStringLiteral("path")).toString() != m_selectedFile) continue;
        fallback = entry;
        if (entry.value(QStringLiteral("staged")).toBool() == m_selectedFileStaged) break;
    }
    if (!fallback.isEmpty()) {
        showFileDiff(m_selectedFile, fallback.value(QStringLiteral("staged")).toBool());
    } else if (!entries.isEmpty()) {
        const auto first = entries.first().toMap();
        showFileDiff(first.value(QStringLiteral("path")).toString(),
                     first.value(QStringLiteral("staged")).toBool());
    } else {
        ++m_diffRequest;
        m_selectedFile.clear();
        setDiffText({});
        emit diffTextChanged();
    }
}

void WorkspaceController::createBranch(const QString &branch)
{
    m_revisions.createBranch(branch, ++m_nextOperationId);
}

void WorkspaceController::deleteBranch(const QString &branch, bool force)
{
    m_revisions.deleteBranch(branch, force, ++m_nextOperationId);
}

QString WorkspaceController::lastCommitMessage() const
{
    return m_lastCommitMessage;
}

void WorkspaceController::requestLastCommitMessage()
{
    if (m_activeIndex < 0 || busy()) return;
    m_lastCommitMessage.clear();
    enqueueGit(m_activeIndex, {"log", "-1", "--pretty=%B"},
               tr("读取最近提交说明"), GitCommandKind::LastCommitMessage);
}

void WorkspaceController::requestRemoteBranches(const QString &remote)
{
    if (m_activeIndex < 0 || busy() || remote.trimmed().isEmpty() || remote.startsWith('-')) return;
    m_branchQueryRemote = remote.trimmed();
    m_pushRemoteUrl.clear();
    m_remoteBranches.clear();
    m_remoteBranchesLoaded = false;
    ++m_branchQuery;
    emit branchesChanged();
    GitCommand command;
    command.arguments = {"remote", "get-url", "--push", m_branchQueryRemote};
    command.workingDirectory = absoluteRepositoryPath(m_activeIndex);
    command.repositoryIndex = m_activeIndex;
    command.repositoryPath = selectedPath();
    command.target.remote = m_branchQueryRemote;
    command.target.request = m_branchQuery;
    command.kind = GitCommandKind::PushRemoteUrl;
    command.title = tr("查询远端目标分支 %1").arg(m_branchQueryRemote);
    enqueue(command);
}

void WorkspaceController::configurePullUpstream(const QString &remote, const QString &branch)
{
    m_sync.configurePullUpstream(remote, branch, ++m_nextOperationId);
}

void WorkspaceController::copyToClipboard(const QString &text)
{
    if (auto *cb = QGuiApplication::clipboard()) {
        cb->setText(text);
        appendConsole(tr("已复制审查链接到剪贴板"));
    }
}

void WorkspaceController::cherryPick(const QString &revision)
{
    m_revisions.cherryPick(revision, ++m_nextOperationId);
}

void WorkspaceController::revertCommit(const QString &revision)
{
    m_revisions.revertCommit(revision, ++m_nextOperationId);
}

void WorkspaceController::pruneBranches()
{
    m_revisions.pruneBranches(++m_nextOperationId);
}

void WorkspaceController::updateCommitHookStatus()
{
    m_hooks.updateStatus();
}

int WorkspaceController::activeProxyIndex() const
{
    if (m_activeIndex < 0)
        return -1;
    return m_proxy.proxyRow(m_activeIndex);
}

void WorkspaceController::enqueueRepositoryRefresh(int index, bool includeBranches)
{
    enqueueGit(index,
               GitQueries::summary(),
               tr("刷新仓库状态"), GitCommandKind::Details);
    if (includeBranches) {
        if (index == m_activeIndex) {
            ++m_historyRequest;
            m_historyLoaded = false;
            m_historyPending = false;
            m_historyEntries.clear();
            emit historyTextChanged();
            if (m_historyRequested) loadHistory();
        }
        enqueueGit(index,
                   GitQueries::branches(),
                   tr("刷新分支"), GitCommandKind::Branches);
    }
}

void WorkspaceController::installCommitHook(bool resumePendingCommit)
{
    m_hooks.install(resumePendingCommit, ++m_nextOperationId);
}

void WorkspaceController::stop()
{
    cancelScheduledRefresh();
    m_batchPush.cancel();
    m_hooks.reset();
    m_reviewPush.cancel();
    ++m_generation;
    m_aiWorkflow.reset();
    m_gitService.setGeneration(m_generation);
    m_historyPending = false;
    m_gitService.stop();
    emit busyChanged();
}

void WorkspaceController::enqueueGit(int index, const QStringList &arguments,
                                     const QString &title, GitCommandKind kind,
                                     bool stopOnFailure)
{
    GitCommand command;
    command.program = QStringLiteral("git");
    command.arguments = arguments;
    command.workingDirectory = absoluteRepositoryPath(index);
    command.title = title;
    command.kind = kind;
    command.repositoryIndex = index;
    command.repositoryPath = m_model.pathAt(index);
    command.stopOnFailure = stopOnFailure;
    if (kind == GitCommandKind::General)
        command.affectedRepositories = {m_model.pathAt(index)};
    enqueue(command);
}

void WorkspaceController::enqueue(GitCommand command)
{
    command.generation = m_generation;
    m_gitService.setGeneration(m_generation);
    command.operationId = m_enqueueOperationId;
    command.historyRequest = m_historyRequest;
    if (command.kind == GitCommandKind::RevisionFiles) {
        command.diffRequest = ++m_revisionFilesRequest;
    } else if (command.kind == GitCommandKind::RevisionDiff) {
        command.diffRequest = ++m_revisionRequest;
    } else if (command.kind == GitCommandKind::Diff) {
        command.diffRequest = ++m_diffRequest;
    }
    m_gitService.enqueue(std::move(command));
}

void WorkspaceController::onGitOutputReceived(const QByteArray &output, const QByteArray &errors)
{
    auto processChunk = [this](const QByteArray &chunk) {
        if (chunk.isEmpty()) return;
        const QString str = QString::fromLocal8Bit(chunk).replace(QChar(0), QLatin1Char('\t'));
        const QStringList lines = str.split(QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts);
        for (const QString &rawLine : lines) {
            const QString line = rawLine.trimmed();
            if (line.isEmpty()) continue;
            const bool isProgress = line.startsWith(QStringLiteral("接收对象中"))
                || line.startsWith(QStringLiteral("Receiving objects"))
                || line.startsWith(QStringLiteral("解决差异中"))
                || line.startsWith(QStringLiteral("Resolving deltas"))
                || line.startsWith(QStringLiteral("压缩对象中"))
                || line.startsWith(QStringLiteral("Compressing objects"))
                || line.startsWith(QStringLiteral("数出对象数"))
                || line.startsWith(QStringLiteral("Counting objects"))
                || line.startsWith(QStringLiteral("写入对象中"))
                || line.startsWith(QStringLiteral("Writing objects"))
                || line.startsWith(QStringLiteral("更新文件中"))
                || line.startsWith(QStringLiteral("Updating files"));
            if (isProgress)
                appendConsoleProgress(line);
            else
                appendConsole(line);
        }
    };
    processChunk(output);
    processChunk(errors);
}

void WorkspaceController::onGitCommandFinished(const GitCommandResult &result)
{
    const GitCommand completed = result.command;
    if (completed.generation != m_generation) {
        m_hooks.handleResult(result); // A stale SSH installation still owns its temporary file.
        return;
    }
    if (completed.target.batchPush) {
        m_batchPush.handleBatchPushResult(result);
        return;
    }
    if (m_hooks.handleResult(result) || m_reviewPush.handleResult(result)
        || m_revisions.handleResult(result) || m_aiWorkflow.handleResult(result) || m_workingTree.handleResult(result) || m_sync.handleResult(result)) return;
    if (result.status == ExecutionStatus::Canceled
        || (completed.kind == GitCommandKind::History && completed.historyRequest != m_historyRequest)
        || (completed.kind == GitCommandKind::Diff && completed.diffRequest != m_diffRequest)
        || (completed.kind == GitCommandKind::RevisionDiff && completed.diffRequest != m_revisionRequest)
        || (completed.kind == GitCommandKind::RevisionFiles && completed.diffRequest != m_revisionFilesRequest)) return;
    const QString output = QString::fromLocal8Bit(result.standardOutput);
    const bool success = result.status == ExecutionStatus::Success;

    if (completed.kind == GitCommandKind::RepoList) {
        if (completed.workspacePath != m_workspacePath) return;
        QStringList paths = completed.paths;
        if (success) {
            paths.clear();
            for (const QString &line : output.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
                paths.append(normalizedPath(line));
        } else {
            appendConsole(tr("Repo 项目列表读取失败，使用本地 project.list 回退。\n") + result.diagnosticMessage);
        }
        discoverManifestRepository(paths);
        return;
    }
    if (completed.kind == GitCommandKind::ManifestRepository) {
        if (completed.workspacePath != m_workspacePath) return;
        QStringList paths = completed.paths;
        const QString directory = QDir(m_workspacePath).filePath(QStringLiteral(".repo/manifests"));
        if (success && QFileInfo(output.trimmed()).canonicalFilePath() == QFileInfo(directory).canonicalFilePath())
            paths.append(QStringLiteral(".repo/manifests"));
        else
            appendConsole(tr("未识别到有效的 Manifest Git 工作目录，未将其加入仓库列表。"));
        applyRepositoryPaths(paths);
        return;
    }

    if (success) switch (completed.kind) {
    case GitCommandKind::PushRemoteUrl:
        if (completed.repositoryIndex == m_activeIndex
            && completed.target.remote == m_branchQueryRemote
            && completed.target.request == m_branchQuery && !output.trimmed().isEmpty()) {
            const QString url = output.trimmed();
            m_pushRemoteUrl = url.startsWith("http") ? QUrl(url).toString(QUrl::RemoveUserInfo) : url;
            emit branchesChanged();
            GitCommand query = completed;
            query.arguments = {"ls-remote", "--heads", "--", url};
            query.kind = GitCommandKind::RemoteBranches;
            enqueue(query);
        }
        break;
    case GitCommandKind::LastCommitMessage:
        if (completed.repositoryIndex == m_activeIndex) {
            m_lastCommitMessage = output.trimmed();
            emit lastCommitMessageReady(m_workspacePath, completed.repositoryPath, m_lastCommitMessage);
        }
        break;
    case GitCommandKind::RemoteBranches:
        if (completed.repositoryIndex == m_activeIndex
            && completed.target.remote == m_branchQueryRemote
            && completed.target.request == m_branchQuery) {
            m_remoteBranches.clear();
            m_remoteBranchesLoaded = false;
            for (const QString &line : output.split('\n', Qt::SkipEmptyParts)) {
                const int tab = line.indexOf('\t');
                const QString ref = line.mid(tab + 1).trimmed();
                if (tab > 0 && ref.startsWith("refs/heads/")) m_remoteBranches.append(ref.mid(11));
            }
            m_remoteBranches.removeDuplicates();
            m_remoteBranchesLoaded = true;
            emit branchesChanged();
        }
        break;
    case GitCommandKind::Status:
        // Summary-only refreshes must not rewrite a potentially large file cache.
        applyStatus(completed.repositoryIndex, GitStatusParser::parseSnapshot(output), false);
        break;
    case GitCommandKind::Details: {
        auto snapshot = GitStatusParser::parseSnapshot(output);
        snapshot.files = ownRepositoryFiles(completed.repositoryIndex, snapshot.files);
        // Active details are persisted once, after updating the pane's snapshot.
        applyStatus(completed.repositoryIndex, snapshot, completed.repositoryIndex != m_activeIndex);
        if (completed.repositoryIndex == m_activeIndex) {
            m_detailsCached = m_refreshPending;
            m_changedFiles = snapshot.files;
            rebuildGroupedChanges();
            emit detailsTextChanged();
            saveFileCache(m_activeIndex, m_changedFiles);
            const quint64 generation = m_generation;
            const quint64 request = m_diffRequest;
            const int repository = m_activeIndex;
            QMetaObject::invokeMethod(this, [this, generation, request, repository] {
                if (generation == m_generation && request == m_diffRequest
                    && repository == m_activeIndex)
                    refreshSelectedFile();
            }, Qt::QueuedConnection);
        }
        break;
    }
    case GitCommandKind::Diff:
        if (completed.repositoryIndex == m_activeIndex && m_diffText != output) {
            setDiffText(output);
            emit diffTextChanged();
        }
        break;
    case GitCommandKind::RevisionFiles:
        if (completed.repositoryIndex == m_activeIndex) {
            m_revisionFiles = output.split(QChar(0), Qt::SkipEmptyParts);
            m_revisionFiles.removeIf([this, &completed](const QString &path) {
                return !isOwnRepositoryPath(completed.repositoryIndex, path);
            });
            m_revisionFiles.removeDuplicates();
            emit revisionDiffChanged();
        }
        break;
    case GitCommandKind::RevisionDiff:
        if (completed.repositoryIndex == m_activeIndex) {
            m_revisionDiff = output;
            emit revisionDiffChanged();
        }
        break;
    case GitCommandKind::History:
        if (completed.repositoryIndex == m_activeIndex) {
            if (m_unbornRepositories.contains(completed.repositoryIndex)) {
                m_historyLoaded = true;
                m_historyHasMore = false;
                m_historyPending = false;
                m_historyEntries.clear();
                emit historyTextChanged();
                saveHistoryCache();
                break;
            }
            const auto page = GitHistoryParser::parse(output);
            m_historyPending = false;
            m_historyLoaded = true;
            m_historyHasMore = page.size() == 30;
            if (completed.appendHistory) {
                if (!output.isEmpty()) {
                    m_historyEntries += page;
                }
            } else {
                m_historyEntries = page;
                m_historyAnchor = page.isEmpty() ? QString() : page.first().toMap().value("revision").toString();
            }
            emit historyTextChanged();
            if (!completed.appendHistory) saveHistoryCache();
        }
        break;
    case GitCommandKind::Branches:
        if (completed.repositoryIndex == m_activeIndex) {
            const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            m_availableBranches.clear();
            m_remoteTrackingBranches.clear();
            for (const QString &ref : lines) {
                if (ref.startsWith(QStringLiteral("refs/heads/"))) {
                    m_availableBranches.append(ref.mid(11));
                } else if (ref.startsWith(QStringLiteral("refs/remotes/"))) {
                    const QString tracking = ref.mid(13);
                    if (!tracking.endsWith(QStringLiteral("/HEAD")))
                        m_remoteTrackingBranches.append(tracking);
                } else {
                    m_availableBranches.append(ref);
                }
            }
            emit branchesChanged();
            saveBranchCache();
        }
        break;
    case GitCommandKind::HunkOperation:
    case GitCommandKind::General:
        break;
    default: break; // Domain-specific commands were consumed above.
    }

    if (!success) {
        invalidateFailedQuery(completed);
        QString errorMsg = result.diagnosticMessage;
        ErrorType errorType = ErrorType::General;
        QStringList errorActions;
        if (completed.repositoryIndex == m_activeIndex && hasPendingOperation()) {
            errorType = ErrorType::Conflict;
            errorActions = {QStringLiteral("conflicts")};
        }
        if (errorMsg != result.diagnosticMessage && !result.diagnosticMessage.isEmpty()
            && !errorMsg.contains(result.diagnosticMessage))
            errorMsg += tr("\n\n原始错误：\n") + result.diagnosticMessage;
        reportError(completed.title, errorMsg, errorType, errorActions, &completed);
        // A failed write may already have changed part of a repository. Cancel
        // dependent writes above, but always reconcile the actual state afterward.
        // Error handlers may switch workspaces, so recheck the captured generation.
        if (completed.generation == m_generation) {
            for (const QString &path : completed.affectedRepositories) {
                const int index = m_model.indexOfPath(path);
                if (index >= 0)
                    enqueueRepositoryRefresh(index, index == m_activeIndex);
            }
        }
    } else {
        if (completed.kind == GitCommandKind::Commit || completed.commitOperation) {
            WorkspaceSettings::clearSubmittedDraft(completed.workspacePath, completed.repositoryPath, completed.submittedMessage);
            emit commitSucceeded(completed.workspacePath, completed.repositoryPath, completed.submittedMessage);
        }

        appendConsole(tr("操作完成 · %1").arg(completed.title));
        emit operationFinished(completed.title);
    }

    emit summaryChanged();
}

void WorkspaceController::invalidateFailedQuery(const GitCommand &command)
{
    if (command.kind == GitCommandKind::Status || command.kind == GitCommandKind::Details) {
        // Preserve the last known values, but never present a failed read as clean.
        const auto summary = m_model.summaryAt(command.repositoryIndex);
        m_model.updateStatus(command.repositoryIndex, summary.value("branch").toString(),
            summary.value("changes").toInt(), summary.value("ahead").toInt(),
            summary.value("behind").toInt(), QStringLiteral("error"));
        emit summaryChanged();
    }
    if (command.repositoryIndex != m_activeIndex) return;
    if (command.kind == GitCommandKind::Details) {
        // Keep the previous file list as an explicitly stale snapshot.
        m_detailsCached = true;
        ++m_diffRequest;
        setDiffText({});
        emit detailsTextChanged();
        emit diffTextChanged();
    } else if (command.kind == GitCommandKind::History) {
        m_historyPending = false;
        if (!command.appendHistory) m_historyLoaded = false;
    }
}

bool WorkspaceController::isOwnRepositoryPath(int index, const QString &path) const
{
    if (index < 0 || path.isEmpty() || QDir::isAbsolutePath(path)) return false;
    const QString relative = QDir::cleanPath(path);
    if (relative == "." || relative == ".." || relative.startsWith("../")) return false;
    const QString base = QDir::cleanPath(absoluteRepositoryPath(index));
    const QString candidate = QDir(base).absoluteFilePath(relative);
    // Known repo projects remain boundaries even if their checkout is missing.
    for (int row = 0; row < m_model.rowCount(); ++row) {
        const QString nested = QDir::cleanPath(absoluteRepositoryPath(row));
        if (row != index && nested.startsWith(base + '/')
            && (candidate == nested || candidate.startsWith(nested + '/'))) return false;
    }
    // Only inspect ancestors of changed paths; never recursively scan the tree.
    QString current = candidate;
    while (current != base && current.startsWith(base + '/')) {
        const QFileInfo marker(QDir(current).filePath(QStringLiteral(".git")));
        if (marker.exists() || marker.isSymLink()) return false;
        current = QFileInfo(current).absolutePath();
    }
    return true;
}

QVariantList WorkspaceController::ownRepositoryFiles(int index, const QVariantList &files) const
{
    QVariantList result;
    for (const auto &file : files) {
        const auto entry = file.toMap();
        const QString original = entry.value(QStringLiteral("originalPath")).toString();
        if (!entry.value(QStringLiteral("gitlink")).toBool()
            && isOwnRepositoryPath(index, entry.value(QStringLiteral("path")).toString())
            && (original.isEmpty() || isOwnRepositoryPath(index, original))) result.append(file);
    }
    return result;
}

void WorkspaceController::applyStatus(int index, const GitStatusParser::Snapshot &rawSnapshot, bool persist)
{
    auto snapshot = rawSnapshot;
    snapshot.files = ownRepositoryFiles(index, snapshot.files);
    if (snapshot.unborn) m_unbornRepositories.insert(index);
    else m_unbornRepositories.remove(index);
    const int changes = snapshot.files.size();
    m_model.updateStatus(index, snapshot.branch, changes, snapshot.ahead, snapshot.behind,
                         changes > 0 ? QStringLiteral("modified") : QStringLiteral("clean"));
    if (!RepositoryCache::save(m_workspacePath, m_model.pathAt(index), RepositoryCache::Section::Summary,
                               m_model.summaryAt(index)))
        appendConsole(tr("仓库摘要缓存未保存：%1").arg(m_model.pathAt(index)));
    // Persist this repository's own files, never the currently active pane.
    if (persist) saveFileCache(index, snapshot.files);
}

QString WorkspaceController::absoluteRepositoryPath(int index) const
{
    return QDir(m_workspacePath).absoluteFilePath(m_model.pathAt(index));
}

void WorkspaceController::reportError(const QString &title, const QString &message, ErrorType type,
                                      const QStringList &actions, const GitCommand *command)
{
    QString code;
    switch (type) {
    case ErrorType::General: code = QStringLiteral("general"); break;
    case ErrorType::Ai: code = QStringLiteral("ai"); break;
    case ErrorType::Upstream: code = QStringLiteral("upstream"); break;
    case ErrorType::Conflict: code = QStringLiteral("conflict"); break;
    case ErrorType::Hook: code = QStringLiteral("hook"); break;
    case ErrorType::ChangeId: code = QStringLiteral("changeId"); break;
    }
    emit operationError({{QStringLiteral("type"), code}, {QStringLiteral("title"), title},
        {QStringLiteral("message"), message}, {QStringLiteral("actions"), actions},
        {QStringLiteral("workspacePath"), command && !command->workspacePath.isEmpty() ? command->workspacePath : m_workspacePath},
        {QStringLiteral("repositoryPath"), command ? command->repositoryPath : selectedPath()},
        {QStringLiteral("generation"), QString::number(command ? command->generation : m_generation)},
        {QStringLiteral("operationId"), QString::number(command ? command->operationId : m_enqueueOperationId)}});
}

void WorkspaceController::appendConsoleProgress(const QString &progress)
{
    if (progress.trimmed().isEmpty()) return;
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    const QString formatted = QStringLiteral("[%1] %2\n").arg(timestamp, progress.trimmed());

    if (m_consoleHasProgress) {
        const int lastNewline = m_consoleText.lastIndexOf(QLatin1Char('\n'), -2);
        if (lastNewline >= 0) {
            m_consoleText = m_consoleText.left(lastNewline + 1) + formatted;
        } else {
            m_consoleText = formatted;
        }
    } else {
        m_consoleText += formatted;
        m_consoleHasProgress = true;
    }
    constexpr int maximumCharacters = 150000;
    if (m_consoleText.size() > maximumCharacters)
        m_consoleText = m_consoleText.right(maximumCharacters);
    emit consoleTextChanged();
}

void WorkspaceController::appendConsole(const QString &text)
{
    if (text.trimmed().isEmpty()) return;
    m_consoleHasProgress = false;
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    m_consoleText += QStringLiteral("[%1] %2\n").arg(timestamp, text.trimmed());
    constexpr int maximumCharacters = 150000;
    if (m_consoleText.size() > maximumCharacters)
        m_consoleText = m_consoleText.right(maximumCharacters);
    emit consoleTextChanged();
}

bool WorkspaceController::hasConflicts() const
{
    for (const auto &file : m_changedFiles) {
        if (file.toMap().value(QStringLiteral("conflict")).toBool())
            return true;
    }
    return false;
}

QString WorkspaceController::pendingOperation() const
{
    if (m_activeIndex < 0) return QString();
    const QString gitDir = gitMetadataDirectory(absoluteRepositoryPath(m_activeIndex));
    if (gitDir.isEmpty()) return {};

    if (QFile::exists(gitDir + QStringLiteral("/rebase-merge")) || QFile::exists(gitDir + QStringLiteral("/rebase-apply")))
        return QStringLiteral("rebase");
    if (QFile::exists(gitDir + QStringLiteral("/MERGE_HEAD")))
        return QStringLiteral("merge");
    if (QFile::exists(gitDir + QStringLiteral("/CHERRY_PICK_HEAD")))
        return QStringLiteral("cherry-pick");
    if (QFile::exists(gitDir + QStringLiteral("/REVERT_HEAD")))
        return QStringLiteral("revert");
    return QString();
}

int WorkspaceController::hunkCount() const
{
    return currentDiffIndex().hunks.size();
}

QString WorkspaceController::pendingCherryPickRevision() const
{
    if (pendingOperation() != QStringLiteral("cherry-pick")) return {};
    QFile file(QDir(gitMetadataDirectory(absoluteRepositoryPath(m_activeIndex)))
                   .filePath(QStringLiteral("CHERRY_PICK_HEAD")));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    const QString revision = QString::fromLatin1(file.read(128)).trimmed();
    static const QRegularExpression hash(QStringLiteral("^(?:[0-9a-fA-F]{40}|[0-9a-fA-F]{64})$"));
    return hash.match(revision).hasMatch() ? revision : QString();
}

bool WorkspaceController::canSkipCherryPick() const
{
    return m_activeIndex >= 0 && !m_detailsCached && m_changedFiles.isEmpty()
        && !pendingCherryPickRevision().isEmpty();
}

bool WorkspaceController::isBinaryDiff() const
{
    return currentDiffIndex().isBinary;
}

void WorkspaceController::setDiffText(const QString &text)
{
    if (m_diffText == text) return;
    m_diffText = text;
    m_diffIndex = {};
    m_diffIndexValid = false;
}

const DiffIndex &WorkspaceController::currentDiffIndex() const
{
    if (!m_diffIndexValid) {
        m_diffIndex = DiffModel::index(m_diffText);
        m_diffIndexValid = true;
    }
    return m_diffIndex;
}

void WorkspaceController::stageHunk(int hunkIndex)
{
    m_workingTree.applyHunk(m_selectedFile, m_diffText, currentDiffIndex(), hunkIndex,
                            WorkingTreeWorkflow::HunkAction::Stage, ++m_nextOperationId);
}

void WorkspaceController::unstageHunk(int hunkIndex)
{
    m_workingTree.applyHunk(m_selectedFile, m_diffText, currentDiffIndex(), hunkIndex,
                            WorkingTreeWorkflow::HunkAction::Unstage, ++m_nextOperationId);
}

void WorkspaceController::discardHunk(int hunkIndex)
{
    m_workingTree.applyHunk(m_selectedFile, m_diffText, currentDiffIndex(), hunkIndex,
                            WorkingTreeWorkflow::HunkAction::Discard, ++m_nextOperationId);
}

QString WorkspaceController::stashSha(int index) const
{
    return m_workingTree.stashSha(index);
}

void WorkspaceController::loadStashList()
{
    m_workingTree.loadStashList();
}

void WorkspaceController::stashSave(const QString &message, bool includeUntracked)
{
    m_workingTree.stashSave(message, includeUntracked, ++m_nextOperationId);
}

void WorkspaceController::stashPop(int index, const QString &expectedSha)
{
    m_workingTree.stashPop(index, expectedSha, ++m_nextOperationId);
}

void WorkspaceController::stashApply(int index, const QString &expectedSha)
{
    m_workingTree.stashApply(index, expectedSha, ++m_nextOperationId);
}

void WorkspaceController::stashDrop(int index, const QString &expectedSha)
{
    m_workingTree.stashDrop(index, expectedSha, ++m_nextOperationId);
}

void WorkspaceController::markResolved(const QStringList &paths)
{
    m_workingTree.markResolved(paths, ++m_nextOperationId);
}

void WorkspaceController::continueOperation()
{
    m_workingTree.continueOperation(++m_nextOperationId);
}

void WorkspaceController::abortOperation()
{
    m_workingTree.abortOperation(++m_nextOperationId);
}

void WorkspaceController::skipCherryPick(const QString &expectedRevision)
{
    m_workingTree.skipCherryPick(expectedRevision, ++m_nextOperationId);
}

WorkflowRepository WorkspaceController::repositoryContext(int index) const
{
    if (index < 0 || index >= m_model.rowCount()) return {};
    WorkflowRepository repository;
    repository.index = index;
    repository.path = m_model.pathAt(index);
    repository.name = m_model.nameAt(index);
    repository.branch = m_model.branchAt(index);
    repository.directory = absoluteRepositoryPath(index);
    repository.workspace = m_workspacePath;
    repository.config = index == m_activeIndex ? m_repositoryConfig
        : WorkspaceSettings::loadRepositoryConfig(m_workspacePath, repository.path);
    if (index == m_activeIndex) {
        repository.branches = m_availableBranches;
        repository.files = m_changedFiles;
        repository.unborn = m_unbornRepositories.contains(index);
        repository.operation = pendingOperation();
        repository.hasConflicts = hasConflicts();
        repository.canSkipCherryPick = canSkipCherryPick();
        repository.cherryPickRevision = pendingCherryPickRevision();
        repository.detailsCached = m_detailsCached;
        repository.hasPendingOperation = hasPendingOperation();
        repository.hasStaged = std::any_of(m_changedFiles.cbegin(), m_changedFiles.cend(), [](const QVariant &file) {
            return file.toMap().value(QStringLiteral("hasStagedChanges")).toBool();
        });
    }
    return repository;
}

WorkflowRepository WorkspaceController::activeRepositoryContext() const
{
    return repositoryContext(m_activeIndex);
}

QList<WorkflowRepository> WorkspaceController::checkedRepositoryContexts() const
{
    QList<WorkflowRepository> repositories;
    for (const auto &path : m_model.selectedPaths()) repositories.append(repositoryContext(m_model.indexOfPath(path)));
    return repositories;
}

void WorkspaceController::bindWorkflow(RepositoryWorkflow &workflow)
{
    connect(&workflow, &RepositoryWorkflow::commandRequested, this, [this](GitCommand command) {
        if ((!command.workspacePath.isEmpty() && command.workspacePath != m_workspacePath)
            || (command.generation != 0 && command.generation != m_generation)
            || (command.repositoryIndex >= 0 && m_model.pathAt(command.repositoryIndex) != command.repositoryPath)) return;
        const QScopedValueRollback<quint64> operation(m_enqueueOperationId,
            command.operationId ? command.operationId : ++m_nextOperationId);
        if (command.workspacePath.isEmpty()) command.workspacePath = m_workspacePath;
        enqueue(command);
    });
    connect(&workflow, &RepositoryWorkflow::refreshRequested, this, [this](const GitCommand &context, bool branches) {
        if (context.generation != m_generation || context.workspacePath != m_workspacePath
            || m_model.pathAt(context.repositoryIndex) != context.repositoryPath) return;
        enqueueRepositoryRefresh(context.repositoryIndex, branches);
    });
    connect(&workflow, &RepositoryWorkflow::failed, this, [this](const WorkflowFailure &failure) {
        reportError(failure.title, failure.message, failure.type, failure.actions,
                    failure.hasCommand ? &failure.command : nullptr);
        emit summaryChanged();
    });
    connect(&workflow, &RepositoryWorkflow::cancelOperationRequested, &m_gitService, &GitService::cancelOperation);
    connect(&workflow, &RepositoryWorkflow::logMessage, this, &WorkspaceController::appendConsole);
    connect(&workflow, &RepositoryWorkflow::finished, this, [this](const QString &message) {
        appendConsole(tr("操作完成 · %1").arg(message));
        emit operationFinished(message);
        emit summaryChanged();
    });
}

WorkflowRepository WorkspaceController::repositoryContextByPath(const QString &path) const
{
    return repositoryContext(m_model.indexOfPath(path));
}
