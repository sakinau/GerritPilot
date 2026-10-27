#include "workingtreeworkflow.h"
#include "gitqueries.h"
#include "gitstatusparser.h"
#include "discardplan.h"
#include "repositorypaths.h"
#include <QFile>
#include <QScopedValueRollback>
#include <QSet>

using RepositoryPaths::gitMetadataDirectory;

void WorkingTreeWorkflow::applyHunk(const QString &path, const QString &diffText, const DiffIndex &index,
                                    int hunkIndex, HunkAction action, quint64 operationId)
{
    const auto repository = m_repositories.activeRepositoryContext();
    if (!repository.valid() || path.isEmpty() || !validateOwnFiles({path})
        || index.isBinary || hunkIndex < 0 || hunkIndex >= index.hunks.size()) return;
    if (index.hunks.size() <= 1) {
        if (action == HunkAction::Stage) stageFiles({path}, operationId);
        else if (action == HunkAction::Unstage) unstageFiles({path}, operationId);
        else discardFiles({path}, operationId);
        return;
    }
    const QScopedValueRollback<quint64> operation(m_operation, operationId);
    QStringList arguments{QStringLiteral("apply")};
    if (action != HunkAction::Discard) arguments.append(QStringLiteral("--cached"));
    if (action != HunkAction::Stage) arguments.append(QStringLiteral("--reverse"));
    arguments.append(QStringLiteral("-"));
    const QString verb = action == HunkAction::Stage ? tr("暂存")
        : action == HunkAction::Unstage ? tr("取消暂存") : tr("丢弃");
    auto command = commandFor(repository, arguments, tr("%1代码块 #%2").arg(verb).arg(hunkIndex + 1),
                              GitCommandKind::HunkOperation, operationId);
    command.affectedRepositories = {repository.path};
    command.standardInput = DiffModel::patch(diffText, index, hunkIndex).toUtf8();
    enqueue(command);
    requestRefresh(repository.index, false);
}

void WorkingTreeWorkflow::stageFiles(const QStringList &paths, quint64 operationId)
{
    const QScopedValueRollback<quint64> operation(m_operation, operationId);
    const auto repository = m_repositories.activeRepositoryContext();
    if (repository.index < 0 || paths.isEmpty())
        return;
    if (!validateOwnFiles(paths)) return;
    QStringList arguments{QStringLiteral("--literal-pathspecs"), QStringLiteral("add"), QStringLiteral("--")};
    arguments.append(paths);
    enqueueGit(repository.index, arguments,
               tr("暂存 %1 个文件").arg(paths.size()), GitCommandKind::General, true);
    requestRefresh(repository.index, false);
}

void WorkingTreeWorkflow::unstageFiles(const QStringList &paths, quint64 operationId)
{
    const QScopedValueRollback<quint64> operation(m_operation, operationId);
    const auto repository = m_repositories.activeRepositoryContext();
    if (repository.index < 0 || paths.isEmpty())
        return;
    if (!validateOwnFiles(paths)) return;
    QStringList arguments{QStringLiteral("--literal-pathspecs")};
    if (repository.unborn) {
        arguments.append({QStringLiteral("rm"), QStringLiteral("--cached"), QStringLiteral("-r"), QStringLiteral("-q"), QStringLiteral("--")});
    } else {
        arguments.append({QStringLiteral("restore"), QStringLiteral("--staged"), QStringLiteral("--")});
    }
    arguments.append(paths);
    enqueueGit(repository.index, arguments,
               tr("取消暂存 %1 个文件").arg(paths.size()), GitCommandKind::General, true);
    requestRefresh(repository.index, false);
}

void WorkingTreeWorkflow::discardFiles(const QStringList &paths, quint64 operationId)
{
    const QScopedValueRollback<quint64> operation(m_operation, operationId);
    const auto repository = m_repositories.activeRepositoryContext();
    if (repository.index < 0 || paths.isEmpty() || m_repositories.busy())
        return;
    if (!validateOwnFiles(paths)) return;
    GitCommand command;
    command.program = QStringLiteral("git");
    command.arguments = GitQueries::files();
    command.workingDirectory = repository.directory;
    command.repositoryIndex = repository.index;
    command.repositoryPath = repository.path;
    command.kind = GitCommandKind::DiscardPreflight;
    command.paths = paths;
    command.stopOnFailure = true;
    command.title = tr("检查待丢弃文件");
    enqueue(command);
}

void WorkingTreeWorkflow::executeDiscard(int repositoryIndex, const QStringList &paths, const QString &statusOutput)
{
    const auto plan = planDiscard(paths, m_repositories.ownRepositoryFiles(repositoryIndex, GitStatusParser::parse(statusOutput)));
    if (!plan.valid) {
        fail(tr("无法丢弃"), tr("文件状态已变化或包含冲突，请刷新后选择未暂存文件：%1").arg(plan.rejectedPath));
        return;
    }
    const auto &tracked = plan.tracked;
    const auto &untracked = plan.untracked;
    if (!tracked.isEmpty()) {
        QStringList arguments{QStringLiteral("--literal-pathspecs"), QStringLiteral("restore"),
                              QStringLiteral("--worktree"), QStringLiteral("--")};
        arguments.append(tracked);
        enqueueGit(repositoryIndex, arguments,
                   tr("丢弃 %1 个已跟踪文件的改动").arg(tracked.size()), GitCommandKind::General, true);
    }
    if (!untracked.isEmpty()) {
        QStringList arguments{QStringLiteral("--literal-pathspecs"), QStringLiteral("clean"), QStringLiteral("-f"), QStringLiteral("--")};
        arguments.append(untracked);
        enqueueGit(repositoryIndex, arguments,
                   tr("清理 %1 个未跟踪文件").arg(untracked.size()), GitCommandKind::General, true);
    }
    requestRefresh(repositoryIndex, false);
}

bool WorkingTreeWorkflow::validateOwnFiles(const QStringList &paths)
{
    const auto repository = m_repositories.activeRepositoryContext();
    const auto files = m_repositories.ownRepositoryFiles(repository.index, repository.files);
    for (const auto &path : paths) {
        bool found = false;
        for (const auto &file : files) {
            if (file.toMap().value(QStringLiteral("path")).toString() == path) { found = true; break; }
        }
        if (!found) {
            fail(tr("无法操作文件"), tr("路径不属于当前仓库的有效改动，请刷新或选择对应的嵌套仓库：%1").arg(path));
            return false;
        }
    }
    return true;
}

bool WorkingTreeWorkflow::prepareScopedStage(GitCommand &command)
{
    const auto repository = m_repositories.activeRepositoryContext();
    if (command.kind != GitCommandKind::StageAll || command.workingDirectory != repository.directory)
        return false;
    QStringList paths;
    for (const auto &file : m_repositories.ownRepositoryFiles(repository.index, repository.files)) {
        const auto entry = file.toMap();
        paths.append(entry.value(QStringLiteral("path")).toString());
        const auto original = entry.value(QStringLiteral("originalPath")).toString();
        if (!original.isEmpty()) paths.append(original);
    }
    paths.removeDuplicates();
    if (paths.isEmpty()) {
        fail(tr("无法暂存"), tr("当前仓库没有可暂存的文件。"));
        return false;
    }
    command.arguments = {QStringLiteral("--literal-pathspecs"), QStringLiteral("add"), QStringLiteral("-A"), QStringLiteral("--")};
    command.arguments.append(paths);
    return true;
}

QString WorkingTreeWorkflow::stashSha(int index) const
{
    if (index >= 0 && index < m_stashEntries.size()) {
        return m_stashEntries.at(index).sha;
    }
    return QString();
}

void WorkingTreeWorkflow::loadStashList()
{
    const auto repository = m_repositories.activeRepositoryContext();
    if (repository.index < 0) return;
    enqueueGit(repository.index, {QStringLiteral("stash"), QStringLiteral("list"), QStringLiteral("--format=%gd%x00%H%x00%gd: %gs")},
               tr("读取 Stash 列表"), GitCommandKind::StashList);
}

void WorkingTreeWorkflow::stashSave(const QString &message, bool includeUntracked, quint64 operationId)
{
    const QScopedValueRollback<quint64> operation(m_operation, operationId);
    const auto repository = m_repositories.activeRepositoryContext();
    if (repository.index < 0) return;
    QStringList args = {QStringLiteral("stash"), QStringLiteral("push")};
    if (includeUntracked)
        args << QStringLiteral("-u");
    if (!message.trimmed().isEmpty())
        args << QStringLiteral("-m") << message.trimmed();
    enqueueGit(repository.index, args, tr("保存 Stash"), GitCommandKind::StashAction, true);
    requestRefresh(repository.index, false);
    loadStashList();
}

void WorkingTreeWorkflow::executeStashAction(const QString &action, int index, const QString &expectedSha, const QString &title)
{
    const auto repository = m_repositories.activeRepositoryContext();
    if (repository.index < 0) return;

    QString targetSha = expectedSha.trimmed();
    if (targetSha.isEmpty() && index >= 0 && index < m_stashEntries.size()
        && m_stashRepositoryPath == repository.path) {
        targetSha = m_stashEntries.at(index).sha;
    }

    if (targetSha.isEmpty()) {
        const QString msg = tr("Stash 操作已拒绝：缺少确定的提交 SHA 标识。为防止外部变动导致误操作，已取消执行并重新加载列表。");
        emit logMessage(msg + QLatin1Char('\n'));
        fail(title, msg);
        loadStashList();
        return;
    }

    GitCommand command;
    command.program = QStringLiteral("git");
    command.arguments = {QStringLiteral("stash"), QStringLiteral("list"), QStringLiteral("--format=%gd%x00%H")};
    command.workingDirectory = repository.directory;
    command.title = tr("校验 Stash 目标");
    command.kind = GitCommandKind::StashVerify;
    command.repositoryIndex = repository.index;
    command.repositoryPath = repository.path;
    command.stash.expectedSha = targetSha;
    command.stash.action = action;
    command.stash.title = title;
    command.stopOnFailure = true;
    enqueue(command);
}

void WorkingTreeWorkflow::stashPop(int index, const QString &expectedSha, quint64 operationId)
{
    const QScopedValueRollback<quint64> operation(m_operation, operationId);
    executeStashAction(QStringLiteral("pop"), index, expectedSha, tr("应用并弹出 Stash"));
}

void WorkingTreeWorkflow::stashApply(int index, const QString &expectedSha, quint64 operationId)
{
    const QScopedValueRollback<quint64> operation(m_operation, operationId);
    executeStashAction(QStringLiteral("apply"), index, expectedSha, tr("应用 Stash"));
}

void WorkingTreeWorkflow::stashDrop(int index, const QString &expectedSha, quint64 operationId)
{
    const QScopedValueRollback<quint64> operation(m_operation, operationId);
    executeStashAction(QStringLiteral("drop"), index, expectedSha, tr("删除 Stash"));
}

void WorkingTreeWorkflow::markResolved(const QStringList &paths, quint64 operationId)
{
    const QScopedValueRollback<quint64> operation(m_operation, operationId);
    const auto repository = m_repositories.activeRepositoryContext();
    if (repository.index < 0 || paths.isEmpty() || m_repositories.busy()) return;
    if (!validateOwnFiles(paths)) return;
    QSet<QString> conflictPaths;
    for (const QVariant &file : m_repositories.ownRepositoryFiles(repository.index, repository.files)) {
        const QVariantMap entry = file.toMap();
        if (entry.value(QStringLiteral("conflict")).toBool())
            conflictPaths.insert(entry.value(QStringLiteral("path")).toString());
    }
    for (const QString &path : paths) {
        if (!conflictPaths.contains(path)) {
            fail(tr("无法标记解决"), tr("所选文件不是当前仓库的冲突文件：%1").arg(path));
            return;
        }
    }
    QStringList args = {QStringLiteral("--literal-pathspecs"), QStringLiteral("add"), QStringLiteral("--")};
    args.append(paths);
    enqueueGit(repository.index, args, tr("标记冲突已解决"), GitCommandKind::General, true);
    requestRefresh(repository.index, false);
}

void WorkingTreeWorkflow::continueOperation(quint64 operationId)
{
    const QScopedValueRollback<quint64> operation(m_operation, operationId);
    const auto repository = m_repositories.activeRepositoryContext();
    if (repository.index < 0 || m_repositories.busy()) return;
    if (repository.operation.isEmpty()) {
        fail(tr("无法继续"), tr("当前仓库没有进行中的合并、变基或拣选操作。"));
        return;
    }
    if (repository.hasConflicts) {
            fail(tr("无法继续"), tr("仍有未解决的冲突文件，请先解决并标记。"),
                        WorkflowErrorType::Conflict, {QStringLiteral("conflicts")});
        return;
    }
    enqueueGit(repository.index, GitQueries::commitScope(), tr("检查提交仓库边界"), GitCommandKind::CommitScopePreflight, true);
    const QString gitDir = gitMetadataDirectory(repository.directory);
    if (gitDir.isEmpty()) return;

    if (QFile::exists(gitDir + QStringLiteral("/rebase-merge")) || QFile::exists(gitDir + QStringLiteral("/rebase-apply"))) {
        enqueueGit(repository.index, {QStringLiteral("rebase"), QStringLiteral("--continue")},
                   tr("继续 Rebase"), GitCommandKind::General, true);
    } else if (QFile::exists(gitDir + QStringLiteral("/MERGE_HEAD"))) {
        enqueueGit(repository.index, {QStringLiteral("merge"), QStringLiteral("--continue")},
                   tr("继续 Merge"), GitCommandKind::General, true);
    } else if (QFile::exists(gitDir + QStringLiteral("/CHERRY_PICK_HEAD"))) {
        enqueueGit(repository.index, {QStringLiteral("cherry-pick"), QStringLiteral("--continue")},
                   tr("继续 Cherry-Pick"), GitCommandKind::General, true);
    } else if (QFile::exists(gitDir + QStringLiteral("/REVERT_HEAD"))) {
        enqueueGit(repository.index, {QStringLiteral("revert"), QStringLiteral("--continue")},
                   tr("继续 Revert"), GitCommandKind::General, true);
    }
    requestRefresh(repository.index, true);
}

void WorkingTreeWorkflow::abortOperation(quint64 operationId)
{
    const QScopedValueRollback<quint64> operation(m_operation, operationId);
    const auto repository = m_repositories.activeRepositoryContext();
    if (repository.index < 0 || m_repositories.busy()) return;
    if (repository.operation.isEmpty()) {
        fail(tr("无法终止"), tr("当前仓库没有进行中的合并、变基或拣选操作。"));
        return;
    }
    const QString gitDir = gitMetadataDirectory(repository.directory);
    if (gitDir.isEmpty()) return;

    if (QFile::exists(gitDir + QStringLiteral("/rebase-merge")) || QFile::exists(gitDir + QStringLiteral("/rebase-apply"))) {
        enqueueGit(repository.index, {QStringLiteral("rebase"), QStringLiteral("--abort")},
                   tr("终止 Rebase"), GitCommandKind::General, true);
    } else if (QFile::exists(gitDir + QStringLiteral("/MERGE_HEAD"))) {
        enqueueGit(repository.index, {QStringLiteral("merge"), QStringLiteral("--abort")},
                   tr("终止 Merge"), GitCommandKind::General, true);
    } else if (QFile::exists(gitDir + QStringLiteral("/CHERRY_PICK_HEAD"))) {
        enqueueGit(repository.index, {QStringLiteral("cherry-pick"), QStringLiteral("--abort")},
                   tr("终止 Cherry-Pick"), GitCommandKind::General, true);
    } else if (QFile::exists(gitDir + QStringLiteral("/REVERT_HEAD"))) {
        enqueueGit(repository.index, {QStringLiteral("revert"), QStringLiteral("--abort")},
                   tr("终止 Revert"), GitCommandKind::General, true);
    }
    requestRefresh(repository.index, true);
}

void WorkingTreeWorkflow::skipCherryPick(const QString &expectedRevision, quint64 operationId)
{
    const QScopedValueRollback<quint64> operation(m_operation, operationId);
    const auto repository = m_repositories.activeRepositoryContext();
    if (m_repositories.busy() || repository.index < 0) return;
    if (!repository.canSkipCherryPick || expectedRevision.isEmpty() || expectedRevision != repository.cherryPickRevision) {
        fail(tr("无法跳过提交"), tr("仅能跳过当前工作区和暂存区均无改动的空拣选提交，请刷新后确认。"));
        return;
    }
    GitCommand command;
    command.arguments = GitQueries::commitScope();
    command.workingDirectory = repository.directory;
    command.repositoryIndex = repository.index;
    command.repositoryPath = repository.path;
    command.revision = expectedRevision;
    command.kind = GitCommandKind::CherryPickSkipPreflight;
    command.title = tr("检查空拣选提交状态");
    command.stopOnFailure = true;
    enqueue(command);
}

void WorkingTreeWorkflow::reset()
{
    m_stashList.clear();
    m_stashEntries.clear();
    m_stashRepositoryPath.clear();
    emit stashChanged();
}

void WorkingTreeWorkflow::enqueueGit(int index, const QStringList &arguments, const QString &title,
                                     GitCommandKind kind, bool stopOnFailure)
{
    auto command = commandFor(m_repositories.repositoryContext(index), arguments, title, kind, m_operation);
    command.stopOnFailure = stopOnFailure;
    enqueue(command);
}

bool WorkingTreeWorkflow::handleResult(const GitCommandResult &result)
{
    const auto &completed = result.command;
    switch (completed.kind) {
    case GitCommandKind::DiscardPreflight:
    case GitCommandKind::CommitScopePreflight:
    case GitCommandKind::CherryPickSkipPreflight:
    case GitCommandKind::StashList:
    case GitCommandKind::StashVerify: break;
    default: return false;
    }
    if (completed.generation != m_repositories.workspaceGeneration() || result.status == ExecutionStatus::Canceled) return true;
    if ((completed.kind == GitCommandKind::StashList || completed.kind == GitCommandKind::StashVerify) && !isActive(completed)) return true;
    if (result.status != ExecutionStatus::Success) {
        fail(completed.title, result.diagnosticMessage, WorkflowErrorType::General, {}, &completed);
        return true;
    }
    const auto repository = m_repositories.activeRepositoryContext();
    const QString output = QString::fromLocal8Bit(result.standardOutput);
    const QScopedValueRollback<quint64> operation(m_operation, completed.operationId);
    switch (completed.kind) {
    case GitCommandKind::CherryPickSkipPreflight: {
        const auto snapshot = GitStatusParser::parseSnapshot(output);
        if (completed.repositoryIndex != repository.index || completed.repositoryPath != repository.path
            || completed.revision != repository.cherryPickRevision || completed.revision.isEmpty()) {
            fail(tr("无法跳过提交"), tr("仓库或正在拣选的提交已变化，请刷新后重试。"), WorkflowErrorType::General, {}, &completed);
            return true;
        }
        if (!snapshot.files.isEmpty()) {
            fail(tr("无法跳过提交"), tr("工作区、暂存区或冲突状态已变化。为避免丢失改动，不执行跳过。"), WorkflowErrorType::General, {}, &completed);
            requestRefresh(repository.index, false);
            return true;
        }
        const QScopedValueRollback<quint64> operation(m_operation, completed.operationId);
        enqueueGit(repository.index, {QStringLiteral("cherry-pick"), QStringLiteral("--skip")},
                   tr("跳过空拣选提交 %1").arg(completed.revision.left(10)), GitCommandKind::General, true);
        requestRefresh(repository.index, true);
        break;
    }

    case GitCommandKind::CommitScopePreflight: {
        const auto files = GitStatusParser::parseSnapshot(output).files;
        for (const auto &file : files) {
            const auto entry = file.toMap();
            if (entry.value(QStringLiteral("hasStagedChanges")).toBool()
                && m_repositories.ownRepositoryFiles(completed.repositoryIndex, QVariantList{file}).isEmpty()) {
                emit cancelOperationRequested(completed.operationId);
                fail(tr("无法提交"),
                    tr("父仓库暂存区包含嵌套仓库路径：%1。本次提交已阻止，嵌套仓库暂存项未被自动取消。")
                        .arg(entry.value(QStringLiteral("path")).toString()));
                break;
            }
        }
        break;
    }

    case GitCommandKind::DiscardPreflight: {
        const QScopedValueRollback<quint64> operation(m_operation, completed.operationId);
        executeDiscard(completed.repositoryIndex, completed.paths, output);
        break;
    }

    case GitCommandKind::StashVerify: {
        const QString expectedSha = completed.stash.expectedSha.trimmed();
        const QString action = completed.stash.action;
        const QString opTitle = completed.stash.title;

        QString matchedRef;
        const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            const QStringList parts = line.split(QLatin1Char('\0'));
            if (parts.size() >= 2) {
                const QString ref = parts.at(0).trimmed();
                const QString sha = parts.at(1).trimmed();
                if (!expectedSha.isEmpty() && (sha.startsWith(expectedSha) || expectedSha.startsWith(sha))) {
                    matchedRef = ref;
                    break;
                }
            }
        }

        if (matchedRef.isEmpty()) {
            const QString msg = tr("Stash 操作已取消：未找到匹配 SHA [%1] 的 Stash（可能已被修改或删除），防止误操作。").arg(expectedSha);
            emit logMessage(msg + QLatin1Char('\n'));
            fail(opTitle, msg);
            loadStashList();
            break;
        }

        enqueueGit(completed.repositoryIndex, {QStringLiteral("stash"), action, matchedRef},
                   opTitle, GitCommandKind::StashAction, true);
        if (action == QLatin1String("pop") || action == QLatin1String("apply")) {
            requestRefresh(completed.repositoryIndex, false);
        }
        loadStashList();
        break;
    }
    case GitCommandKind::StashList: {
        if (completed.generation == m_repositories.workspaceGeneration() && completed.repositoryIndex == repository.index
            && completed.repositoryPath == repository.path) {
            m_stashList.clear();
            m_stashEntries.clear();
            const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            for (const QString &line : lines) {
                const QStringList parts = line.split(QLatin1Char('\0'));
                if (parts.size() >= 3) {
                    m_stashEntries.append(StashEntry{parts.at(0).trimmed(), parts.at(1).trimmed(), parts.at(2).trimmed(), completed.repositoryPath});
                    m_stashList.append(parts.at(2).trimmed());
                } else {
                    m_stashEntries.append(StashEntry{QString(), QString(), line.trimmed(), completed.repositoryPath});
                    m_stashList.append(line.trimmed());
                }
            }
            m_stashRepositoryPath = completed.repositoryPath;
            emit stashChanged();
        }
        break;
    }
    default: break;
    }
    return true;
}
