#include "workspacecontroller.h"
#include "gitqueries.h"
#include "repositorycache.h"
#include "discardplan.h"
#include "commitcommandbuilder.h"

#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QScopedValueRollback>
#include <QSettings>
#include <QTemporaryFile>
#include <QTextStream>
#include <QUrl>
#include <algorithm>
#include <utility>

namespace {
QString gitMetadataDirectory(const QString &repository)
{
    const QString path = QDir(repository).filePath(QStringLiteral(".git"));
    if (QFileInfo(path).isDir()) return path;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    const QString content = QString::fromUtf8(file.readAll()).trimmed();
    if (!content.startsWith(QStringLiteral("gitdir:"))) return {};
    return QDir(repository).absoluteFilePath(content.mid(7).trimmed());
}

QString normalizedPath(QString path)
{
    path = QDir::cleanPath(path.trimmed());
    while (path.startsWith(QStringLiteral("./")))
        path.remove(0, 2);
    return path;
}

QStringList dailyPullArguments(const QString &remote, const QString &remoteRef, bool rebase)
{
    // Gerrit chains keep their Change-Id trailers through a rebase; a merge commit would not be reviewable.
    QStringList args{QStringLiteral("pull"), rebase ? QStringLiteral("--rebase") : QStringLiteral("--ff-only")};
    if (!remote.isEmpty() && !remoteRef.isEmpty()) {
        args.append({QStringLiteral("--no-tags"), QStringLiteral("--"), remote, remoteRef});
    }
    return args;
}

GerritPushOptions pushOptionsFromMap(const QVariantMap &map)
{
    GerritPushOptions options;
    options.topic = map.value(QStringLiteral("topic")).toString().trimmed();
    options.reviewers = GerritService::splitList(map.value(QStringLiteral("reviewers")).toString());
    options.cc = GerritService::splitList(map.value(QStringLiteral("cc")).toString());
    options.hashtags = GerritService::splitList(map.value(QStringLiteral("hashtags")).toString());
    const int workState = map.value(QStringLiteral("workState")).toInt();
    options.workState = workState == 1 ? GerritPushOptions::WorkInProgress
        : workState == 2 ? GerritPushOptions::ReadyForReview : GerritPushOptions::KeepWorkState;
    options.privateChange = map.value(QStringLiteral("private")).toBool();
    options.message = map.value(QStringLiteral("message")).toString().simplified();
    return options;
}

struct PendingCommits {
    int count = 0;
    QStringList missingChangeId;
};

// Parses `git log --format=%H%x00%B%x00 <target>..HEAD`.
PendingCommits scanPendingCommits(const QString &output)
{
    static const QRegularExpression changeIdLine(
        QStringLiteral("(?m)^Change-Id:[ \\t]*I[0-9a-fA-F]{40}[ \\t]*$"));
    static const QRegularExpression commitHash(QStringLiteral("^[0-9a-fA-F]{40,64}$"));
    PendingCommits pending;
    const QStringList records = output.split(QChar(0));
    for (int i = 0; i + 1 < records.size(); i += 2) {
        const QString sha = records.at(i).trimmed();
        if (!commitHash.match(sha).hasMatch()) continue;
        ++pending.count;
        if (!changeIdLine.match(records.at(i + 1)).hasMatch())
            pending.missingChangeId.append(sha.left(10));
    }
    return pending;
}

bool isDetachedBranch(const QString &branch)
{
    return branch == QStringLiteral("(detached)") || branch == QStringLiteral("detached");
}

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

    connect(&m_gerrit, &GerritService::hookDownloadFinished, this,
            [this](const GitCommand &context, bool success, const QString &error) {
        if (context.generation != m_generation) {
            QFile::remove(context.hook.temporaryPath);
            return;
        }
        if (!success) {
            QFile::remove(context.hook.temporaryPath);
            m_resumePendingCommit = false;
            reportError(tr("无法安装 Hook"),
                error + tr("\n\n也可以在浏览器打开该地址下载，保存为仓库的 .git/hooks/commit-msg 并添加可执行权限。"),
                ErrorType::Hook, {}, &context);
            return;
        }
        appendConsole(tr("已从 %1 下载 Change-Id Hook。\n").arg(context.hook.source));
        finishHookInstall(context);
    });

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
    if (m_pushState == 1) {
        reportError(tr("无法切换项目"), tr("Gerrit 推送尚未结束，请等待结果或取消任务。"));
        return;
    }
    cancelScheduledRefresh();
    m_repositoryWatcher.setActiveRepository(QString());

    // Invalidate old results before changing the base path. In particular, a
    // completed discard preflight must never schedule writes in the new workspace.
    ++m_generation;
    ++m_aiRequest;
    m_commitAi.cancel();
    m_pushOperationId = 0;
    m_pushRepositoryKey.clear();
    m_pendingCommit = {};
    m_resumePendingCommit = false;
    m_repairChangeIdRevision.clear();
    setPushStatus(0, QString());
    setLastGerritReviewUrl(QString());
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
    m_stashList.clear();
    m_stashEntries.clear();
    m_stashRepositoryPath.clear();
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
    if (m_pushState == 1) {
        reportError(tr("无法重新载入项目"), tr("Gerrit 推送尚未结束，请等待结果或取消任务。"));
        return;
    }
    cancelScheduledRefresh();
    m_pendingCommit = {};
    m_resumePendingCommit = false;
    m_repairChangeIdRevision.clear();
    ++m_aiRequest;
    m_commitAi.cancel();
    m_pushOperationId = 0;
    m_pushRepositoryKey.clear();
    setPushStatus(0, QString());
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
    resetBatchPush();
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
    m_pendingCommit = {};
    m_resumePendingCommit = false;
    m_repairChangeIdRevision.clear();
    cancelScheduledRefresh();
    m_activeIndex = sourceIndex;
    ++m_aiRequest;
    m_commitAi.cancel();
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
    m_stashList.clear();
    m_stashRepositoryPath.clear();
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
    if (busy()) {
        appendConsole(tr("当前已有操作正在执行：%1，请等待完成后再试。").arg(activeTask()));
        reportError(tr("无法拉取"), tr("当前正在执行操作：%1，请等待完成后再试。").arg(activeTask()));
        return;
    }
    const QScopedValueRollback<quint64> operation(m_enqueueOperationId, ++m_nextOperationId);
    const QStringList paths = actionPaths(scope);
    if (paths.isEmpty()) {
        appendConsole(tr("未选择任何可拉取的仓库。"));
        reportError(tr("无法同步"), tr("没有选择要同步的仓库。"));
        return;
    }
    for (const QString &path : paths) {
        const int index = m_model.indexOfPath(path);
        const QString branch = index >= 0 ? m_model.branchAt(index) : QString();
        if (branch == QStringLiteral("(detached)") || branch == QStringLiteral("detached")) {
            const QString error = isRepoWorkspace()
                ? tr("仓库 %1 处于 detached HEAD，没有可拉取的本地分支。请在拉取面板选择“Repo 按清单同步”。").arg(m_model.nameAt(index))
                : tr("仓库 %1 处于 detached HEAD，请先切换到本地分支。").arg(m_model.nameAt(index));
            appendConsole(error);
            reportError(tr("无法 Git 拉取"), error);
            return;
        }
    }
    appendConsole(tr("开始拉取代码 · 共 %1 个仓库...").arg(paths.size()));
    for (const QString &path : paths) {
        const int index = m_model.indexOfPath(path);
        if (index >= 0) {
            GitCommand command;
            command.arguments = {QStringLiteral("for-each-ref"),
                QStringLiteral("--format=%(HEAD)%00%(upstream:remotename)%00%(upstream:remoteref)"),
                QStringLiteral("refs/heads/")};
            command.workingDirectory = absoluteRepositoryPath(index);
            command.repositoryIndex = index;
            command.repositoryPath = path;
            command.title = tr("解析拉取上游 %1").arg(m_model.nameAt(index));
            command.kind = GitCommandKind::PullTarget;
            command.stopOnFailure = true;
            command.target.pullRebase = m_pullRebase;
            enqueue(command);
        }
    }
}

void WorkspaceController::syncRepoSelection(int scope)
{
    if (busy()) {
        reportError(tr("无法同步"), tr("当前已有操作正在执行，请等待完成后再试。"));
        return;
    }
    if (!isRepoWorkspace() || !QFileInfo(QDir(m_workspacePath).filePath(QStringLiteral(".repo/manifest.xml"))).isFile()) {
        reportError(tr("无法同步"), tr("请先初始化或添加 Repo 项目。"));
        return;
    }
    if (scope != 0 && scope != 1 && scope != 2) {
        reportError(tr("无法同步"), tr("请选择当前、已勾选或全部仓库进行 Repo 同步。"));
        return;
    }

    const QScopedValueRollback<quint64> operation(m_enqueueOperationId, ++m_nextOperationId);
    GitCommand command;
    command.program = QStringLiteral("repo");
    command.arguments = {QStringLiteral("sync"), QStringLiteral("--no-manifest-update"),
                         QStringLiteral("-j1"), QStringLiteral("--fail-fast")};

    if (scope == 2) {
        command.title = tr("按 Manifest 全量同步全部仓库");
        command.affectedRepositories = actionPaths(2);
    } else {
        const QStringList paths = actionPaths(scope);
        if (paths.isEmpty()) {
            reportError(tr("无法同步"), tr("没有选择要同步的仓库。"));
            return;
        }
        const QString root = QFileInfo(m_workspacePath).canonicalFilePath();
        for (const QString &path : paths) {
            const QString target = QFileInfo(QDir(m_workspacePath).filePath(path)).canonicalFilePath();
            if (path == QStringLiteral(".") || path.startsWith(QStringLiteral(".repo/")) || path.startsWith(QStringLiteral("../"))
                || target.isEmpty() || !target.startsWith(root + QLatin1Char('/'))
                || !QFileInfo(QDir(target).filePath(QStringLiteral(".git"))).exists()) {
                reportError(tr("无法同步"), tr("所选仓库不属于当前 Repo 项目：%1").arg(path));
                return;
            }
        }
        command.arguments.append(paths);
        command.title = tr("按 Manifest 同步 %1 个仓库").arg(paths.size());
        command.affectedRepositories = paths;
    }

    command.workingDirectory = m_workspacePath;
    command.workspacePath = m_workspacePath;
    command.rediscoverProjects = scope == 2;
    command.kind = GitCommandKind::RepoSync;
    command.stopOnFailure = true;
    command.timeoutMilliseconds = 30 * 60 * 1000;
    enqueue(command);
}

void WorkspaceController::stageFiles(const QStringList &paths)
{
    const QScopedValueRollback<quint64> operation(m_enqueueOperationId, ++m_nextOperationId);
    if (m_activeIndex < 0 || paths.isEmpty())
        return;
    if (!validateOwnFiles(paths)) return;
    QStringList arguments{QStringLiteral("--literal-pathspecs"), QStringLiteral("add"), QStringLiteral("--")};
    arguments.append(paths);
    enqueueGit(m_activeIndex, arguments,
               tr("暂存 %1 个文件").arg(paths.size()), GitCommandKind::General, true);
    enqueueRepositoryRefresh(m_activeIndex, false);
}

void WorkspaceController::unstageFiles(const QStringList &paths)
{
    const QScopedValueRollback<quint64> operation(m_enqueueOperationId, ++m_nextOperationId);
    if (m_activeIndex < 0 || paths.isEmpty())
        return;
    if (!validateOwnFiles(paths)) return;
    QStringList arguments{QStringLiteral("--literal-pathspecs")};
    if (m_unbornRepositories.contains(m_activeIndex)) {
        arguments.append({QStringLiteral("rm"), QStringLiteral("--cached"), QStringLiteral("-r"), QStringLiteral("-q"), QStringLiteral("--")});
    } else {
        arguments.append({QStringLiteral("restore"), QStringLiteral("--staged"), QStringLiteral("--")});
    }
    arguments.append(paths);
    enqueueGit(m_activeIndex, arguments,
               tr("取消暂存 %1 个文件").arg(paths.size()), GitCommandKind::General, true);
    enqueueRepositoryRefresh(m_activeIndex, false);
}

void WorkspaceController::discardFiles(const QStringList &paths)
{
    const QScopedValueRollback<quint64> operation(m_enqueueOperationId, ++m_nextOperationId);
    if (m_activeIndex < 0 || paths.isEmpty() || busy())
        return;
    if (!validateOwnFiles(paths)) return;
    GitCommand command;
    command.program = QStringLiteral("git");
    command.arguments = GitQueries::files();
    command.workingDirectory = absoluteRepositoryPath(m_activeIndex);
    command.repositoryIndex = m_activeIndex;
    command.repositoryPath = selectedPath();
    command.kind = GitCommandKind::DiscardPreflight;
    command.paths = paths;
    command.stopOnFailure = true;
    command.title = tr("检查待丢弃文件");
    enqueue(command);
}

void WorkspaceController::executeDiscard(int repositoryIndex, const QStringList &paths, const QString &statusOutput)
{
    const auto plan = planDiscard(paths, ownRepositoryFiles(repositoryIndex, GitStatusParser::parse(statusOutput)));
    if (!plan.valid) {
        reportError(tr("无法丢弃"), tr("文件状态已变化或包含冲突，请刷新后选择未暂存文件：%1").arg(plan.rejectedPath));
        return;
    }
    const auto &tracked = plan.tracked;
    const auto &untracked = plan.untracked;
    if (!tracked.isEmpty()) {
        QStringList arguments{QStringLiteral("--literal-pathspecs"), QStringLiteral("restore"),
                              QStringLiteral("--worktree"), QStringLiteral("--")};
        arguments.append(tracked);
        enqueueGit(repositoryIndex, arguments,
                   tr("丢弃 %1 个已跟踪文件的改动").arg(tracked.size()), GitCommandKind::General, true);
    }
    if (!untracked.isEmpty()) {
        QStringList arguments{QStringLiteral("--literal-pathspecs"), QStringLiteral("clean"), QStringLiteral("-f"), QStringLiteral("--")};
        arguments.append(untracked);
        enqueueGit(repositoryIndex, arguments,
                   tr("清理 %1 个未跟踪文件").arg(untracked.size()), GitCommandKind::General, true);
    }
    enqueueRepositoryRefresh(repositoryIndex, false);
}

void WorkspaceController::commitActive(const QString &message, bool amend, bool stageAll)
{
    if (m_activeIndex < 0) return;
    m_pendingCommit = {};
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

    if (stageAll && !prepareScopedStage(stageCommand)) return;
    if (requireChangeIdHook()) {
        m_pendingCommit = {true, m_workspacePath, selectedPath(), message, amend, stageAll};
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
    if (m_activeIndex < 0) return;
    if (busy()) {
        reportError(tr("AI 提交说明"), tr("Git 正在处理其他任务，请稍候再试。"), ErrorType::Ai);
        return;
    }
    if (scope != 0 && scope != 1) {
        reportError(tr("AI 提交说明"), tr("请选择有效的改动范围。"), ErrorType::Ai);
        return;
    }
    static const QStringList allowedTypes = {
        QStringLiteral("fix"),
        QStringLiteral("feat"),
        QStringLiteral("chg"),
        QStringLiteral("refactor"),
        QStringLiteral("docs"),
        QStringLiteral("style"),
        QStringLiteral("test"),
        QStringLiteral("chore"),
        QStringLiteral("add"),
        QStringLiteral("del"),
        QStringLiteral("merge")
    };
    const QString normalizedType = commitType.trimmed().toLower();
    if (!allowedTypes.contains(normalizedType)) {
        reportError(tr("AI 提交说明"), tr("请选择有效的提交类型（如 fix, feat, chg, refactor 等）。"), ErrorType::Ai);
        return;
    }
    QString normalizedIssue;
    if (!issueId.trimmed().isEmpty()) {
        const QStringList rawList = issueId.split(QRegularExpression(QStringLiteral("[,;，；\\s]+")), Qt::SkipEmptyParts);
        static const QRegularExpression singleIssuePattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,49}$"));
        QStringList issueTokens;
        for (QString item : rawList) {
            item = item.trimmed();
            if (item.isEmpty()) continue;
            if (!singleIssuePattern.match(item).hasMatch()) {
                reportError(tr("AI 提交说明"), tr("单号格式不正确（单个单号只能包含字母、数字、点、下划线和连字符，多个可用逗号隔开）：%1").arg(item), ErrorType::Ai);
                return;
            }
            if (!issueTokens.contains(item, Qt::CaseInsensitive))
                issueTokens.append(item);
        }
        normalizedIssue = issueTokens.join(QStringLiteral(","));
    }
    QString normalizedSecondaryIssue;
    if (!secondaryIssueId.trimmed().isEmpty()) {
        const QStringList rawList = secondaryIssueId.split(QRegularExpression(QStringLiteral("[,;，；\\s]+")), Qt::SkipEmptyParts);
        static const QRegularExpression singleIssuePattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,49}$"));
        QStringList secondaryTokens;
        for (QString item : rawList) {
            item = item.trimmed();
            if (item.isEmpty()) continue;
            if (!singleIssuePattern.match(item).hasMatch()) {
                reportError(tr("AI 提交说明"), tr("关联单号格式不正确：%1").arg(item), ErrorType::Ai);
                return;
            }
            if (!secondaryTokens.contains(item, Qt::CaseInsensitive))
                secondaryTokens.append(item);
        }
        normalizedSecondaryIssue = secondaryTokens.join(QStringLiteral(","));
    }
    if (!m_commitAi.configured()) {
        reportError(tr("AI 提交说明"), tr("请先在设置中填写有效的 AI 接口地址和模型名称。"), ErrorType::Ai);
        return;
    }
    m_aiExistingMessage = existingMessage;
    m_aiCommitType = normalizedType;
    m_aiIssueId = normalizedIssue;
    m_aiSecondaryIssueId = normalizedSecondaryIssue;
    m_aiManualCompiled = manualCompiled;
    m_aiCompileNote = compileNote;
    m_aiPurpose = purpose.trimmed().left(300);
    m_aiChangedFiles.clear();
    m_aiUntrackedFiles.clear();
    ++m_aiRequest;
    GitCommand command;
    command.program = QStringLiteral("git");
    command.arguments = scope == 0
        ? QStringList{QStringLiteral("diff"), QStringLiteral("--cached"),
                      QStringLiteral("--name-only"), QStringLiteral("-z"), QStringLiteral("--no-renames")}
        : GitQueries::files();
    command.workingDirectory = absoluteRepositoryPath(m_activeIndex);
    command.repositoryIndex = m_activeIndex;
    command.repositoryPath = selectedPath();
    command.title = scope == 0 ? tr("读取暂存文件") : tr("读取仓库改动文件");
    command.kind = scope == 0 ? GitCommandKind::AiStagedFiles : GitCommandKind::AiWorkspaceFiles;
    command.diffRequest = m_aiRequest;
    enqueue(command);
}

QString WorkspaceController::pushPreview() const
{
    GerritPushOptions options;
    options.topic = m_repositoryConfig.topic;
    options.reviewers = GerritService::splitList(m_repositoryConfig.reviewers);
    options.cc = GerritService::splitList(m_repositoryConfig.cc);
    return GerritService::pushPreview(m_repositoryConfig.remote, m_repositoryConfig.targetBranch, options);
}

QString WorkspaceController::pushCommandPreview(const QString &remote, const QString &branch,
                                                const QVariantMap &options) const
{
    if (branch.trimmed().isEmpty()) return tr("未指定目标分支");
    const QString error = pushOptionsError(options);
    if (!error.isEmpty()) return error;
    return GerritService::pushPreview(remote.trimmed().isEmpty() ? QStringLiteral("origin") : remote.trimmed(),
                                      branch, pushOptionsFromMap(options));
}

QString WorkspaceController::pushOptionsError(const QVariantMap &options) const
{
    QString error;
    GerritService::validatePushOptions(pushOptionsFromMap(options), &error);
    return error;
}

void WorkspaceController::pushActive(const QVariantMap &extraOptions)
{
    if (busy()) { reportError(tr("无法上传"), tr("请等待当前任务完成。")); return; }
    const QScopedValueRollback<quint64> operation(m_enqueueOperationId, ++m_nextOperationId);
    if (m_activeIndex < 0) return;
    if (selectedBranch() == QStringLiteral("(detached)") || selectedBranch() == QStringLiteral("detached")) {
        reportError(tr("无法上传"), tr("当前处于 detached HEAD。请先切换到承载本次提交的本地分支。"));
        return;
    }
    if (m_repositoryConfig.targetBranch.isEmpty() || m_repositoryConfig.remote.isEmpty()) {
        reportError(tr("无法上传"), tr("当前仓库未配置 Remote 或 Gerrit 目标分支。"));
        return;
    }
    const QRegularExpression branchPattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._/-]*$"));
    if (!branchPattern.match(m_repositoryConfig.targetBranch).hasMatch() || m_repositoryConfig.targetBranch.contains(QStringLiteral(".."))) {
        reportError(tr("目标分支无效"),
                             tr("目标分支包含 Gerrit 不接受的字符。"));
        return;
    }
    // Topic, reviewers and CC are persisted per repository; the rest applies to this push only.
    QVariantMap optionMap = extraOptions;
    optionMap.insert(QStringLiteral("topic"), m_repositoryConfig.topic);
    optionMap.insert(QStringLiteral("reviewers"), m_repositoryConfig.reviewers);
    optionMap.insert(QStringLiteral("cc"), m_repositoryConfig.cc);
    const GerritPushOptions pushOptions = pushOptionsFromMap(optionMap);
    QString optionsError;
    if (!GerritService::validatePushOptions(pushOptions, &optionsError)) {
        reportError(tr("推送选项无效"), optionsError);
        return;
    }
    GitCommand command;
    command.program = QStringLiteral("git");
    const QString targetRef = QStringLiteral("refs/remotes/%1/%2")
        .arg(m_repositoryConfig.remote, m_repositoryConfig.targetBranch);
    command.arguments = {QStringLiteral("log"), QStringLiteral("--format=%H%x00%B%x00"),
                         targetRef + QStringLiteral("..HEAD")};
    command.workingDirectory = absoluteRepositoryPath(m_activeIndex);
    command.repositoryIndex = m_activeIndex;
    command.repositoryPath = selectedPath();
    command.workspacePath = m_workspacePath;
    command.target.remote = m_repositoryConfig.remote;
    command.target.branch = m_repositoryConfig.targetBranch;
    command.target.options = pushOptions;
    command.kind = GitCommandKind::PushChangeIdPreflight;
    command.title = tr("检查待推送提交的 Change-Id");
    command.stopOnFailure = true;
    m_pushOperationId = m_enqueueOperationId;
    m_pushRepositoryKey = m_workspacePath + QLatin1Char('/') + selectedPath();
    setLastGerritReviewUrl(QString());
    setPushStatus(1, tr("正在检查提交并准备推送 %1 → %2…").arg(selectedName(), m_repositoryConfig.targetBranch));
    enqueue(command);
}

void WorkspaceController::fetchChange(const QString &spec, int mode)
{
    if (m_activeIndex < 0 || mode < 0 || mode > 2) return;
    const QString title = tr("无法下载 Change");
    if (busy()) { reportError(title, tr("请等待当前任务完成。")); return; }
    int change = 0;
    int patchset = 0;
    QString error;
    if (!GerritService::parseChangeSpec(spec, &change, &patchset, &error)) {
        reportError(title, error);
        return;
    }
    if (hasPendingOperation()) {
        reportError(title, tr("请先完成或终止当前未完成的 Git 操作。"));
        return;
    }
    if (mode == 1 && isDetachedBranch(selectedBranch())) {
        reportError(title, tr("当前处于 detached HEAD；请先切换到需要接收该 Change 的本地分支。"));
        return;
    }
    const QString remote = m_repositoryConfig.remote.trimmed();
    if (remote.isEmpty() || remote.startsWith(QLatin1Char('-'))) {
        reportError(title, tr("请先在推送面板为当前仓库配置 Gerrit Remote。"));
        return;
    }
    const QScopedValueRollback<quint64> operation(m_enqueueOperationId, ++m_nextOperationId);
    GitCommand command;
    command.workingDirectory = absoluteRepositoryPath(m_activeIndex);
    command.repositoryIndex = m_activeIndex;
    command.repositoryPath = selectedPath();
    command.target.remote = remote;
    command.change.number = change;
    command.change.patchset = patchset;
    command.change.mode = mode;
    command.stopOnFailure = true;
    if (patchset > 0) {
        enqueueChangeFetch(command);
        return;
    }
    command.arguments = {QStringLiteral("ls-remote"), remote, GerritService::changeRef(change, 0)};
    command.kind = GitCommandKind::ChangeLookup;
    command.title = tr("查询 Change %1 的最新补丁集").arg(change);
    enqueue(command);
}

void WorkspaceController::enqueueChangeFetch(GitCommand command)
{
    const GitCommand::ChangeRequest change = command.change;
    command.arguments = {QStringLiteral("fetch"), QStringLiteral("--no-tags"), command.target.remote,
                         GerritService::changeRef(change.number, change.patchset)};
    command.kind = GitCommandKind::ChangeFetch;
    command.title = tr("下载 Change %1 补丁集 %2").arg(change.number).arg(change.patchset);
    enqueue(command);

    // FETCH_HEAD is consumed by the next command of the same operation; a failed fetch cancels it.
    QStringList arguments;
    QString title;
    if (change.mode == 0) {
        const QString branch = QStringLiteral("review/%1-%2").arg(change.number).arg(change.patchset);
        arguments = {QStringLiteral("checkout"), QStringLiteral("-b"), branch, QStringLiteral("FETCH_HEAD")};
        title = tr("检出到新分支 %1").arg(branch);
    } else if (change.mode == 1) {
        arguments = {QStringLiteral("cherry-pick"), QStringLiteral("FETCH_HEAD")};
        title = tr("摘取 Change %1/%2 到当前分支").arg(change.number).arg(change.patchset);
    } else {
        arguments = {QStringLiteral("checkout"), QStringLiteral("--detach"), QStringLiteral("FETCH_HEAD")};
        title = tr("以 detached HEAD 检出 Change %1/%2").arg(change.number).arg(change.patchset);
    }
    enqueueGit(command.repositoryIndex, arguments, title, GitCommandKind::General, true);
    enqueueRepositoryRefresh(command.repositoryIndex, true);
}

void WorkspaceController::requestCommitMessage(const QString &revision)
{
    static const QRegularExpression hashPattern(QStringLiteral("^[0-9a-fA-F]{7,64}$"));
    if (m_activeIndex < 0 || !hashPattern.match(revision.trimmed()).hasMatch()) return;
    GitCommand command;
    command.arguments = {QStringLiteral("log"), QStringLiteral("-1"), QStringLiteral("--format=%B"),
                         revision.trimmed(), QStringLiteral("--")};
    command.workingDirectory = absoluteRepositoryPath(m_activeIndex);
    command.repositoryIndex = m_activeIndex;
    command.repositoryPath = selectedPath();
    command.revision = revision.trimmed();
    command.kind = GitCommandKind::CommitMessage;
    command.title = tr("读取提交说明");
    enqueue(command);
}

void WorkspaceController::rewriteCommit(const QString &revision, int action, const QString &message)
{
    if (m_activeIndex < 0 || action < 0 || action > 2) return;
    const QString title = tr("无法改写提交");
    if (busy()) { reportError(title, tr("请等待当前任务完成。")); return; }
    static const QRegularExpression hashPattern(QStringLiteral("^[0-9a-fA-F]{7,64}$"));
    const QString hash = revision.trimmed().toLower();
    if (!hashPattern.match(hash).hasMatch()) {
        reportError(title, tr("提交 ID 无效。"));
        return;
    }
    if (isDetachedBranch(selectedBranch())) {
        reportError(title, tr("当前处于 detached HEAD；请先切换到承载该提交链的本地分支。"));
        return;
    }
    if (hasPendingOperation()) {
        reportError(title, tr("请先完成或终止当前未完成的 Git 操作。"));
        return;
    }
    if (action == 1) {
        const bool hasStaged = std::any_of(m_changedFiles.cbegin(), m_changedFiles.cend(), [](const QVariant &file) {
            return file.toMap().value(QStringLiteral("hasStagedChanges")).toBool();
        });
        if (!hasStaged || m_detailsCached) {
            reportError(title, tr("没有已暂存的改动。请先暂存要并入该提交的修改。"));
            return;
        }
    }
    if (action == 2 && message.trimmed().isEmpty()) {
        reportError(title, tr("提交说明不能为空。"));
        return;
    }
    // Only commits that are not yet in the Gerrit target branch may be rewritten.
    const QString remote = m_repositoryConfig.remote.trimmed();
    const QString target = m_repositoryConfig.targetBranch.trimmed();
    const QString base = !remote.isEmpty() && !target.isEmpty()
        ? QStringLiteral("refs/remotes/%1/%2").arg(remote, target) : QStringLiteral("@{upstream}");
    const QScopedValueRollback<quint64> operation(m_enqueueOperationId, ++m_nextOperationId);
    GitCommand command;
    command.arguments = {QStringLiteral("log"), QStringLiteral("--format=%H%x00%P%x00%B%x00"),
                         base + QStringLiteral("..HEAD"), QStringLiteral("--")};
    command.workingDirectory = absoluteRepositoryPath(m_activeIndex);
    command.repositoryIndex = m_activeIndex;
    command.repositoryPath = selectedPath();
    command.revision = hash;
    command.rewrite.action = action;
    command.rewrite.message = message.trimmed();
    command.target.remote = remote;
    command.target.branch = target;
    command.kind = GitCommandKind::RewritePreflight;
    command.title = tr("检查待改写的提交链");
    command.stopOnFailure = true;
    enqueue(command);
}

void WorkspaceController::enqueueRewrite(const GitCommand &completed, const QString &output)
{
    const QString title = tr("无法改写提交");
    struct Entry { QString sha; QStringList parents; QString message; };
    QList<Entry> entries;
    const QStringList fields = output.split(QChar(0));
    for (int i = 0; i + 2 < fields.size(); i += 3) {
        const QString sha = fields.at(i).trimmed();
        if (sha.isEmpty()) break;
        entries.append({sha, fields.at(i + 1).split(QLatin1Char(' '), Qt::SkipEmptyParts), fields.at(i + 2)});
    }
    int position = -1;
    for (int i = 0; i < entries.size(); ++i) {
        if (entries.at(i).sha.startsWith(completed.revision)) { position = i; break; }
    }
    if (position < 0) {
        reportError(title, tr("提交 %1 已在目标分支中或不在当前分支上。已合入 Gerrit 的提交不能改写，请改为提交新的修复。")
            .arg(completed.revision.left(10)), ErrorType::General, {}, &completed);
        return;
    }
    for (int i = 0; i <= position; ++i) {
        if (entries.at(i).parents.size() > 1) {
            reportError(title, tr("提交链中包含合并提交，无法自动改写；请在终端处理。"),
                        ErrorType::General, {}, &completed);
            return;
        }
    }
    const Entry &entry = entries.at(position);
    const QString onto = entry.parents.isEmpty() ? QStringLiteral("--root") : entry.parents.first();
    const int index = completed.repositoryIndex;
    const QScopedValueRollback<quint64> operation(m_enqueueOperationId, completed.operationId);

    if (completed.rewrite.action == 1) {
        enqueueGit(index, GitQueries::commitScope(), tr("检查提交仓库边界"),
                   GitCommandKind::CommitScopePreflight, true);
        enqueueGit(index, {QStringLiteral("commit"), QStringLiteral("--fixup=") + entry.sha},
                   tr("创建待并入 %1 的修正提交").arg(entry.sha.left(10)), GitCommandKind::General, true);
        enqueueGit(index, {QStringLiteral("-c"), QStringLiteral("sequence.editor=:"),
                           QStringLiteral("rebase"), QStringLiteral("-i"), QStringLiteral("--autosquash"),
                           QStringLiteral("--autostash"), onto},
                   tr("将暂存改动并入提交 %1").arg(entry.sha.left(10)), GitCommandKind::General, true);
        enqueueRepositoryRefresh(index, true);
        return;
    }

    // Mark only the selected commit as "edit"; the hash is validated hex, so the shell script is fixed text.
    const QString editScript = QStringLiteral("sequence.editor=sed -i -E -e 's/^(pick|p) (%1[0-9a-f]*)( |$)/edit \\2\\3/'")
        .arg(entry.sha.left(7));
    GitCommand stop = completed;
    stop.arguments = {QStringLiteral("-c"), editScript, QStringLiteral("rebase"), QStringLiteral("-i"),
                      QStringLiteral("--autostash"), onto};
    stop.kind = GitCommandKind::RewriteStop;
    stop.title = tr("停在提交 %1").arg(entry.sha.left(10));
    stop.affectedRepositories = {completed.repositoryPath};
    enqueue(stop);
    if (completed.rewrite.action == 2) {
        // Keep the Change-Id so Gerrit updates the existing change instead of creating a new one.
        static const QRegularExpression changeIdLine(QStringLiteral("(?m)^Change-Id:[ \\t]*I[0-9a-fA-F]{40}[ \\t]*$"));
        QString message = completed.rewrite.message;
        const auto original = changeIdLine.match(entry.message);
        if (original.hasMatch() && !changeIdLine.match(message).hasMatch())
            message += QStringLiteral("\n\n") + original.captured(0).trimmed();
        GitCommand amend;
        amend.arguments = {QStringLiteral("commit"), QStringLiteral("--amend"), QStringLiteral("--allow-empty"),
                           QStringLiteral("-F"), QStringLiteral("-")};
        amend.standardInput = (message + QLatin1Char('\n')).toUtf8();
        amend.workingDirectory = completed.workingDirectory;
        amend.repositoryIndex = index;
        amend.repositoryPath = completed.repositoryPath;
        amend.kind = GitCommandKind::General;
        amend.affectedRepositories = {completed.repositoryPath};
        amend.title = tr("改写提交说明");
        amend.stopOnFailure = true;
        enqueue(amend);
        enqueueGit(index, {QStringLiteral("rebase"), QStringLiteral("--continue")},
                   tr("继续 Rebase"), GitCommandKind::General, true);
    }
    enqueueRepositoryRefresh(index, true);
}

QVariantList WorkspaceController::batchPushRows() const
{
    QVariantList rows;
    for (const BatchPushRow &row : m_batchRows) {
        // Explain where the target came from so a wrong branch is noticed before pushing.
        QString note;
        bool warning = false;
        if (row.target.isEmpty()) {
            note = row.manifestRevision.isEmpty()
                ? tr("未配置目标分支，请填写")
                : tr("manifest 中为 %1，不是分支，请填写目标分支").arg(row.manifestRevision);
            warning = true;
        } else if (row.target != row.savedTarget) {
            note = row.target == row.manifestTarget ? tr("来自 manifest，推送后保存") : tr("手动指定，推送后保存");
            warning = true;
        } else if (!row.manifestTarget.isEmpty() && row.manifestTarget != row.target) {
            note = tr("与 manifest 的 %1 不一致，请确认").arg(row.manifestTarget);
            warning = true;
        }
        rows.append(QVariantMap{
            {QStringLiteral("path"), row.path},
            {QStringLiteral("name"), row.name},
            {QStringLiteral("branch"), row.branch},
            {QStringLiteral("remote"), row.remote},
            {QStringLiteral("target"), row.target},
            {QStringLiteral("state"), row.state},
            {QStringLiteral("message"), row.message},
            {QStringLiteral("url"), row.url},
            {QStringLiteral("commits"), row.commits},
            {QStringLiteral("included"), row.included},
            {QStringLiteral("note"), note},
            {QStringLiteral("warning"), warning},
        });
    }
    return rows;
}

int WorkspaceController::batchRowIndex(const QString &path) const
{
    for (int i = 0; i < m_batchRows.size(); ++i) {
        if (m_batchRows.at(i).path == path) return i;
    }
    return -1;
}

void WorkspaceController::resetBatchPush()
{
    ++m_batchRequest;
    m_batchRows.clear();
    m_batchRunning = false;
    emit batchPushChanged();
}

void WorkspaceController::prepareBatchPush()
{
    if (m_batchRunning) return;
    if (busy()) {
        reportError(tr("无法批量推送"), tr("请等待当前任务完成。"));
        return;
    }
    resetBatchPush();
    for (const QString &path : m_model.selectedPaths()) {
        const int index = m_model.indexOfPath(path);
        if (index < 0) continue;
        const auto config = WorkspaceSettings::loadRepositoryConfig(m_workspacePath, path);
        BatchPushRow row;
        row.path = path;
        row.name = m_model.nameAt(index);
        row.branch = m_model.branchAt(index);
        row.remote = config.remote.trimmed().isEmpty() ? QStringLiteral("origin") : config.remote.trimmed();
        row.savedTarget = config.targetBranchConfirmed ? config.targetBranch : QString();
        row.state = QStringLiteral("checking");
        m_batchRows.append(row);
    }
    emit batchPushChanged();
    if (m_batchRows.isEmpty()) return;
    if (!isRepoWorkspace()) {
        resolveBatchTargets({});
        return;
    }
    const QScopedValueRollback<quint64> operation(m_enqueueOperationId, ++m_nextOperationId);
    GitCommand command;
    command.program = QStringLiteral("repo");
    command.arguments = {QStringLiteral("manifest"), QStringLiteral("-o"), QStringLiteral("-")};
    command.workingDirectory = m_workspacePath;
    command.workspacePath = m_workspacePath;
    command.kind = GitCommandKind::BatchManifest;
    command.title = tr("读取 Manifest 中各仓库的分支");
    command.timeoutMilliseconds = 30000;
    command.target.batchPush = true;
    command.target.request = m_batchRequest;
    enqueue(command);
}

void WorkspaceController::resolveBatchTargets(const QHash<QString, GerritService::ManifestTarget> &manifest)
{
    for (int i = 0; i < m_batchRows.size(); ++i) {
        BatchPushRow &row = m_batchRows[i];
        const auto entry = manifest.constFind(QDir::cleanPath(row.path));
        if (entry != manifest.constEnd()) {
            row.manifestTarget = entry->branch;
            row.manifestRevision = entry->revision;
        }
        if (!row.savedTarget.isEmpty()) {
            row.target = row.savedTarget;
        } else if (!row.manifestTarget.isEmpty()) {
            row.target = row.manifestTarget;
            // Repo names each project's Git remote after the manifest remote.
            if (entry != manifest.constEnd() && !entry->remote.isEmpty())
                row.remote = entry->remote;
        }
        enqueueBatchPreflight(i, false);
    }
    emit batchPushChanged();
}

void WorkspaceController::enqueueBatchPreflight(int rowIndex, bool execute)
{
    BatchPushRow &row = m_batchRows[rowIndex];
    const int index = m_model.indexOfPath(row.path);
    row.commits = 0;
    row.url.clear();
    // Problems found while executing count as failures in the batch summary.
    const QString invalid = execute ? QStringLiteral("failed") : QStringLiteral("error");
    if (index < 0) {
        row.state = invalid;
        row.message = tr("仓库已不在列表中。");
        return;
    }
    row.branch = m_model.branchAt(index);
    if (isDetachedBranch(row.branch)) {
        row.state = invalid;
        row.message = tr("处于 detached HEAD，请先切换到承载本次提交的本地分支（如 repo start 创建的分支）。");
        return;
    }
    if (row.target.isEmpty()) {
        row.state = invalid;
        row.message = tr("请填写目标分支。");
        return;
    }
    QString branchError;
    if (!GitService::validateBranchName(row.target, &branchError)
        || !GitService::validateBranchName(row.remote, &branchError) || row.remote.contains(QLatin1Char('/'))) {
        row.state = invalid;
        row.message = tr("Remote 或目标分支无效。");
        return;
    }
    row.state = execute ? QStringLiteral("queued") : QStringLiteral("checking");
    row.message.clear();
    // Every repository is its own operation, so one failure cancels only that repository's push.
    const QScopedValueRollback<quint64> operation(m_enqueueOperationId, ++m_nextOperationId);
    GitCommand command;
    command.arguments = {QStringLiteral("log"), QStringLiteral("--format=%H%x00%B%x00"),
                         QStringLiteral("refs/remotes/%1/%2..HEAD").arg(row.remote, row.target),
                         QStringLiteral("--")};
    command.workingDirectory = absoluteRepositoryPath(index);
    command.repositoryIndex = index;
    command.repositoryPath = row.path;
    command.workspacePath = m_workspacePath;
    command.target.remote = row.remote;
    command.target.branch = row.target;
    command.target.batchPush = true;
    command.target.request = m_batchRequest;
    command.kind = execute ? GitCommandKind::PushChangeIdPreflight : GitCommandKind::BatchPreflight;
    command.title = tr("检查 %1 的待推送提交").arg(row.name);
    command.stopOnFailure = true;
    enqueue(command);
}

void WorkspaceController::setBatchPushTarget(const QString &path, const QString &branch)
{
    const int rowIndex = batchRowIndex(path);
    if (m_batchRunning || rowIndex < 0) return;
    BatchPushRow &row = m_batchRows[rowIndex];
    const QString target = branch.trimmed();
    if (row.target == target && row.state != QStringLiteral("error")) return;
    row.target = target;
    enqueueBatchPreflight(rowIndex, false);
    emit batchPushChanged();
}

void WorkspaceController::setBatchPushIncluded(const QString &path, bool included)
{
    const int rowIndex = batchRowIndex(path);
    if (m_batchRunning || rowIndex < 0 || m_batchRows.at(rowIndex).included == included) return;
    m_batchRows[rowIndex].included = included;
    emit batchPushChanged();
}

void WorkspaceController::startBatchPush(const QVariantMap &options)
{
    const QString title = tr("无法批量推送");
    if (m_batchRunning) return;
    if (busy()) { reportError(title, tr("请等待仓库检查完成。")); return; }
    QString optionsError;
    if (!GerritService::validatePushOptions(pushOptionsFromMap(options), &optionsError)) {
        reportError(title, optionsError);
        return;
    }
    QList<int> ready;
    for (int i = 0; i < m_batchRows.size(); ++i) {
        if (m_batchRows.at(i).included && m_batchRows.at(i).state == QStringLiteral("ready"))
            ready.append(i);
    }
    if (ready.isEmpty()) {
        reportError(title, tr("没有可推送的仓库。"));
        return;
    }
    m_batchPushOptions = pushOptionsFromMap(options);
    m_batchRunning = true;
    for (int i : ready) {
        // Pushing confirms the target, exactly like the single-repository push dialog.
        BatchPushRow &row = m_batchRows[i];
        auto config = WorkspaceSettings::loadRepositoryConfig(m_workspacePath, row.path);
        config.remote = row.remote;
        config.targetBranch = row.target;
        config.targetBranchConfirmed = true;
        WorkspaceSettings::saveRepositoryConfig(m_workspacePath, row.path, config);
        row.savedTarget = row.target;
        if (row.path == selectedPath() && m_activeIndex >= 0) {
            m_repositoryConfig.remote = row.remote;
            m_repositoryConfig.targetBranch = row.target;
            m_repositoryConfig.targetBranchConfirmed = true;
            emit settingsChanged();
        }
        enqueueBatchPreflight(i, true);
    }
    appendConsole(tr("开始批量推送 %1 个仓库。").arg(ready.size()));
    finishBatchPushIfDone();
    emit batchPushChanged();
}

void WorkspaceController::handleBatchPushResult(const GitCommandResult &result)
{
    const GitCommand &completed = result.command;
    if (completed.generation != m_generation || completed.target.request != m_batchRequest) return;
    const bool success = result.status == ExecutionStatus::Success;
    if (completed.kind == GitCommandKind::BatchManifest) {
        if (!success)
            appendConsole(tr("读取 Manifest 失败，仅使用已保存的目标分支。\n") + result.diagnosticMessage);
        resolveBatchTargets(success ? GerritService::parseManifestTargets(result.standardOutput)
                                    : QHash<QString, GerritService::ManifestTarget>{});
        return;
    }
    const int rowIndex = batchRowIndex(completed.repositoryPath);
    if (rowIndex < 0) return;
    BatchPushRow &row = m_batchRows[rowIndex];
    // A target edited while the previous check was queued makes that check stale.
    if (completed.target.remote != row.remote || completed.target.branch != row.target) return;

    if (result.status == ExecutionStatus::Canceled) {
        row.state = QStringLiteral("canceled");
        row.message = completed.kind == GitCommandKind::GerritPush
            ? tr("已取消；推送结果未确认，请在 Gerrit 核对。") : tr("已取消。");
    } else if (completed.kind == GitCommandKind::BatchPreflight
               || completed.kind == GitCommandKind::PushChangeIdPreflight) {
        const bool execute = completed.kind == GitCommandKind::PushChangeIdPreflight;
        const QString failed = execute ? QStringLiteral("failed") : QStringLiteral("error");
        const PendingCommits pending = scanPendingCommits(QString::fromLocal8Bit(result.standardOutput));
        row.commits = pending.count;
        if (!success) {
            row.state = failed;
            row.message = tr("本地没有 %1/%2 的远端跟踪分支，请先拉取，或检查目标分支名。")
                .arg(row.remote, row.target);
        } else if (pending.count == 0) {
            row.state = QStringLiteral("nothing");
            row.message = tr("没有待推送的提交。");
        } else if (!pending.missingChangeId.isEmpty()) {
            row.state = failed;
            row.message = tr("%1 个提交缺少 Change-Id（%2），请在该仓库单独推送修复。")
                .arg(pending.missingChangeId.size()).arg(pending.missingChangeId.join(QStringLiteral(", ")));
        } else if (!execute) {
            row.state = QStringLiteral("ready");
            row.message = tr("待推送 %1 个提交").arg(pending.count);
        } else {
            row.state = QStringLiteral("pushing");
            row.message = tr("正在推送 %1 个提交…").arg(pending.count);
            const QScopedValueRollback<quint64> operation(m_enqueueOperationId, completed.operationId);
            GitCommand push = m_gerrit.createPushCommand(completed.repositoryIndex, completed.workingDirectory,
                completed.repositoryPath, row.remote, row.target, m_batchPushOptions);
            push.workspacePath = completed.workspacePath;
            push.target = completed.target;
            push.affectedRepositories.clear();
            enqueue(push);
        }
    } else if (completed.kind == GitCommandKind::GerritPush) {
        const QString url = success ? GerritService::extractReviewUrl(result.standardOutput, result.standardError)
                                    : QString();
        if (!success) {
            row.state = QStringLiteral("failed");
            row.message = GerritService::parseGerritError(result.standardError, result.diagnosticMessage);
        } else if (url.isEmpty()) {
            row.state = QStringLiteral("failed");
            row.message = tr("未收到 Gerrit 审查链接，结果未确认；请先核对 Gerrit，避免盲目重推。");
        } else {
            row.state = QStringLiteral("success");
            row.url = url;
            row.message = tr("推送成功");
            appendConsole(tr("%1：%2").arg(row.name, url));
        }
        enqueueGit(completed.repositoryIndex, GitQueries::summary(), tr("刷新仓库状态"), GitCommandKind::Status);
    }
    finishBatchPushIfDone();
    emit batchPushChanged();
}

void WorkspaceController::finishBatchPushIfDone()
{
    if (!m_batchRunning) return;
    int succeeded = 0;
    int failed = 0;
    for (const BatchPushRow &row : m_batchRows) {
        if (row.state == QStringLiteral("queued") || row.state == QStringLiteral("pushing")) return;
        if (row.state == QStringLiteral("success")) ++succeeded;
        else if (row.state == QStringLiteral("failed") || row.state == QStringLiteral("canceled")) ++failed;
    }
    m_batchRunning = false;
    const QString summary = tr("批量推送结束：成功 %1 个，失败或未确认 %2 个。").arg(succeeded).arg(failed);
    appendConsole(summary);
    emit operationFinished(summary);
}

void WorkspaceController::repairLastCommitChangeId()
{
    if (m_activeIndex < 0 || busy()) return;
    if (m_repairChangeIdRevision.isEmpty()) {
        reportError(tr("无法修复 Change-Id"), tr("待修复提交已变化，请重新发起推送检查。"));
        return;
    }
    if (selectedBranch() == QStringLiteral("(detached)") || selectedBranch() == QStringLiteral("detached")) {
        reportError(tr("无法修复 Change-Id"), tr("请先切换到包含该提交的本地分支。"));
        return;
    }
    if (hasPendingOperation()) {
        reportError(tr("无法修复 Change-Id"), tr("请先完成或终止当前 Git 操作。"));
        return;
    }
    const QScopedValueRollback<quint64> operation(m_enqueueOperationId, ++m_nextOperationId);
    enqueue(m_gerrit.createQueryHookPathCommand(m_activeIndex, absoluteRepositoryPath(m_activeIndex),
                                               selectedPath(), GitCommandKind::HookRepairPreflight));
    GitCommand command;
    command.program = QStringLiteral("git");
    command.arguments = GitQueries::files();
    command.workingDirectory = absoluteRepositoryPath(m_activeIndex);
    command.repositoryIndex = m_activeIndex;
    command.repositoryPath = selectedPath();
    command.kind = GitCommandKind::ChangeIdRepairPreflight;
    command.revision = m_repairChangeIdRevision;
    command.title = tr("检查修订提交前的工作区");
    command.stopOnFailure = true;
    enqueue(command);
}

void WorkspaceController::checkoutBranch(const QString &branch)
{
    const QScopedValueRollback<quint64> operation(m_enqueueOperationId, ++m_nextOperationId);
    if (m_activeIndex < 0 || branch.trimmed() == selectedBranch())
        return;
    QString error;
    if (!GitService::validateBranchName(branch, &error)) {
        reportError(tr("分支名无效"), error);
        return;
    }
    enqueueGit(m_activeIndex,
               {QStringLiteral("checkout"), branch.trimmed()},
               tr("切换到 %1").arg(branch.trimmed()), GitCommandKind::General, true);
    enqueueRepositoryRefresh(m_activeIndex, true);
}

void WorkspaceController::checkoutRevision(const QString &revision)
{
    const QScopedValueRollback<quint64> operation(m_enqueueOperationId, ++m_nextOperationId);
    const QString hash = revision.trimmed();
    const QRegularExpression hashPattern(QStringLiteral("^[0-9a-fA-F]{7,64}$"));
    if (m_activeIndex < 0 || !hashPattern.match(hash).hasMatch())
        return;

    enqueueGit(m_activeIndex,
               {QStringLiteral("checkout"), QStringLiteral("--detach"), hash},
               tr("检出提交 %1").arg(hash), GitCommandKind::General, true);
    enqueueRepositoryRefresh(m_activeIndex, true);
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
    if (!validateOwnFiles({path})) return;
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
    if (m_activeIndex < 0) return;
    if (busy() || hasPendingOperation()) {
        reportError(tr("无法创建分支"), tr("请先完成当前任务或 Git 操作。"));
        return;
    }
    QString error;
    const QString name = branch.trimmed();
    if (!GitService::validateBranchName(name, &error)) {
        reportError(tr("分支名无效"), error);
        return;
    }
    if (m_availableBranches.contains(name)) {
        reportError(tr("分支已存在"), tr("请选择其他名称或切换到已有分支。"));
        return;
    }
    const QScopedValueRollback<quint64> operation(m_enqueueOperationId, ++m_nextOperationId);
    enqueueGit(m_activeIndex,
               {QStringLiteral("checkout"), QStringLiteral("-b"), name},
               tr("创建本地分支 %1").arg(name), GitCommandKind::General, true);
    enqueueRepositoryRefresh(m_activeIndex, true);
}

void WorkspaceController::deleteBranch(const QString &branch, bool force)
{
    const QScopedValueRollback<quint64> operation(m_enqueueOperationId, ++m_nextOperationId);
    if (m_activeIndex < 0)
        return;
    const QString trimmedBranch = branch.trimmed();
    if (trimmedBranch.isEmpty())
        return;
    if (trimmedBranch == selectedBranch()) {
        reportError(tr("无法删除分支"), tr("不能删除当前正在检出的分支 %1。请先切换到其他分支。").arg(trimmedBranch));
        return;
    }
    enqueueGit(m_activeIndex,
               {QStringLiteral("branch"), force ? QStringLiteral("-D") : QStringLiteral("-d"), trimmedBranch},
               tr("删除分支 %1").arg(trimmedBranch), GitCommandKind::General, true);
    enqueueRepositoryRefresh(m_activeIndex, true);
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
    if (m_activeIndex < 0 || busy()) return;
    const QString remoteName = remote.trimmed();
    const QString targetBranch = branch.trimmed();
    const QString localBranch = selectedBranch();
    static const QRegularExpression remotePattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]*$"));
    QString branchError;
    if (!remotePattern.match(remoteName).hasMatch()
        || !GitService::validateBranchName(targetBranch, &branchError)
        || !GitService::validateBranchName(localBranch, &branchError)) {
        reportError(tr("无法设置上游"), tr("请填写有效的 Remote 名称和分支名，并确认当前处于本地分支。"));
        return;
    }
    const QScopedValueRollback<quint64> operation(m_enqueueOperationId, ++m_nextOperationId);
    GitCommand query;
    query.arguments = {QStringLiteral("ls-remote"), QStringLiteral("--heads"),
                       QStringLiteral("--exit-code"), QStringLiteral("--"),
                       remoteName, QStringLiteral("refs/heads/") + targetBranch};
    query.workingDirectory = absoluteRepositoryPath(m_activeIndex);
    query.repositoryIndex = m_activeIndex;
    query.repositoryPath = selectedPath();
    query.target.remote = remoteName;
    query.target.branch = targetBranch;
    query.target.localBranch = localBranch;
    query.kind = GitCommandKind::UpstreamPreflight;
    query.title = tr("确认上游分支 %1/%2").arg(remoteName, targetBranch);
    query.stopOnFailure = true;
    enqueue(query);
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
    const QScopedValueRollback<quint64> operation(m_enqueueOperationId, ++m_nextOperationId);
    if (m_activeIndex < 0 || revision.trimmed().isEmpty())
        return;
    const QString trimmedRev = revision.trimmed();
    enqueueGit(m_activeIndex,
               {QStringLiteral("cherry-pick"), trimmedRev},
               tr("Cherry-pick 提交 %1").arg(trimmedRev.left(7)), GitCommandKind::General, true);
    enqueueRepositoryRefresh(m_activeIndex, true);
}

void WorkspaceController::revertCommit(const QString &revision)
{
    if (m_activeIndex < 0 || busy()) return;
    const QString hash = revision.trimmed();
    static const QRegularExpression hashPattern(QStringLiteral("^[0-9a-fA-F]{7,64}$"));
    if (!hashPattern.match(hash).hasMatch()) {
        reportError(tr("无法撤销提交"), tr("提交 ID 无效。"));
        return;
    }
    if (selectedBranch() == QStringLiteral("(detached)") || selectedBranch() == QStringLiteral("detached")) {
        reportError(tr("无法撤销提交"), tr("当前仓库处于 detached HEAD；请先切换到需要接收反向提交的本地分支。"));
        return;
    }
    if (hasPendingOperation()) {
        reportError(tr("无法撤销提交"), tr("请先完成或终止当前未完成的 Git 操作。"));
        return;
    }
    const QScopedValueRollback<quint64> operation(m_enqueueOperationId, ++m_nextOperationId);
    GitCommand command;
    command.program = QStringLiteral("git");
    command.arguments = GitQueries::files();
    command.workingDirectory = absoluteRepositoryPath(m_activeIndex);
    command.repositoryIndex = m_activeIndex;
    command.repositoryPath = selectedPath();
    command.kind = GitCommandKind::RevertPreflight;
    command.revision = hash;
    command.stopOnFailure = true;
    command.title = tr("检查撤销提交前的工作区");
    enqueue(command);
}

void WorkspaceController::pruneBranches()
{
    if (busy()) return;
    const QScopedValueRollback<quint64> operation(m_enqueueOperationId, ++m_nextOperationId);
    if (m_activeIndex < 0)
        return;
    if (isRepoWorkspace()) {
        GitCommand command;
        command.program = QStringLiteral("repo");
        command.arguments = {QStringLiteral("prune"), QStringLiteral(".")};
        command.workingDirectory = absoluteRepositoryPath(m_activeIndex);
        command.title = tr("清理已合入分支 (repo prune)");
        command.repositoryIndex = m_activeIndex;
        command.repositoryPath = selectedPath();
        command.stopOnFailure = false;
        enqueue(command);
    } else {
        const QString remoteName = m_repositoryConfig.remote.trimmed().isEmpty() ? QStringLiteral("origin") : m_repositoryConfig.remote.trimmed();
        enqueueGit(m_activeIndex, {"remote", "prune", remoteName},
                   tr("清理失效远端分支 (remote prune)"), GitCommandKind::General, true);
    }
    enqueueRepositoryRefresh(m_activeIndex, true);
}

void WorkspaceController::setCommitHookStatus(bool installed)
{
    if (m_hasCommitHook == installed) return;
    m_hasCommitHook = installed;
    emit hasCommitHookChanged();
}

void WorkspaceController::updateCommitHookStatus()
{
    // Unknown status is conservative; never carry a previous repository's result.
    setCommitHookStatus(m_activeIndex < 0);
    if (m_activeIndex >= 0)
        enqueue(m_gerrit.createQueryHookPathCommand(m_activeIndex, absoluteRepositoryPath(m_activeIndex),
                                                   selectedPath(), GitCommandKind::HookStatus));
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
    const QScopedValueRollback<quint64> operation(m_enqueueOperationId, ++m_nextOperationId);
    if (m_activeIndex < 0 || busy()) return;
    m_resumePendingCommit = resumePendingCommit && m_pendingCommit.valid
        && m_pendingCommit.workspacePath == m_workspacePath
        && m_pendingCommit.repositoryPath == selectedPath();
    const QString remote = m_repositoryConfig.remote.trimmed();
    if (remote.isEmpty() || remote.startsWith(QLatin1Char('-'))) {
        m_resumePendingCommit = false;
        reportError(tr("无法安装 Hook"), tr("请先为当前仓库配置有效的 Gerrit Remote。"));
        return;
    }
    const QString repository = absoluteRepositoryPath(m_activeIndex);
    GitCommand command = m_gerrit.createQueryHookRemoteCommand(m_activeIndex, repository,
                                                               selectedPath(), remote);
    enqueue(command);
}

void WorkspaceController::resumePendingCommitAfterHook()
{
    if (!m_resumePendingCommit) return;
    m_resumePendingCommit = false;
    const PendingCommit pending = m_pendingCommit;
    m_pendingCommit = {};
    if (!pending.valid || pending.workspacePath != m_workspacePath
        || pending.repositoryPath != selectedPath()) return;
    QTimer::singleShot(0, this, [this, pending] {
        if (busy() || pending.workspacePath != m_workspacePath
            || pending.repositoryPath != selectedPath()) {
            reportError(tr("提交未继续"), tr("仓库或任务状态已变化，请重新检查后提交。"));
            return;
        }
        commitActive(pending.message, pending.amend, pending.stageAll);
    });
}

void WorkspaceController::finishHookInstall(const GitCommand &completed)
{
    const QString hookPath = completed.hook.targetPath;
    const QString temporaryPath = completed.hook.temporaryPath;
    if (completed.repositoryIndex != m_activeIndex || completed.repositoryPath != selectedPath()
        || temporaryPath.isEmpty() || QFileInfo(temporaryPath).size() == 0
        || QFileInfo::exists(hookPath) || QFileInfo(hookPath).isSymLink()
        || !QFile::rename(temporaryPath, hookPath)) {
        QFile::remove(temporaryPath);
        m_resumePendingCommit = false;
        reportError(tr("无法安装 Hook"), tr("Hook 安装结果无效或目标路径已被其他程序占用；未覆盖现有文件。"));
        return;
    }
    GerritService::finalizeHookPermissions(hookPath);
    const QFileInfo installedHook(hookPath);
    setCommitHookStatus(installedHook.isFile() && installedHook.isExecutable());
    if (!m_hasCommitHook) {
        m_resumePendingCommit = false;
        reportError(tr("无法安装 Hook"), tr("Hook 文件已下载，但未通过可执行性检查：%1").arg(hookPath));
        return;
    }
    resumePendingCommitAfterHook();
}

void WorkspaceController::stop()
{
    cancelScheduledRefresh();
    if (m_batchRunning) {
        for (BatchPushRow &row : m_batchRows) {
            if (row.state == QStringLiteral("queued") || row.state == QStringLiteral("pushing")) {
                row.state = QStringLiteral("canceled");
                row.message = tr("已取消；正在推送的仓库结果未确认，请在 Gerrit 核对。");
            }
        }
        finishBatchPushIfDone();
    }
    m_pendingCommit = {};
    m_resumePendingCommit = false;
    m_repairChangeIdRevision.clear();
    if (m_pushState == 1) {
        m_pushOperationId = 0;
        setPushStatus(3, tr("推送已取消，结果未确认。"));
    }
    ++m_generation;
    ++m_aiRequest;
    m_commitAi.cancel();
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
    const QString output = QString::fromLocal8Bit(result.standardOutput);
    const bool success = result.status == ExecutionStatus::Success;

    if (completed.target.batchPush) {
        handleBatchPushResult(result);
        return;
    }
    if (!success && completed.kind == GitCommandKind::InstallHook)
        QFile::remove(completed.hook.temporaryPath);
    if (!success && (completed.kind == GitCommandKind::HookRemoteUrl
                     || completed.kind == GitCommandKind::HookDirectory
                     || completed.kind == GitCommandKind::InstallHook))
        m_resumePendingCommit = false;
    if (!success && completed.kind == GitCommandKind::PushChangeIdPreflight
        && completed.operationId == m_pushOperationId) {
        m_pushOperationId = 0;
        setPushStatus(3, tr("提交检查失败，未执行推送。"));
    }

    if (completed.kind == GitCommandKind::GerritPush
        && completed.operationId == m_pushOperationId
        && result.status == ExecutionStatus::Canceled) {
        m_pushOperationId = 0;
        setPushStatus(3, tr("推送已取消，结果未确认。"));
    }

    if (result.status == ExecutionStatus::Canceled
        || completed.generation != m_generation
        || ((completed.kind == GitCommandKind::AiStagedFiles || completed.kind == GitCommandKind::AiStagedDiff
             || completed.kind == GitCommandKind::AiWorkspaceFiles || completed.kind == GitCommandKind::AiWorkspaceDiff)
            && (completed.diffRequest != m_aiRequest || completed.repositoryIndex != m_activeIndex
                || completed.repositoryPath != selectedPath()))
        || (completed.kind == GitCommandKind::History && completed.historyRequest != m_historyRequest)
        || (completed.kind == GitCommandKind::Diff && completed.diffRequest != m_diffRequest)
        || (completed.kind == GitCommandKind::RevisionDiff && completed.diffRequest != m_revisionRequest)
        || (completed.kind == GitCommandKind::RevisionFiles && completed.diffRequest != m_revisionFilesRequest)) {
        return;
    }

    if (completed.kind == GitCommandKind::HookStatus
        || completed.kind == GitCommandKind::HookCommitPreflight
        || completed.kind == GitCommandKind::HookRepairPreflight) {
        const bool preflight = completed.kind != GitCommandKind::HookStatus;
        if (completed.repositoryIndex != m_activeIndex || completed.repositoryPath != selectedPath()) {
            if (preflight) m_gitService.cancelOperation(completed.operationId);
            return;
        }
        const QString path = output.trimmed();
        const QFileInfo hook(path);
        const bool installed = success && QDir::isAbsolutePath(path)
            && hook.fileName() == QStringLiteral("commit-msg") && hook.isFile() && hook.isExecutable();
        setCommitHookStatus(installed);
        if (completed.kind == GitCommandKind::HookRepairPreflight) m_pendingCommit = {};
        if (!preflight) {
            if (!success) appendConsole(result.diagnosticMessage);
            return;
        }
        if (!installed) {
            // Cancel staged writes and the commit/repair belonging to this task.
            m_gitService.cancelOperation(completed.operationId);
            if (!success) {
                m_pendingCommit = {};
                reportError(tr("无法检查 Change-Id Hook"), result.diagnosticMessage, ErrorType::Hook, {}, &completed);
            } else {
                reportError(tr("缺少 Change-Id Hook"),
                    completed.kind == GitCommandKind::HookCommitPreflight
                        ? tr("当前仓库没有可执行的 commit-msg Hook。请选择“安装 Hook 并继续提交”；安装失败时不会创建提交。")
                        : tr("请先安装当前仓库的 Hook，再修订提交。"), ErrorType::Hook,
                    completed.kind == GitCommandKind::HookCommitPreflight
                        ? QStringList{QStringLiteral("installHook"), QStringLiteral("resumeCommit")}
                        : QStringList{QStringLiteral("installHook")}, &completed);
            }
            return;
        }
        if (completed.kind == GitCommandKind::HookCommitPreflight) m_pendingCommit = {};
        return;
    }

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
    case GitCommandKind::CherryPickSkipPreflight: {
        const auto snapshot = GitStatusParser::parseSnapshot(output);
        if (completed.repositoryIndex != m_activeIndex || completed.repositoryPath != selectedPath()
            || completed.revision != pendingCherryPickRevision() || completed.revision.isEmpty()) {
            reportError(tr("无法跳过提交"), tr("仓库或正在拣选的提交已变化，请刷新后重试。"), ErrorType::General, {}, &completed);
            return;
        }
        if (!snapshot.files.isEmpty()) {
            reportError(tr("无法跳过提交"), tr("工作区、暂存区或冲突状态已变化。为避免丢失改动，不执行跳过。"), ErrorType::General, {}, &completed);
            enqueueRepositoryRefresh(m_activeIndex, false);
            return;
        }
        const QScopedValueRollback<quint64> operation(m_enqueueOperationId, completed.operationId);
        enqueueGit(m_activeIndex, {QStringLiteral("cherry-pick"), QStringLiteral("--skip")},
                   tr("跳过空拣选提交 %1").arg(completed.revision.left(10)), GitCommandKind::General, true);
        enqueueRepositoryRefresh(m_activeIndex, true);
        break;
    }
    case GitCommandKind::HookStatus:
    case GitCommandKind::HookCommitPreflight:
    case GitCommandKind::HookRepairPreflight:
        break; // Handled before ordinary command completion notifications.
    case GitCommandKind::PushChangeIdPreflight: {
        if (completed.repositoryIndex != m_activeIndex
            || completed.repositoryPath != selectedPath()
            || completed.workspacePath != m_workspacePath) {
            m_pushOperationId = 0;
            setPushStatus(3, tr("仓库已变化，推送已取消。"));
            return;
        }
        static const QRegularExpression changeIdLine(
            QStringLiteral("(?m)^Change-Id:[ \\t]*I[0-9a-fA-F]{40}[ \\t]*$"));
        static const QRegularExpression commitHash(QStringLiteral("^[0-9a-fA-F]{40,64}$"));
        QStringList missing;
        QStringList missingFull;
        const QStringList records = output.split(QChar(0));
        int checkedCommits = 0;
        for (int i = 0; i + 1 < records.size(); i += 2) {
            const QString sha = records.at(i).trimmed();
            if (!commitHash.match(sha).hasMatch()) continue;
            ++checkedCommits;
            if (!changeIdLine.match(records.at(i + 1)).hasMatch()) {
                missing.append(sha.left(10));
                missingFull.append(sha);
            }
        }
        if (checkedCommits == 0) {
            m_pushOperationId = 0;
            setPushStatus(3, tr("没有可确认的待推送提交。"));
            reportError(tr("无法推送"), output.trimmed().isEmpty()
                ? tr("当前 HEAD 没有领先目标远端分支的提交。")
                : tr("待推送提交的检查结果无法解析；为避免绕过 Change-Id 检查，已取消推送。"));
            return;
        }
        if (!missing.isEmpty()) {
            m_pushOperationId = 0;
            setPushStatus(3, tr("提交缺少 Change-Id，未执行推送。"));
            const bool headOnly = missingFull.size() == 1
                && !records.isEmpty() && records.first().trimmed() == missingFull.first();
            m_repairChangeIdRevision = headOnly ? missingFull.first() : QString();
            reportError(headOnly ? tr("最近提交缺少 Change-Id") : tr("待推送提交缺少 Change-Id"),
                (m_hasCommitHook
                    ? tr("待推送提交缺少有效 Change-Id：%1。修订对应提交会改变提交 SHA。")
                    : tr("待推送提交缺少有效 Change-Id：%1。请先安装 Hook；修订对应提交会改变提交 SHA。"))
                    .arg(missing.join(QStringLiteral(", "))), ErrorType::ChangeId,
                !m_hasCommitHook ? QStringList{QStringLiteral("installHook")}
                    : headOnly ? QStringList{QStringLiteral("repairChangeId")} : QStringList{}, &completed);
            return;
        }
        m_repairChangeIdRevision.clear();
        const QScopedValueRollback<quint64> operation(m_enqueueOperationId, completed.operationId);
        GitCommand push = m_gerrit.createPushCommand(completed.repositoryIndex,
            completed.workingDirectory, completed.repositoryPath,
            completed.target.remote, completed.target.branch, completed.target.options);
        push.workspacePath = completed.workspacePath;
        setPushStatus(1, tr("正在推送 %1 → %2…").arg(selectedName(), completed.target.branch));
        enqueue(push);
        enqueueGit(completed.repositoryIndex, GitQueries::summary(),
                   tr("刷新仓库状态"), GitCommandKind::Status);
        return;
    }
    case GitCommandKind::ChangeIdRepairPreflight: {
        if (completed.repositoryIndex != m_activeIndex || completed.repositoryPath != selectedPath()) {
            reportError(tr("无法修复 Change-Id"), tr("当前仓库已变化，请重新检查。"));
            return;
        }
        if (!output.isEmpty()) {
            reportError(tr("无法修复 Change-Id"), tr("工作区或暂存区有改动；为避免把额外文件带入修订提交，请先处理这些改动。"));
            return;
        }
        const QScopedValueRollback<quint64> operation(m_enqueueOperationId, completed.operationId);
        GitCommand revision = completed;
        revision.arguments = {QStringLiteral("rev-parse"), QStringLiteral("HEAD")};
        revision.kind = GitCommandKind::ChangeIdRepairRevision;
        revision.title = tr("核对待修订提交");
        enqueue(revision);
        return;
    }
    case GitCommandKind::ChangeIdRepairRevision: {
        if (completed.repositoryIndex != m_activeIndex || completed.repositoryPath != selectedPath()
            || completed.revision != m_repairChangeIdRevision
            || output.trimmed() != m_repairChangeIdRevision) {
            reportError(tr("无法修复 Change-Id"), tr("HEAD 已变化；请重新检查待推送提交。"));
            return;
        }
        m_repairChangeIdRevision.clear();
        const QScopedValueRollback<quint64> operation(m_enqueueOperationId, completed.operationId);
        enqueueGit(completed.repositoryIndex,
                   {QStringLiteral("commit"), QStringLiteral("--amend"), QStringLiteral("--no-edit")},
                   tr("修复最近提交的 Change-Id"), GitCommandKind::General, true);
        enqueueGit(completed.repositoryIndex,
                   {QStringLiteral("log"), QStringLiteral("-1"), QStringLiteral("--format=%B")},
                   tr("验证修订后的 Change-Id"), GitCommandKind::ChangeIdRepairVerification, true);
        enqueueRepositoryRefresh(completed.repositoryIndex, true);
        return;
    }
    case GitCommandKind::ChangeIdRepairVerification: {
        static const QRegularExpression changeIdLine(
            QStringLiteral("(?m)^Change-Id:[ \\t]*I[0-9a-fA-F]{40}[ \\t]*$"));
        if (!changeIdLine.match(output).hasMatch()) {
            reportError(tr("Change-Id 修复未生效"),
                tr("Amend 已执行，但最近提交仍无有效 Change-Id。请检查当前仓库的 commit-msg Hook；未执行推送。"));
        } else {
            appendConsole(tr("最近提交已补齐 Change-Id；请核对提交后再推送。") + QLatin1Char('\n'));
        }
        return;
    }
    case GitCommandKind::RepoSync:
        if (completed.workspacePath != m_workspacePath) return;
        if (completed.rediscoverProjects) {
            appendConsole(tr("操作完成 · %1").arg(completed.title));
            emit operationFinished(completed.title);
            scanWorkspace();
            return;
        }
        for (const QString &path : completed.affectedRepositories) {
            const int index = m_model.indexOfPath(path);
            if (index >= 0)
                enqueueRepositoryRefresh(index, index == m_activeIndex);
        }
        break;
    case GitCommandKind::RevertPreflight: {
        if (completed.repositoryIndex != m_activeIndex || completed.repositoryPath != selectedPath()) {
            reportError(tr("无法撤销提交"), tr("当前仓库已变化，请重新选择提交。"));
            return;
        }
        if (!output.isEmpty()) {
            reportError(tr("无法撤销提交"), tr("工作区或暂存区有未提交改动，请先提交或暂存，再撤销该提交。"));
            return;
        }
        const QScopedValueRollback<quint64> operation(m_enqueueOperationId, completed.operationId);
        enqueueGit(completed.repositoryIndex,
                   {QStringLiteral("revert"), QStringLiteral("--no-edit"), completed.revision},
                   tr("撤销提交 %1").arg(completed.revision.left(10)), GitCommandKind::General, true);
        enqueueRepositoryRefresh(completed.repositoryIndex, true);
        break;
    }
    case GitCommandKind::AiStagedFiles: {
        QStringList files;
        for (const QString &path : output.split(QChar(0), Qt::SkipEmptyParts)) {
            if (isOwnRepositoryPath(completed.repositoryIndex, path)) files.append(path);
        }
        files.removeDuplicates();
        if (files.isEmpty()) {
            reportError(tr("AI 提交说明"), tr("当前仓库没有可用于生成说明的暂存文件。"), ErrorType::Ai);
            break;
        }
        GitCommand diff;
        diff.program = QStringLiteral("git");
        diff.arguments = {QStringLiteral("--literal-pathspecs"), QStringLiteral("diff"), QStringLiteral("--cached"),
            QStringLiteral("--no-renames"), QStringLiteral("--no-ext-diff"),
            QStringLiteral("--no-textconv"), QStringLiteral("--no-color"),
            QStringLiteral("--unified=4"), QStringLiteral("--")};
        diff.arguments.append(files);
        diff.workingDirectory = completed.workingDirectory;
        diff.repositoryIndex = completed.repositoryIndex;
        diff.repositoryPath = completed.repositoryPath;
        diff.title = tr("读取暂存区 Diff");
        diff.kind = GitCommandKind::AiStagedDiff;
        diff.diffRequest = completed.diffRequest;
        diff.paths = files;
        enqueue(diff);
        break;
    }
    case GitCommandKind::AiStagedDiff:
        if (output.trimmed().isEmpty()) {
            reportError(tr("AI 提交说明"), tr("暂存区 Diff 为空，未生成提交说明。"), ErrorType::Ai);
        } else {
            m_commitAi.generate(selectedName(), selectedBranch(), output, completed.paths,
                                m_aiExistingMessage, m_aiCommitType, m_aiIssueId, false,
                                m_aiManualCompiled, m_aiCompileNote, m_aiPurpose, m_aiSecondaryIssueId);
        }
        break;
    case GitCommandKind::AiWorkspaceFiles: {
        const auto changed = ownRepositoryFiles(completed.repositoryIndex, GitStatusParser::parse(output));
        QStringList trackedPaths;
        for (const QVariant &value : changed) {
            const QVariantMap entry = value.toMap();
            const QString path = entry.value(QStringLiteral("path")).toString();
            if (path.isEmpty()) continue;
            m_aiChangedFiles.append(path);
            if (entry.value(QStringLiteral("untracked")).toBool()) {
                m_aiUntrackedFiles.append(path);
            } else {
                trackedPaths.append(path);
                const QString original = entry.value(QStringLiteral("originalPath")).toString();
                if (!original.isEmpty()) trackedPaths.append(original);
            }
        }
        m_aiChangedFiles.removeDuplicates();
        trackedPaths.removeDuplicates();
        if (m_aiChangedFiles.isEmpty()) {
            reportError(tr("AI 提交说明"), tr("当前仓库没有改动文件。"), ErrorType::Ai);
            break;
        }
        if (trackedPaths.isEmpty()) {
            const QString preview = untrackedAiPreview(completed.workingDirectory, m_aiUntrackedFiles);
            if (preview.trimmed().isEmpty()) {
                reportError(tr("AI 提交说明"), tr("未找到可供生成说明的文本改动。"), ErrorType::Ai);
                break;
            }
            m_commitAi.generate(selectedName(), selectedBranch(), preview, m_aiChangedFiles,
                                m_aiExistingMessage, m_aiCommitType, m_aiIssueId, true,
                                m_aiManualCompiled, m_aiCompileNote, m_aiPurpose, m_aiSecondaryIssueId);
            break;
        }
        GitCommand diff;
        diff.program = QStringLiteral("git");
        diff.arguments = {QStringLiteral("--literal-pathspecs"), QStringLiteral("diff"), QStringLiteral("HEAD"),
                          QStringLiteral("--no-renames"), QStringLiteral("--no-ext-diff"),
                          QStringLiteral("--no-textconv"), QStringLiteral("--no-color"),
                          QStringLiteral("--unified=4"), QStringLiteral("--")};
        diff.arguments.append(trackedPaths);
        diff.workingDirectory = completed.workingDirectory;
        diff.repositoryIndex = completed.repositoryIndex;
        diff.repositoryPath = completed.repositoryPath;
        diff.title = tr("读取仓库 Diff");
        diff.kind = GitCommandKind::AiWorkspaceDiff;
        diff.diffRequest = completed.diffRequest;
        enqueue(diff);
        break;
    }
    case GitCommandKind::AiWorkspaceDiff: {
        const QString preview = untrackedAiPreview(completed.workingDirectory, m_aiUntrackedFiles);
        const QString combined = output.left(6000) + preview;
        if (combined.trimmed().isEmpty()) {
            reportError(tr("AI 提交说明"), tr("仓库 Diff 为空，未生成提交说明。"), ErrorType::Ai);
        } else {
            m_commitAi.generate(selectedName(), selectedBranch(), combined, m_aiChangedFiles,
                                m_aiExistingMessage, m_aiCommitType, m_aiIssueId, true,
                                m_aiManualCompiled, m_aiCompileNote, m_aiPurpose, m_aiSecondaryIssueId);
        }
        break;
    }
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
    case GitCommandKind::CommitMessage:
        if (completed.repositoryIndex == m_activeIndex && completed.repositoryPath == selectedPath()) {
            QString message = output;
            while (message.endsWith(QLatin1Char('\n'))) message.chop(1);
            emit commitMessageReady(completed.revision, message);
        }
        break;
    case GitCommandKind::ChangeLookup: {
        if (completed.repositoryIndex != m_activeIndex || completed.repositoryPath != selectedPath()) {
            reportError(tr("无法下载 Change"), tr("当前仓库已变化，请重新操作。"));
            return;
        }
        const int latest = GerritService::latestPatchset(output, completed.change.number);
        if (latest <= 0) {
            reportError(tr("无法下载 Change"),
                tr("远端 %1 上找不到 Change %2。请确认 Change 号正确、当前账号有权访问，且该 Change 属于当前仓库。")
                    .arg(completed.target.remote).arg(completed.change.number),
                ErrorType::General, {}, &completed);
            return;
        }
        appendConsole(tr("Change %1 的最新补丁集为 %2。\n").arg(completed.change.number).arg(latest));
        const QScopedValueRollback<quint64> operation(m_enqueueOperationId, completed.operationId);
        GitCommand fetch = completed;
        fetch.change.patchset = latest;
        enqueueChangeFetch(fetch);
        return;
    }
    case GitCommandKind::ChangeFetch:
    case GitCommandKind::BatchManifest:
    case GitCommandKind::BatchPreflight:
        break;
    case GitCommandKind::RewritePreflight:
        if (completed.repositoryIndex != m_activeIndex || completed.repositoryPath != selectedPath()) {
            reportError(tr("无法改写提交"), tr("当前仓库已变化，请重新操作。"));
            return;
        }
        enqueueRewrite(completed, output);
        return;
    case GitCommandKind::RewriteStop:
        if (completed.rewrite.action == 0)
            appendConsole(tr("已停在提交 %1：修改文件并暂存后，在提交面板勾选“修订”提交，再点击顶部“继续”。\n")
                              .arg(completed.revision.left(10)));
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
    case GitCommandKind::UpstreamPreflight: {
        const QString remoteName = completed.target.remote;
        const QString branch = completed.target.branch;
        const QString localBranch = completed.target.localBranch;
        if (completed.repositoryIndex != m_activeIndex || localBranch != selectedBranch()) {
            m_gitService.cancelOperation(completed.operationId);
            reportError(tr("无法设置上游"), tr("当前仓库或本地分支已变化，请重新选择。"));
            break;
        }
        bool branchExists = false;
        for (const QString &line : output.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            if (line.endsWith(QStringLiteral("\trefs/heads/") + branch)) {
                branchExists = true;
                break;
            }
        }
        if (!branchExists) {
            m_gitService.cancelOperation(completed.operationId);
            reportError(tr("无法设置上游"), tr("远端分支不存在：%1/%2").arg(remoteName, branch));
            break;
        }
        const QScopedValueRollback<quint64> operation(m_enqueueOperationId, completed.operationId);
        GitCommand setRemote = completed;
        setRemote.arguments = {QStringLiteral("config"), QStringLiteral("--local"), QStringLiteral("--replace-all"),
                               QStringLiteral("branch.%1.remote").arg(localBranch), remoteName};
        setRemote.kind = GitCommandKind::UpstreamConfigRemote;
        setRemote.title = tr("设置 %1 的上游 Remote").arg(localBranch);
        enqueue(setRemote);
        GitCommand setMerge = completed;
        setMerge.arguments = {QStringLiteral("config"), QStringLiteral("--local"), QStringLiteral("--replace-all"),
                              QStringLiteral("branch.%1.merge").arg(localBranch),
                              QStringLiteral("refs/heads/") + branch};
        setMerge.kind = GitCommandKind::UpstreamConfigMerge;
        setMerge.title = tr("设置 %1 的上游分支").arg(localBranch);
        enqueue(setMerge);
        break;
    }
    case GitCommandKind::UpstreamConfigMerge:
        appendConsole(tr("已将 %1 的拉取上游设为 %2/%3；可重新执行拉取。")
                      .arg(completed.target.localBranch, completed.target.remote, completed.target.branch));
        enqueueRepositoryRefresh(completed.repositoryIndex, false);
        break;
    case GitCommandKind::UpstreamConfigRemote:
        break;
    case GitCommandKind::PullTarget: {
        const QScopedValueRollback<quint64> operation(m_enqueueOperationId, completed.operationId);
        QString remote, remoteRef;
        for (const QString &line : output.split(QLatin1Char('\n'))) {
            const auto fields = line.split(QChar(0));
            if (fields.size() == 3 && fields.at(0) == QStringLiteral("*")) {
                remote = fields.at(1);
                remoteRef = fields.at(2);
                break;
            }
        }
        if (remote.isEmpty() || !remoteRef.startsWith(QStringLiteral("refs/heads/"))
            || remoteRef.contains(QLatin1Char('*')) || remoteRef.contains(QLatin1Char(':'))) {
            m_gitService.cancelOperation(completed.operationId);
            const QString error = tr("仓库 %1 的当前分支未设置可用的拉取上游。请选中该仓库，在拉取面板明确设置 Remote 和远端分支；不会使用 Gerrit 推送目标代替。")
                .arg(m_model.nameAt(completed.repositoryIndex));
            appendConsole(error);
            reportError(tr("无法拉取"), error, ErrorType::Upstream, {QStringLiteral("upstream")}, &completed);
            break;
        }
        appendConsole(tr("仅拉取 %1 的 %2；不获取其他分支、标签或子模块。")
                      .arg(remote, remoteRef));
        const bool rebase = completed.target.pullRebase;
        GitCommand pull = completed;
        pull.arguments = dailyPullArguments(remote, remoteRef, rebase);
        pull.title = rebase ? tr("变基拉取仓库 %1").arg(m_model.nameAt(completed.repositoryIndex))
                            : tr("拉取仓库 %1").arg(m_model.nameAt(completed.repositoryIndex));
        pull.kind = GitCommandKind::PullExecution;
        // A conflicting rebase stops mid-way; refresh so the conflict banner appears.
        pull.affectedRepositories = {completed.repositoryPath};
        enqueue(pull);
        enqueueRepositoryRefresh(completed.repositoryIndex, completed.repositoryIndex == m_activeIndex);
        break;
    }
    case GitCommandKind::CommitScopePreflight: {
        const auto files = GitStatusParser::parseSnapshot(output).files;
        for (const auto &file : files) {
            const auto entry = file.toMap();
            if (entry.value(QStringLiteral("hasStagedChanges")).toBool()
                && ownRepositoryFiles(completed.repositoryIndex, QVariantList{file}).isEmpty()) {
                m_gitService.cancelOperation(completed.operationId);
                reportError(tr("无法提交"),
                    tr("父仓库暂存区包含嵌套仓库路径：%1。本次提交已阻止，嵌套仓库暂存项未被自动取消。")
                        .arg(entry.value(QStringLiteral("path")).toString()));
                break;
            }
        }
        break;
    }
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
    case GitCommandKind::DiscardPreflight: {
        const QScopedValueRollback<quint64> operation(m_enqueueOperationId, completed.operationId);
        executeDiscard(completed.repositoryIndex, completed.paths, output);
        break;
    }
    case GitCommandKind::HookRemoteUrl: {
        if (completed.repositoryIndex != m_activeIndex || completed.repositoryPath != selectedPath()) {
            m_resumePendingCommit = false;
            reportError(tr("无法安装 Hook"), tr("当前仓库已变化，请重新操作。"));
            return;
        }
        QString source, port, error;
        if (!GerritService::hookSourceFromRemoteUrl(output, &source, &port, &error)) {
            m_resumePendingCommit = false;
            reportError(tr("无法安装 Hook"), error);
            return;
        }
        const QScopedValueRollback<quint64> operation(m_enqueueOperationId, completed.operationId);
        GitCommand query = completed;
        query.arguments = {QStringLiteral("rev-parse"), QStringLiteral("--path-format=absolute"),
                           QStringLiteral("--git-path"), QStringLiteral("hooks/commit-msg")};
        query.kind = GitCommandKind::HookDirectory;
        query.hook.source = source;
        query.hook.port = port;
        query.title = tr("读取有效的 Hook 路径");
        enqueue(query);
        return;
    }
    case GitCommandKind::HookDirectory: {
        const QScopedValueRollback<quint64> operation(m_enqueueOperationId, completed.operationId);
        const QString targetHook = output.trimmed();
        if (completed.repositoryIndex != m_activeIndex || completed.repositoryPath != selectedPath()
            || !QDir::isAbsolutePath(targetHook)
            || QFileInfo(targetHook).fileName() != QStringLiteral("commit-msg")) {
            m_resumePendingCommit = false;
            reportError(tr("无法安装 Hook"), tr("无法确认当前仓库的有效 Hook 路径。"));
            return;
        }
        const QFileInfo existing(targetHook);
        if (existing.exists() || existing.isSymLink()) {
            setCommitHookStatus(existing.isFile() && existing.isExecutable());
            if (m_hasCommitHook) {
                resumePendingCommitAfterHook();
            } else {
                m_resumePendingCommit = false;
                reportError(tr("无法安装 Hook"),
                    tr("有效 Hook 路径已有文件但不可执行：%1。程序不会覆盖它，请检查后重试。")
                        .arg(targetHook));
            }
            return;
        }
        if (!QDir().mkpath(existing.path())) {
            m_resumePendingCommit = false;
            reportError(tr("无法安装 Hook"), tr("无法创建 Hook 目录：%1").arg(existing.path()));
            return;
        }

        // smart-commit spec: in a repo workspace, check if <repo根>/.repo/repo/hooks/commit-msg exists locally
        const QString repoHook = QDir(m_workspacePath).filePath(QStringLiteral(".repo/repo/hooks/commit-msg"));
        if (QFile::exists(repoHook) && QFileInfo(repoHook).size() > 0) {
            if (QFile::copy(repoHook, targetHook)) {
                GerritService::finalizeHookPermissions(targetHook);
                const QFileInfo installed(targetHook);
                setCommitHookStatus(installed.isFile() && installed.isExecutable());
                if (m_hasCommitHook) {
                    appendConsole(tr("已从本地 Repo 工具源 (.repo/repo/hooks/commit-msg) 自动就地安装 Hook。\n"));
                    resumePendingCommitAfterHook();
                    return;
                }
            }
        }
        QTemporaryFile temporary(QDir(existing.path()).filePath(QStringLiteral(".commit-msg.XXXXXX")));
        temporary.setAutoRemove(false);
        if (!temporary.open()) {
            m_resumePendingCommit = false;
            reportError(tr("无法安装 Hook"), tr("无法准备 Hook 临时文件。"));
            return;
        }
        const QString temporaryPath = temporary.fileName();
        temporary.close();
        if (GerritService::isHttpHookSource(completed.hook.source)) {
            appendConsole(tr("正在从 %1 下载 Change-Id Hook…\n").arg(completed.hook.source));
            m_gerrit.downloadHook(completed, targetHook, temporaryPath);
            return;
        }
        GitCommand command = m_gerrit.createInstallHookCommand(completed, targetHook, temporaryPath);
        enqueue(command);
        return;
    }
    case GitCommandKind::StashVerify: {
        const QString expectedSha = completed.stash.expectedSha.trimmed();
        const QString action = completed.stash.action;
        const QString opTitle = completed.stash.title;

        QString matchedRef;
        const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            const QStringList parts = line.split(QLatin1Char('\0'));
            if (parts.size() >= 2) {
                const QString ref = parts.at(0).trimmed();
                const QString sha = parts.at(1).trimmed();
                if (!expectedSha.isEmpty() && (sha.startsWith(expectedSha) || expectedSha.startsWith(sha))) {
                    matchedRef = ref;
                    break;
                }
            }
        }

        if (matchedRef.isEmpty()) {
            const QString msg = tr("Stash 操作已取消：未找到匹配 SHA [%1] 的 Stash（可能已被修改或删除），防止误操作。").arg(expectedSha);
            appendConsole(msg + QLatin1Char('\n'));
            reportError(opTitle, msg);
            loadStashList();
            break;
        }

        enqueueGit(completed.repositoryIndex, {QStringLiteral("stash"), action, matchedRef},
                   opTitle, GitCommandKind::StashAction, true);
        if (action == QLatin1String("pop") || action == QLatin1String("apply")) {
            enqueueRepositoryRefresh(completed.repositoryIndex, false);
        }
        loadStashList();
        break;
    }
    case GitCommandKind::StashList: {
        if (completed.generation == m_generation && completed.repositoryIndex == m_activeIndex
            && completed.repositoryPath == selectedPath()) {
            m_stashList.clear();
            m_stashEntries.clear();
            const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            for (const QString &line : lines) {
                const QStringList parts = line.split(QLatin1Char('\0'));
                if (parts.size() >= 3) {
                    m_stashEntries.append(StashEntry{parts.at(0).trimmed(), parts.at(1).trimmed(), parts.at(2).trimmed(), completed.repositoryPath});
                    m_stashList.append(parts.at(2).trimmed());
                } else {
                    m_stashEntries.append(StashEntry{QString(), QString(), line.trimmed(), completed.repositoryPath});
                    m_stashList.append(line.trimmed());
                }
            }
            m_stashRepositoryPath = completed.repositoryPath;
            emit stashListChanged();
        }
        break;
    }
    case GitCommandKind::InstallHook:
        finishHookInstall(completed);
        return;
    case GitCommandKind::HunkOperation:
    case GitCommandKind::General:
        break;
    }

    if (!success) {
        invalidateFailedQuery(completed);
        QString errorMsg = result.diagnosticMessage;
        ErrorType errorType = ErrorType::General;
        QStringList errorActions;
        if (completed.kind == GitCommandKind::AiStagedFiles || completed.kind == GitCommandKind::AiStagedDiff
            || completed.kind == GitCommandKind::AiWorkspaceFiles || completed.kind == GitCommandKind::AiWorkspaceDiff)
            errorType = ErrorType::Ai;
        else if (completed.kind == GitCommandKind::UpstreamPreflight
                 || completed.kind == GitCommandKind::UpstreamConfigRemote
                 || completed.kind == GitCommandKind::UpstreamConfigMerge) {
            errorType = ErrorType::Upstream;
            errorActions = {QStringLiteral("upstream")};
        }
        if (completed.repositoryIndex == m_activeIndex && hasPendingOperation() && errorType == ErrorType::General) {
            errorType = ErrorType::Conflict;
            errorActions = {QStringLiteral("conflicts")};
        }
        if (completed.kind == GitCommandKind::GerritPush) {
            errorMsg = GerritService::parseGerritError(result.standardError, result.diagnosticMessage);
            if (completed.operationId == m_pushOperationId) {
                m_pushOperationId = 0;
                setPushStatus(3, tr("推送失败，请查看错误信息和命令日志。"));
            }
        } else if (completed.kind == GitCommandKind::ChangeLookup || completed.kind == GitCommandKind::ChangeFetch) {
            const QString stderrLower = QString::fromLocal8Bit(result.standardError).toLower();
            if (!GerritService::credentialHint(result.standardError).isEmpty())
                errorMsg = GerritService::credentialHint(result.standardError);
            else if (stderrLower.contains(QStringLiteral("couldn't find remote ref")))
                errorMsg = tr("远端不存在 Change %1 的补丁集 %2，或该 Change 不属于当前仓库。")
                    .arg(completed.change.number).arg(completed.change.patchset);
        } else if (completed.kind == GitCommandKind::RewritePreflight) {
            errorMsg = tr("无法确定提交链的基准分支。请先在推送面板选择 Gerrit 目标分支，或为当前分支设置拉取上游。\n")
                + result.diagnosticMessage;
        } else if (completed.kind == GitCommandKind::PushChangeIdPreflight) {
            errorMsg = tr("无法检查目标分支到 HEAD 的待推送提交。请先获取目标分支的远端跟踪引用，再重试推送。\n")
                + result.diagnosticMessage;
        } else if (completed.kind == GitCommandKind::UpstreamPreflight && result.exitCode == 2) {
            errorMsg = tr("远端不存在分支 %1/%2，请重新选择。")
                .arg(completed.target.remote, completed.target.branch);
        } else if (completed.kind == GitCommandKind::UpstreamConfigRemote
                   || completed.kind == GitCommandKind::UpstreamConfigMerge) {
            errorMsg = tr("写入本地上游配置失败，配置可能只完成了一部分。请检查当前分支设置。\n") + errorMsg;
        } else if (completed.kind == GitCommandKind::PullExecution) {
            const QString stderrLower = QString::fromLocal8Bit(result.standardError).toLower();
            if (stderrLower.contains(QStringLiteral("no tracking information")) || stderrLower.contains(QStringLiteral("set-upstream"))) {
                errorType = ErrorType::Upstream;
                errorActions = {QStringLiteral("upstream")};
                errorMsg = tr("当前分支未设置拉取上游。请在拉取面板选择目标 Remote 和远端分支后重试。");
            } else if (stderrLower.contains(QStringLiteral("not possible to fast-forward")) || stderrLower.contains(QStringLiteral("non-fast-forward"))) {
                errorMsg = tr("本地分支与远端已产生分叉，无法快进（fast-forward）拉取。\n可在拉取面板选择“变基（Rebase）”方式重新拉取，本地提交会保留 Change-Id 并接到远端最新提交之后。");
            } else if (completed.target.pullRebase && stderrLower.contains(QStringLiteral("could not apply"))) {
                errorMsg = tr("变基拉取时发生冲突，变基已暂停。\n请在改动列表解决冲突并标记已解决后点击“继续”，或点击“终止”恢复到拉取前的状态。");
            } else if (stderrLower.contains(QStringLiteral("cannot pull with rebase")) || stderrLower.contains(QStringLiteral("cannot rebase"))) {
                errorMsg = tr("工作区或暂存区存在未提交的改动，无法变基拉取。\n请先提交或贮藏（Stash）本地改动后再拉取。");
            } else if (!GerritService::credentialHint(result.standardError).isEmpty()) {
                errorMsg = GerritService::credentialHint(result.standardError);
            } else if (stderrLower.contains(QStringLiteral("would be overwritten by merge")) || stderrLower.contains(QStringLiteral("local changes"))) {
                errorMsg = tr("本地存在未提交的文件改动，会被拉取操作覆盖。\n请先暂存（Stash）或提交本地改动后再拉取。");
            } else if (stderrLower.contains(QStringLiteral("could not resolve host")) || stderrLower.contains(QStringLiteral("connection timed out")) || stderrLower.contains(QStringLiteral("network is unreachable"))) {
                errorMsg = tr("无法连接远端服务器，请检查网络连接、VPN 或代理设置。");
            } else if (stderrLower.contains(QStringLiteral("permission denied")) || stderrLower.contains(QStringLiteral("authentication failed"))) {
                errorMsg = tr("Git 远端认证失败，请检查 SSH 密钥或访问凭据。");
            }
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

        if (completed.kind == GitCommandKind::GerritPush) {
            const QString url = GerritService::extractReviewUrl(result.standardOutput, result.standardError);
            if (url.isEmpty()) {
                const QString message = tr("Git 命令已结束，但未收到 Gerrit 审查链接；按推送未确认处理，请先核对命令日志或 Gerrit，避免盲目重推。");
                if (completed.operationId == m_pushOperationId) {
                    m_pushOperationId = 0;
                    setPushStatus(3, tr("未收到 Gerrit 回传；推送未确认，按失败处理。"));
                }
                appendConsole(message);
                reportError(completed.title, message);
                emit summaryChanged();
                return;
            }
            if (completed.workspacePath == m_workspacePath)
                setLastGerritReviewUrl(url);
            appendConsole(tr("Gerrit 审查链接已生成：%1").arg(url));
            if (completed.operationId == m_pushOperationId) {
                m_pushOperationId = 0;
                setPushStatus(2, tr("推送成功，已收到 Gerrit 审查链接。"));
            }
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

bool WorkspaceController::validateOwnFiles(const QStringList &paths)
{
    const auto files = ownRepositoryFiles(m_activeIndex, m_changedFiles);
    for (const auto &path : paths) {
        bool found = false;
        for (const auto &file : files) {
            if (file.toMap().value(QStringLiteral("path")).toString() == path) { found = true; break; }
        }
        if (!found) {
            reportError(tr("无法操作文件"), tr("路径不属于当前仓库的有效改动，请刷新或选择对应的嵌套仓库：%1").arg(path));
            return false;
        }
    }
    return true;
}

bool WorkspaceController::prepareScopedStage(GitCommand &command)
{
    if (command.kind != GitCommandKind::StageAll || command.workingDirectory != absoluteRepositoryPath(m_activeIndex))
        return false;
    QStringList paths;
    for (const auto &file : ownRepositoryFiles(m_activeIndex, m_changedFiles)) {
        const auto entry = file.toMap();
        paths.append(entry.value(QStringLiteral("path")).toString());
        const auto original = entry.value(QStringLiteral("originalPath")).toString();
        if (!original.isEmpty()) paths.append(original);
    }
    paths.removeDuplicates();
    if (paths.isEmpty()) {
        reportError(tr("无法暂存"), tr("当前仓库没有可暂存的文件。"));
        return false;
    }
    command.arguments = {QStringLiteral("--literal-pathspecs"), QStringLiteral("add"), QStringLiteral("-A"), QStringLiteral("--")};
    command.arguments.append(paths);
    return true;
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

void WorkspaceController::setLastGerritReviewUrl(const QString &url)
{
    if (m_lastGerritReviewUrl == url) return;
    m_lastGerritReviewUrl = url;
    emit lastGerritReviewUrlChanged();
}

void WorkspaceController::setPushStatus(int state, const QString &text)
{
    m_pushState = state;
    m_pushStatusText = text;
    emit pushStatusChanged();
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
    if (m_activeIndex < 0 || m_selectedFile.isEmpty()) return;
    if (!validateOwnFiles({m_selectedFile})) return;
    const auto &file = currentDiffIndex();
    if (file.isBinary || hunkIndex < 0 || hunkIndex >= file.hunks.size()) return;

    if (file.hunks.size() <= 1) {
        stageFiles({m_selectedFile});
        return;
    }

    GitCommand command;
    command.program = QStringLiteral("git");
    command.arguments = {QStringLiteral("apply"), QStringLiteral("--cached"), QStringLiteral("-")};
    command.workingDirectory = absoluteRepositoryPath(m_activeIndex);
    command.title = tr("暂存代码块 #%1").arg(hunkIndex + 1);
    command.kind = GitCommandKind::HunkOperation;
    command.repositoryIndex = m_activeIndex;
    command.repositoryPath = m_model.pathAt(m_activeIndex);
    command.affectedRepositories = {m_model.pathAt(m_activeIndex)};
    command.standardInput = DiffModel::patch(m_diffText, file, hunkIndex).toUtf8();
    command.stopOnFailure = true;
    enqueue(command);
    enqueueRepositoryRefresh(m_activeIndex, false);
}

void WorkspaceController::unstageHunk(int hunkIndex)
{
    if (m_activeIndex < 0 || m_selectedFile.isEmpty()) return;
    if (!validateOwnFiles({m_selectedFile})) return;
    const auto &file = currentDiffIndex();
    if (file.isBinary || hunkIndex < 0 || hunkIndex >= file.hunks.size()) return;

    if (file.hunks.size() <= 1) {
        unstageFiles({m_selectedFile});
        return;
    }

    GitCommand command;
    command.program = QStringLiteral("git");
    command.arguments = {QStringLiteral("apply"), QStringLiteral("--cached"), QStringLiteral("--reverse"), QStringLiteral("-")};
    command.workingDirectory = absoluteRepositoryPath(m_activeIndex);
    command.title = tr("取消暂存代码块 #%1").arg(hunkIndex + 1);
    command.kind = GitCommandKind::HunkOperation;
    command.repositoryIndex = m_activeIndex;
    command.repositoryPath = m_model.pathAt(m_activeIndex);
    command.affectedRepositories = {m_model.pathAt(m_activeIndex)};
    command.standardInput = DiffModel::patch(m_diffText, file, hunkIndex).toUtf8();
    command.stopOnFailure = true;
    enqueue(command);
    enqueueRepositoryRefresh(m_activeIndex, false);
}

void WorkspaceController::discardHunk(int hunkIndex)
{
    if (m_activeIndex < 0 || m_selectedFile.isEmpty()) return;
    if (!validateOwnFiles({m_selectedFile})) return;
    const auto &file = currentDiffIndex();
    if (file.isBinary || hunkIndex < 0 || hunkIndex >= file.hunks.size()) return;

    if (file.hunks.size() <= 1) {
        discardFiles({m_selectedFile});
        return;
    }

    GitCommand command;
    command.program = QStringLiteral("git");
    command.arguments = {QStringLiteral("apply"), QStringLiteral("--reverse"), QStringLiteral("-")};
    command.workingDirectory = absoluteRepositoryPath(m_activeIndex);
    command.title = tr("丢弃代码块 #%1").arg(hunkIndex + 1);
    command.kind = GitCommandKind::HunkOperation;
    command.repositoryIndex = m_activeIndex;
    command.repositoryPath = m_model.pathAt(m_activeIndex);
    command.affectedRepositories = {m_model.pathAt(m_activeIndex)};
    command.standardInput = DiffModel::patch(m_diffText, file, hunkIndex).toUtf8();
    command.stopOnFailure = true;
    enqueue(command);
    enqueueRepositoryRefresh(m_activeIndex, false);
}

QString WorkspaceController::stashSha(int index) const
{
    if (index >= 0 && index < m_stashEntries.size()) {
        return m_stashEntries.at(index).sha;
    }
    return QString();
}

void WorkspaceController::loadStashList()
{
    if (m_activeIndex < 0) return;
    enqueueGit(m_activeIndex, {QStringLiteral("stash"), QStringLiteral("list"), QStringLiteral("--format=%gd%x00%H%x00%gd: %gs")},
               tr("读取 Stash 列表"), GitCommandKind::StashList);
}

void WorkspaceController::stashSave(const QString &message, bool includeUntracked)
{
    if (m_activeIndex < 0) return;
    QStringList args = {QStringLiteral("stash"), QStringLiteral("push")};
    if (includeUntracked)
        args << QStringLiteral("-u");
    if (!message.trimmed().isEmpty())
        args << QStringLiteral("-m") << message.trimmed();
    enqueueGit(m_activeIndex, args, tr("保存 Stash"), GitCommandKind::StashAction, true);
    enqueueRepositoryRefresh(m_activeIndex, false);
    loadStashList();
}

void WorkspaceController::executeStashAction(const QString &action, int index, const QString &expectedSha, const QString &title)
{
    if (m_activeIndex < 0) return;

    QString targetSha = expectedSha.trimmed();
    if (targetSha.isEmpty() && index >= 0 && index < m_stashEntries.size()
        && m_stashRepositoryPath == selectedPath()) {
        targetSha = m_stashEntries.at(index).sha;
    }

    if (targetSha.isEmpty()) {
        const QString msg = tr("Stash 操作已拒绝：缺少确定的提交 SHA 标识。为防止外部变动导致误操作，已取消执行并重新加载列表。");
        appendConsole(msg + QLatin1Char('\n'));
        reportError(title, msg);
        loadStashList();
        return;
    }

    GitCommand command;
    command.program = QStringLiteral("git");
    command.arguments = {QStringLiteral("stash"), QStringLiteral("list"), QStringLiteral("--format=%gd%x00%H")};
    command.workingDirectory = absoluteRepositoryPath(m_activeIndex);
    command.title = tr("校验 Stash 目标");
    command.kind = GitCommandKind::StashVerify;
    command.repositoryIndex = m_activeIndex;
    command.repositoryPath = selectedPath();
    command.stash.expectedSha = targetSha;
    command.stash.action = action;
    command.stash.title = title;
    command.stopOnFailure = true;
    enqueue(command);
}

void WorkspaceController::stashPop(int index, const QString &expectedSha)
{
    executeStashAction(QStringLiteral("pop"), index, expectedSha, tr("应用并弹出 Stash"));
}

void WorkspaceController::stashApply(int index, const QString &expectedSha)
{
    executeStashAction(QStringLiteral("apply"), index, expectedSha, tr("应用 Stash"));
}

void WorkspaceController::stashDrop(int index, const QString &expectedSha)
{
    executeStashAction(QStringLiteral("drop"), index, expectedSha, tr("删除 Stash"));
}

void WorkspaceController::markResolved(const QStringList &paths)
{
    if (m_activeIndex < 0 || paths.isEmpty() || busy()) return;
    if (!validateOwnFiles(paths)) return;
    QSet<QString> conflictPaths;
    for (const QVariant &file : ownRepositoryFiles(m_activeIndex, m_changedFiles)) {
        const QVariantMap entry = file.toMap();
        if (entry.value(QStringLiteral("conflict")).toBool())
            conflictPaths.insert(entry.value(QStringLiteral("path")).toString());
    }
    for (const QString &path : paths) {
        if (!conflictPaths.contains(path)) {
            reportError(tr("无法标记解决"), tr("所选文件不是当前仓库的冲突文件：%1").arg(path));
            return;
        }
    }
    QStringList args = {QStringLiteral("--literal-pathspecs"), QStringLiteral("add"), QStringLiteral("--")};
    args.append(paths);
    enqueueGit(m_activeIndex, args, tr("标记冲突已解决"), GitCommandKind::General, true);
    enqueueRepositoryRefresh(m_activeIndex, false);
}

void WorkspaceController::continueOperation()
{
    if (m_activeIndex < 0 || busy()) return;
    if (pendingOperation().isEmpty()) {
        reportError(tr("无法继续"), tr("当前仓库没有进行中的合并、变基或拣选操作。"));
        return;
    }
    if (hasConflicts()) {
            reportError(tr("无法继续"), tr("仍有未解决的冲突文件，请先解决并标记。"),
                        ErrorType::Conflict, {QStringLiteral("conflicts")});
        return;
    }
    const QScopedValueRollback<quint64> operation(m_enqueueOperationId, ++m_nextOperationId);
    enqueueGit(m_activeIndex, GitQueries::commitScope(), tr("检查提交仓库边界"), GitCommandKind::CommitScopePreflight, true);
    const QString gitDir = gitMetadataDirectory(absoluteRepositoryPath(m_activeIndex));
    if (gitDir.isEmpty()) return;

    if (QFile::exists(gitDir + QStringLiteral("/rebase-merge")) || QFile::exists(gitDir + QStringLiteral("/rebase-apply"))) {
        enqueueGit(m_activeIndex, {QStringLiteral("rebase"), QStringLiteral("--continue")},
                   tr("继续 Rebase"), GitCommandKind::General, true);
    } else if (QFile::exists(gitDir + QStringLiteral("/MERGE_HEAD"))) {
        enqueueGit(m_activeIndex, {QStringLiteral("merge"), QStringLiteral("--continue")},
                   tr("继续 Merge"), GitCommandKind::General, true);
    } else if (QFile::exists(gitDir + QStringLiteral("/CHERRY_PICK_HEAD"))) {
        enqueueGit(m_activeIndex, {QStringLiteral("cherry-pick"), QStringLiteral("--continue")},
                   tr("继续 Cherry-Pick"), GitCommandKind::General, true);
    } else if (QFile::exists(gitDir + QStringLiteral("/REVERT_HEAD"))) {
        enqueueGit(m_activeIndex, {QStringLiteral("revert"), QStringLiteral("--continue")},
                   tr("继续 Revert"), GitCommandKind::General, true);
    }
    enqueueRepositoryRefresh(m_activeIndex, true);
}

void WorkspaceController::abortOperation()
{
    if (m_activeIndex < 0 || busy()) return;
    if (pendingOperation().isEmpty()) {
        reportError(tr("无法终止"), tr("当前仓库没有进行中的合并、变基或拣选操作。"));
        return;
    }
    const QString gitDir = gitMetadataDirectory(absoluteRepositoryPath(m_activeIndex));
    if (gitDir.isEmpty()) return;

    if (QFile::exists(gitDir + QStringLiteral("/rebase-merge")) || QFile::exists(gitDir + QStringLiteral("/rebase-apply"))) {
        enqueueGit(m_activeIndex, {QStringLiteral("rebase"), QStringLiteral("--abort")},
                   tr("终止 Rebase"), GitCommandKind::General, true);
    } else if (QFile::exists(gitDir + QStringLiteral("/MERGE_HEAD"))) {
        enqueueGit(m_activeIndex, {QStringLiteral("merge"), QStringLiteral("--abort")},
                   tr("终止 Merge"), GitCommandKind::General, true);
    } else if (QFile::exists(gitDir + QStringLiteral("/CHERRY_PICK_HEAD"))) {
        enqueueGit(m_activeIndex, {QStringLiteral("cherry-pick"), QStringLiteral("--abort")},
                   tr("终止 Cherry-Pick"), GitCommandKind::General, true);
    } else if (QFile::exists(gitDir + QStringLiteral("/REVERT_HEAD"))) {
        enqueueGit(m_activeIndex, {QStringLiteral("revert"), QStringLiteral("--abort")},
                   tr("终止 Revert"), GitCommandKind::General, true);
    }
    enqueueRepositoryRefresh(m_activeIndex, true);
}

void WorkspaceController::skipCherryPick(const QString &expectedRevision)
{
    if (busy() || m_activeIndex < 0) return;
    if (!canSkipCherryPick() || expectedRevision.isEmpty() || expectedRevision != pendingCherryPickRevision()) {
        reportError(tr("无法跳过提交"), tr("仅能跳过当前工作区和暂存区均无改动的空拣选提交，请刷新后确认。"));
        return;
    }
    const QScopedValueRollback<quint64> operation(m_enqueueOperationId, ++m_nextOperationId);
    GitCommand command;
    command.arguments = GitQueries::commitScope();
    command.workingDirectory = absoluteRepositoryPath(m_activeIndex);
    command.repositoryIndex = m_activeIndex;
    command.repositoryPath = selectedPath();
    command.revision = expectedRevision;
    command.kind = GitCommandKind::CherryPickSkipPreflight;
    command.title = tr("检查空拣选提交状态");
    command.stopOnFailure = true;
    enqueue(command);
}
