#include "hookworkflow.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QScopedValueRollback>
#include <QTemporaryFile>
#include <QTimer>

HookWorkflow::HookWorkflow(const WorkflowRepositoryAccess &repositories, GerritService &gerrit)
    : RepositoryWorkflow(repositories), m_gerrit(gerrit)
{
    connect(&m_gerrit, &GerritService::hookDownloadFinished, this,
        [this](const GitCommand &context, bool success, const QString &error) {
            if (!isActive(context) || context.target.request != m_installRequest) {
                QFile::remove(context.hook.temporaryPath);
                return;
            }
            m_downloading = false;
            emit busyChanged();
            if (!success) {
                QFile::remove(context.hook.temporaryPath);
                m_resumePendingCommit = false;
                fail(tr("无法安装 Hook"), error, WorkflowErrorType::Hook, {}, &context);
                return;
            }
            emit logMessage(tr("已从 %1 下载 Change-Id Hook。\n").arg(context.hook.source));
            finishInstall(context);
        });
}

void HookWorkflow::rememberCommit(const QString &message, bool amend, bool stageAll)
{
    const auto repository = m_repositories.activeRepositoryContext();
    m_pendingCommit = {repository.valid(), repository.workspace, repository.path, message, amend, stageAll};
}

void HookWorkflow::clearPendingCommit()
{
    m_pendingCommit = {};
    m_resumePendingCommit = false;
}

void HookWorkflow::reset()
{
    ++m_installRequest;
    clearPendingCommit();
    m_gerrit.cancelHookDownload();
    if (m_downloading) {
        m_downloading = false;
        emit busyChanged();
    }
}

void HookWorkflow::setStatus(bool installed)
{
    if (m_hasCommitHook == installed) return;
    m_hasCommitHook = installed;
    emit statusChanged();
}

void HookWorkflow::updateStatus()
{
    const auto repository = m_repositories.activeRepositoryContext();
    // Unknown status is conservative; never carry a previous repository's result.
    setStatus(repository.index < 0);
    if (repository.index >= 0)
        enqueue(m_gerrit.createQueryHookPathCommand(repository.index, repository.directory,
                                                   repository.path, GitCommandKind::HookStatus));
}

void HookWorkflow::install(bool resume, quint64 operationId)
{
    const auto repository = m_repositories.activeRepositoryContext();
    const QScopedValueRollback<quint64> operation(m_operation, operationId);
    if (repository.index < 0 || m_repositories.busy()) return;
    m_resumePendingCommit = resume && m_pendingCommit.valid
        && m_pendingCommit.workspacePath == repository.workspace
        && m_pendingCommit.repositoryPath == repository.path;
    const QString remote = repository.config.remote.trimmed();
    if (remote.isEmpty() || remote.startsWith(QLatin1Char('-'))) {
        m_resumePendingCommit = false;
        fail(tr("无法安装 Hook"), tr("请先为当前仓库配置有效的 Gerrit Remote。"));
        return;
    }
    GitCommand command = m_gerrit.createQueryHookRemoteCommand(repository.index, repository.directory,
                                                               repository.path, remote);
    command.target.request = ++m_installRequest;
    enqueue(command);
}

void HookWorkflow::resumePendingCommit()
{
    const auto repository = m_repositories.activeRepositoryContext();
    if (!m_resumePendingCommit) return;
    m_resumePendingCommit = false;
    const PendingCommit pending = m_pendingCommit;
    m_pendingCommit = {};
    if (!pending.valid || pending.workspacePath != repository.workspace
        || pending.repositoryPath != repository.path) return;
    QTimer::singleShot(0, this, [this, pending] {
        const auto repository = m_repositories.activeRepositoryContext();
        if (m_repositories.busy() || pending.workspacePath != repository.workspace
            || pending.repositoryPath != repository.path) {
            fail(tr("提交未继续"), tr("仓库或任务状态已变化，请重新检查后提交。"));
            return;
        }
        emit resumeCommitRequested(pending.message, pending.amend, pending.stageAll);
    });
}

void HookWorkflow::finishInstall(const GitCommand &completed)
{
    const auto repository = m_repositories.activeRepositoryContext();
    const QString hookPath = completed.hook.targetPath;
    const QString temporaryPath = completed.hook.temporaryPath;
    if (completed.repositoryIndex != repository.index || completed.repositoryPath != repository.path
        || temporaryPath.isEmpty() || QFileInfo(temporaryPath).size() == 0
        || QFileInfo::exists(hookPath) || QFileInfo(hookPath).isSymLink()
        || !QFile::rename(temporaryPath, hookPath)) {
        QFile::remove(temporaryPath);
        m_resumePendingCommit = false;
        fail(tr("无法安装 Hook"), tr("Hook 安装结果无效或目标路径已被其他程序占用；未覆盖现有文件。"));
        return;
    }
    GerritService::finalizeHookPermissions(hookPath);
    const QFileInfo installedHook(hookPath);
    setStatus(installedHook.isFile() && installedHook.isExecutable());
    if (!m_hasCommitHook) {
        m_resumePendingCommit = false;
        fail(tr("无法安装 Hook"), tr("Hook 文件已下载，但未通过可执行性检查：%1").arg(hookPath));
        return;
    }
    resumePendingCommit();
}

bool HookWorkflow::handleResult(const GitCommandResult &result)
{
    const auto &completed = result.command;
    switch (completed.kind) {
    case GitCommandKind::HookStatus:
    case GitCommandKind::HookCommitPreflight:
    case GitCommandKind::HookRepairPreflight:
    case GitCommandKind::HookRemoteUrl:
    case GitCommandKind::HookDirectory:
    case GitCommandKind::InstallHook: break;
    default: return false;
    }
    const auto repository = m_repositories.activeRepositoryContext();
    const bool success = result.status == ExecutionStatus::Success;
    const QString output = QString::fromLocal8Bit(result.standardOutput);
    if (result.status == ExecutionStatus::Canceled || completed.generation != m_repositories.workspaceGeneration()) {
        if (completed.kind == GitCommandKind::InstallHook) QFile::remove(completed.hook.temporaryPath);
        return true;
    }
    if (completed.kind == GitCommandKind::HookStatus
        || completed.kind == GitCommandKind::HookCommitPreflight
        || completed.kind == GitCommandKind::HookRepairPreflight) {
        const bool preflight = completed.kind != GitCommandKind::HookStatus;
        if (completed.repositoryIndex != repository.index || completed.repositoryPath != repository.path) {
            if (preflight) emit cancelOperationRequested(completed.operationId);
            return true;
        }
        const QString path = output.trimmed();
        const QFileInfo hook(path);
        const bool installed = success && QDir::isAbsolutePath(path)
            && hook.fileName() == QStringLiteral("commit-msg") && hook.isFile() && hook.isExecutable();
        setStatus(installed);
        if (completed.kind == GitCommandKind::HookRepairPreflight) m_pendingCommit = {};
        if (!preflight) {
            if (!success) emit logMessage(result.diagnosticMessage);
            return true;
        }
        if (!installed) {
            // Cancel staged writes and the commit/repair belonging to this task.
            emit cancelOperationRequested(completed.operationId);
            if (!success) {
                m_pendingCommit = {};
                fail(tr("无法检查 Change-Id Hook"), result.diagnosticMessage, WorkflowErrorType::Hook, {}, &completed);
            } else {
                fail(tr("缺少 Change-Id Hook"),
                    completed.kind == GitCommandKind::HookCommitPreflight
                        ? tr("当前仓库没有可执行的 commit-msg Hook。请选择“安装 Hook 并继续提交”；安装失败时不会创建提交。")
                        : tr("请先安装当前仓库的 Hook，再修订提交。"), WorkflowErrorType::Hook,
                    completed.kind == GitCommandKind::HookCommitPreflight
                        ? QStringList{QStringLiteral("installHook"), QStringLiteral("resumeCommit")}
                        : QStringList{QStringLiteral("installHook")}, &completed);
            }
            return true;
        }
        if (completed.kind == GitCommandKind::HookCommitPreflight) m_pendingCommit = {};
        return true;
    }

    if (!isActive(completed) || completed.target.request != m_installRequest) {
        if (completed.kind == GitCommandKind::InstallHook) QFile::remove(completed.hook.temporaryPath);
        return true;
    }
    if (!success) {
        m_resumePendingCommit = false;
        if (completed.kind == GitCommandKind::InstallHook) QFile::remove(completed.hook.temporaryPath);
        fail(completed.title, result.diagnosticMessage, WorkflowErrorType::Hook, {}, &completed);
        return true;
    }
    const QScopedValueRollback<quint64> operation(m_operation, completed.operationId);
    switch (completed.kind) {
    case GitCommandKind::HookRemoteUrl: {
        if (completed.repositoryIndex != repository.index || completed.repositoryPath != repository.path) {
            m_resumePendingCommit = false;
            fail(tr("无法安装 Hook"), tr("当前仓库已变化，请重新操作。"));
            return true;
        }
        QString source, port, error;
        if (!GerritService::hookSourceFromRemoteUrl(output, &source, &port, &error)) {
            m_resumePendingCommit = false;
            fail(tr("无法安装 Hook"), error);
            return true;
        }
        const QScopedValueRollback<quint64> operation(m_operation, completed.operationId);
        GitCommand query = completed;
        query.arguments = {QStringLiteral("rev-parse"), QStringLiteral("--path-format=absolute"),
                           QStringLiteral("--git-path"), QStringLiteral("hooks/commit-msg")};
        query.kind = GitCommandKind::HookDirectory;
        query.hook.source = source;
        query.hook.port = port;
        query.title = tr("读取有效的 Hook 路径");
        enqueue(query);
        return true;
    }
    case GitCommandKind::HookDirectory: {
        const QScopedValueRollback<quint64> operation(m_operation, completed.operationId);
        const QString targetHook = output.trimmed();
        if (completed.repositoryIndex != repository.index || completed.repositoryPath != repository.path
            || !QDir::isAbsolutePath(targetHook)
            || QFileInfo(targetHook).fileName() != QStringLiteral("commit-msg")) {
            m_resumePendingCommit = false;
            fail(tr("无法安装 Hook"), tr("无法确认当前仓库的有效 Hook 路径。"));
            return true;
        }
        const QFileInfo existing(targetHook);
        if (existing.exists() || existing.isSymLink()) {
            setStatus(existing.isFile() && existing.isExecutable());
            if (m_hasCommitHook) {
                resumePendingCommit();
            } else {
                m_resumePendingCommit = false;
                fail(tr("无法安装 Hook"),
                    tr("有效 Hook 路径已有文件但不可执行：%1。程序不会覆盖它，请检查后重试。")
                        .arg(targetHook));
            }
            return true;
        }
        if (!QDir().mkpath(existing.path())) {
            m_resumePendingCommit = false;
            fail(tr("无法安装 Hook"), tr("无法创建 Hook 目录：%1").arg(existing.path()));
            return true;
        }

        // smart-commit spec: in a repo workspace, check if <repo根>/.repo/repo/hooks/commit-msg exists locally
        const QString repoHook = QDir(repository.workspace).filePath(QStringLiteral(".repo/repo/hooks/commit-msg"));
        if (QFile::exists(repoHook) && QFileInfo(repoHook).size() > 0) {
            if (QFile::copy(repoHook, targetHook)) {
                GerritService::finalizeHookPermissions(targetHook);
                const QFileInfo installed(targetHook);
                setStatus(installed.isFile() && installed.isExecutable());
                if (m_hasCommitHook) {
                    emit logMessage(tr("已从本地 Repo 工具源 (.repo/repo/hooks/commit-msg) 自动就地安装 Hook。\n"));
                    resumePendingCommit();
                    return true;
                }
            }
        }
        QTemporaryFile temporary(QDir(existing.path()).filePath(QStringLiteral(".commit-msg.XXXXXX")));
        temporary.setAutoRemove(false);
        if (!temporary.open()) {
            m_resumePendingCommit = false;
            fail(tr("无法安装 Hook"), tr("无法准备 Hook 临时文件。"));
            return true;
        }
        const QString temporaryPath = temporary.fileName();
        temporary.close();
        if (GerritService::isHttpHookSource(completed.hook.source)) {
            emit logMessage(tr("正在从 %1 下载 Change-Id Hook…\n").arg(completed.hook.source));
            m_downloading = true;
            emit busyChanged();
            m_gerrit.downloadHook(completed, targetHook, temporaryPath);
            return true;
        }
        GitCommand command = m_gerrit.createInstallHookCommand(completed, targetHook, temporaryPath);
        enqueue(command);
        return true;
    }
    case GitCommandKind::InstallHook:
        finishInstall(completed);
        break;
    default: break;
    }
    return true;
}
