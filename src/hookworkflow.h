#pragma once

#include "repositoryworkflow.h"
#include "gerritservice.h"

// Owns Hook status, installation lifetime and the optional deferred commit.
class HookWorkflow final : public RepositoryWorkflow {
    Q_OBJECT
public:
    HookWorkflow(const WorkflowRepositoryAccess &repositories, GerritService &gerrit);
    bool hasHook() const { return m_hasCommitHook; }
    bool busy() const { return m_downloading; }
    void rememberCommit(const QString &message, bool amend, bool stageAll);
    void clearPendingCommit();
    void reset();
    void updateStatus();
    void install(bool resume, quint64 operationId);
    bool handleResult(const GitCommandResult &result);
signals:
    void statusChanged();
    void busyChanged();
    void resumeCommitRequested(const QString &message, bool amend, bool stageAll);
private:
    void setStatus(bool installed);
    void resumePendingCommit();
    void finishInstall(const GitCommand &completed);
    void enqueue(GitCommand command) { command.operationId = m_operation; emit commandRequested(command); }
    struct PendingCommit {
        bool valid = false;
        QString workspacePath, repositoryPath, message;
        bool amend = false;
        bool stageAll = false;
    };
    GerritService &m_gerrit;
    PendingCommit m_pendingCommit;
    bool m_resumePendingCommit = false;
    bool m_hasCommitHook = true;
    bool m_downloading = false;
    quint64 m_installRequest = 0;
    quint64 m_operation = 0;
};
