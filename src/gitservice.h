#pragma once

#include "processrunner.h"
#include "taskreporter.h"
#include <QElapsedTimer>
#include <QTimer>
#include <QProcess>
#include <QQueue>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

enum class GitCommandKind {
    Status,
    Details,
    Diff,
    AiStagedFiles,
    AiStagedDiff,
    AiWorkspaceFiles,
    AiWorkspaceDiff,
    RevisionDiff,
    RevisionFiles,
    History,
    Branches,
    DiscardPreflight,
    CommitScopePreflight,
    HookStatus,
    HookCommitPreflight,
    HookRepairPreflight,
    HookRemoteUrl,
    HookDirectory,
    PushChangeIdPreflight,
    ChangeIdRepairPreflight,
    ChangeIdRepairRevision,
    ChangeIdRepairVerification,
    StashVerify,
    StashList,
    StashAction,
    PullTarget,
    PullExecution,
    RepoSync,
    RevertPreflight,
    UpstreamPreflight,
    UpstreamConfigRemote,
    UpstreamConfigMerge,
    LastCommitMessage,
    RemoteBranches,
    PushRemoteUrl,
    Commit,
    StageAll,
    HunkOperation,
    InstallHook,
    GerritPush,
    RepoList,
    ManifestRepository,
    RepoPrune,
    CherryPick,
    CherryPickSkipPreflight,
    ChangeLookup,
    ChangeFetch,
    RewritePreflight,
    RewriteStop,
    CommitMessage,
    BatchManifest,
    BatchPreflight,
    General
};

enum class ExecutionStatus {
    Success,
    Failure,
    Canceled,
    Timeout
};

// Gerrit magic-refspec options appended to refs/for/<branch>.
struct GerritPushOptions {
    enum WorkState { KeepWorkState, WorkInProgress, ReadyForReview };
    QString topic;
    QStringList reviewers;
    QStringList cc;
    QStringList hashtags;
    WorkState workState = KeepWorkState;
    bool privateChange = false;
    QString message;
};

struct GitCommand {
    struct RemoteTarget {
        QString remote;
        QString branch;
        GerritPushOptions options;
        QString localBranch;
        bool pullRebase = false;
        bool batchPush = false; // Results belong to the batch push table, not the active repository.
        quint64 request = 0;
    };
    struct ChangeRequest {
        int number = 0;
        int patchset = 0;
        int mode = 0; // 0 new local branch, 1 cherry-pick, 2 detached checkout
    };
    struct RewriteRequest {
        int action = 0; // 0 stop for edit, 1 fixup staged changes, 2 reword
        QString message;
    };
    struct HookTransfer {
        QString source;
        QString port;
        QString targetPath;
        QString temporaryPath;
    };
    struct StashAction {
        QString action;
        QString expectedSha;
        QString title;
    };
    QString program = QStringLiteral("git");
    QStringList arguments;
    QString workingDirectory;
    QString title;
    GitCommandKind kind = GitCommandKind::General;
    int repositoryIndex = -1;
    QString repositoryPath;
    bool stopOnFailure = false;
    bool differenceExitCode = false;
    quint64 generation = 0;
    quint64 operationId = 0;
    quint64 historyRequest = 0;
    bool appendHistory = false;
    quint64 diffRequest = 0;
    bool commitOperation = false;
    QString workspacePath;
    QString submittedMessage;
    QStringList paths;
    RemoteTarget target;
    HookTransfer hook;
    StashAction stash;
    ChangeRequest change;
    RewriteRequest rewrite;
    QString revision;
    bool rediscoverProjects = false;
    QStringList affectedRepositories;
    int timeoutMilliseconds = 0;
    QByteArray standardInput;
};

struct GitCommandResult {
    GitCommand command;
    ExecutionStatus status = ExecutionStatus::Success;
    int exitCode = 0;
    QProcess::ExitStatus exitStatus = QProcess::NormalExit;
    QByteArray standardOutput;
    QByteArray standardError;
    QString diagnosticMessage;
};

class GitService final : public TaskReporter
{
    Q_OBJECT
public:
    explicit GitService(QObject *parent = nullptr);

    bool busy() const override;
    QString activeTask() const override { return m_activeTask; }
    int queueSize() const { return m_queue.size(); }
    void setGeneration(quint64 generation) { m_currentGeneration = generation; }

    void enqueue(GitCommand command);
    void stop();
    void clearQueue();
    void cancelOperation(quint64 id);

    static QString displayCommand(const QString &program, const QStringList &arguments);
    static bool validateBranchName(const QString &branch, QString *error = nullptr);
    static bool validateTopicName(const QString &topic, QString *error = nullptr);

signals:
    void commandStarted(const GitCommand &command);
    void commandFinished(const GitCommandResult &result);
    void outputReceived(const QByteArray &output, const QByteArray &errors);
    void dependentCommandsCanceled(quint64 operationId);

private slots:
    void onProcessOutput(const QByteArray &output, const QByteArray &errors);
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus,
                           const QByteArray &output, const QByteArray &errors);
    void onProcessError(const QString &reason);

private:
    void startNext();
    void cancelDependentCommands(const GitCommand &command);

    ProcessRunner m_runner;
    QTimer m_waitNotice;
    QElapsedTimer m_elapsed;
    QQueue<GitCommand> m_queue;
    GitCommand m_current;
    QString m_activeTask;
    quint64 m_currentGeneration = 0;
};
