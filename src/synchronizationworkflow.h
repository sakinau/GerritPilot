#pragma once

#include "repositoryworkflow.h"

// Daily pull follows the local branch's actual upstream. Repo synchronization
// and configuring that upstream are handled here, separately from review targets.
class SynchronizationWorkflow final : public RepositoryWorkflow {
    Q_OBJECT
public:
    using RepositoryWorkflow::RepositoryWorkflow;
    void syncSelection(const QStringList &paths, bool rebase, quint64 operationId);
    void syncRepoSelection(int scope, const QStringList &paths, quint64 operationId);
    void configurePullUpstream(const QString &remote, const QString &branch, quint64 operationId);
    bool handleResult(const GitCommandResult &result);
signals:
    void rescanRequested();
private:
    void enqueue(GitCommand command) { command.operationId = m_operation; emit commandRequested(command); }
    quint64 m_operation = 0;
};
