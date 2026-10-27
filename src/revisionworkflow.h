#pragma once

#include "repositoryworkflow.h"

// Change downloads and local commit-chain rewrites. Repository selection,
// models, command execution and refresh ownership remain outside this object.
class RevisionWorkflow final : public RepositoryWorkflow {
    Q_OBJECT
public:
    using RepositoryWorkflow::RepositoryWorkflow;
    void fetchChange(const QString &spec, int mode, quint64 operationId);
    void requestCommitMessage(const QString &revision);
    void rewriteCommit(const QString &revision, int action, const QString &message, quint64 operationId);
    void checkoutBranch(const QString &branch, quint64 operationId);
    void checkoutRevision(const QString &revision, quint64 operationId);
    void createBranch(const QString &branch, quint64 operationId);
    void deleteBranch(const QString &branch, bool force, quint64 operationId);
    void cherryPick(const QString &revision, quint64 operationId);
    void revertCommit(const QString &revision, quint64 operationId);
    void pruneBranches(quint64 operationId);
    bool handleResult(const GitCommandResult &result);
signals:
    void commitMessageReady(const QString &revision, const QString &message);
private:
    void enqueueChangeFetch(GitCommand command);
    void enqueueRewrite(const GitCommand &completed, const QString &output);
    void enqueue(GitCommand command) { command.operationId = m_operation; emit commandRequested(command); }
    void enqueueGit(int index, const QStringList &arguments, const QString &title, GitCommandKind kind, bool stopOnFailure);
    quint64 m_operation = 0; // Scoped command-chain identity, never a second Git queue.
};
