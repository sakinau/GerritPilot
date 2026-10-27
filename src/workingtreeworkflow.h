#pragma once

#include "repositoryworkflow.h"
#include "diffmodel.h"

// Working-tree mutations, stash identities and pending-operation recovery.
// Displayed file/diff state stays in the controller; this object owns only stash state.
class WorkingTreeWorkflow final : public RepositoryWorkflow {
    Q_OBJECT
public:
    using RepositoryWorkflow::RepositoryWorkflow;
    void stageFiles(const QStringList &paths, quint64 operationId);
    void unstageFiles(const QStringList &paths, quint64 operationId);
    void discardFiles(const QStringList &paths, quint64 operationId);
    bool validateOwnFiles(const QStringList &paths);
    bool prepareScopedStage(GitCommand &command);
    enum class HunkAction { Stage, Unstage, Discard };
    void applyHunk(const QString &path, const QString &diffText, const DiffIndex &index,
                   int hunkIndex, HunkAction action, quint64 operationId);
    QString stashSha(int index) const;
    QStringList stashList() const { return m_stashList; }
    void loadStashList();
    void stashSave(const QString &message, bool includeUntracked, quint64 operationId);
    void stashPop(int index, const QString &expectedSha, quint64 operationId);
    void stashApply(int index, const QString &expectedSha, quint64 operationId);
    void stashDrop(int index, const QString &expectedSha, quint64 operationId);
    void markResolved(const QStringList &paths, quint64 operationId);
    void continueOperation(quint64 operationId);
    void abortOperation(quint64 operationId);
    void skipCherryPick(const QString &expectedRevision, quint64 operationId);
    void reset();
    bool handleResult(const GitCommandResult &result);
signals:
    void stashChanged();
private:
    void executeDiscard(int repositoryIndex, const QStringList &paths, const QString &statusOutput);
    void executeStashAction(const QString &action, int index, const QString &expectedSha, const QString &title);
    void enqueue(GitCommand command) { command.operationId = m_operation; emit commandRequested(command); }
    void enqueueGit(int index, const QStringList &arguments, const QString &title,
                    GitCommandKind kind = GitCommandKind::General, bool stopOnFailure = false);
    struct StashEntry { QString ref, sha, description, repositoryPath; };
    QStringList m_stashList;
    QList<StashEntry> m_stashEntries;
    QString m_stashRepositoryPath;
    quint64 m_operation = 0;
};
