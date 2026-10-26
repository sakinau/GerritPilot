#pragma once

#include "repositorymodel.h"
#include "repositoryproxymodel.h"
#include "gitstatusparser.h"
#include "githistoryparser.h"
#include "gitservice.h"
#include "workspacesettings.h"
#include "repositorywatcher.h"
#include "diffmodel.h"
#include "gerritservice.h"
#include "commitmessageaiservice.h"
#include "projectmanager.h"

#include <QObject>
#include <QSet>
#include <QTimer>

class WorkspaceController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(GerritService* gerrit READ gerrit CONSTANT)
    Q_PROPERTY(CommitMessageAiService* commitAi READ commitAi CONSTANT)
    Q_PROPERTY(RepositoryModel *repositoryModelSource READ repositoryModelSource CONSTANT)
    Q_PROPERTY(RepositoryProxyModel *repositoryModel READ repositoryModel CONSTANT)
    Q_PROPERTY(ProjectManager *projectManager READ projectManager CONSTANT)
    Q_PROPERTY(QString workspacePath READ workspacePath WRITE setWorkspacePath NOTIFY workspacePathChanged)
    Q_PROPERTY(QString selectedName READ selectedName NOTIFY selectedRepositoryChanged)
    Q_PROPERTY(QString selectedPath READ selectedPath NOTIFY selectedRepositoryChanged)
    Q_PROPERTY(QString operationGeneration READ operationGeneration NOTIFY selectedRepositoryChanged)
    Q_PROPERTY(QString selectedBranch READ selectedBranch NOTIFY summaryChanged)
    Q_PROPERTY(QStringList availableBranches READ availableBranches NOTIFY branchesChanged)
    Q_PROPERTY(QStringList remoteBranches READ remoteBranches NOTIFY branchesChanged)
    Q_PROPERTY(QStringList remoteTrackingBranches READ remoteTrackingBranches NOTIFY branchesChanged)
    Q_PROPERTY(bool remoteBranchesLoaded READ remoteBranchesLoaded NOTIFY branchesChanged)
    Q_PROPERTY(QString remoteBranchesRemote READ remoteBranchesRemote NOTIFY branchesChanged)
    Q_PROPERTY(QString pushRemoteUrl READ pushRemoteUrl NOTIFY branchesChanged)
    Q_PROPERTY(QVariantList changedFiles READ changedFiles NOTIFY detailsTextChanged)
    Q_PROPERTY(QVariantList groupedChanges READ groupedChanges NOTIFY detailsTextChanged)
    Q_PROPERTY(bool detailsCached READ detailsCached NOTIFY detailsTextChanged)
    Q_PROPERTY(QString diffText READ diffText NOTIFY diffTextChanged)
    Q_PROPERTY(QString selectedFile READ selectedFile NOTIFY diffTextChanged)
    Q_PROPERTY(bool selectedFileStaged READ selectedFileStaged NOTIFY diffTextChanged)
    Q_PROPERTY(QVariantList historyEntries READ historyEntries NOTIFY historyTextChanged)
    Q_PROPERTY(bool historyCached READ historyCached NOTIFY historyTextChanged)
    Q_PROPERTY(bool historyHasMore READ historyHasMore NOTIFY historyTextChanged)
    Q_PROPERTY(QString revisionDiff READ revisionDiff NOTIFY revisionDiffChanged)
    Q_PROPERTY(QString selectedRevision READ selectedRevision NOTIFY revisionDiffChanged)
    Q_PROPERTY(QStringList revisionFiles READ revisionFiles NOTIFY revisionDiffChanged)
    Q_PROPERTY(QString revisionFile READ revisionFile NOTIFY revisionDiffChanged)
    Q_PROPERTY(QString consoleText READ consoleText NOTIFY consoleTextChanged)
    Q_PROPERTY(QString activeTask READ activeTask NOTIFY busyChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(int repositoryCount READ repositoryCount NOTIFY summaryChanged)
    Q_PROPERTY(int changedRepositoryCount READ changedRepositoryCount NOTIFY summaryChanged)
    Q_PROPERTY(int aheadRepositoryCount READ aheadRepositoryCount NOTIFY summaryChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectionChanged)
    Q_PROPERTY(int viewMode READ viewMode WRITE setViewMode NOTIFY viewModeChanged)
    Q_PROPERTY(bool selectedIgnored READ selectedIgnored NOTIFY selectedRepositoryChanged)
    Q_PROPERTY(QString remote READ remote WRITE setRemote NOTIFY settingsChanged)
    Q_PROPERTY(QString targetBranch READ targetBranch WRITE setTargetBranch NOTIFY settingsChanged)
    Q_PROPERTY(QString topic READ topic WRITE setTopic NOTIFY settingsChanged)
    Q_PROPERTY(bool requireChangeIdHook READ requireChangeIdHook WRITE setRequireChangeIdHook NOTIFY settingsChanged)
    Q_PROPERTY(QString gerritHost READ gerritHost WRITE setGerritHost NOTIFY settingsChanged)
    Q_PROPERTY(QString gerritUser READ gerritUser WRITE setGerritUser NOTIFY settingsChanged)
    Q_PROPERTY(int gerritPort READ gerritPort WRITE setGerritPort NOTIFY settingsChanged)
    Q_PROPERTY(QStringList ignoredRepositories READ ignoredRepositories NOTIFY ignoredRepositoriesChanged)
    Q_PROPERTY(QStringList stashList READ stashList NOTIFY stashListChanged)
    Q_PROPERTY(int hunkCount READ hunkCount NOTIFY diffTextChanged)
    Q_PROPERTY(bool isBinaryDiff READ isBinaryDiff NOTIFY diffTextChanged)
    Q_PROPERTY(bool hasConflicts READ hasConflicts NOTIFY detailsTextChanged)
    Q_PROPERTY(QString pendingOperation READ pendingOperation NOTIFY detailsTextChanged)
    Q_PROPERTY(QString pendingCherryPickRevision READ pendingCherryPickRevision NOTIFY detailsTextChanged)
    Q_PROPERTY(bool canSkipCherryPick READ canSkipCherryPick NOTIFY detailsTextChanged)
    Q_PROPERTY(bool hasPendingOperation READ hasPendingOperation NOTIFY detailsTextChanged)
    Q_PROPERTY(bool ignoreWhitespace READ ignoreWhitespace WRITE setIgnoreWhitespace NOTIFY ignoreWhitespaceChanged)
    Q_PROPERTY(bool hasCommitHook READ hasCommitHook NOTIFY hasCommitHookChanged)
    Q_PROPERTY(QString lastGerritReviewUrl READ lastGerritReviewUrl NOTIFY lastGerritReviewUrlChanged)
    Q_PROPERTY(int pushState READ pushState NOTIFY pushStatusChanged)
    Q_PROPERTY(QString pushStatusText READ pushStatusText NOTIFY pushStatusChanged)
    Q_PROPERTY(QString pushRepositoryKey READ pushRepositoryKey NOTIFY pushStatusChanged)
    Q_PROPERTY(bool isRepoWorkspace READ isRepoWorkspace NOTIFY workspacePathChanged)

public:
    explicit WorkspaceController(QObject *parent = nullptr);

    RepositoryModel *repositoryModelSource() { return &m_model; }
    RepositoryProxyModel *repositoryModel() { return &m_proxy; }
    QString workspacePath() const { return m_workspacePath; }
    void setWorkspacePath(const QString &path);
    QString selectedName() const;
    QString selectedPath() const;
    QString operationGeneration() const { return QString::number(m_generation); }
    QString selectedBranch() const;
    QStringList availableBranches() const { return m_availableBranches; }
    QStringList remoteBranches() const { return m_remoteBranches; }
    QStringList remoteTrackingBranches() const { return m_remoteTrackingBranches; }
    bool remoteBranchesLoaded() const { return m_remoteBranchesLoaded; }
    QString remoteBranchesRemote() const { return m_branchQueryRemote; }
    QString pushRemoteUrl() const { return m_pushRemoteUrl; }
    Q_INVOKABLE void requestRemoteBranches(const QString &remote);
    Q_INVOKABLE void configurePullUpstream(const QString &remote, const QString &branch);
    Q_INVOKABLE void requestLastCommitMessage();
    QVariantList changedFiles() const { return m_changedFiles; }
    bool detailsCached() const { return m_detailsCached; }
    bool hasConflicts() const;
    QString pendingOperation() const;
    QString pendingCherryPickRevision() const;
    bool canSkipCherryPick() const;
    bool hasPendingOperation() const { return !pendingOperation().isEmpty(); }
    QVariantList groupedChanges() const { return m_groupedChanges; }
    QString diffText() const { return m_diffText; }
    QString selectedFile() const { return m_selectedFile; }
    bool selectedFileStaged() const { return m_selectedFileStaged; }
    QVariantList historyEntries() const { return m_historyEntries; }
    bool historyCached() const { return !m_historyLoaded && !m_historyEntries.isEmpty(); }
    bool historyHasMore() const { return m_historyLoaded && m_historyHasMore; }
    QString revisionDiff() const { return m_revisionDiff; }
    QString selectedRevision() const { return m_selectedRevision; }
    QStringList revisionFiles() const { return m_revisionFiles; }
    QString revisionFile() const { return m_revisionFile; }
    QString consoleText() const { return m_consoleText; }
    QString activeTask() const { return m_gitService.activeTask(); }
    bool busy() const { return m_gitService.busy(); }
    int repositoryCount() const { return m_model.rowCount(); }
    int changedRepositoryCount() const { return m_model.changedCount(); }
    int aheadRepositoryCount() const { return m_model.aheadCount(); }
    int selectedCount() const { return m_model.selectedCount(); }
    int viewMode() const { return m_proxy.filterMode(); }
    void setViewMode(int mode);
    bool selectedIgnored() const { return m_model.ignoredAt(m_activeIndex); }

    QString remote() const { return m_repositoryConfig.remote; }
    void setRemote(const QString &value);
    QString targetBranch() const { return m_repositoryConfig.targetBranch; }
    void setTargetBranch(const QString &value);
    QString topic() const { return m_repositoryConfig.topic; }
    void setTopic(const QString &value);
    bool requireChangeIdHook() const { return m_repositoryConfig.requireChangeIdHook; }
    void setRequireChangeIdHook(bool required);
    QString gerritHost() const { return m_repositoryConfig.gerritHost; }
    void setGerritHost(const QString &value);
    QString gerritUser() const { return m_repositoryConfig.gerritUser; }
    void setGerritUser(const QString &value);
    int gerritPort() const { return m_repositoryConfig.gerritPort; }
    void setGerritPort(int value);
    QStringList ignoredRepositories() const { return m_ignoredRepositories; }
    bool ignoreWhitespace() const { return m_ignoreWhitespace; }
    void setIgnoreWhitespace(bool value);

    Q_INVOKABLE void scanWorkspace();
    Q_INVOKABLE void refreshAll();
    Q_INVOKABLE void refreshActive();
    Q_INVOKABLE void requestActiveRefresh();
    Q_INVOKABLE void loadHistory();
    Q_INVOKABLE void loadMoreHistory();
    Q_INVOKABLE void activateRepository(int index);
    Q_INVOKABLE void toggleRepository(int index);
    Q_INVOKABLE void selectAll(bool selected);
    Q_INVOKABLE void setActiveRepositoryIgnored(bool ignored);
    Q_INVOKABLE void setRepositoryIgnored(const QString &path, bool ignored);
    GerritService *gerrit() { return &m_gerrit; }
    CommitMessageAiService *commitAi() { return &m_commitAi; }
    ProjectManager *projectManager() { return &m_projectManager; }

    Q_INVOKABLE void syncSelection(int scope = 0);
    Q_INVOKABLE void syncRepoSelection(int scope = 0);
    Q_INVOKABLE void stageFiles(const QStringList &paths);
    Q_INVOKABLE void unstageFiles(const QStringList &paths);
    Q_INVOKABLE void discardFiles(const QStringList &paths);
    Q_INVOKABLE void commitActive(const QString &message, bool amend, bool stageAll);
    Q_INVOKABLE void generateCommitMessage(const QString &existingMessage, int scope,
                                           const QString &commitType, const QString &issueId,
                                           bool manualCompiled = false,
                                           const QString &compileNote = QString(),
                                           const QString &purpose = QString(),
                                           const QString &secondaryIssueId = QString());
    Q_INVOKABLE void pushActive();
    Q_INVOKABLE void checkoutBranch(const QString &branch);
    Q_INVOKABLE void checkoutRevision(const QString &revision);
    Q_INVOKABLE void showRevisionDiff(const QString &revision);
    Q_INVOKABLE void showRevisionFileDiff(const QString &path);
    Q_INVOKABLE void showWorkingTreeDiff();
    Q_INVOKABLE void showFileDiff(const QString &path, bool staged = false);
    Q_INVOKABLE void openFile(const QString &path);
    Q_INVOKABLE int activeProxyIndex() const;
    Q_INVOKABLE void createBranch(const QString &branch);
    Q_INVOKABLE void deleteBranch(const QString &branch, bool force = false);
    Q_INVOKABLE void installCommitHook(bool resumePendingCommit = false);
    Q_INVOKABLE void repairLastCommitChangeId();
    Q_INVOKABLE void stop();
    Q_INVOKABLE QString pushPreview() const;
    Q_INVOKABLE QString syncPreview(int scope = 0, int method = 0) const;
    Q_INVOKABLE int checkedRepositoryCount() const;
    Q_INVOKABLE int totalRepositoryCount() const;

    // Hunk-level operations
    Q_INVOKABLE int hunkCount() const;
    Q_INVOKABLE bool isBinaryDiff() const;
    Q_INVOKABLE void stageHunk(int hunkIndex);
    Q_INVOKABLE void unstageHunk(int hunkIndex);
    Q_INVOKABLE void discardHunk(int hunkIndex);

    // Stash & Conflict operations
    Q_INVOKABLE QStringList stashList() const { return m_stashList; }
    Q_INVOKABLE void loadStashList();
    Q_INVOKABLE void stashSave(const QString &message = QString(), bool includeUntracked = false);
    Q_INVOKABLE void stashPop(int index = 0, const QString &expectedSha = QString());
    Q_INVOKABLE void stashApply(int index = 0, const QString &expectedSha = QString());
    Q_INVOKABLE void stashDrop(int index = 0, const QString &expectedSha = QString());
    Q_INVOKABLE QString stashSha(int index) const;
    Q_INVOKABLE void markResolved(const QStringList &paths);
    Q_INVOKABLE void continueOperation();
    Q_INVOKABLE void skipCherryPick(const QString &expectedRevision);
    Q_INVOKABLE void abortOperation();

    // Draft persistence
    Q_INVOKABLE QString loadCommitDraft(const QString &repoPath = QString(), const QString &workspacePath = QString()) const;
    Q_INVOKABLE void saveCommitDraft(const QString &repoPath, const QString &text, const QString &workspacePath = QString());

    bool hasCommitHook() const { return m_hasCommitHook; }
    QString lastGerritReviewUrl() const { return m_lastGerritReviewUrl; }
    int pushState() const { return m_pushState; }
    QString pushStatusText() const { return m_pushStatusText; }
    QString pushRepositoryKey() const { return m_pushRepositoryKey; }
    bool isRepoWorkspace() const;
    Q_INVOKABLE QString lastCommitMessage() const;
    Q_INVOKABLE void cherryPick(const QString &revision);
    Q_INVOKABLE void revertCommit(const QString &revision);
    Q_INVOKABLE void pruneBranches();
    Q_INVOKABLE void copyToClipboard(const QString &text);

signals:
    void workspacePathChanged();
    void selectedRepositoryChanged();
    void detailsTextChanged();
    void diffTextChanged();
    void historyTextChanged();
    void revisionDiffChanged();
    void consoleTextChanged();
    void busyChanged();
    void summaryChanged();
    void selectionChanged();
    void ignoredRepositoriesChanged();
    void viewModeChanged();
    void settingsChanged();
    void branchesChanged();
    void stashListChanged();
    void ignoreWhitespaceChanged();
    void hasCommitHookChanged();
    void lastGerritReviewUrlChanged();
    void pushStatusChanged();
    void operationError(const QVariantMap &error);
    void operationFinished(const QString &message);
    void lastCommitMessageReady(const QString &workspacePath, const QString &repositoryPath, const QString &message);
    void commitSucceeded(const QString &workspacePath, const QString &repositoryPath,
                         const QString &submittedMessage);

private slots:
    void onGitCommandFinished(const GitCommandResult &result);
    void onGitOutputReceived(const QByteArray &output, const QByteArray &errors);

private:
    enum class ErrorType { General, Ai, Upstream, Conflict, Hook, ChangeId };
    void reportError(const QString &title, const QString &message, ErrorType type = ErrorType::General,
                     const QStringList &actions = {}, const GitCommand *command = nullptr);
    void applyRepositoryPaths(QStringList paths);
    void discoverManifestRepository(const QStringList &paths);
    void loadSettings();
    void loadRepositorySettings();
    void saveSettings() const;
    void loadRepositoryCache();
    void restoreRepositoryCache(const QString &repositoryPath);
    void saveFileCache(int index, const QVariantList &files);
    void saveBranchCache();
    void saveHistoryCache();

    void enqueue(GitCommand command);
    void enqueueGit(int index, const QStringList &arguments, const QString &title,
                    GitCommandKind kind = GitCommandKind::General, bool stopOnFailure = false);
    void invalidateFailedQuery(const GitCommand &command);
    void cancelScheduledRefresh();
    void appendConsole(const QString &text);
    void appendConsoleProgress(const QString &progress);
    void setPushStatus(int state, const QString &text);
    void setLastGerritReviewUrl(const QString &url);
    QString absoluteRepositoryPath(int index) const;
    QStringList actionPaths(int scope = -1) const;
    void updateCommitHookStatus();
    void setCommitHookStatus(bool installed);
    void resumePendingCommitAfterHook();
    void applyStatus(int index, const GitStatusParser::Snapshot &snapshot, bool persist = true);
    bool isOwnRepositoryPath(int index, const QString &path) const;
    QVariantList ownRepositoryFiles(int index, const QVariantList &files) const;
    void rebuildGroupedChanges();
    bool validateOwnFiles(const QStringList &paths);
    bool prepareScopedStage(GitCommand &command);
    void enqueueRepositoryRefresh(int index, bool includeBranches);
    void refreshSelectedFile();
    void setDiffText(const QString &text);
    const DiffIndex &currentDiffIndex() const;
    void executeDiscard(int repositoryIndex, const QStringList &paths, const QString &statusOutput);
    void executeStashAction(const QString &action, int index, const QString &expectedSha, const QString &title);

    RepositoryModel m_model;
    RepositoryProxyModel m_proxy;
    GitService m_gitService;
    RepositoryWatcher m_repositoryWatcher;
    WorkspaceSettings::RepositoryConfig m_repositoryConfig;
    GerritService m_gerrit;
    CommitMessageAiService m_commitAi;
    QString m_workspacePath;
    ProjectManager m_projectManager;

    QTimer m_refreshTimer;
    bool m_refreshPending = false;
    quint64 m_generation = 0;
    quint64 m_nextOperationId = 0;
    quint64 m_enqueueOperationId = 0;
    QSet<int> m_unbornRepositories;
    quint64 m_diffRequest = 0;
    quint64 m_aiRequest = 0;
    QString m_aiExistingMessage;
    QString m_aiCommitType;
    QString m_aiIssueId;
    QString m_aiSecondaryIssueId;
    bool m_aiManualCompiled = false;
    QString m_aiCompileNote;
    QString m_aiPurpose;
    QStringList m_aiChangedFiles;
    QStringList m_aiUntrackedFiles;
    quint64 m_revisionRequest = 0;
    quint64 m_revisionFilesRequest = 0;
    QVariantList m_changedFiles;
    QVariantList m_groupedChanges;
    bool m_detailsCached = false;
    QString m_diffText;
    mutable DiffIndex m_diffIndex;
    mutable bool m_diffIndexValid = false;
    QString m_selectedFile;
    bool m_selectedFileStaged = false;
    QVariantList m_historyEntries;
    bool m_historyRequested = false;
    bool m_historyPending = false;
    bool m_historyLoaded = false;
    bool m_historyHasMore = false;
    quint64 m_historyRequest = 0;
    QString m_historyAnchor;
    QString m_revisionDiff;
    QString m_selectedRevision;
    QStringList m_revisionFiles;
    QString m_revisionFile;
    QStringList m_availableBranches;
    QStringList m_remoteBranches;
    QStringList m_remoteTrackingBranches;
    bool m_remoteBranchesLoaded = false;
    QString m_consoleText;
    bool m_consoleHasProgress = false;
    QStringList m_ignoredRepositories;
    QString m_cachedWorkspacePath;
    QString m_cachedRepositoryPath;
    struct StashEntry {
        QString ref;
        QString sha;
        QString description;
        QString repositoryPath;
    };
    QStringList m_stashList;
    QList<StashEntry> m_stashEntries;
    QString m_stashRepositoryPath;
    bool m_ignoreWhitespace = false;
    bool m_hasCommitHook = true;
    QString m_repairChangeIdRevision;
    struct PendingCommit {
        bool valid = false;
        QString workspacePath;
        QString repositoryPath;
        QString message;
        bool amend = false;
        bool stageAll = false;
    };
    PendingCommit m_pendingCommit;
    bool m_resumePendingCommit = false;
    QString m_lastGerritReviewUrl;
    int m_pushState = 0; // 0 idle, 1 running, 2 confirmed, 3 failed/canceled
    QString m_pushStatusText;
    QString m_pushRepositoryKey;
    quint64 m_pushOperationId = 0;
    int m_activeIndex = -1;
    QString m_lastCommitMessage;
    QString m_branchQueryRemote;
    QString m_pushRemoteUrl;
    quint64 m_branchQuery = 0;
};
