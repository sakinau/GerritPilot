#include "batchpushworkflow.h"
#include "gerritworkflowutils.h"
#include "gitqueries.h"
#include <QDir>

using namespace GerritWorkflow;

QVariantList BatchPushWorkflow::batchPushRows() const
{
    QVariantList rows;
    for (const BatchPushRow &row : m_batchRows) {
        // Explain where the target came from so a wrong branch is noticed before pushing.
        QString note;
        bool warning = false;
        if (row.target.isEmpty()) {
            note = row.manifestRevision.isEmpty()
                ? tr("未配置目标分支，请填写")
                : tr("manifest 中为 %1，不是分支，请填写目标分支").arg(row.manifestRevision);
            warning = true;
        } else if (row.target != row.savedTarget) {
            note = row.target == row.manifestTarget ? tr("来自 manifest，推送后保存") : tr("手动指定，推送后保存");
            warning = true;
        } else if (!row.manifestTarget.isEmpty() && row.manifestTarget != row.target) {
            note = tr("与 manifest 的 %1 不一致，请确认").arg(row.manifestTarget);
            warning = true;
        }
        rows.append(QVariantMap{
            {QStringLiteral("path"), row.path},
            {QStringLiteral("name"), row.name},
            {QStringLiteral("branch"), row.branch},
            {QStringLiteral("remote"), row.remote},
            {QStringLiteral("target"), row.target},
            {QStringLiteral("state"), row.state},
            {QStringLiteral("message"), row.message},
            {QStringLiteral("url"), row.url},
            {QStringLiteral("commits"), row.commits},
            {QStringLiteral("included"), row.included},
            {QStringLiteral("note"), note},
            {QStringLiteral("warning"), warning},
        });
    }
    return rows;
}

int BatchPushWorkflow::batchRowIndex(const QString &path) const
{
    for (int i = 0; i < m_batchRows.size(); ++i) {
        if (m_batchRows.at(i).path == path) return i;
    }
    return -1;
}

void BatchPushWorkflow::resetBatchPush()
{
    ++m_batchRequest;
    m_batchRows.clear();
    m_batchRunning = false;
    emit changed();
}

void BatchPushWorkflow::prepareBatchPush()
{
    if (m_batchRunning) return;
    if (m_repositories.busy()) {
        fail(tr("无法批量推送"), tr("请等待当前任务完成。"));
        return;
    }
    resetBatchPush();
    for (const auto &repository : m_repositories.checkedRepositoryContexts()) {
        const QString path = repository.path;
        if (!repository.valid()) continue;
        const auto config = repository.config;
        BatchPushRow row;
        row.path = path;
        row.name = repository.name;
        row.branch = repository.branch;
        row.remote = config.remote.trimmed().isEmpty() ? QStringLiteral("origin") : config.remote.trimmed();
        row.savedTarget = config.targetBranchConfirmed ? config.targetBranch : QString();
        row.state = QStringLiteral("checking");
        m_batchRows.append(row);
    }
    emit changed();
    if (m_batchRows.isEmpty()) return;
    if (!m_repositories.isRepoWorkspace()) {
        resolveBatchTargets({});
        return;
    }
    GitCommand command;
    command.program = QStringLiteral("repo");
    command.arguments = {QStringLiteral("manifest"), QStringLiteral("-o"), QStringLiteral("-")};
    command.workingDirectory = m_repositories.workspacePath();
    command.workspacePath = m_repositories.workspacePath();
    command.kind = GitCommandKind::BatchManifest;
    command.title = tr("读取 Manifest 中各仓库的分支");
    command.timeoutMilliseconds = 30000;
    command.target.batchPush = true;
    command.target.request = m_batchRequest;
    enqueue(command);
}

void BatchPushWorkflow::resolveBatchTargets(const QHash<QString, GerritService::ManifestTarget> &manifest)
{
    for (int i = 0; i < m_batchRows.size(); ++i) {
        BatchPushRow &row = m_batchRows[i];
        const auto entry = manifest.constFind(QDir::cleanPath(row.path));
        if (entry != manifest.constEnd()) {
            row.manifestTarget = entry->branch;
            row.manifestRevision = entry->revision;
        }
        if (!row.savedTarget.isEmpty()) {
            row.target = row.savedTarget;
        } else if (!row.manifestTarget.isEmpty()) {
            row.target = row.manifestTarget;
            // Repo names each project's Git remote after the manifest remote.
            if (entry != manifest.constEnd() && !entry->remote.isEmpty())
                row.remote = entry->remote;
        }
        enqueueBatchPreflight(i, false);
    }
    emit changed();
}

void BatchPushWorkflow::enqueueBatchPreflight(int rowIndex, bool execute)
{
    BatchPushRow &row = m_batchRows[rowIndex];
    const auto repository = repositoryForPath(row.path);
    const int index = repository.index;
    row.commits = 0;
    row.url.clear();
    // Problems found while executing count as failures in the batch summary.
    const QString invalid = execute ? QStringLiteral("failed") : QStringLiteral("error");
    if (index < 0) {
        row.state = invalid;
        row.message = tr("仓库已不在列表中。");
        return;
    }
    row.branch = repository.branch;
    if (isDetachedBranch(row.branch)) {
        row.state = invalid;
        row.message = tr("处于 detached HEAD，请先切换到承载本次提交的本地分支（如 repo start 创建的分支）。");
        return;
    }
    if (row.target.isEmpty()) {
        row.state = invalid;
        row.message = tr("请填写目标分支。");
        return;
    }
    QString branchError;
    if (!GitService::validateBranchName(row.target, &branchError)
        || !GitService::validateBranchName(row.remote, &branchError) || row.remote.contains(QLatin1Char('/'))) {
        row.state = invalid;
        row.message = tr("Remote 或目标分支无效。");
        return;
    }
    row.state = execute ? QStringLiteral("queued") : QStringLiteral("checking");
    row.message.clear();
    // Every repository is its own operation, so one failure cancels only that repository's push.
    GitCommand command;
    command.arguments = {QStringLiteral("log"), QStringLiteral("--format=%H%x00%B%x00"),
                         QStringLiteral("refs/remotes/%1/%2..HEAD").arg(row.remote, row.target),
                         QStringLiteral("--")};
    command.workingDirectory = repository.directory;
    command.repositoryIndex = index;
    command.repositoryPath = row.path;
    command.workspacePath = m_repositories.workspacePath();
    command.target.remote = row.remote;
    command.target.branch = row.target;
    command.target.batchPush = true;
    command.target.request = m_batchRequest;
    command.kind = execute ? GitCommandKind::PushChangeIdPreflight : GitCommandKind::BatchPreflight;
    command.title = tr("检查 %1 的待推送提交").arg(row.name);
    command.stopOnFailure = true;
    enqueue(command);
}

void BatchPushWorkflow::setBatchPushTarget(const QString &path, const QString &branch)
{
    const int rowIndex = batchRowIndex(path);
    if (m_batchRunning || rowIndex < 0) return;
    BatchPushRow &row = m_batchRows[rowIndex];
    const QString target = branch.trimmed();
    if (row.target == target && row.state != QStringLiteral("error")) return;
    row.target = target;
    enqueueBatchPreflight(rowIndex, false);
    emit changed();
}

void BatchPushWorkflow::setBatchPushIncluded(const QString &path, bool included)
{
    const int rowIndex = batchRowIndex(path);
    if (m_batchRunning || rowIndex < 0 || m_batchRows.at(rowIndex).included == included) return;
    m_batchRows[rowIndex].included = included;
    emit changed();
}

void BatchPushWorkflow::startBatchPush(const QVariantMap &options)
{
    const QString title = tr("无法批量推送");
    if (m_batchRunning) return;
    if (m_repositories.busy()) { fail(title, tr("请等待仓库检查完成。")); return; }
    QString optionsError;
    if (!GerritService::validatePushOptions(pushOptionsFromMap(options), &optionsError)) {
        fail(title, optionsError);
        return;
    }
    QList<int> ready;
    for (int i = 0; i < m_batchRows.size(); ++i) {
        if (m_batchRows.at(i).included && m_batchRows.at(i).state == QStringLiteral("ready"))
            ready.append(i);
    }
    if (ready.isEmpty()) {
        fail(title, tr("没有可推送的仓库。"));
        return;
    }
    m_batchPushOptions = pushOptionsFromMap(options);
    m_batchRunning = true;
    for (int i : ready) {
        // Pushing confirms the target, exactly like the single-repository push dialog.
        BatchPushRow &row = m_batchRows[i];
        auto config = WorkspaceSettings::loadRepositoryConfig(m_repositories.workspacePath(), row.path);
        config.remote = row.remote;
        config.targetBranch = row.target;
        config.targetBranchConfirmed = true;
        WorkspaceSettings::saveRepositoryConfig(m_repositories.workspacePath(), row.path, config);
        row.savedTarget = row.target;
        emit targetConfirmed(row.path, row.remote, row.target);
        enqueueBatchPreflight(i, true);
    }
    emit logMessage(tr("开始批量推送 %1 个仓库。").arg(ready.size()));
    finishBatchPushIfDone();
    emit changed();
}

void BatchPushWorkflow::handleBatchPushResult(const GitCommandResult &result)
{
    const GitCommand &completed = result.command;
    if (completed.generation != m_repositories.workspaceGeneration() || completed.target.request != m_batchRequest) return;
    const bool success = result.status == ExecutionStatus::Success;
    if (completed.kind == GitCommandKind::BatchManifest) {
        if (!success)
            emit logMessage(tr("读取 Manifest 失败，仅使用已保存的目标分支。\n") + result.diagnosticMessage);
        resolveBatchTargets(success ? GerritService::parseManifestTargets(result.standardOutput)
                                    : QHash<QString, GerritService::ManifestTarget>{});
        return;
    }
    const int rowIndex = batchRowIndex(completed.repositoryPath);
    if (rowIndex < 0) return;
    BatchPushRow &row = m_batchRows[rowIndex];
    // A target edited while the previous check was queued makes that check stale.
    if (completed.target.remote != row.remote || completed.target.branch != row.target) return;

    if (result.status == ExecutionStatus::Canceled) {
        row.state = QStringLiteral("canceled");
        row.message = completed.kind == GitCommandKind::GerritPush
            ? tr("已取消；推送结果未确认，请在 Gerrit 核对。") : tr("已取消。");
    } else if (completed.kind == GitCommandKind::BatchPreflight
               || completed.kind == GitCommandKind::PushChangeIdPreflight) {
        const bool execute = completed.kind == GitCommandKind::PushChangeIdPreflight;
        const QString failed = execute ? QStringLiteral("failed") : QStringLiteral("error");
        const PendingCommits pending = scanPendingCommits(QString::fromLocal8Bit(result.standardOutput));
        row.commits = pending.count;
        if (!success) {
            row.state = failed;
            row.message = tr("本地没有 %1/%2 的远端跟踪分支，请先拉取，或检查目标分支名。")
                .arg(row.remote, row.target);
        } else if (pending.count == 0) {
            row.state = QStringLiteral("nothing");
            row.message = tr("没有待推送的提交。");
        } else if (!pending.missingChangeId.isEmpty()) {
            row.state = failed;
            row.message = tr("%1 个提交缺少 Change-Id（%2），请在该仓库单独推送修复。")
                .arg(pending.missingChangeId.size()).arg(pending.missingChangeId.join(QStringLiteral(", ")));
        } else if (!execute) {
            row.state = QStringLiteral("ready");
            row.message = tr("待推送 %1 个提交").arg(pending.count);
        } else {
            row.state = QStringLiteral("pushing");
            row.message = tr("正在推送 %1 个提交…").arg(pending.count);
                    GitCommand push = m_gerrit.createPushCommand(completed.repositoryIndex, completed.workingDirectory,
                completed.repositoryPath, row.remote, row.target, m_batchPushOptions);
            push.workspacePath = completed.workspacePath;
            push.operationId = completed.operationId;
            push.target = completed.target;
            push.affectedRepositories.clear();
            enqueue(push);
        }
    } else if (completed.kind == GitCommandKind::GerritPush) {
        const QString url = success ? GerritService::extractReviewUrl(result.standardOutput, result.standardError)
                                    : QString();
        if (!success) {
            row.state = QStringLiteral("failed");
            row.message = GerritService::parseGerritError(result.standardError, result.diagnosticMessage);
        } else if (url.isEmpty()) {
            row.state = QStringLiteral("failed");
            row.message = tr("未收到 Gerrit 审查链接，结果未确认；请先核对 Gerrit，避免盲目重推。");
        } else {
            row.state = QStringLiteral("success");
            row.url = url;
            row.message = tr("推送成功");
            emit logMessage(tr("%1：%2").arg(row.name, url));
        }
        enqueue(commandFor(m_repositories.repositoryContext(completed.repositoryIndex),
                           GitQueries::summary(), tr("刷新仓库状态"), GitCommandKind::Status));
    }
    finishBatchPushIfDone();
    emit changed();
}

void BatchPushWorkflow::finishBatchPushIfDone()
{
    if (!m_batchRunning) return;
    int succeeded = 0;
    int failed = 0;
    for (const BatchPushRow &row : m_batchRows) {
        if (row.state == QStringLiteral("queued") || row.state == QStringLiteral("pushing")) return;
        if (row.state == QStringLiteral("success")) ++succeeded;
        else if (row.state == QStringLiteral("failed") || row.state == QStringLiteral("canceled")) ++failed;
    }
    m_batchRunning = false;
    const QString summary = tr("批量推送结束：成功 %1 个，失败或未确认 %2 个。").arg(succeeded).arg(failed);
    emit logMessage(summary);
    emit finished(summary);
}

WorkflowRepository BatchPushWorkflow::repositoryForPath(const QString &path) const
{
    return m_repositories.repositoryContextByPath(path);
}

void BatchPushWorkflow::cancel()
{
    if (!m_batchRunning) return;
    for (auto &row : m_batchRows) {
        if (row.state == QStringLiteral("queued") || row.state == QStringLiteral("pushing")) {
            row.state = QStringLiteral("canceled");
            row.message = tr("已取消；正在推送的仓库结果未确认，请在 Gerrit 核对。");
        }
    }
    finishBatchPushIfDone();
    emit changed();
}
