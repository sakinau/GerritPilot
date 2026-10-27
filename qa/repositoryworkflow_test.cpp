#include "aicommitworkflow.h"
#include "batchpushworkflow.h"
#include "hookworkflow.h"
#include "reviewpushworkflow.h"
#include "revisionworkflow.h"
#include "synchronizationworkflow.h"
#include "workingtreeworkflow.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>
#include <cstdlib>

#define CHECK(condition) do { if (!(condition)) { \
    qCritical() << "FAIL at line" << __LINE__ << #condition; return EXIT_FAILURE; \
} } while (false)

// No controller, Git process or model API is used by these workflow tests.
struct Repositories final : WorkflowRepositoryAccess {
    QTemporaryDir directory;
    QList<WorkflowRepository> entries;
    int active = 0;
    quint64 generation = 7;
    bool blocked = false;
    bool repo = false;
    Repositories() {
        for (int i = 0; i < 2; ++i) {
            WorkflowRepository repository;
            repository.index = i;
            repository.path = i ? QStringLiteral("second") : QStringLiteral("first");
            repository.name = repository.path;
            repository.directory = directory.path() + QLatin1Char('/') + repository.path;
            repository.workspace = directory.path();
            repository.branch = QStringLiteral("topic");
            repository.config.remote = QStringLiteral("origin");
            repository.config.targetBranch = QStringLiteral("review-target");
            repository.config.targetBranchConfirmed = true;
            repository.files = {QVariantMap{{QStringLiteral("path"), QStringLiteral("a.cpp")},
                {QStringLiteral("status"), QStringLiteral(" M")}, {QStringLiteral("hasStagedChanges"), false}}};
            QDir().mkpath(repository.directory);
            entries.append(repository);
        }
    }
    WorkflowRepository activeRepositoryContext() const override { return repositoryContext(active); }
    WorkflowRepository repositoryContext(int index) const override {
        return index >= 0 && index < entries.size() ? entries.at(index) : WorkflowRepository{};
    }
    WorkflowRepository repositoryContextByPath(const QString &path) const override {
        for (const auto &repository : entries) if (repository.path == path) return repository;
        return {};
    }
    QList<WorkflowRepository> checkedRepositoryContexts() const override { return entries; }
    QString workspacePath() const override { return directory.path(); }
    quint64 workspaceGeneration() const override { return generation; }
    bool isRepoWorkspace() const override { return repo; }
    bool busy() const override { return blocked; }
    bool isOwnRepositoryPath(int, const QString &path) const override {
        return !path.isEmpty() && !path.startsWith(QLatin1Char('/')) && !path.startsWith(QStringLiteral("nested/"))
            && !path.contains(QStringLiteral(".."));
    }
    QVariantList ownRepositoryFiles(int index, const QVariantList &files) const override {
        QVariantList filtered;
        for (const auto &file : files)
            if (isOwnRepositoryPath(index, file.toMap().value(QStringLiteral("path")).toString())) filtered.append(file);
        return filtered;
    }
};

struct Trace {
    QList<GitCommand> commands;
    QList<WorkflowFailure> errors;
    QList<quint64> canceled;
    int refreshes = 0;
    quint64 nextOperation = 100;
    void attach(RepositoryWorkflow &workflow, const Repositories &repositories) {
        QObject::connect(&workflow, &RepositoryWorkflow::commandRequested, &workflow, [this, &repositories](GitCommand command) {
            command.generation = repositories.generation;
            if (!command.operationId) command.operationId = ++nextOperation;
            if (command.workspacePath.isEmpty()) command.workspacePath = repositories.workspacePath();
            commands.append(command);
        });
        QObject::connect(&workflow, &RepositoryWorkflow::failed, &workflow, [this](const WorkflowFailure &failure) { errors.append(failure); });
        QObject::connect(&workflow, &RepositoryWorkflow::cancelOperationRequested, &workflow, [this](quint64 id) { canceled.append(id); });
        QObject::connect(&workflow, &RepositoryWorkflow::refreshRequested, &workflow, [this](const GitCommand &, bool) { ++refreshes; });
    }
};

static GitCommandResult reply(const GitCommand &command, const QString &output = {}, bool success = true)
{
    GitCommandResult result;
    result.command = command;
    result.status = success ? ExecutionStatus::Success : ExecutionStatus::Failure;
    result.standardOutput = output.toUtf8();
    result.diagnosticMessage = success ? QString() : QStringLiteral("simulated failure");
    return result;
}

static QString pendingCommit(bool changeId = true)
{
    return QString(40, QLatin1Char('a')) + QChar(0) + QStringLiteral("subject\n\n")
        + (changeId ? QStringLiteral("Change-Id: I") + QString(40, QLatin1Char('b')) : QString()) + QChar(0);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("GerritPilotTests"));
    QCoreApplication::setApplicationName(QStringLiteral("WorkflowIsolation"));
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());

    {
        Repositories repositories;
        GerritService gerrit;
        BatchPushWorkflow batch(repositories, gerrit);
        Trace trace;
        trace.attach(batch, repositories);
        batch.prepareBatchPush();
        CHECK(trace.commands.size() == 2);
        const auto checks = trace.commands;
        batch.handleBatchPushResult(reply(checks.at(0), pendingCommit()));
        batch.handleBatchPushResult(reply(checks.at(1), pendingCommit()));
        CHECK(batch.batchPushRows().at(0).toMap().value("state") == QStringLiteral("ready"));
        trace.commands.clear();
        batch.startBatchPush({{QStringLiteral("topic"), QStringLiteral("shared-topic")}});
        CHECK(batch.running() && trace.commands.size() == 2);
        const auto pushes = trace.commands;
        CHECK(pushes.at(0).operationId != pushes.at(1).operationId);
        batch.handleBatchPushResult(reply(pushes.at(0), {}, false));
        CHECK(batch.running());
        batch.handleBatchPushResult(reply(pushes.at(1), pendingCommit()));
        const auto push = trace.commands.last();
        CHECK(push.kind == GitCommandKind::GerritPush && push.operationId == pushes.at(1).operationId);
        CHECK(push.arguments.last().contains(QStringLiteral("%topic=shared-topic")));
        batch.handleBatchPushResult(reply(push, QStringLiteral("remote: https://review.example/c/project/+/12345")));
        CHECK(!batch.running());
        CHECK(batch.batchPushRows().at(0).toMap().value("state") == QStringLiteral("failed"));
        CHECK(batch.batchPushRows().at(1).toMap().value("state") == QStringLiteral("success"));
        batch.resetBatchPush();
        batch.handleBatchPushResult(reply(push, QStringLiteral("https://review.example/c/project/+/12345")));
        CHECK(batch.batchPushRows().isEmpty());
    }
    {
        Repositories repositories;
        RevisionWorkflow revisions(repositories);
        Trace trace;
        trace.attach(revisions, repositories);
        const QString sha(40, QLatin1Char('a'));
        const QString parent(40, QLatin1Char('c'));
        const QString cid = QStringLiteral("I") + QString(40, QLatin1Char('b'));
        revisions.rewriteCommit(sha, 2, QStringLiteral("new subject"), 42);
        CHECK(trace.commands.size() == 1 && trace.commands.first().operationId == 42);
        const auto preflight = trace.commands.first();
        const QString history = sha + QChar(0) + parent + QChar(0)
            + QStringLiteral("old subject\n\nChange-Id: ") + cid + QChar(0);
        revisions.handleResult(reply(preflight, history));
        CHECK(trace.commands.size() == 4);
        CHECK(trace.commands.at(1).kind == GitCommandKind::RewriteStop);
        CHECK(trace.commands.at(2).arguments.contains(QStringLiteral("--amend")));
        CHECK(trace.commands.at(2).standardInput.contains(cid.toUtf8()));
        for (const auto &command : trace.commands) CHECK(command.operationId == 42);
        trace.commands.clear();
        repositories.active = 1;
        revisions.handleResult(reply(preflight, history));
        CHECK(trace.commands.isEmpty());
        repositories.active = 0;
        revisions.fetchChange(QStringLiteral("12345/3"), 0, 43);
        CHECK(trace.commands.size() == 2);
        CHECK(trace.commands.first().kind == GitCommandKind::ChangeFetch);
        CHECK(trace.commands.last().arguments.contains(QStringLiteral("review/12345-3")));
        CHECK(trace.commands.last().operationId == 43);
    }
    {
        Repositories repositories;
        CommitMessageAiService service;
        service.setApiEndpoint(QStringLiteral("http://127.0.0.1:1/v1"));
        service.setModelName(QStringLiteral("offline-fixture"));
        AiCommitWorkflow collection(repositories, service);
        Trace trace;
        trace.attach(collection, repositories);
        collection.generate({}, 0, QStringLiteral("fix"), QStringLiteral("m-7114324437"), false, {}, {}, {});
        CHECK(trace.commands.isEmpty() && trace.errors.size() == 1); // Missing companion is blocked before source collection.
        collection.generate({}, 0, QStringLiteral("fix"), QStringLiteral("m-7114324437"), false, {}, {}, QStringLiteral("r-7118000461"));
        CHECK(trace.commands.size() == 1);
        const auto files = trace.commands.first();
        const QString paths = QStringLiteral("a.cpp") + QChar(0) + QStringLiteral("nested/child.cpp") + QChar(0);
        collection.handleResult(reply(files, paths));
        CHECK(trace.commands.size() == 2);
        CHECK(trace.commands.last().kind == GitCommandKind::AiStagedDiff);
        CHECK(trace.commands.last().paths == QStringList{QStringLiteral("a.cpp")});
        collection.reset();
        trace.commands.clear();
        collection.handleResult(reply(files, paths));
        CHECK(trace.commands.isEmpty());
    }
    {
        Repositories repositories;
        GerritService gerrit;
        HookWorkflow hooks(repositories, gerrit);
        Trace trace;
        trace.attach(hooks, repositories);
        QObject::connect(&hooks, &HookWorkflow::busyChanged, &hooks, [&] { repositories.blocked = hooks.busy(); });
        hooks.rememberCommit(QStringLiteral("pending"), false, false);
        GitCommand preflight;
        preflight.kind = GitCommandKind::HookCommitPreflight;
        preflight.generation = repositories.generation;
        preflight.repositoryIndex = 0;
        preflight.repositoryPath = repositories.entries.first().path;
        preflight.workspacePath = repositories.workspacePath();
        preflight.operationId = 80;
        hooks.handleResult(reply(preflight, repositories.directory.path() + QStringLiteral("/missing/commit-msg")));
        CHECK(!hooks.hasHook() && trace.canceled == QList<quint64>{80});
        CHECK(trace.errors.last().actions.contains(QStringLiteral("resumeCommit")));
        hooks.install(false, 81);
        const auto remote = trace.commands.last();
        hooks.handleResult(reply(remote, QStringLiteral("http://127.0.0.1:1/project")));
        const auto directory = trace.commands.last();
        const QString target = repositories.directory.path() + QStringLiteral("/hooks/commit-msg");
        hooks.handleResult(reply(directory, target));
        CHECK(hooks.busy() && repositories.blocked);
        hooks.reset(); // Abort a local, unserved download; no external network endpoint is contacted.
        QCoreApplication::processEvents();
        CHECK(!hooks.busy() && !repositories.blocked && !QFileInfo::exists(target));
    }
    {
        Repositories repositories;
        GerritService gerrit;
        HookWorkflow hooks(repositories, gerrit);
        ReviewPushWorkflow review(repositories, gerrit, hooks);
        Trace trace;
        trace.attach(review, repositories);
        review.pushActive({}, 90);
        CHECK(review.state() == 1 && trace.commands.size() == 1);
        review.handleResult(reply(trace.commands.first(), pendingCommit()));
        CHECK(trace.commands.at(1).kind == GitCommandKind::GerritPush);
        CHECK(trace.commands.at(1).operationId == 90);
        review.handleResult(reply(trace.commands.at(1), QStringLiteral("Everything up-to-date")));
        CHECK(review.state() == 3 && review.lastReviewUrl().isEmpty());
        trace.commands.clear();
        review.pushActive({}, 91);
        review.handleResult(reply(trace.commands.first(), pendingCommit(false)));
        CHECK(trace.commands.size() == 1 && review.state() == 3);
        CHECK(trace.errors.last().actions.contains(QStringLiteral("repairChangeId")));
    }
    {
        Repositories repositories;
        WorkingTreeWorkflow tree(repositories);
        Trace trace;
        trace.attach(tree, repositories);
        tree.stageFiles({QStringLiteral("nested/child.cpp")}, 100);
        CHECK(trace.commands.isEmpty() && trace.errors.size() == 1);
        tree.discardFiles({QStringLiteral("a.cpp")}, 101);
        CHECK(trace.commands.size() == 1);
        tree.handleResult(reply(trace.commands.first(), QStringLiteral("UU a.cpp") + QChar(0)));
        CHECK(trace.commands.size() == 1 && trace.errors.size() == 2); // Changed/conflicted status prevents any write.
        tree.loadStashList();
        const auto list = trace.commands.last();
        const QString sha(40, QLatin1Char('e'));
        const QString stash = QStringLiteral("stash@{0}") + QChar(0) + sha + QChar(0) + QStringLiteral("stash@{0}: original");
        tree.handleResult(reply(list, stash));
        CHECK(tree.stashSha(0) == sha);
        tree.stashPop(0, sha, 102);
        const auto verify = trace.commands.last();
        tree.handleResult(reply(verify, QStringLiteral("stash@{1}") + QChar(0) + sha));
        CHECK(trace.commands.at(trace.commands.size() - 2).arguments == QStringList({QStringLiteral("stash"), QStringLiteral("pop"), QStringLiteral("stash@{1}")}));
        CHECK(trace.commands.at(trace.commands.size() - 2).operationId == 102);
        tree.reset();
        CHECK(tree.stashList().isEmpty());
    }
    {
        Repositories repositories;
        SynchronizationWorkflow sync(repositories);
        Trace trace;
        trace.attach(sync, repositories);
        sync.syncSelection({repositories.entries.first().path}, true, 110);
        CHECK(trace.commands.size() == 1);
        const QString upstream = QStringLiteral("*") + QChar(0) + QStringLiteral("actual-remote") + QChar(0) + QStringLiteral("refs/heads/actual-branch");
        sync.handleResult(reply(trace.commands.first(), upstream));
        CHECK(trace.commands.last().kind == GitCommandKind::PullExecution);
        CHECK(trace.commands.last().arguments.contains(QStringLiteral("--rebase")));
        CHECK(trace.commands.last().arguments.contains(QStringLiteral("actual-remote")));
        CHECK(!trace.commands.last().arguments.contains(QStringLiteral("review-target")));
        CHECK(trace.commands.last().operationId == 110);
        trace.commands.clear();
        sync.syncSelection({repositories.entries.first().path}, false, 111);
        sync.handleResult(reply(trace.commands.first()));
        CHECK(trace.commands.size() == 1 && trace.canceled.contains(111));
    }
    qInfo() << "PASS: seven workflows, repository isolation, operation chaining, stale results, safe discard, stash identity and HTTP Hook cancellation";
    return EXIT_SUCCESS;
}
