#pragma once

#include "repositoryworkflow.h"
#include "gerritservice.h"
#include "hookworkflow.h"

// Single-repository review upload and the guarded repair of its missing Change-Id.
class ReviewPushWorkflow final : public RepositoryWorkflow {
    Q_OBJECT
public:
    ReviewPushWorkflow(const WorkflowRepositoryAccess &repositories, GerritService &gerrit, HookWorkflow &hooks)
        : RepositoryWorkflow(repositories), m_gerrit(gerrit), m_hooks(hooks) {}
    QString pushPreview() const;
    QString pushCommandPreview(const QString &remote, const QString &branch, const QVariantMap &options) const;
    QString pushOptionsError(const QVariantMap &options) const;
    void pushActive(const QVariantMap &options, quint64 operationId);
    void repairLastCommitChangeId(quint64 operationId);
    bool handleResult(const GitCommandResult &result);
    void reset();
    void cancel();
    void clearRepair() { m_repairChangeIdRevision.clear(); }
    int state() const { return m_pushState; }
    QString statusText() const { return m_pushStatusText; }
    QString repositoryKey() const { return m_pushRepositoryKey; }
    QString lastReviewUrl() const { return m_lastGerritReviewUrl; }
signals:
    void pushStatusChanged();
    void lastGerritReviewUrlChanged();
private:
    void setPushStatus(int state, const QString &text);
    void setLastGerritReviewUrl(const QString &url);
    void enqueue(GitCommand command) { command.operationId = m_operation; emit commandRequested(command); }
    void enqueueGit(int index, const QStringList &arguments, const QString &title,
                    GitCommandKind kind, bool stopOnFailure = false);
    GerritService &m_gerrit;
    HookWorkflow &m_hooks;
    QString m_repairChangeIdRevision, m_lastGerritReviewUrl;
    int m_pushState = 0;
    QString m_pushStatusText, m_pushRepositoryKey;
    quint64 m_pushOperationId = 0;
    quint64 m_operation = 0;
};
