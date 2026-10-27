#include "reviewpushworkflow.h"
#include "gerritworkflowutils.h"
#include "gitqueries.h"
#include <QScopedValueRollback>
#include <QRegularExpression>

using namespace GerritWorkflow;

QString ReviewPushWorkflow::pushPreview() const
{
    const auto repository = m_repositories.activeRepositoryContext();
    GerritPushOptions options;
    options.topic = repository.config.topic;
    options.reviewers = GerritService::splitList(repository.config.reviewers);
    options.cc = GerritService::splitList(repository.config.cc);
    return GerritService::pushPreview(repository.config.remote, repository.config.targetBranch, options);
}

QString ReviewPushWorkflow::pushCommandPreview(const QString &remote, const QString &branch,
                                                const QVariantMap &options) const
{
    if (branch.trimmed().isEmpty()) return tr("未指定目标分支");
    const QString error = pushOptionsError(options);
    if (!error.isEmpty()) return error;
    return GerritService::pushPreview(remote.trimmed().isEmpty() ? QStringLiteral("origin") : remote.trimmed(),
                                      branch, pushOptionsFromMap(options));
}

QString ReviewPushWorkflow::pushOptionsError(const QVariantMap &options) const
{
    QString error;
    GerritService::validatePushOptions(pushOptionsFromMap(options), &error);
    return error;
}

void ReviewPushWorkflow::pushActive(const QVariantMap &extraOptions, quint64 operationId)
{
    const auto repository = m_repositories.activeRepositoryContext();
    if (m_repositories.busy()) { fail(tr("无法上传"), tr("请等待当前任务完成。")); return; }
    const QScopedValueRollback<quint64> operation(m_operation, operationId);
    if (repository.index < 0) return;
    if (repository.branch == QStringLiteral("(detached)") || repository.branch == QStringLiteral("detached")) {
        fail(tr("无法上传"), tr("当前处于 detached HEAD。请先切换到承载本次提交的本地分支。"));
        return;
    }
    if (repository.config.targetBranch.isEmpty() || repository.config.remote.isEmpty()) {
        fail(tr("无法上传"), tr("当前仓库未配置 Remote 或 Gerrit 目标分支。"));
        return;
    }
    const QRegularExpression branchPattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._/-]*$"));
    if (!branchPattern.match(repository.config.targetBranch).hasMatch() || repository.config.targetBranch.contains(QStringLiteral(".."))) {
        fail(tr("目标分支无效"),
                             tr("目标分支包含 Gerrit 不接受的字符。"));
        return;
    }
    // Topic, reviewers and CC are persisted per repository; the rest applies to this push only.
    QVariantMap optionMap = extraOptions;
    optionMap.insert(QStringLiteral("topic"), repository.config.topic);
    optionMap.insert(QStringLiteral("reviewers"), repository.config.reviewers);
    optionMap.insert(QStringLiteral("cc"), repository.config.cc);
    const GerritPushOptions pushOptions = pushOptionsFromMap(optionMap);
    QString optionsError;
    if (!GerritService::validatePushOptions(pushOptions, &optionsError)) {
        fail(tr("推送选项无效"), optionsError);
        return;
    }
    GitCommand command;
    command.program = QStringLiteral("git");
    const QString targetRef = QStringLiteral("refs/remotes/%1/%2")
        .arg(repository.config.remote, repository.config.targetBranch);
    command.arguments = {QStringLiteral("log"), QStringLiteral("--format=%H%x00%B%x00"),
                         targetRef + QStringLiteral("..HEAD")};
    command.workingDirectory = repository.directory;
    command.repositoryIndex = repository.index;
    command.repositoryPath = repository.path;
    command.workspacePath = repository.workspace;
    command.target.remote = repository.config.remote;
    command.target.branch = repository.config.targetBranch;
    command.target.options = pushOptions;
    command.kind = GitCommandKind::PushChangeIdPreflight;
    command.title = tr("检查待推送提交的 Change-Id");
    command.stopOnFailure = true;
    m_pushOperationId = m_operation;
    m_pushRepositoryKey = repository.workspace + QLatin1Char('/') + repository.path;
    setLastGerritReviewUrl(QString());
    setPushStatus(1, tr("正在检查提交并准备推送 %1 → %2…").arg(repository.name, repository.config.targetBranch));
    enqueue(command);
}

void ReviewPushWorkflow::repairLastCommitChangeId(quint64 operationId)
{
    const auto repository = m_repositories.activeRepositoryContext();
    if (repository.index < 0 || m_repositories.busy()) return;
    if (m_repairChangeIdRevision.isEmpty()) {
        fail(tr("无法修复 Change-Id"), tr("待修复提交已变化，请重新发起推送检查。"));
        return;
    }
    if (repository.branch == QStringLiteral("(detached)") || repository.branch == QStringLiteral("detached")) {
        fail(tr("无法修复 Change-Id"), tr("请先切换到包含该提交的本地分支。"));
        return;
    }
    if (repository.hasPendingOperation) {
        fail(tr("无法修复 Change-Id"), tr("请先完成或终止当前 Git 操作。"));
        return;
    }
    const QScopedValueRollback<quint64> operation(m_operation, operationId);
    enqueue(m_gerrit.createQueryHookPathCommand(repository.index, repository.directory,
                                               repository.path, GitCommandKind::HookRepairPreflight));
    GitCommand command;
    command.program = QStringLiteral("git");
    command.arguments = GitQueries::files();
    command.workingDirectory = repository.directory;
    command.repositoryIndex = repository.index;
    command.repositoryPath = repository.path;
    command.kind = GitCommandKind::ChangeIdRepairPreflight;
    command.revision = m_repairChangeIdRevision;
    command.title = tr("检查修订提交前的工作区");
    command.stopOnFailure = true;
    enqueue(command);
}

void ReviewPushWorkflow::setLastGerritReviewUrl(const QString &url)
{
    if (m_lastGerritReviewUrl == url) return;
    m_lastGerritReviewUrl = url;
    emit lastGerritReviewUrlChanged();
}

void ReviewPushWorkflow::setPushStatus(int state, const QString &text)
{
    m_pushState = state;
    m_pushStatusText = text;
    emit pushStatusChanged();
}

void ReviewPushWorkflow::reset()
{
    m_pushOperationId = 0;
    m_pushRepositoryKey.clear();
    m_repairChangeIdRevision.clear();
    setPushStatus(0, {});
    setLastGerritReviewUrl({});
}

void ReviewPushWorkflow::cancel()
{
    m_repairChangeIdRevision.clear();
    if (m_pushState != 1) return;
    m_pushOperationId = 0;
    setPushStatus(3, tr("推送已取消，结果未确认。"));
}

void ReviewPushWorkflow::enqueueGit(int index, const QStringList &arguments, const QString &title,
                                    GitCommandKind kind, bool stopOnFailure)
{
    auto command = commandFor(m_repositories.repositoryContext(index), arguments, title, kind, m_operation);
    command.stopOnFailure = stopOnFailure;
    enqueue(command);
}

bool ReviewPushWorkflow::handleResult(const GitCommandResult &result)
{
    const auto &completed = result.command;
    switch (completed.kind) {
    case GitCommandKind::PushChangeIdPreflight:
    case GitCommandKind::GerritPush:
    case GitCommandKind::ChangeIdRepairPreflight:
    case GitCommandKind::ChangeIdRepairRevision:
    case GitCommandKind::ChangeIdRepairVerification: break;
    default: return false;
    }
    if (completed.generation != m_repositories.workspaceGeneration()) return true;
    if (result.status == ExecutionStatus::Canceled) {
        if (completed.operationId == m_pushOperationId) cancel();
        return true;
    }
    const auto repository = m_repositories.activeRepositoryContext();
    const QString output = QString::fromLocal8Bit(result.standardOutput);
    const QScopedValueRollback<quint64> operation(m_operation, completed.operationId);
    if (result.status != ExecutionStatus::Success) {
        QString message = result.diagnosticMessage;
        if (completed.kind == GitCommandKind::GerritPush)
            message = GerritService::parseGerritError(result.standardError, message);
        else if (completed.kind == GitCommandKind::PushChangeIdPreflight)
            message = tr("无法检查目标分支到 HEAD 的待推送提交。请先获取目标分支的远端跟踪引用，再重试推送。\n") + message;
        if (completed.operationId == m_pushOperationId) {
            m_pushOperationId = 0;
            setPushStatus(3, tr("推送失败，请查看错误信息和命令日志。"));
        }
        fail(completed.title, message, WorkflowErrorType::General, {}, &completed);
        if (!completed.affectedRepositories.isEmpty()) emit refreshRequested(completed, true);
        return true;
    }
    switch (completed.kind) {
    case GitCommandKind::PushChangeIdPreflight: {
        if (completed.repositoryIndex != repository.index
            || completed.repositoryPath != repository.path
            || completed.workspacePath != repository.workspace) {
            m_pushOperationId = 0;
            setPushStatus(3, tr("仓库已变化，推送已取消。"));
            return true;
        }
        static const QRegularExpression changeIdLine(
            QStringLiteral("(?m)^Change-Id:[ \\t]*I[0-9a-fA-F]{40}[ \\t]*$"));
        static const QRegularExpression commitHash(QStringLiteral("^[0-9a-fA-F]{40,64}$"));
        QStringList missing;
        QStringList missingFull;
        const QStringList records = output.split(QChar(0));
        int checkedCommits = 0;
        for (int i = 0; i + 1 < records.size(); i += 2) {
            const QString sha = records.at(i).trimmed();
            if (!commitHash.match(sha).hasMatch()) continue;
            ++checkedCommits;
            if (!changeIdLine.match(records.at(i + 1)).hasMatch()) {
                missing.append(sha.left(10));
                missingFull.append(sha);
            }
        }
        if (checkedCommits == 0) {
            m_pushOperationId = 0;
            setPushStatus(3, tr("没有可确认的待推送提交。"));
            fail(tr("无法推送"), output.trimmed().isEmpty()
                ? tr("当前 HEAD 没有领先目标远端分支的提交。")
                : tr("待推送提交的检查结果无法解析；为避免绕过 Change-Id 检查，已取消推送。"));
            return true;
        }
        if (!missing.isEmpty()) {
            m_pushOperationId = 0;
            setPushStatus(3, tr("提交缺少 Change-Id，未执行推送。"));
            const bool headOnly = missingFull.size() == 1
                && !records.isEmpty() && records.first().trimmed() == missingFull.first();
            m_repairChangeIdRevision = headOnly ? missingFull.first() : QString();
            fail(headOnly ? tr("最近提交缺少 Change-Id") : tr("待推送提交缺少 Change-Id"),
                (m_hooks.hasHook()
                    ? tr("待推送提交缺少有效 Change-Id：%1。修订对应提交会改变提交 SHA。")
                    : tr("待推送提交缺少有效 Change-Id：%1。请先安装 Hook；修订对应提交会改变提交 SHA。"))
                    .arg(missing.join(QStringLiteral(", "))), WorkflowErrorType::ChangeId,
                !m_hooks.hasHook() ? QStringList{QStringLiteral("installHook")}
                    : headOnly ? QStringList{QStringLiteral("repairChangeId")} : QStringList{}, &completed);
            return true;
        }
        m_repairChangeIdRevision.clear();
        const QScopedValueRollback<quint64> operation(m_operation, completed.operationId);
        GitCommand push = m_gerrit.createPushCommand(completed.repositoryIndex,
            completed.workingDirectory, completed.repositoryPath,
            completed.target.remote, completed.target.branch, completed.target.options);
        push.workspacePath = completed.workspacePath;
        setPushStatus(1, tr("正在推送 %1 → %2…").arg(repository.name, completed.target.branch));
        enqueue(push);
        enqueueGit(completed.repositoryIndex, GitQueries::summary(),
                   tr("刷新仓库状态"), GitCommandKind::Status);
        return true;
    }
    case GitCommandKind::ChangeIdRepairPreflight: {
        if (completed.repositoryIndex != repository.index || completed.repositoryPath != repository.path) {
            fail(tr("无法修复 Change-Id"), tr("当前仓库已变化，请重新检查。"));
            return true;
        }
        if (!output.isEmpty()) {
            fail(tr("无法修复 Change-Id"), tr("工作区或暂存区有改动；为避免把额外文件带入修订提交，请先处理这些改动。"));
            return true;
        }
        const QScopedValueRollback<quint64> operation(m_operation, completed.operationId);
        GitCommand revision = completed;
        revision.arguments = {QStringLiteral("rev-parse"), QStringLiteral("HEAD")};
        revision.kind = GitCommandKind::ChangeIdRepairRevision;
        revision.title = tr("核对待修订提交");
        enqueue(revision);
        return true;
    }
    case GitCommandKind::ChangeIdRepairRevision: {
        if (completed.repositoryIndex != repository.index || completed.repositoryPath != repository.path
            || completed.revision != m_repairChangeIdRevision
            || output.trimmed() != m_repairChangeIdRevision) {
            fail(tr("无法修复 Change-Id"), tr("HEAD 已变化；请重新检查待推送提交。"));
            return true;
        }
        m_repairChangeIdRevision.clear();
        const QScopedValueRollback<quint64> operation(m_operation, completed.operationId);
        enqueueGit(completed.repositoryIndex,
                   {QStringLiteral("commit"), QStringLiteral("--amend"), QStringLiteral("--no-edit")},
                   tr("修复最近提交的 Change-Id"), GitCommandKind::General, true);
        enqueueGit(completed.repositoryIndex,
                   {QStringLiteral("log"), QStringLiteral("-1"), QStringLiteral("--format=%B")},
                   tr("验证修订后的 Change-Id"), GitCommandKind::ChangeIdRepairVerification, true);
        requestRefresh(completed.repositoryIndex, true);
        return true;
    }
    case GitCommandKind::ChangeIdRepairVerification: {
        static const QRegularExpression changeIdLine(
            QStringLiteral("(?m)^Change-Id:[ \\t]*I[0-9a-fA-F]{40}[ \\t]*$"));
        if (!changeIdLine.match(output).hasMatch()) {
            fail(tr("Change-Id 修复未生效"),
                tr("Amend 已执行，但最近提交仍无有效 Change-Id。请检查当前仓库的 commit-msg Hook；未执行推送。"));
        } else {
            emit logMessage(tr("最近提交已补齐 Change-Id；请核对提交后再推送。") + QLatin1Char('\n'));
        }
        return true;
    }
    case GitCommandKind::GerritPush: {
        const QString url = GerritService::extractReviewUrl(result.standardOutput, result.standardError);
        if (url.isEmpty()) {
            if (completed.operationId == m_pushOperationId) {
                m_pushOperationId = 0;
                setPushStatus(3, tr("未收到 Gerrit 回传；推送未确认，按失败处理。"));
            }
            fail(completed.title, tr("Git 命令已结束，但未收到 Gerrit 审查链接；请先核对 Gerrit，避免盲目重推。"));
            return true;
        }
        if (completed.workspacePath == m_repositories.workspacePath()) setLastGerritReviewUrl(url);
        emit logMessage(tr("Gerrit 审查链接已生成：%1").arg(url));
        if (completed.operationId == m_pushOperationId) {
            m_pushOperationId = 0;
            setPushStatus(2, tr("推送成功，已收到 Gerrit 审查链接。"));
        }
        emit finished(completed.title);
        break;
    }
    default: break;
    }
    return true;
}
