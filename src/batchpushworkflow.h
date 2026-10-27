#pragma once

#include "repositoryworkflow.h"
#include "gerritservice.h"

class BatchPushWorkflow final : public RepositoryWorkflow {
    Q_OBJECT
public:
    BatchPushWorkflow(const WorkflowRepositoryAccess &repositories, GerritService &gerrit)
        : RepositoryWorkflow(repositories), m_gerrit(gerrit) {}
    QVariantList batchPushRows() const;
    bool running() const { return m_batchRunning; }
    void prepareBatchPush();
    void setBatchPushTarget(const QString &path, const QString &branch);
    void setBatchPushIncluded(const QString &path, bool included);
    void startBatchPush(const QVariantMap &options);
    void handleBatchPushResult(const GitCommandResult &result);
    void resetBatchPush();
    void cancel();
signals:
    void changed();
    void targetConfirmed(const QString &path, const QString &remote, const QString &branch);
private:
    struct BatchPushRow {
        QString path, name, branch, remote, target, savedTarget, manifestTarget, manifestRevision;
        QString state, message, url;
        int commits = 0;
        bool included = true;
    };
    int batchRowIndex(const QString &path) const;
    WorkflowRepository repositoryForPath(const QString &path) const;
    void resolveBatchTargets(const QHash<QString, GerritService::ManifestTarget> &manifest);
    void enqueueBatchPreflight(int row, bool execute);
    void finishBatchPushIfDone();
    void enqueue(const GitCommand &command) { emit commandRequested(command); }
    GerritService &m_gerrit;
    QList<BatchPushRow> m_batchRows;
    quint64 m_batchRequest = 0;
    bool m_batchRunning = false;
    GerritPushOptions m_batchPushOptions;
};
