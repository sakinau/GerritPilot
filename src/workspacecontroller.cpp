#include "workspacecontroller.h"
#include "gitqueries.h"
#include "repositorycache.h"
#include "discardplan.h"

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
#include <utility>

namespace {
QString normalizedPath(QString path)
{
    path = QDir::cleanPath(path.trimmed());
    while (path.startsWith(QStringLiteral("./")))
        path.remove(0, 2);
    return path;
}

QStringList dailyPullArguments(const QString &remote, const QString &remoteRef)
{
    QStringList args{QStringLiteral("pull"), QStringLiteral("--ff-only")};
    if (!remote.isEmpty() && !remoteRef.isEmpty()) {
        args.append({QStringLiteral("--no-tags"), QStringLiteral("--"), remote, remoteRef});
    }
    return args;
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

    connect(&m_commitWorkflow, &CommitWorkflowService::commitSucceeded,
            this, &WorkspaceController::commitSucceeded);
    connect(&m_gerrit, &GerritService::lastReviewUrlChanged,
            this, [this](const QString &url) {
        if (m_lastGerritReviewUrl != url) {
            m_lastGerritReviewUrl = url;
            emit lastGerritReviewUrlChanged();
        }
    });
    connect(&m_commitAi, &CommitMessageAiService::candidateReady,
            this, [this](const QString &) {
        appendConsole(tr("✓ AI 提交说明已生成\n"));
    });
    connect(&m_commitAi, &CommitMessageAiService::errorOccurred,
            this, [this](const QString &err) {
        emit operationFailed(tr("AI 说明生成失败"), err);
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
        if (!busy() && !m_autoPushRepository.isEmpty()) {
            const QString workspace = m_autoPushWorkspace;
            const QString repository = m_autoPushRepository;
            const QString remote = m_autoPushRemote;
            const QString targetBranch = m_autoPushTargetBranch;
            const QString topic = m_autoPushTopic;
            m_autoPushWorkspace.clear();
            m_autoPushRepository.clear();
            m_autoPushRemote.clear();
            m_autoPushTargetBranch.clear();
            m_autoPushTopic.clear();
            QTimer::singleShot(0, this, [this, workspace, repository, remote, targetBranch, topic] {
                if (!busy() && workspace == m_workspacePath && repository == selectedPath()
                    && remote == m_repositoryConfig.remote
                    && targetBranch == m_repositoryConfig.targetBranch
                    && topic == m_repositoryConfig.topic)
                    pushActive();
                else
                    emit operationFailed(tr("提交已完成，未自动推送"),
                        tr("仓库、推送目标或任务状态已变化，请核对后手动推送。"));
            });
        }
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
        emit operationFailed(tr("无法切换项目"), tr("Gerrit 推送尚未结束，请等待结果或取消任务。"));
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
    m_autoPushWorkspace.clear();
    m_autoPushRepository.clear();
    m_autoPushRemote.clear();
    m_autoPushTargetBranch.clear();
    m_autoPushTopic.clear();
    setPushStatus(0, QString());
    m_gerrit.setLastReviewUrl(QString());
    m_gitService.setGeneration(m_generation);
    m_gitService.clearQueue();
    m_activeIndex = -1;
    m_model.resetRepositories({}, {});
    m_changedFiles.clear();
    m_groupedChanges.clear();
    m_detailsText.clear();
    m_detailsCached = false;
    m_selectedFile.clear();
    m_diffText.clear();
    m_historyText.clear();
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
    restoreRepositoryCache(m_cachedRepositoryPath);
    m_cachedWorkspacePath = m_detailsCached ? m_workspacePath : QString();
}

void WorkspaceController::restoreRepositoryCache(const QString &repositoryPath)
{
    const auto data = RepositoryCache::load(m_workspacePath, repositoryPath);
    const int repositoryIndex = m_model.indexOfPath(repositoryPath);
    m_detailsCached = data.contains(QStringLiteral("files"));
    m_changedFiles = ownRepositoryFiles(repositoryIndex, data.value(QStringLiteral("files")).toList());
    rebuildGroupedChanges();
    if (repositoryIndex >= 0 && m_detailsCached) {
        const auto summary = m_model.summaryAt(repositoryIndex);
        m_model.updateStatus(repositoryIndex, m_model.branchAt(repositoryIndex), m_changedFiles.size(),
            summary.value(QStringLiteral("ahead")).toInt(), summary.value(QStringLiteral("behind")).toInt(),
            QStringLiteral("cached"));
    }
    m_detailsText.clear();
    for (const auto &file : m_changedFiles) {
        const auto entry = file.toMap();
        m_detailsText += entry.value(QStringLiteral("status")).toString() + ' '
            + entry.value(QStringLiteral("path")).toString() + '\n';
    }
    m_historyText = data.value(QStringLiteral("history")).toString();
    m_historyEntries = GitHistoryParser::parse(m_historyText);
    m_availableBranches = data.value(QStringLiteral("branches")).toStringList();
    m_remoteBranches = data.value(QStringLiteral("remoteBranches")).toStringList();
    m_remoteTrackingBranches = data.value(QStringLiteral("remoteTrackingBranches")).toStringList();
    m_remoteBranchesLoaded = false;
    m_diffText.clear();
    m_selectedFile.clear();
}

void WorkspaceController::saveRepositoryCache() const
{
    if (m_activeIndex < 0) return;
    const QVariantMap data{{QStringLiteral("files"), m_changedFiles},
        {QStringLiteral("summary"), m_model.summaryAt(m_activeIndex)},
        {QStringLiteral("details"), m_detailsText},
        {QStringLiteral("history"), m_historyText},
        {QStringLiteral("branches"), m_availableBranches},
        {QStringLiteral("remoteBranches"), m_remoteBranches},
        {QStringLiteral("remoteTrackingBranches"), m_remoteTrackingBranches}};
    if (RepositoryCache::save(m_workspacePath, selectedPath(), data)) {
        QSettings settings;
        settings.setValue(QStringLiteral("cacheSelection/") + RepositoryCache::key(m_workspacePath), selectedPath());
    }
}

QString WorkspaceController::loadCommitDraft(const QString &repoPath, const QString &workspacePath) const
{
    const QString ws = workspacePath.isEmpty() ? m_workspacePath : workspacePath;
    return m_commitWorkflow.loadDraft(repoPath.isEmpty() ? selectedPath() : repoPath, ws);
}

void WorkspaceController::saveCommitDraft(const QString &repoPath, const QString &text, const QString &workspacePath)
{
    const QString ws = workspacePath.isEmpty() ? m_workspacePath : workspacePath;
    m_commitWorkflow.saveDraft(repoPath.isEmpty() ? selectedPath() : repoPath, text, ws);
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
        emit operationFailed(tr("无法重新载入项目"), tr("Gerrit 推送尚未结束，请等待结果或取消任务。"));
        return;
    }
    cancelScheduledRefresh();
    m_pendingCommit = {};
    m_resumePendingCommit = false;
    m_repairChangeIdRevision.clear();
    m_autoPushWorkspace.clear();
    m_autoPushRepository.clear();
    m_autoPushRemote.clear();
    m_autoPushTargetBranch.clear();
    m_autoPushTopic.clear();
    ++m_aiRequest;
    m_commitAi.cancel();
    m_pushOperationId = 0;
    m_pushRepositoryKey.clear();
    setPushStatus(0, QString());
    const QFileInfo root(m_workspacePath);
    if (!root.isDir()) {
        emit operationFailed(tr("工作区不可用"), tr("目录不存在：%1").arg(m_workspacePath));
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
        emit operationFailed(tr("无法识别工作区"),
                             tr("没有找到 .repo/project.list 或 .git。"));
        return;
    }

    paths.removeDuplicates();
    paths.sort(Qt::CaseInsensitive);
    m_gitService.clearQueue();
    m_model.resetRepositories(paths, m_ignoredRepositories);
    for (int row = 0; row < m_model.rowCount(); ++row) {
        const auto cached = RepositoryCache::load(m_workspacePath, m_model.pathAt(row));
        m_model.restoreSummary(row, cached.value(QStringLiteral("summary")).toMap());
        if (cached.contains(QStringLiteral("files"))) {
            const auto files = ownRepositoryFiles(row, cached.value(QStringLiteral("files")).toList());
            const auto summary = m_model.summaryAt(row);
            m_model.updateStatus(row, m_model.branchAt(row), files.size(),
                summary.value(QStringLiteral("ahead")).toInt(), summary.value(QStringLiteral("behind")).toInt(),
                QStringLiteral("cached"));
        }
    }
    m_historyRequested = false;
    m_historyPending = false;
    m_historyLoaded = false;
    m_unbornRepositories.clear();
    ++m_generation;
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
        m_detailsText.clear();
        m_diffText.clear();
        m_historyText.clear();
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
    updateCommitHookStatus();
    appendConsole(tr("已载入 %1 个仓库 · %2").arg(paths.size()).arg(m_workspacePath));
    saveSettings();
    if (m_activeIndex >= 0) {
        appendConsole(tr("已恢复缓存 · %1").arg(m_model.nameAt(m_activeIndex)));
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
    m_autoPushWorkspace.clear();
    m_autoPushRepository.clear();
    m_autoPushRemote.clear();
    m_autoPushTargetBranch.clear();
    m_autoPushTopic.clear();
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
    m_historyRequested = true;
    if (m_historyPending || m_historyLoaded) return;
    ++m_historyRequest;
    m_historyAnchor.clear();
    m_historyPending = true;
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
            return QStringLiteral("repo sync --no-manifest-update -j8 --fail-fast (全部 %1 个仓库)").arg(m_model.rowCount());
        }
        QStringList args{QStringLiteral("sync"), QStringLiteral("--no-manifest-update"),
                         QStringLiteral("-j8"), QStringLiteral("--fail-fast")};
        args.append(paths);
        return GitService::displayCommand(QStringLiteral("repo"), args);
    }
    if (scope == 0 || paths.size() == 1) {
        const int index = paths.isEmpty() ? -1 : m_model.indexOfPath(paths.first());
        const QString targetName = (index >= 0) ? m_model.nameAt(index) : QString();
        const QString branch = (index >= 0) ? m_model.branchAt(index) : QString();
        QString desc = QStringLiteral("git pull --ff-only");
        if (!targetName.isEmpty()) {
            desc += QStringLiteral(" (%1%2)").arg(targetName, branch.isEmpty() ? QString() : QStringLiteral(" · %1").arg(branch));
        }
        return desc;
    }
    return QStringLiteral("git pull --ff-only") + tr("（共 %1 个仓库）").arg(paths.size());
}

void WorkspaceController::syncSelection(int scope)
{
    if (busy()) {
        appendConsole(tr("当前已有操作正在执行：%1，请等待完成后再试。").arg(activeTask()));
        emit operationFailed(tr("无法拉取"), tr("当前正在执行操作：%1，请等待完成后再试。").arg(activeTask()));
        return;
    }
    const QScopedValueRollback<quint64> operation(m_enqueueOperationId, ++m_nextOperationId);
    const QStringList paths = actionPaths(scope);
    if (paths.isEmpty()) {
        appendConsole(tr("未选择任何可拉取的仓库。"));
        emit operationFailed(tr("无法同步"), tr("没有选择要同步的仓库。"));
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
            emit operationFailed(tr("无法 Git 拉取"), error);
            return;
        }
    }
    appendConsole(tr("开始拉取代码 · 共 %1 个仓库...").arg(paths.size()));
    for (const QString &path : paths) {
        const int index = m_model.indexOfPath(path);
        if (index >= 0) {
            enqueueGit(index,
                       {QStringLiteral("for-each-ref"),
                        QStringLiteral("--format=%(HEAD)%00%(upstream:remotename)%00%(upstream:remoteref)"),
                        QStringLiteral("refs/heads/")},
                       tr("解析拉取上游 %1").arg(m_model.nameAt(index)), GitCommandKind::PullTarget, true);
        }
    }
}

void WorkspaceController::syncRepoSelection(int scope)
{
    if (busy()) {
        emit operationFailed(tr("无法同步"), tr("当前已有操作正在执行，请等待完成后再试。"));
        return;
    }
    if (!isRepoWorkspace() || !QFileInfo(QDir(m_workspacePath).filePath(QStringLiteral(".repo/manifest.xml"))).isFile()
        || !QFileInfo(QDir(m_workspacePath).filePath(QStringLiteral(".repo/project.list"))).isFile()) {
        emit operationFailed(tr("无法同步"), tr("当前工作区不是已同步的 Repo 项目。"));
        return;
    }
    if (scope != 0 && scope != 1 && scope != 2) {
        emit operationFailed(tr("无法同步"), tr("请选择当前、已勾选或全部仓库进行 Repo 同步。"));
        return;
    }

    const QScopedValueRollback<quint64> operation(m_enqueueOperationId, ++m_nextOperationId);
    GitCommand command;
    command.program = QStringLiteral("repo");
    command.arguments = {QStringLiteral("sync"), QStringLiteral("--no-manifest-update"),
                         QStringLiteral("-j8"), QStringLiteral("--fail-fast")};

    if (scope == 2) {
        command.title = tr("按 Manifest 全量同步全部仓库");
        command.affectedRepositories = actionPaths(2);
    } else {
        const QStringList paths = actionPaths(scope);
        if (paths.isEmpty()) {
            emit operationFailed(tr("无法同步"), tr("没有选择要同步的仓库。"));
            return;
        }
        const QString root = QFileInfo(m_workspacePath).canonicalFilePath();
        for (const QString &path : paths) {
            const QString target = QFileInfo(QDir(m_workspacePath).filePath(path)).canonicalFilePath();
            if (path == QStringLiteral(".") || path.startsWith(QStringLiteral("../"))
                || target.isEmpty() || !target.startsWith(root + QLatin1Char('/'))
                || !QFileInfo(QDir(target).filePath(QStringLiteral(".git"))).exists()) {
                emit operationFailed(tr("无法同步"), tr("所选仓库不属于当前 Repo 项目：%1").arg(path));
                return;
            }
        }
        command.arguments.append(paths);
        command.title = tr("按 Manifest 同步 %1 个仓库").arg(paths.size());
        command.affectedRepositories = paths;
    }

    command.workingDirectory = m_workspacePath;
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
        emit operationFailed(tr("无法丢弃"), tr("文件状态已变化或包含冲突，请刷新后选择未暂存文件：%1").arg(plan.rejectedPath));
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
    if (!busy() && !checkHasCommitHook(absoluteRepositoryPath(m_activeIndex))) {
        m_pendingCommit = {true, m_workspacePath, selectedPath(), message, amend, stageAll, false};
        emit operationFailed(tr("缺少 Change-Id Hook"),
                             tr("当前仓库没有可执行的 commit-msg Hook。请选择“安装 Hook 并继续提交”；安装失败时不会创建提交。"));
        updateCommitHookStatus();
        return;
    }
    m_pendingCommit = {};
    const QScopedValueRollback<quint64> operation(m_enqueueOperationId, ++m_nextOperationId);

    GitCommand stageCommand;
    GitCommand commitCommand;
    QString errorMessage;
    if (!m_commitWorkflow.prepareCommit(m_activeIndex,
                                       absoluteRepositoryPath(m_activeIndex),
                                       selectedPath(),
                                       m_workspacePath,
                                       message, amend, stageAll,
                                       m_detailsCached, busy(),
                                       &stageCommand, &commitCommand,
                                       &errorMessage)) {
        emit operationFailed(tr("暂时无法提交"), errorMessage);
        return;
    }

    if (stageAll && !prepareScopedStage(stageCommand)) return;
    if (stageAll)
        enqueue(stageCommand);

    enqueueGit(m_activeIndex, GitQueries::commitScope(), tr("检查提交仓库边界"), GitCommandKind::CommitScopePreflight, true);
    enqueue(commitCommand);
    enqueueRepositoryRefresh(m_activeIndex, true);
}

void WorkspaceController::commitAndPushActive(const QString &message, bool amend, bool stageAll)
{
    if (m_activeIndex < 0) return;
    if (!busy() && !checkHasCommitHook(absoluteRepositoryPath(m_activeIndex))) {
        m_pendingCommit = {true, m_workspacePath, selectedPath(), message, amend, stageAll, true};
        emit operationFailed(tr("缺少 Change-Id Hook"),
                             tr("当前仓库没有可执行的 commit-msg Hook。请选择“安装 Hook 并继续提交”；安装失败时不会创建提交。"));
        updateCommitHookStatus();
        return;
    }
    m_pendingCommit = {};
    const QScopedValueRollback<quint64> operation(m_enqueueOperationId, ++m_nextOperationId);

    GitCommand stageCommand;
    GitCommand commitCommand;
    QString errorMessage;
    if (!m_commitWorkflow.prepareCommitAndPush(m_activeIndex,
                                               absoluteRepositoryPath(m_activeIndex),
                                               selectedPath(),
                                               m_workspacePath,
                                               message, amend, stageAll,
                                               m_detailsCached, busy(),
                                               m_repositoryConfig.remote,
                                               m_repositoryConfig.targetBranch,
                                               m_repositoryConfig.topic,
                                               &stageCommand, &commitCommand,
                                               &errorMessage)) {
        emit operationFailed(tr("无法提交并推送"), errorMessage);
        return;
    }

    if (stageAll && !prepareScopedStage(stageCommand)) return;
    if (stageAll)
        enqueue(stageCommand);

    enqueueGit(m_activeIndex, GitQueries::commitScope(), tr("检查提交仓库边界"), GitCommandKind::CommitScopePreflight, true);
    enqueue(commitCommand);
    enqueueRepositoryRefresh(m_activeIndex, true);
}

void WorkspaceController::generateCommitMessage(const QString &existingMessage, int scope,
                                                const QString &commitType, const QString &issueId)
{
    if (m_activeIndex < 0 || busy()) return;
    if (scope != 0 && scope != 1) {
        emit operationFailed(tr("AI 提交说明"), tr("请选择有效的改动范围。"));
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
        emit operationFailed(tr("AI 提交说明"), tr("请选择有效的提交类型（如 fix, feat, chg, refactor 等）。"));
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
                emit operationFailed(tr("AI 提交说明"), tr("单号格式不正确（单个单号只能包含字母、数字、点、下划线和连字符，多个可用逗号隔开）：%1").arg(item));
                return;
            }
            static const QRegularExpression pureNum(QStringLiteral("^[0-9]{4,}$"));
            if (pureNum.match(item).hasMatch()) {
                item = QStringLiteral("m-") + item;
            }
            if (!issueTokens.contains(item, Qt::CaseInsensitive))
                issueTokens.append(item);
        }
        normalizedIssue = issueTokens.join(QStringLiteral(","));
    }
    if (!m_commitAi.configured()) {
        emit operationFailed(tr("AI 提交说明"), tr("请先在设置中填写有效的 AI 接口地址和模型名称。"));
        return;
    }
    m_aiExistingMessage = existingMessage;
    m_aiCommitType = normalizedType;
    m_aiIssueId = normalizedIssue;
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
    return GerritService::pushPreview(m_repositoryConfig.remote,
                                      m_repositoryConfig.targetBranch,
                                      m_repositoryConfig.topic);
}

void WorkspaceController::pushActive()
{
    if (busy()) { emit operationFailed(tr("无法上传"), tr("请等待当前任务完成。")); return; }
    const QScopedValueRollback<quint64> operation(m_enqueueOperationId, ++m_nextOperationId);
    if (m_activeIndex < 0) return;
    if (selectedBranch() == QStringLiteral("(detached)") || selectedBranch() == QStringLiteral("detached")) {
        emit operationFailed(tr("无法上传"), tr("当前处于 detached HEAD。请先切换到承载本次提交的本地分支。"));
        return;
    }
    if (m_repositoryConfig.targetBranch.isEmpty() || m_repositoryConfig.remote.isEmpty()) {
        emit operationFailed(tr("无法上传"), tr("当前仓库未配置 Remote 或 Gerrit 目标分支。"));
        return;
    }
    const QRegularExpression branchPattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._/-]*$"));
    if (!branchPattern.match(m_repositoryConfig.targetBranch).hasMatch() || m_repositoryConfig.targetBranch.contains(QStringLiteral(".."))) {
        emit operationFailed(tr("目标分支无效"),
                             tr("目标分支包含 Gerrit 不接受的字符。"));
        return;
    }
    QString topicError;
    if (!GitService::validateTopicName(m_repositoryConfig.topic, &topicError)) {
        emit operationFailed(tr("Topic 无效"), topicError);
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
    command.paths = {m_repositoryConfig.remote, m_repositoryConfig.targetBranch,
                     m_repositoryConfig.topic};
    command.kind = GitCommandKind::PushChangeIdPreflight;
    command.title = tr("检查待推送提交的 Change-Id");
    command.stopOnFailure = true;
    m_pushOperationId = m_enqueueOperationId;
    m_pushRepositoryKey = m_workspacePath + QLatin1Char('/') + selectedPath();
    m_gerrit.setLastReviewUrl(QString());
    setPushStatus(1, tr("正在检查提交并准备推送 %1 → %2…").arg(selectedName(), m_repositoryConfig.targetBranch));
    enqueue(command);
}

void WorkspaceController::repairLastCommitChangeId()
{
    if (m_activeIndex < 0 || busy()) return;
    if (m_repairChangeIdRevision.isEmpty()) {
        emit operationFailed(tr("无法修复 Change-Id"), tr("待修复提交已变化，请重新发起推送检查。"));
        return;
    }
    if (selectedBranch() == QStringLiteral("(detached)") || selectedBranch() == QStringLiteral("detached")) {
        emit operationFailed(tr("无法修复 Change-Id"), tr("请先切换到包含该提交的本地分支。"));
        return;
    }
    if (!checkHasCommitHook(absoluteRepositoryPath(m_activeIndex))) {
        emit operationFailed(tr("缺少 Change-Id Hook"), tr("请先安装当前仓库的 Hook，再修订提交。"));
        updateCommitHookStatus();
        return;
    }
    if (hasPendingOperation()) {
        emit operationFailed(tr("无法修复 Change-Id"), tr("请先完成或终止当前 Git 操作。"));
        return;
    }
    const QScopedValueRollback<quint64> operation(m_enqueueOperationId, ++m_nextOperationId);
    GitCommand command;
    command.program = QStringLiteral("git");
    command.arguments = GitQueries::files();
    command.workingDirectory = absoluteRepositoryPath(m_activeIndex);
    command.repositoryIndex = m_activeIndex;
    command.repositoryPath = selectedPath();
    command.kind = GitCommandKind::ChangeIdRepairPreflight;
    command.paths = {m_repairChangeIdRevision};
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
        emit operationFailed(tr("分支名无效"), error);
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
    m_diffText.clear();
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
        m_diffText.clear();
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
            emit operationFailed(tr("无法读取 Diff"), tr("文件路径不在当前仓库内。"));
            return;
        }
        const QFileInfo file(QDir(absoluteRepositoryPath(m_activeIndex)).absoluteFilePath(path));
        if (!file.exists() || (!file.isFile() && !file.isSymLink())) {
            emit operationFailed(tr("无法读取 Diff"), tr("文件不存在或不是普通文件：%1").arg(path));
            return;
        }
        if (file.size() > 2 * 1024 * 1024) {
            emit operationFailed(tr("文件过大"), tr("新文件超过 2 MiB，请使用打开文件按钮查看：%1").arg(path));
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
        emit operationFailed(tr("无法打开文件"), tr("文件路径不在当前仓库内。"));
        return;
    }
    const QFileInfo file(repository.absoluteFilePath(relative));
    if (!file.exists() || !file.isFile()) {
        emit operationFailed(tr("无法打开文件"), tr("工作区中不存在此文件：%1").arg(path));
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(file.absoluteFilePath())))
        emit operationFailed(tr("无法打开文件"), tr("没有可用的默认应用打开：%1").arg(path));
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
        m_diffText.clear();
        emit diffTextChanged();
    }
}

void WorkspaceController::createBranch(const QString &branch, bool useRepoStart)
{
    const QScopedValueRollback<quint64> operation(m_enqueueOperationId, ++m_nextOperationId);
    if (m_activeIndex < 0)
        return;
    QString error;
    if (!GitService::validateBranchName(branch, &error)) {
        emit operationFailed(tr("分支名无效"), error);
        return;
    }
    if (m_availableBranches.contains(branch.trimmed())) {
        emit operationFailed(tr("分支已存在"), tr("本地分支 %1 已存在，请直接切换。").arg(branch.trimmed()));
        return;
    }
    if (useRepoStart && isRepoWorkspace()) {
        GitCommand command;
        command.program = QStringLiteral("repo");
        command.arguments = {QStringLiteral("start"), branch.trimmed(), QStringLiteral(".")};
        command.workingDirectory = absoluteRepositoryPath(m_activeIndex);
        command.title = tr("新建开发分支 (repo start) %1").arg(branch.trimmed());
        command.repositoryIndex = m_activeIndex;
        command.repositoryPath = selectedPath();
        command.stopOnFailure = true;
        enqueue(command);
    } else {
        enqueueGit(m_activeIndex,
                   {QStringLiteral("checkout"), QStringLiteral("-b"), branch.trimmed()},
                   tr("新建分支 %1").arg(branch.trimmed()), GitCommandKind::General, true);
    }
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
        emit operationFailed(tr("无法删除分支"), tr("不能删除当前正在检出的分支 %1。请先切换到其他分支。").arg(trimmedBranch));
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
    command.paths = {m_branchQueryRemote, QString::number(m_branchQuery)};
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
        emit operationFailed(tr("无法设置上游"), tr("请填写有效的 Remote 名称和分支名，并确认当前处于本地分支。"));
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
    query.paths = {remoteName, targetBranch, localBranch};
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
        emit operationFailed(tr("无法撤销提交"), tr("提交 ID 无效。"));
        return;
    }
    if (selectedBranch() == QStringLiteral("(detached)") || selectedBranch() == QStringLiteral("detached")) {
        emit operationFailed(tr("无法撤销提交"), tr("当前仓库处于 detached HEAD；请先切换到需要接收反向提交的本地分支。"));
        return;
    }
    if (hasPendingOperation()) {
        emit operationFailed(tr("无法撤销提交"), tr("请先完成或终止当前未完成的 Git 操作。"));
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
    command.paths = {hash};
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

bool WorkspaceController::checkHasCommitHook(const QString &repoPath) const
{
    return GerritService::checkHasCommitHook(repoPath);
}

void WorkspaceController::updateCommitHookStatus()
{
    const bool prev = m_hasCommitHook;
    if (m_activeIndex < 0) {
        m_hasCommitHook = true;
    } else {
        m_hasCommitHook = checkHasCommitHook(absoluteRepositoryPath(m_activeIndex));
    }
    if (prev != m_hasCommitHook) {
        emit hasCommitHookChanged();
    }
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
            m_historyText.clear();
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
    if (checkHasCommitHook(absoluteRepositoryPath(m_activeIndex))) {
        updateCommitHookStatus();
        resumePendingCommitAfterHook();
        return;
    }
    const QString remote = m_repositoryConfig.remote.trimmed();
    if (remote.isEmpty() || remote.startsWith(QLatin1Char('-'))) {
        m_resumePendingCommit = false;
        emit operationFailed(tr("无法安装 Hook"), tr("请先为当前仓库配置有效的 Gerrit Remote。"));
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
            emit operationFailed(tr("提交未继续"), tr("仓库或任务状态已变化，请重新检查后提交。"));
            return;
        }
        if (pending.pushAfterCommit)
            commitAndPushActive(pending.message, pending.amend, pending.stageAll);
        else
            commitActive(pending.message, pending.amend, pending.stageAll);
    });
}

void WorkspaceController::stop()
{
    cancelScheduledRefresh();
    m_pendingCommit = {};
    m_resumePendingCommit = false;
    m_repairChangeIdRevision.clear();
    m_autoPushWorkspace.clear();
    m_autoPushRepository.clear();
    m_autoPushRemote.clear();
    m_autoPushTargetBranch.clear();
    m_autoPushTopic.clear();
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

    if (!success && completed.kind == GitCommandKind::InstallHook)
        QFile::remove(completed.paths.value(1));
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

    if (success) switch (completed.kind) {
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
            emit operationFailed(tr("无法推送"), output.trimmed().isEmpty()
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
            updateCommitHookStatus();
            emit operationFailed(headOnly ? tr("最近提交缺少 Change-Id") : tr("待推送提交缺少 Change-Id"),
                (m_hasCommitHook
                    ? tr("待推送提交缺少有效 Change-Id：%1。修订对应提交会改变提交 SHA。")
                    : tr("待推送提交缺少有效 Change-Id：%1。请先安装 Hook；修订对应提交会改变提交 SHA。"))
                    .arg(missing.join(QStringLiteral(", "))));
            return;
        }
        m_repairChangeIdRevision.clear();
        const QScopedValueRollback<quint64> operation(m_enqueueOperationId, completed.operationId);
        GitCommand push = m_gerrit.createPushCommand(completed.repositoryIndex,
            completed.workingDirectory, completed.repositoryPath,
            completed.paths.value(0), completed.paths.value(1), completed.paths.value(2));
        push.workspacePath = completed.workspacePath;
        setPushStatus(1, tr("正在推送 %1 → %2…").arg(selectedName(), completed.paths.value(1)));
        enqueue(push);
        enqueueGit(completed.repositoryIndex, GitQueries::summary(),
                   tr("刷新仓库状态"), GitCommandKind::Status);
        return;
    }
    case GitCommandKind::ChangeIdRepairPreflight: {
        if (completed.repositoryIndex != m_activeIndex || completed.repositoryPath != selectedPath()) {
            emit operationFailed(tr("无法修复 Change-Id"), tr("当前仓库已变化，请重新检查。"));
            return;
        }
        if (!output.isEmpty()) {
            emit operationFailed(tr("无法修复 Change-Id"), tr("工作区或暂存区有改动；为避免把额外文件带入修订提交，请先处理这些改动。"));
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
            || completed.paths.value(0) != m_repairChangeIdRevision
            || output.trimmed() != m_repairChangeIdRevision) {
            emit operationFailed(tr("无法修复 Change-Id"), tr("HEAD 已变化；请重新检查待推送提交。"));
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
            emit operationFailed(tr("Change-Id 修复未生效"),
                tr("Amend 已执行，但最近提交仍无有效 Change-Id。请检查当前仓库的 commit-msg Hook；未执行推送。"));
        } else {
            appendConsole(tr("最近提交已补齐 Change-Id；请核对提交后再推送。") + QLatin1Char('\n'));
        }
        return;
    }
    case GitCommandKind::RepoSync:
        for (const QString &path : completed.affectedRepositories) {
            const int index = m_model.indexOfPath(path);
            if (index >= 0)
                enqueueRepositoryRefresh(index, index == m_activeIndex);
        }
        break;
    case GitCommandKind::RevertPreflight: {
        if (completed.repositoryIndex != m_activeIndex || completed.repositoryPath != selectedPath()) {
            emit operationFailed(tr("无法撤销提交"), tr("当前仓库已变化，请重新选择提交。"));
            return;
        }
        if (!output.isEmpty()) {
            emit operationFailed(tr("无法撤销提交"), tr("工作区或暂存区有未提交改动，请先提交或暂存，再撤销该提交。"));
            return;
        }
        const QScopedValueRollback<quint64> operation(m_enqueueOperationId, completed.operationId);
        enqueueGit(completed.repositoryIndex,
                   {QStringLiteral("revert"), QStringLiteral("--no-edit"), completed.paths.first()},
                   tr("撤销提交 %1").arg(completed.paths.first().left(10)), GitCommandKind::General, true);
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
            emit operationFailed(tr("AI 提交说明"), tr("当前仓库没有可用于生成说明的暂存文件。"));
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
            emit operationFailed(tr("AI 提交说明"), tr("暂存区 Diff 为空，未生成提交说明。"));
        } else {
            m_commitAi.generate(selectedName(), selectedBranch(), output, completed.paths,
                                m_aiExistingMessage, m_aiCommitType, m_aiIssueId, false);
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
            emit operationFailed(tr("AI 提交说明"), tr("当前仓库没有改动文件。"));
            break;
        }
        if (trackedPaths.isEmpty()) {
            const QString preview = untrackedAiPreview(completed.workingDirectory, m_aiUntrackedFiles);
            if (preview.trimmed().isEmpty()) {
                emit operationFailed(tr("AI 提交说明"), tr("未找到可供生成说明的文本改动。"));
                break;
            }
            m_commitAi.generate(selectedName(), selectedBranch(), preview, m_aiChangedFiles,
                                m_aiExistingMessage, m_aiCommitType, m_aiIssueId, true);
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
            emit operationFailed(tr("AI 提交说明"), tr("仓库 Diff 为空，未生成提交说明。"));
        } else {
            m_commitAi.generate(selectedName(), selectedBranch(), combined, m_aiChangedFiles,
                                m_aiExistingMessage, m_aiCommitType, m_aiIssueId, true);
        }
        break;
    }
    case GitCommandKind::PushRemoteUrl:
        if (completed.repositoryIndex == m_activeIndex
            && completed.paths.value(0) == m_branchQueryRemote
            && completed.paths.value(1).toULongLong() == m_branchQuery && !output.trimmed().isEmpty()) {
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
            && completed.paths.value(0) == m_branchQueryRemote
            && completed.paths.value(1).toULongLong() == m_branchQuery) {
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
        const QString remoteName = completed.paths.value(0);
        const QString branch = completed.paths.value(1);
        const QString localBranch = completed.paths.value(2);
        if (completed.repositoryIndex != m_activeIndex || localBranch != selectedBranch()) {
            m_gitService.cancelOperation(completed.operationId);
            emit operationFailed(tr("无法设置上游"), tr("当前仓库或本地分支已变化，请重新选择。"));
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
            emit operationFailed(tr("无法设置上游"), tr("远端分支不存在：%1/%2").arg(remoteName, branch));
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
                      .arg(completed.paths.value(2), completed.paths.value(0), completed.paths.value(1)));
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
            emit operationFailed(tr("无法拉取"), error);
            break;
        }
        appendConsole(tr("仅拉取 %1 的 %2；不获取其他分支、标签或子模块。")
                      .arg(remote, remoteRef));
        enqueueGit(completed.repositoryIndex, dailyPullArguments(remote, remoteRef),
                   tr("拉取仓库 %1").arg(m_model.nameAt(completed.repositoryIndex)),
                   GitCommandKind::PullExecution, true);
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
                emit operationFailed(tr("无法提交"),
                    tr("父仓库暂存区包含嵌套仓库路径：%1。本次提交已阻止，嵌套仓库暂存项未被自动取消。")
                        .arg(entry.value(QStringLiteral("path")).toString()));
                break;
            }
        }
        break;
    }
    case GitCommandKind::Status:
        applyStatus(completed.repositoryIndex, GitStatusParser::parseSnapshot(output));
        break;
    case GitCommandKind::Details: {
        auto snapshot = GitStatusParser::parseSnapshot(output);
        snapshot.files = ownRepositoryFiles(completed.repositoryIndex, snapshot.files);
        applyStatus(completed.repositoryIndex, snapshot);
        if (completed.repositoryIndex == m_activeIndex) {
            m_detailsCached = m_refreshPending;
            m_changedFiles = snapshot.files;
            rebuildGroupedChanges();
            m_detailsText.clear();
            for (const auto &file : m_changedFiles) {
                const auto entry = file.toMap();
                m_detailsText += entry.value(QStringLiteral("status")).toString() + ' '
                    + entry.value(QStringLiteral("path")).toString() + '\n';
            }
            emit detailsTextChanged();
            saveRepositoryCache();
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
        if (completed.repositoryIndex == m_activeIndex) {
            m_diffText = output;
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
                m_historyText.clear();
                m_historyEntries.clear();
                emit historyTextChanged();
                saveRepositoryCache();
                break;
            }
            const auto page = GitHistoryParser::parse(output);
            m_historyPending = false;
            m_historyLoaded = true;
            m_historyHasMore = page.size() == 30;
            if (completed.appendHistory) {
                if (!output.isEmpty()) {
                    m_historyText += '\n' + output;
                    m_historyEntries += page;
                }
            } else {
                m_historyText = output;
                m_historyEntries = page;
                m_historyAnchor = page.isEmpty() ? QString() : page.first().toMap().value("revision").toString();
            }
            emit historyTextChanged();
            saveRepositoryCache();
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
            saveRepositoryCache();
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
            emit operationFailed(tr("无法安装 Hook"), tr("当前仓库已变化，请重新操作。"));
            return;
        }
        QString source, port, error;
        if (!GerritService::hookSourceFromRemoteUrl(output, &source, &port, &error)) {
            m_resumePendingCommit = false;
            emit operationFailed(tr("无法安装 Hook"), error);
            return;
        }
        const QScopedValueRollback<quint64> operation(m_enqueueOperationId, completed.operationId);
        GitCommand query = completed;
        query.arguments = {QStringLiteral("rev-parse"), QStringLiteral("--path-format=absolute"),
                           QStringLiteral("--git-path"), QStringLiteral("hooks/commit-msg")};
        query.kind = GitCommandKind::HookDirectory;
        query.paths = {source, port};
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
            emit operationFailed(tr("无法安装 Hook"), tr("无法确认当前仓库的有效 Hook 路径。"));
            return;
        }
        const QFileInfo existing(targetHook);
        if (existing.exists() || existing.isSymLink()) {
            updateCommitHookStatus();
            if (m_hasCommitHook) {
                resumePendingCommitAfterHook();
            } else {
                m_resumePendingCommit = false;
                emit operationFailed(tr("无法安装 Hook"),
                    tr("有效 Hook 路径已有文件但不可执行：%1。程序不会覆盖它，请检查后重试。")
                        .arg(targetHook));
            }
            return;
        }
        if (!QDir().mkpath(existing.path())) {
            m_resumePendingCommit = false;
            emit operationFailed(tr("无法安装 Hook"), tr("无法创建 Hook 目录：%1").arg(existing.path()));
            return;
        }
        QTemporaryFile temporary(QDir(existing.path()).filePath(QStringLiteral(".commit-msg.XXXXXX")));
        temporary.setAutoRemove(false);
        if (!temporary.open()) {
            m_resumePendingCommit = false;
            emit operationFailed(tr("无法安装 Hook"), tr("无法准备 Hook 临时文件。"));
            return;
        }
        const QString temporaryPath = temporary.fileName();
        temporary.close();
        GitCommand command = m_gerrit.createInstallHookCommand(completed, temporaryPath);
        command.paths = {targetHook, temporaryPath};
        enqueue(command);
        return;
    }
    case GitCommandKind::StashVerify: {
        const QString expectedSha = completed.submittedMessage.trimmed();
        const QString action = completed.paths.value(0);
        const QString opTitle = completed.paths.value(2);

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
            emit operationFailed(opTitle, msg);
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
    case GitCommandKind::InstallHook: {
        const QString hookPath = completed.paths.value(0);
        const QString temporaryPath = completed.paths.value(1);
        if (completed.repositoryIndex != m_activeIndex || completed.repositoryPath != selectedPath()
            || temporaryPath.isEmpty() || QFileInfo(temporaryPath).size() == 0
            || QFileInfo::exists(hookPath) || QFileInfo(hookPath).isSymLink()
            || !QFile::rename(temporaryPath, hookPath)) {
            QFile::remove(temporaryPath);
            m_resumePendingCommit = false;
            emit operationFailed(tr("无法安装 Hook"), tr("Hook 安装结果无效或目标路径已被其他程序占用；未覆盖现有文件。"));
            return;
        }
        GerritService::finalizeHookPermissions(hookPath);
        updateCommitHookStatus();
        if (!m_hasCommitHook) {
            m_resumePendingCommit = false;
            emit operationFailed(tr("无法安装 Hook"), tr("Hook 文件已下载，但未通过可执行性检查：%1").arg(hookPath));
            return;
        }
        resumePendingCommitAfterHook();
        return;
    }
    case GitCommandKind::HunkOperation:
    case GitCommandKind::General:
        break;
    }

    if (!completed.paths.isEmpty() && (completed.kind == GitCommandKind::HunkOperation || completed.title.contains(QStringLiteral("代码块")))) {
        QFile::remove(completed.paths.first());
    }

    if (!success) {
        invalidateFailedQuery(completed);
        QString errorMsg = result.diagnosticMessage;
        if (completed.kind == GitCommandKind::GerritPush) {
            errorMsg = GerritService::parseGerritError(result.standardError, result.diagnosticMessage);
            if (completed.operationId == m_pushOperationId) {
                m_pushOperationId = 0;
                setPushStatus(3, tr("推送失败，请查看错误信息和命令日志。"));
            }
        } else if (completed.kind == GitCommandKind::PushChangeIdPreflight) {
            errorMsg = tr("无法检查目标分支到 HEAD 的待推送提交。请先获取目标分支的远端跟踪引用，再重试推送。\n")
                + result.diagnosticMessage;
        } else if (completed.kind == GitCommandKind::UpstreamPreflight && result.exitCode == 2) {
            errorMsg = tr("远端不存在分支 %1/%2，请重新选择。")
                .arg(completed.paths.value(0), completed.paths.value(1));
        } else if (completed.kind == GitCommandKind::UpstreamConfigRemote
                   || completed.kind == GitCommandKind::UpstreamConfigMerge) {
            errorMsg = tr("写入本地上游配置失败，配置可能只完成了一部分。请检查当前分支设置。\n") + errorMsg;
        } else if (completed.kind == GitCommandKind::PullExecution) {
            const QString stderrLower = QString::fromLocal8Bit(result.standardError).toLower();
            if (stderrLower.contains(QStringLiteral("no tracking information")) || stderrLower.contains(QStringLiteral("set-upstream"))) {
                errorMsg = tr("当前分支未设置拉取上游。请在拉取面板选择目标 Remote 和远端分支后重试。");
            } else if (stderrLower.contains(QStringLiteral("not possible to fast-forward")) || stderrLower.contains(QStringLiteral("non-fast-forward"))) {
                errorMsg = tr("本地分支与远端已产生分叉，无法快进（fast-forward）拉取。\n请在终端执行合并（git merge）或变基（git rebase）。");
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
        emit operationFailed(completed.title, errorMsg);
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
        if (completed.kind == GitCommandKind::Commit) {
            m_commitWorkflow.onCommitCompleted(completed);
            if (completed.pushAfterCommit) {
                m_autoPushWorkspace = completed.workspacePath;
                m_autoPushRepository = completed.repositoryPath;
                m_autoPushRemote = completed.pushRemote;
                m_autoPushTargetBranch = completed.pushTargetBranch;
                m_autoPushTopic = completed.pushTopic;
            }
        } else if (completed.commitOperation) {
            WorkspaceSettings::clearDraft(completed.workspacePath, completed.repositoryPath);
            emit commitSucceeded(completed.workspacePath, completed.repositoryPath,
                                 completed.submittedMessage);
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
                emit operationFailed(completed.title, message);
                emit summaryChanged();
                return;
            }
            if (completed.workspacePath == m_workspacePath)
                m_gerrit.setLastReviewUrl(url);
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
        m_diffText.clear();
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
            emit operationFailed(tr("无法操作文件"), tr("路径不属于当前仓库的有效改动，请刷新或选择对应的嵌套仓库：%1").arg(path));
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
        emit operationFailed(tr("无法暂存"), tr("当前仓库没有可暂存的文件。"));
        return false;
    }
    command.arguments = {QStringLiteral("--literal-pathspecs"), QStringLiteral("add"), QStringLiteral("-A"), QStringLiteral("--")};
    command.arguments.append(paths);
    return true;
}

void WorkspaceController::applyStatus(int index, const GitStatusParser::Snapshot &rawSnapshot)
{
    auto snapshot = rawSnapshot;
    snapshot.files = ownRepositoryFiles(index, snapshot.files);
    if (snapshot.unborn) m_unbornRepositories.insert(index);
    else m_unbornRepositories.remove(index);
    const int changes = snapshot.files.size();
    m_model.updateStatus(index, snapshot.branch, changes, snapshot.ahead, snapshot.behind,
                         changes > 0 ? QStringLiteral("modified") : QStringLiteral("clean"));
    auto cached = RepositoryCache::load(m_workspacePath, m_model.pathAt(index));
    cached.insert(QStringLiteral("summary"), m_model.summaryAt(index));
    // Persist the entire snapshot even if the user switched repositories while
    // the query was running. Never source these fields from the active pane.
    cached.insert(QStringLiteral("files"), snapshot.files);
    QString details;
    for (const auto &file : snapshot.files) {
        const auto entry = file.toMap();
        details += entry.value(QStringLiteral("status")).toString() + ' '
            + entry.value(QStringLiteral("path")).toString() + '\n';
    }
    cached.insert(QStringLiteral("details"), details);
    RepositoryCache::save(m_workspacePath, m_model.pathAt(index), cached);
    if (index == m_activeIndex) {
        emit selectedRepositoryChanged();
        updateCommitHookStatus();
    }
}

QString WorkspaceController::absoluteRepositoryPath(int index) const
{
    return QDir(m_workspacePath).absoluteFilePath(m_model.pathAt(index));
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
    const QString repoDir = absoluteRepositoryPath(m_activeIndex);
    QString gitDir = repoDir + QStringLiteral("/.git");
    QFileInfo gitDirInfo(gitDir);
    if (gitDirInfo.isFile()) {
        QFile f(gitDir);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString content = QString::fromUtf8(f.readAll()).trimmed();
            if (content.startsWith(QStringLiteral("gitdir:"))) {
                gitDir = QDir(repoDir).absoluteFilePath(content.mid(7).trimmed());
            }
        }
    }

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
    const DiffFile file = DiffModel::parse(m_diffText);
    return file.hunks.size();
}

bool WorkspaceController::isBinaryDiff() const
{
    const DiffFile file = DiffModel::parse(m_diffText);
    return file.isBinary;
}

void WorkspaceController::stageHunk(int hunkIndex)
{
    if (m_activeIndex < 0 || m_selectedFile.isEmpty()) return;
    if (!validateOwnFiles({m_selectedFile})) return;
    const DiffFile file = DiffModel::parse(m_diffText);
    if (hunkIndex < 0 || hunkIndex >= file.hunks.size()) return;

    if (file.hunks.size() <= 1) {
        stageFiles({m_selectedFile});
        return;
    }

    const DiffHunk hunk = file.hunks.at(hunkIndex);
    GitCommand command;
    command.program = QStringLiteral("git");
    command.arguments = {QStringLiteral("apply"), QStringLiteral("--cached"), QStringLiteral("-")};
    command.workingDirectory = absoluteRepositoryPath(m_activeIndex);
    command.title = tr("暂存代码块 #%1").arg(hunkIndex + 1);
    command.kind = GitCommandKind::HunkOperation;
    command.repositoryIndex = m_activeIndex;
    command.repositoryPath = m_model.pathAt(m_activeIndex);
    command.affectedRepositories = {m_model.pathAt(m_activeIndex)};
    command.standardInput = hunk.patch.toUtf8();
    command.stopOnFailure = true;
    enqueue(command);
    enqueueRepositoryRefresh(m_activeIndex, false);
}

void WorkspaceController::unstageHunk(int hunkIndex)
{
    if (m_activeIndex < 0 || m_selectedFile.isEmpty()) return;
    if (!validateOwnFiles({m_selectedFile})) return;
    const DiffFile file = DiffModel::parse(m_diffText);
    if (hunkIndex < 0 || hunkIndex >= file.hunks.size()) return;

    if (file.hunks.size() <= 1) {
        unstageFiles({m_selectedFile});
        return;
    }

    const DiffHunk hunk = file.hunks.at(hunkIndex);
    GitCommand command;
    command.program = QStringLiteral("git");
    command.arguments = {QStringLiteral("apply"), QStringLiteral("--cached"), QStringLiteral("--reverse"), QStringLiteral("-")};
    command.workingDirectory = absoluteRepositoryPath(m_activeIndex);
    command.title = tr("取消暂存代码块 #%1").arg(hunkIndex + 1);
    command.kind = GitCommandKind::HunkOperation;
    command.repositoryIndex = m_activeIndex;
    command.repositoryPath = m_model.pathAt(m_activeIndex);
    command.affectedRepositories = {m_model.pathAt(m_activeIndex)};
    command.standardInput = hunk.patch.toUtf8();
    command.stopOnFailure = true;
    enqueue(command);
    enqueueRepositoryRefresh(m_activeIndex, false);
}

void WorkspaceController::discardHunk(int hunkIndex)
{
    if (m_activeIndex < 0 || m_selectedFile.isEmpty()) return;
    if (!validateOwnFiles({m_selectedFile})) return;
    const DiffFile file = DiffModel::parse(m_diffText);
    if (hunkIndex < 0 || hunkIndex >= file.hunks.size()) return;

    if (file.hunks.size() <= 1) {
        discardFiles({m_selectedFile});
        return;
    }

    const DiffHunk hunk = file.hunks.at(hunkIndex);
    GitCommand command;
    command.program = QStringLiteral("git");
    command.arguments = {QStringLiteral("apply"), QStringLiteral("--reverse"), QStringLiteral("-")};
    command.workingDirectory = absoluteRepositoryPath(m_activeIndex);
    command.title = tr("丢弃代码块 #%1").arg(hunkIndex + 1);
    command.kind = GitCommandKind::HunkOperation;
    command.repositoryIndex = m_activeIndex;
    command.repositoryPath = m_model.pathAt(m_activeIndex);
    command.affectedRepositories = {m_model.pathAt(m_activeIndex)};
    command.standardInput = hunk.patch.toUtf8();
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
        emit operationFailed(title, msg);
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
    command.submittedMessage = targetSha;
    command.paths = {action, QString::number(index), title};
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
            emit operationFailed(tr("无法标记解决"), tr("所选文件不是当前仓库的冲突文件：%1").arg(path));
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
        emit operationFailed(tr("无法继续"), tr("当前仓库没有进行中的合并、变基或拣选操作。"));
        return;
    }
    if (hasConflicts()) {
        emit operationFailed(tr("无法继续"), tr("仍有未解决的冲突文件，请先解决并标记。"));
        return;
    }
    const QScopedValueRollback<quint64> operation(m_enqueueOperationId, ++m_nextOperationId);
    enqueueGit(m_activeIndex, GitQueries::commitScope(), tr("检查提交仓库边界"), GitCommandKind::CommitScopePreflight, true);
    const QString repoDir = absoluteRepositoryPath(m_activeIndex);
    QString gitDir = repoDir + QStringLiteral("/.git");
    QFileInfo gitDirInfo(gitDir);
    if (gitDirInfo.isFile()) {
        QFile f(gitDir);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString content = QString::fromUtf8(f.readAll()).trimmed();
            if (content.startsWith(QStringLiteral("gitdir:"))) {
                gitDir = QDir(repoDir).absoluteFilePath(content.mid(7).trimmed());
            }
        }
    }

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
        emit operationFailed(tr("无法终止"), tr("当前仓库没有进行中的合并、变基或拣选操作。"));
        return;
    }
    const QString repoDir = absoluteRepositoryPath(m_activeIndex);
    QString gitDir = repoDir + QStringLiteral("/.git");
    QFileInfo gitDirInfo(gitDir);
    if (gitDirInfo.isFile()) {
        QFile f(gitDir);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString content = QString::fromUtf8(f.readAll()).trimmed();
            if (content.startsWith(QStringLiteral("gitdir:"))) {
                gitDir = QDir(repoDir).absoluteFilePath(content.mid(7).trimmed());
            }
        }
    }

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
