#include "synchronizationworkflow.h"
#include "gerritservice.h"
#include <QDir>
#include <QFileInfo>
#include <QScopedValueRollback>
#include <QRegularExpression>

namespace {
QStringList dailyPullArguments(const QString &remote, const QString &remoteRef, bool rebase)
{
    // Gerrit chains keep their Change-Id trailers through a rebase; a merge commit would not be reviewable.
    QStringList args{QStringLiteral("pull"), rebase ? QStringLiteral("--rebase") : QStringLiteral("--ff-only")};
    if (!remote.isEmpty() && !remoteRef.isEmpty()) {
        args.append({QStringLiteral("--no-tags"), QStringLiteral("--"), remote, remoteRef});
    }
    return args;
}

}

void SynchronizationWorkflow::syncSelection(const QStringList &paths, bool rebase, quint64 operationId)
{
    if (m_repositories.busy()) {
        emit logMessage(tr("当前已有操作正在执行：%1，请等待完成后再试。").arg(QStringLiteral("Git/Hook")));
        fail(tr("无法拉取"), tr("当前正在执行操作：%1，请等待完成后再试。").arg(QStringLiteral("Git/Hook")));
        return;
    }
    const QScopedValueRollback<quint64> operation(m_operation, operationId);
    if (paths.isEmpty()) {
        emit logMessage(tr("未选择任何可拉取的仓库。"));
        fail(tr("无法同步"), tr("没有选择要同步的仓库。"));
        return;
    }
    for (const QString &path : paths) {
        const auto repository = m_repositories.repositoryContextByPath(path);
        const int index = repository.index;
        const QString branch = index >= 0 ? repository.branch : QString();
        if (branch == QStringLiteral("(detached)") || branch == QStringLiteral("detached")) {
            const QString error = m_repositories.isRepoWorkspace()
                ? tr("仓库 %1 处于 detached HEAD，没有可拉取的本地分支。请在拉取面板选择“Repo 按清单同步”。").arg(repository.name)
                : tr("仓库 %1 处于 detached HEAD，请先切换到本地分支。").arg(repository.name);
            emit logMessage(error);
            fail(tr("无法 Git 拉取"), error);
            return;
        }
    }
    emit logMessage(tr("开始拉取代码 · 共 %1 个仓库...").arg(paths.size()));
    for (const QString &path : paths) {
        const auto repository = m_repositories.repositoryContextByPath(path);
        const int index = repository.index;
        if (index >= 0) {
            GitCommand command;
            command.arguments = {QStringLiteral("for-each-ref"),
                QStringLiteral("--format=%(HEAD)%00%(upstream:remotename)%00%(upstream:remoteref)"),
                QStringLiteral("refs/heads/")};
            command.workingDirectory = repository.directory;
            command.repositoryIndex = index;
            command.repositoryPath = path;
            command.title = tr("解析拉取上游 %1").arg(repository.name);
            command.kind = GitCommandKind::PullTarget;
            command.stopOnFailure = true;
            command.target.pullRebase = rebase;
            enqueue(command);
        }
    }
}

void SynchronizationWorkflow::syncRepoSelection(int scope, const QStringList &paths, quint64 operationId)
{
    if (m_repositories.busy()) {
        fail(tr("无法同步"), tr("当前已有操作正在执行，请等待完成后再试。"));
        return;
    }
    if (!m_repositories.isRepoWorkspace() || !QFileInfo(QDir(m_repositories.workspacePath()).filePath(QStringLiteral(".repo/manifest.xml"))).isFile()) {
        fail(tr("无法同步"), tr("请先初始化或添加 Repo 项目。"));
        return;
    }
    if (scope != 0 && scope != 1 && scope != 2) {
        fail(tr("无法同步"), tr("请选择当前、已勾选或全部仓库进行 Repo 同步。"));
        return;
    }

    const QScopedValueRollback<quint64> operation(m_operation, operationId);
    GitCommand command;
    command.program = QStringLiteral("repo");
    command.arguments = {QStringLiteral("sync"), QStringLiteral("--no-manifest-update"),
                         QStringLiteral("-j1"), QStringLiteral("--fail-fast")};

    if (scope == 2) {
        command.title = tr("按 Manifest 全量同步全部仓库");
        command.affectedRepositories = paths;
    } else {
        if (paths.isEmpty()) {
            fail(tr("无法同步"), tr("没有选择要同步的仓库。"));
            return;
        }
        const QString root = QFileInfo(m_repositories.workspacePath()).canonicalFilePath();
        for (const QString &path : paths) {
            const QString target = QFileInfo(QDir(m_repositories.workspacePath()).filePath(path)).canonicalFilePath();
            if (path == QStringLiteral(".") || path.startsWith(QStringLiteral(".repo/")) || path.startsWith(QStringLiteral("../"))
                || target.isEmpty() || !target.startsWith(root + QLatin1Char('/'))
                || !QFileInfo(QDir(target).filePath(QStringLiteral(".git"))).exists()) {
                fail(tr("无法同步"), tr("所选仓库不属于当前 Repo 项目：%1").arg(path));
                return;
            }
        }
        command.arguments.append(paths);
        command.title = tr("按 Manifest 同步 %1 个仓库").arg(paths.size());
        command.affectedRepositories = paths;
    }

    command.workingDirectory = m_repositories.workspacePath();
    command.workspacePath = m_repositories.workspacePath();
    command.rediscoverProjects = scope == 2;
    command.kind = GitCommandKind::RepoSync;
    command.stopOnFailure = true;
    command.timeoutMilliseconds = 30 * 60 * 1000;
    enqueue(command);
}

void SynchronizationWorkflow::configurePullUpstream(const QString &remote, const QString &branch, quint64 operationId)
{
    const auto repository = m_repositories.activeRepositoryContext();
    if (repository.index < 0 || m_repositories.busy()) return;
    const QString remoteName = remote.trimmed();
    const QString targetBranch = branch.trimmed();
    const QString localBranch = repository.branch;
    static const QRegularExpression remotePattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]*$"));
    QString branchError;
    if (!remotePattern.match(remoteName).hasMatch()
        || !GitService::validateBranchName(targetBranch, &branchError)
        || !GitService::validateBranchName(localBranch, &branchError)) {
        fail(tr("无法设置上游"), tr("请填写有效的 Remote 名称和分支名，并确认当前处于本地分支。"));
        return;
    }
    const QScopedValueRollback<quint64> operation(m_operation, operationId);
    GitCommand query;
    query.arguments = {QStringLiteral("ls-remote"), QStringLiteral("--heads"),
                       QStringLiteral("--exit-code"), QStringLiteral("--"),
                       remoteName, QStringLiteral("refs/heads/") + targetBranch};
    query.workingDirectory = repository.directory;
    query.repositoryIndex = repository.index;
    query.repositoryPath = repository.path;
    query.target.remote = remoteName;
    query.target.branch = targetBranch;
    query.target.localBranch = localBranch;
    query.kind = GitCommandKind::UpstreamPreflight;
    query.title = tr("确认上游分支 %1/%2").arg(remoteName, targetBranch);
    query.stopOnFailure = true;
    enqueue(query);
}

bool SynchronizationWorkflow::handleResult(const GitCommandResult &result)
{
    const auto &completed = result.command;
    switch (completed.kind) {
    case GitCommandKind::RepoSync:
    case GitCommandKind::UpstreamPreflight:
    case GitCommandKind::UpstreamConfigRemote:
    case GitCommandKind::UpstreamConfigMerge:
    case GitCommandKind::PullTarget:
    case GitCommandKind::PullExecution: break;
    default: return false;
    }
    if (completed.generation != m_repositories.workspaceGeneration() || result.status == ExecutionStatus::Canceled) return true;
    const auto active = m_repositories.activeRepositoryContext();
    const QScopedValueRollback<quint64> operation(m_operation, completed.operationId);
    const QString output = QString::fromLocal8Bit(result.standardOutput);
    if (result.status != ExecutionStatus::Success) {
        bool upstream = completed.kind == GitCommandKind::UpstreamPreflight
            || completed.kind == GitCommandKind::UpstreamConfigRemote || completed.kind == GitCommandKind::UpstreamConfigMerge;
        QString message = result.diagnosticMessage;
        const QString lower = QString::fromLocal8Bit(result.standardError).toLower();
        if (completed.kind == GitCommandKind::UpstreamPreflight && result.exitCode == 2)
            message = tr("远端不存在分支 %1/%2，请重新选择。").arg(completed.target.remote, completed.target.branch);
        else if (completed.kind == GitCommandKind::UpstreamConfigRemote || completed.kind == GitCommandKind::UpstreamConfigMerge)
            message = tr("写入本地上游配置失败，配置可能只完成了一部分。请检查当前分支设置。\n") + message;
        else if (completed.kind == GitCommandKind::PullExecution) {
            if (lower.contains("no tracking information") || lower.contains("set-upstream")) {
                upstream = true;
                message = tr("当前分支未设置拉取上游，请在拉取面板选择 Remote 和远端分支。");
            } else if (lower.contains("not possible to fast-forward") || lower.contains("non-fast-forward"))
                message = tr("本地与远端分支已分叉，无法快进拉取；可选择变基方式重试。");
            else if (completed.target.pullRebase && lower.contains("could not apply"))
                message = tr("变基拉取发生冲突，已暂停；解决并标记后继续，或终止以恢复原状态。");
            else if (lower.contains("cannot pull with rebase") || lower.contains("cannot rebase"))
                message = tr("工作区或暂存区有未提交改动，请先提交或贮藏后再变基拉取。");
            else if (!GerritService::credentialHint(result.standardError).isEmpty())
                message = GerritService::credentialHint(result.standardError);
            else if (lower.contains("would be overwritten by merge") || lower.contains("local changes"))
                message = tr("本地存在可能被拉取覆盖的改动，请先提交或贮藏后再拉取。");
            else if (lower.contains("could not resolve host") || lower.contains("connection timed out") || lower.contains("network is unreachable"))
                message = tr("无法连接远端服务器，请检查网络、VPN 或代理设置。");
            else if (lower.contains("permission denied") || lower.contains("authentication failed"))
                message = tr("Git 远端认证失败，请检查 SSH 密钥或访问凭据。");
        }
        if (message != result.diagnosticMessage && !result.diagnosticMessage.isEmpty())
            message += tr("\n\n原始错误：\n") + result.diagnosticMessage;
        const bool conflict = completed.repositoryIndex == active.index && active.hasPendingOperation;
        fail(completed.title, message, upstream ? WorkflowErrorType::Upstream
            : conflict ? WorkflowErrorType::Conflict : WorkflowErrorType::General,
            upstream ? QStringList{QStringLiteral("upstream")}
                : conflict ? QStringList{QStringLiteral("conflicts")} : QStringList{}, &completed);
        if (completed.generation == m_repositories.workspaceGeneration()) {
            for (const auto &path : completed.affectedRepositories) {
                const auto repository = m_repositories.repositoryContextByPath(path);
                requestRefresh(repository.index, repository.index == active.index);
            }
        }
        return true;
    }
    switch (completed.kind) {
    case GitCommandKind::UpstreamPreflight: {
        const QString remoteName = completed.target.remote;
        const QString branch = completed.target.branch;
        const QString localBranch = completed.target.localBranch;
        if (completed.repositoryIndex != active.index || localBranch != active.branch) {
            emit cancelOperationRequested(completed.operationId);
            fail(tr("无法设置上游"), tr("当前仓库或本地分支已变化，请重新选择。"));
            break;
        }
        bool branchExists = false;
        for (const QString &line : output.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            if (line.endsWith(QStringLiteral("\trefs/heads/") + branch)) {
                branchExists = true;
                break;
            }
        }
        if (!branchExists) {
            emit cancelOperationRequested(completed.operationId);
            fail(tr("无法设置上游"), tr("远端分支不存在：%1/%2").arg(remoteName, branch));
            break;
        }
        const QScopedValueRollback<quint64> operation(m_operation, completed.operationId);
        GitCommand setRemote = completed;
        setRemote.arguments = {QStringLiteral("config"), QStringLiteral("--local"), QStringLiteral("--replace-all"),
                               QStringLiteral("branch.%1.remote").arg(localBranch), remoteName};
        setRemote.kind = GitCommandKind::UpstreamConfigRemote;
        setRemote.title = tr("设置 %1 的上游 Remote").arg(localBranch);
        enqueue(setRemote);
        GitCommand setMerge = completed;
        setMerge.arguments = {QStringLiteral("config"), QStringLiteral("--local"), QStringLiteral("--replace-all"),
                              QStringLiteral("branch.%1.merge").arg(localBranch),
                              QStringLiteral("refs/heads/") + branch};
        setMerge.kind = GitCommandKind::UpstreamConfigMerge;
        setMerge.title = tr("设置 %1 的上游分支").arg(localBranch);
        enqueue(setMerge);
        break;
    }
    case GitCommandKind::UpstreamConfigMerge:
        emit logMessage(tr("已将 %1 的拉取上游设为 %2/%3；可重新执行拉取。")
                      .arg(completed.target.localBranch, completed.target.remote, completed.target.branch));
        requestRefresh(completed.repositoryIndex, false);
        break;
    case GitCommandKind::UpstreamConfigRemote:
        break;
    case GitCommandKind::PullTarget: {
        const QScopedValueRollback<quint64> operation(m_operation, completed.operationId);
        QString remote, remoteRef;
        for (const QString &line : output.split(QLatin1Char('\n'))) {
            const auto fields = line.split(QChar(0));
            if (fields.size() == 3 && fields.at(0) == QStringLiteral("*")) {
                remote = fields.at(1);
                remoteRef = fields.at(2);
                break;
            }
        }
        if (remote.isEmpty() || !remoteRef.startsWith(QStringLiteral("refs/heads/"))
            || remoteRef.contains(QLatin1Char('*')) || remoteRef.contains(QLatin1Char(':'))) {
            emit cancelOperationRequested(completed.operationId);
            const QString error = tr("仓库 %1 的当前分支未设置可用的拉取上游。请选中该仓库，在拉取面板明确设置 Remote 和远端分支；不会使用 Gerrit 推送目标代替。")
                .arg(m_repositories.repositoryContext(completed.repositoryIndex).name);
            emit logMessage(error);
            fail(tr("无法拉取"), error, WorkflowErrorType::Upstream, {QStringLiteral("upstream")}, &completed);
            break;
        }
        emit logMessage(tr("仅拉取 %1 的 %2；不获取其他分支、标签或子模块。")
                      .arg(remote, remoteRef));
        const bool rebase = completed.target.pullRebase;
        GitCommand pull = completed;
        pull.arguments = dailyPullArguments(remote, remoteRef, rebase);
        pull.title = rebase ? tr("变基拉取仓库 %1").arg(m_repositories.repositoryContext(completed.repositoryIndex).name)
                            : tr("拉取仓库 %1").arg(m_repositories.repositoryContext(completed.repositoryIndex).name);
        pull.kind = GitCommandKind::PullExecution;
        // A conflicting rebase stops mid-way; refresh so the conflict banner appears.
        pull.affectedRepositories = {completed.repositoryPath};
        enqueue(pull);
        requestRefresh(completed.repositoryIndex, completed.repositoryIndex == active.index);
        break;
    }
    case GitCommandKind::RepoSync:
        if (completed.rediscoverProjects) {
            emit finished(completed.title);
            emit rescanRequested();
            return true;
        }
        for (const auto &path : completed.affectedRepositories) {
            const auto repository = m_repositories.repositoryContextByPath(path);
            requestRefresh(repository.index, repository.index == active.index);
        }
        emit finished(completed.title);
        break;
    case GitCommandKind::PullExecution:
        emit finished(completed.title);
        break;
    default: break;
    }
    return true;
}
