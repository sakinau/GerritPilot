#pragma once

#include "gitservice.h"
#include "workspacesettings.h"
#include <QObject>
#include <QVariantList>

// A value snapshot, never a second owner of the selected repository's state.
struct WorkflowRepository {
    int index = -1;
    QString path;
    QString name;
    QString branch;
    QString directory;
    QString workspace;
    WorkspaceSettings::RepositoryConfig config;
    bool detailsCached = false;
    bool hasStaged = false;
    bool hasPendingOperation = false;
    bool hasConflicts = false;
    bool unborn = false;
    bool canSkipCherryPick = false;
    QString operation, cherryPickRevision;
    QStringList branches;
    QVariantList files;
    bool valid() const { return index >= 0 && !directory.isEmpty(); }
};

// Workflows can read repository context, but cannot reach controller internals,
// mutate models, or start processes. Tests supply this interface independently.
class WorkflowRepositoryAccess {
public:
    virtual ~WorkflowRepositoryAccess() = default;
    virtual WorkflowRepository activeRepositoryContext() const = 0;
    virtual WorkflowRepository repositoryContext(int index) const = 0;
    virtual QList<WorkflowRepository> checkedRepositoryContexts() const = 0;
    virtual WorkflowRepository repositoryContextByPath(const QString &path) const = 0;
    virtual QString workspacePath() const = 0;
    virtual quint64 workspaceGeneration() const = 0;
    virtual bool isRepoWorkspace() const = 0;
    virtual bool busy() const = 0;
    virtual bool isOwnRepositoryPath(int index, const QString &path) const = 0;
    virtual QVariantList ownRepositoryFiles(int index, const QVariantList &files) const = 0;
};

enum class WorkflowErrorType { General, Ai, Upstream, Conflict, Hook, ChangeId };

struct WorkflowFailure {
    QString title;
    QString message;
    WorkflowErrorType type = WorkflowErrorType::General;
    QStringList actions;
    GitCommand command;
    bool hasCommand = false;
};

class RepositoryWorkflow : public QObject {
    Q_OBJECT
public:
    explicit RepositoryWorkflow(const WorkflowRepositoryAccess &repositories, QObject *parent = nullptr)
        : QObject(parent), m_repositories(repositories) {}

signals:
    void commandRequested(const GitCommand &command);
    void refreshRequested(const GitCommand &context, bool branches);
    void failed(const WorkflowFailure &failure);
    void logMessage(const QString &message);
    void finished(const QString &message);
    void cancelOperationRequested(quint64 operationId);

protected:
    bool isActive(const GitCommand &command) const {
        const auto current = m_repositories.activeRepositoryContext();
        return command.generation == m_repositories.workspaceGeneration()
            && command.repositoryIndex == current.index && command.repositoryPath == current.path
            && command.workspacePath == current.workspace;
    }
    static GitCommand commandFor(const WorkflowRepository &repository, const QStringList &arguments,
                                 const QString &title, GitCommandKind kind, quint64 operation = 0) {
        GitCommand command;
        command.arguments = arguments;
        command.title = title;
        command.kind = kind;
        command.operationId = operation;
        command.repositoryIndex = repository.index;
        command.repositoryPath = repository.path;
        command.workingDirectory = repository.directory;
        command.workspacePath = repository.workspace;
        command.stopOnFailure = true;
        if (kind == GitCommandKind::General) command.affectedRepositories = {repository.path};
        return command;
    }
    void fail(const QString &title, const QString &message,
              WorkflowErrorType type = WorkflowErrorType::General,
              const QStringList &actions = {}, const GitCommand *command = nullptr) {
        emit failed({title, message, type, actions, command ? *command : GitCommand{}, command != nullptr});
    }
    void requestRefresh(int index, bool branches) {
        auto context = commandFor(m_repositories.repositoryContext(index), {}, {}, GitCommandKind::Status);
        context.generation = m_repositories.workspaceGeneration();
        emit refreshRequested(context, branches);
    }
    const WorkflowRepositoryAccess &m_repositories;
};
