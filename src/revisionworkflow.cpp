#include "revisionworkflow.h"
#include "gerritworkflowutils.h"
#include "gitqueries.h"
#include <QScopedValueRollback>
#include <QRegularExpression>

using namespace GerritWorkflow;

void RevisionWorkflow::fetchChange(const QString &spec, int mode, quint64 operationId)
{
    const QScopedValueRollback<quint64> operation(m_operation, operationId);
    const auto repository = m_repositories.activeRepositoryContext();
    if (repository.index < 0 || mode < 0 || mode > 2) return;
    const QString title = tr("无法下载 Change");
    if (m_repositories.busy()) { fail(title, tr("请等待当前任务完成。")); return; }
    int change = 0;
    int patchset = 0;
    QString error;
    if (!GerritService::parseChangeSpec(spec, &change, &patchset, &error)) {
        fail(title, error);
        return;
    }
    if (repository.hasPendingOperation) {
        fail(title, tr("请先完成或终止当前未完成的 Git 操作。"));
        return;
    }
    if (mode == 1 && isDetachedBranch(repository.branch)) {
        fail(title, tr("当前处于 detached HEAD；请先切换到需要接收该 Change 的本地分支。"));
        return;
    }
    const QString remote = repository.config.remote.trimmed();
    if (remote.isEmpty() || remote.startsWith(QLatin1Char('-'))) {
        fail(title, tr("请先在推送面板为当前仓库配置 Gerrit Remote。"));
        return;
    }
    GitCommand command;
    command.workingDirectory = repository.directory;
    command.repositoryIndex = repository.index;
    command.repositoryPath = repository.path;
    command.target.remote = remote;
    command.change.number = change;
    command.change.patchset = patchset;
    command.change.mode = mode;
    command.stopOnFailure = true;
    if (patchset > 0) {
        enqueueChangeFetch(command);
        return;
    }
    command.arguments = {QStringLiteral("ls-remote"), remote, GerritService::changeRef(change, 0)};
    command.kind = GitCommandKind::ChangeLookup;
    command.title = tr("查询 Change %1 的最新补丁集").arg(change);
    enqueue(command);
}

void RevisionWorkflow::enqueueChangeFetch(GitCommand command)
{
    const GitCommand::ChangeRequest change = command.change;
    command.arguments = {QStringLiteral("fetch"), QStringLiteral("--no-tags"), command.target.remote,
                         GerritService::changeRef(change.number, change.patchset)};
    command.kind = GitCommandKind::ChangeFetch;
    command.title = tr("下载 Change %1 补丁集 %2").arg(change.number).arg(change.patchset);
    enqueue(command);

    // FETCH_HEAD is consumed by the next command of the same operation; a failed fetch cancels it.
    QStringList arguments;
    QString title;
    if (change.mode == 0) {
        const QString branch = QStringLiteral("review/%1-%2").arg(change.number).arg(change.patchset);
        arguments = {QStringLiteral("checkout"), QStringLiteral("-b"), branch, QStringLiteral("FETCH_HEAD")};
        title = tr("检出到新分支 %1").arg(branch);
    } else if (change.mode == 1) {
        arguments = {QStringLiteral("cherry-pick"), QStringLiteral("FETCH_HEAD")};
        title = tr("摘取 Change %1/%2 到当前分支").arg(change.number).arg(change.patchset);
    } else {
        arguments = {QStringLiteral("checkout"), QStringLiteral("--detach"), QStringLiteral("FETCH_HEAD")};
        title = tr("以 detached HEAD 检出 Change %1/%2").arg(change.number).arg(change.patchset);
    }
    enqueueGit(command.repositoryIndex, arguments, title, GitCommandKind::General, true);
    requestRefresh(command.repositoryIndex, true);
}

void RevisionWorkflow::requestCommitMessage(const QString &revision)
{
    const auto repository = m_repositories.activeRepositoryContext();
    static const QRegularExpression hashPattern(QStringLiteral("^[0-9a-fA-F]{7,64}$"));
    if (repository.index < 0 || !hashPattern.match(revision.trimmed()).hasMatch()) return;
    GitCommand command;
    command.arguments = {QStringLiteral("log"), QStringLiteral("-1"), QStringLiteral("--format=%B"),
                         revision.trimmed(), QStringLiteral("--")};
    command.workingDirectory = repository.directory;
    command.repositoryIndex = repository.index;
    command.repositoryPath = repository.path;
    command.revision = revision.trimmed();
    command.kind = GitCommandKind::CommitMessage;
    command.title = tr("读取提交说明");
    enqueue(command);
}

void RevisionWorkflow::rewriteCommit(const QString &revision, int action, const QString &message, quint64 operationId)
{
    const QScopedValueRollback<quint64> operation(m_operation, operationId);
    const auto repository = m_repositories.activeRepositoryContext();
    if (repository.index < 0 || action < 0 || action > 2) return;
    const QString title = tr("无法改写提交");
    if (m_repositories.busy()) { fail(title, tr("请等待当前任务完成。")); return; }
    static const QRegularExpression hashPattern(QStringLiteral("^[0-9a-fA-F]{7,64}$"));
    const QString hash = revision.trimmed().toLower();
    if (!hashPattern.match(hash).hasMatch()) {
        fail(title, tr("提交 ID 无效。"));
        return;
    }
    if (isDetachedBranch(repository.branch)) {
        fail(title, tr("当前处于 detached HEAD；请先切换到承载该提交链的本地分支。"));
        return;
    }
    if (repository.hasPendingOperation) {
        fail(title, tr("请先完成或终止当前未完成的 Git 操作。"));
        return;
    }
    if (action == 1) {
        if (!repository.hasStaged || repository.detailsCached) {
            fail(title, tr("没有已暂存的改动。请先暂存要并入该提交的修改。"));
            return;
        }
    }
    if (action == 2 && message.trimmed().isEmpty()) {
        fail(title, tr("提交说明不能为空。"));
        return;
    }
    // Only commits that are not yet in the Gerrit target branch may be rewritten.
    const QString remote = repository.config.remote.trimmed();
    const QString target = repository.config.targetBranch.trimmed();
    const QString base = !remote.isEmpty() && !target.isEmpty()
        ? QStringLiteral("refs/remotes/%1/%2").arg(remote, target) : QStringLiteral("@{upstream}");
    GitCommand command;
    command.arguments = {QStringLiteral("log"), QStringLiteral("--format=%H%x00%P%x00%B%x00"),
                         base + QStringLiteral("..HEAD"), QStringLiteral("--")};
    command.workingDirectory = repository.directory;
    command.repositoryIndex = repository.index;
    command.repositoryPath = repository.path;
    command.revision = hash;
    command.rewrite.action = action;
    command.rewrite.message = message.trimmed();
    command.target.remote = remote;
    command.target.branch = target;
    command.kind = GitCommandKind::RewritePreflight;
    command.title = tr("检查待改写的提交链");
    command.stopOnFailure = true;
    enqueue(command);
}

void RevisionWorkflow::enqueueRewrite(const GitCommand &completed, const QString &output)
{
    const QString title = tr("无法改写提交");
    struct Entry { QString sha; QStringList parents; QString message; };
    QList<Entry> entries;
    const QStringList fields = output.split(QChar(0));
    for (int i = 0; i + 2 < fields.size(); i += 3) {
        const QString sha = fields.at(i).trimmed();
        if (sha.isEmpty()) break;
        entries.append({sha, fields.at(i + 1).split(QLatin1Char(' '), Qt::SkipEmptyParts), fields.at(i + 2)});
    }
    int position = -1;
    for (int i = 0; i < entries.size(); ++i) {
        if (entries.at(i).sha.startsWith(completed.revision)) { position = i; break; }
    }
    if (position < 0) {
        fail(title, tr("提交 %1 已在目标分支中或不在当前分支上。已合入 Gerrit 的提交不能改写，请改为提交新的修复。")
            .arg(completed.revision.left(10)), WorkflowErrorType::General, {}, &completed);
        return;
    }
    for (int i = 0; i <= position; ++i) {
        if (entries.at(i).parents.size() > 1) {
            fail(title, tr("提交链中包含合并提交，无法自动改写；请在终端处理。"),
                        WorkflowErrorType::General, {}, &completed);
            return;
        }
    }
    const Entry &entry = entries.at(position);
    const QString onto = entry.parents.isEmpty() ? QStringLiteral("--root") : entry.parents.first();
    const int index = completed.repositoryIndex;
    const QScopedValueRollback<quint64> operation(m_operation, completed.operationId);

    if (completed.rewrite.action == 1) {
        enqueueGit(index, GitQueries::commitScope(), tr("检查提交仓库边界"),
                   GitCommandKind::CommitScopePreflight, true);
        enqueueGit(index, {QStringLiteral("commit"), QStringLiteral("--fixup=") + entry.sha},
                   tr("创建待并入 %1 的修正提交").arg(entry.sha.left(10)), GitCommandKind::General, true);
        enqueueGit(index, {QStringLiteral("-c"), QStringLiteral("sequence.editor=:"),
                           QStringLiteral("rebase"), QStringLiteral("-i"), QStringLiteral("--autosquash"),
                           QStringLiteral("--autostash"), onto},
                   tr("将暂存改动并入提交 %1").arg(entry.sha.left(10)), GitCommandKind::General, true);
        requestRefresh(index, true);
        return;
    }

    // Mark only the selected commit as "edit"; the hash is validated hex, so the shell script is fixed text.
    const QString editScript = QStringLiteral("sequence.editor=sed -i -E -e 's/^(pick|p) (%1[0-9a-f]*)( |$)/edit \\2\\3/'")
        .arg(entry.sha.left(7));
    GitCommand stop = completed;
    stop.arguments = {QStringLiteral("-c"), editScript, QStringLiteral("rebase"), QStringLiteral("-i"),
                      QStringLiteral("--autostash"), onto};
    stop.kind = GitCommandKind::RewriteStop;
    stop.title = tr("停在提交 %1").arg(entry.sha.left(10));
    stop.affectedRepositories = {completed.repositoryPath};
    enqueue(stop);
    if (completed.rewrite.action == 2) {
        // Keep the Change-Id so Gerrit updates the existing change instead of creating a new one.
        static const QRegularExpression changeIdLine(QStringLiteral("(?m)^Change-Id:[ \\t]*I[0-9a-fA-F]{40}[ \\t]*$"));
        QString message = completed.rewrite.message;
        const auto original = changeIdLine.match(entry.message);
        if (original.hasMatch() && !changeIdLine.match(message).hasMatch())
            message += QStringLiteral("\n\n") + original.captured(0).trimmed();
        GitCommand amend;
        amend.arguments = {QStringLiteral("commit"), QStringLiteral("--amend"), QStringLiteral("--allow-empty"),
                           QStringLiteral("-F"), QStringLiteral("-")};
        amend.standardInput = (message + QLatin1Char('\n')).toUtf8();
        amend.workingDirectory = completed.workingDirectory;
        amend.repositoryIndex = index;
        amend.repositoryPath = completed.repositoryPath;
        amend.kind = GitCommandKind::General;
        amend.affectedRepositories = {completed.repositoryPath};
        amend.title = tr("改写提交说明");
        amend.stopOnFailure = true;
        enqueue(amend);
        enqueueGit(index, {QStringLiteral("rebase"), QStringLiteral("--continue")},
                   tr("继续 Rebase"), GitCommandKind::General, true);
    }
    requestRefresh(index, true);
}

void RevisionWorkflow::enqueueGit(int index, const QStringList &arguments, const QString &title,
                                  GitCommandKind kind, bool stopOnFailure)
{
    auto command = commandFor(m_repositories.repositoryContext(index), arguments, title, kind, m_operation);
    command.stopOnFailure = stopOnFailure;
    enqueue(command);
}

bool RevisionWorkflow::handleResult(const GitCommandResult &result)
{
    const auto &completed = result.command;
    switch (completed.kind) {
    case GitCommandKind::CommitMessage:
    case GitCommandKind::ChangeLookup:
    case GitCommandKind::ChangeFetch:
    case GitCommandKind::RewritePreflight:
    case GitCommandKind::RewriteStop:
    case GitCommandKind::RevertPreflight: break;
    default: return false;
    }
    if (!isActive(completed) || result.status == ExecutionStatus::Canceled) return true;
    const QScopedValueRollback<quint64> operation(m_operation, completed.operationId);
    const auto repository = m_repositories.activeRepositoryContext();
    const QString output = QString::fromLocal8Bit(result.standardOutput);
    if (result.status != ExecutionStatus::Success) {
        QString message = result.diagnosticMessage;
        if (completed.kind == GitCommandKind::RewritePreflight) {
            message = tr("无法确定提交链的基准分支。请先在推送面板选择 Gerrit 目标分支，或为当前分支设置拉取上游。\n") + message;
        } else if (completed.kind == GitCommandKind::ChangeLookup || completed.kind == GitCommandKind::ChangeFetch) {
            const auto hint = GerritService::credentialHint(result.standardError);
            if (!hint.isEmpty()) message = hint;
            else if (result.standardError.toLower().contains("couldn't find remote ref"))
                message = tr("远端不存在 Change %1 的补丁集 %2，或该 Change 不属于当前仓库。")
                    .arg(completed.change.number).arg(completed.change.patchset);
        }
        if (message != result.diagnosticMessage && !result.diagnosticMessage.isEmpty())
            message += tr("\n\n原始错误：\n") + result.diagnosticMessage;
        const bool conflict = repository.hasPendingOperation;
        fail(completed.title, message, conflict ? WorkflowErrorType::Conflict : WorkflowErrorType::General,
             conflict ? QStringList{QStringLiteral("conflicts")} : QStringList{}, &completed);
        if (!completed.affectedRepositories.isEmpty()) emit refreshRequested(completed, true);
        return true;
    }
    switch (completed.kind) {
    case GitCommandKind::CommitMessage: {
        QString message = output;
        while (message.endsWith(QLatin1Char('\n'))) message.chop(1);
        emit commitMessageReady(completed.revision, message);
        break;
    }
    case GitCommandKind::ChangeLookup: {
        const int latest = GerritService::latestPatchset(output, completed.change.number);
        if (latest <= 0) {
            fail(tr("无法下载 Change"), tr("远端 %1 上找不到 Change %2。请确认 Change 号、访问权限及当前仓库。")
                .arg(completed.target.remote).arg(completed.change.number), WorkflowErrorType::General, {}, &completed);
            break;
        }
        auto fetch = completed;
        fetch.change.patchset = latest;
        enqueueChangeFetch(fetch);
        break;
    }
    case GitCommandKind::RewritePreflight:
        enqueueRewrite(completed, output);
        break;
    case GitCommandKind::RevertPreflight: {
        if (completed.repositoryIndex != repository.index || completed.repositoryPath != repository.path) {
            fail(tr("无法撤销提交"), tr("当前仓库已变化，请重新选择提交。"));
            return true;
        }
        if (!output.isEmpty()) {
            fail(tr("无法撤销提交"), tr("工作区或暂存区有未提交改动，请先提交或暂存，再撤销该提交。"));
            return true;
        }
        const QScopedValueRollback<quint64> operation(m_operation, completed.operationId);
        enqueueGit(completed.repositoryIndex,
                   {QStringLiteral("revert"), QStringLiteral("--no-edit"), completed.revision},
                   tr("撤销提交 %1").arg(completed.revision.left(10)), GitCommandKind::General, true);
        requestRefresh(completed.repositoryIndex, true);
        break;
    }
    case GitCommandKind::RewriteStop:
        if (completed.rewrite.action == 0)
            emit logMessage(tr("已停在提交 %1：修改文件并暂存后，在提交面板勾选“修订”提交，再点击顶部“继续”。\n")
                .arg(completed.revision.left(10)));
        emit finished(completed.title);
        break;
    default: emit finished(completed.title); break;
    }
    return true;
}

void RevisionWorkflow::checkoutBranch(const QString &branch, quint64 operationId)
{
    const auto repository = m_repositories.activeRepositoryContext();
    const QScopedValueRollback<quint64> operation(m_operation, operationId);
    if (repository.index < 0 || branch.trimmed() == repository.branch)
        return;
    QString error;
    if (!GitService::validateBranchName(branch, &error)) {
        fail(tr("分支名无效"), error);
        return;
    }
    enqueueGit(repository.index,
               {QStringLiteral("checkout"), branch.trimmed()},
               tr("切换到 %1").arg(branch.trimmed()), GitCommandKind::General, true);
    requestRefresh(repository.index, true);
}

void RevisionWorkflow::checkoutRevision(const QString &revision, quint64 operationId)
{
    const auto repository = m_repositories.activeRepositoryContext();
    const QScopedValueRollback<quint64> operation(m_operation, operationId);
    const QString hash = revision.trimmed();
    const QRegularExpression hashPattern(QStringLiteral("^[0-9a-fA-F]{7,64}$"));
    if (repository.index < 0 || !hashPattern.match(hash).hasMatch())
        return;

    enqueueGit(repository.index,
               {QStringLiteral("checkout"), QStringLiteral("--detach"), hash},
               tr("检出提交 %1").arg(hash), GitCommandKind::General, true);
    requestRefresh(repository.index, true);
}

void RevisionWorkflow::createBranch(const QString &branch, quint64 operationId)
{
    const auto repository = m_repositories.activeRepositoryContext();
    const QScopedValueRollback<quint64> operation(m_operation, operationId);
    if (repository.index < 0) return;
    if (m_repositories.busy() || repository.hasPendingOperation) {
        fail(tr("无法创建分支"), tr("请先完成当前任务或 Git 操作。"));
        return;
    }
    QString error;
    const QString name = branch.trimmed();
    if (!GitService::validateBranchName(name, &error)) {
        fail(tr("分支名无效"), error);
        return;
    }
    if (repository.branches.contains(name)) {
        fail(tr("分支已存在"), tr("请选择其他名称或切换到已有分支。"));
        return;
    }
    enqueueGit(repository.index,
               {QStringLiteral("checkout"), QStringLiteral("-b"), name},
               tr("创建本地分支 %1").arg(name), GitCommandKind::General, true);
    requestRefresh(repository.index, true);
}

void RevisionWorkflow::deleteBranch(const QString &branch, bool force, quint64 operationId)
{
    const auto repository = m_repositories.activeRepositoryContext();
    const QScopedValueRollback<quint64> operation(m_operation, operationId);
    if (repository.index < 0)
        return;
    const QString trimmedBranch = branch.trimmed();
    if (trimmedBranch.isEmpty())
        return;
    if (trimmedBranch == repository.branch) {
        fail(tr("无法删除分支"), tr("不能删除当前正在检出的分支 %1。请先切换到其他分支。").arg(trimmedBranch));
        return;
    }
    enqueueGit(repository.index,
               {QStringLiteral("branch"), force ? QStringLiteral("-D") : QStringLiteral("-d"), trimmedBranch},
               tr("删除分支 %1").arg(trimmedBranch), GitCommandKind::General, true);
    requestRefresh(repository.index, true);
}

void RevisionWorkflow::cherryPick(const QString &revision, quint64 operationId)
{
    const auto repository = m_repositories.activeRepositoryContext();
    const QScopedValueRollback<quint64> operation(m_operation, operationId);
    if (repository.index < 0 || revision.trimmed().isEmpty())
        return;
    const QString trimmedRev = revision.trimmed();
    enqueueGit(repository.index,
               {QStringLiteral("cherry-pick"), trimmedRev},
               tr("Cherry-pick 提交 %1").arg(trimmedRev.left(7)), GitCommandKind::General, true);
    requestRefresh(repository.index, true);
}

void RevisionWorkflow::revertCommit(const QString &revision, quint64 operationId)
{
    const auto repository = m_repositories.activeRepositoryContext();
    const QScopedValueRollback<quint64> operation(m_operation, operationId);
    if (repository.index < 0 || m_repositories.busy()) return;
    const QString hash = revision.trimmed();
    static const QRegularExpression hashPattern(QStringLiteral("^[0-9a-fA-F]{7,64}$"));
    if (!hashPattern.match(hash).hasMatch()) {
        fail(tr("无法撤销提交"), tr("提交 ID 无效。"));
        return;
    }
    if (repository.branch == QStringLiteral("(detached)") || repository.branch == QStringLiteral("detached")) {
        fail(tr("无法撤销提交"), tr("当前仓库处于 detached HEAD；请先切换到需要接收反向提交的本地分支。"));
        return;
    }
    if (repository.hasPendingOperation) {
        fail(tr("无法撤销提交"), tr("请先完成或终止当前未完成的 Git 操作。"));
        return;
    }
    GitCommand command;
    command.program = QStringLiteral("git");
    command.arguments = GitQueries::files();
    command.workingDirectory = repository.directory;
    command.repositoryIndex = repository.index;
    command.repositoryPath = repository.path;
    command.kind = GitCommandKind::RevertPreflight;
    command.revision = hash;
    command.stopOnFailure = true;
    command.title = tr("检查撤销提交前的工作区");
    enqueue(command);
}

void RevisionWorkflow::pruneBranches(quint64 operationId)
{
    const auto repository = m_repositories.activeRepositoryContext();
    const QScopedValueRollback<quint64> operation(m_operation, operationId);
    if (m_repositories.busy()) return;
    if (repository.index < 0)
        return;
    if (m_repositories.isRepoWorkspace()) {
        GitCommand command;
        command.program = QStringLiteral("repo");
        command.arguments = {QStringLiteral("prune"), QStringLiteral(".")};
        command.workingDirectory = repository.directory;
        command.title = tr("清理已合入分支 (repo prune)");
        command.repositoryIndex = repository.index;
        command.repositoryPath = repository.path;
        command.stopOnFailure = false;
        enqueue(command);
    } else {
        const QString remoteName = repository.config.remote.trimmed().isEmpty() ? QStringLiteral("origin") : repository.config.remote.trimmed();
        enqueueGit(repository.index, {"remote", "prune", remoteName},
                   tr("清理失效远端分支 (remote prune)"), GitCommandKind::General, true);
    }
    requestRefresh(repository.index, true);
}
