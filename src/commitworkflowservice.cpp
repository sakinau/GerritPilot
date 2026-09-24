#include "commitworkflowservice.h"

CommitWorkflowService::CommitWorkflowService(QObject *parent)
    : QObject(parent)
{
}

void CommitWorkflowService::setPendingPush(bool pending)
{
    if (m_pendingPush != pending) {
        m_pendingPush = pending;
        emit pendingPushChanged(m_pendingPush);
    }
}

bool CommitWorkflowService::prepareCommit(int repoIndex, const QString &repoAbsPath,
                                          const QString &repoRelPath, const QString &workspacePath,
                                          const QString &message, bool amend, bool stageAll,
                                          bool isDetailsCached, bool isBusy,
                                          GitCommand *stageCommand, GitCommand *commitCommand,
                                          QString *errorMessage) const
{
    if (repoIndex < 0 || repoAbsPath.isEmpty()) {
        if (errorMessage) *errorMessage = tr("未选择有效仓库。");
        return false;
    }
    if (isDetailsCached) {
        if (errorMessage) *errorMessage = tr("仓库状态尚未验证。");
        return false;
    }
    if (isBusy) {
        if (errorMessage) *errorMessage = tr("请等待当前操作完成。");
        return false;
    }
    if (!amend && message.trimmed().isEmpty()) {
        if (errorMessage) *errorMessage = tr("创建新提交时必须填写提交说明。");
        return false;
    }

    if (stageAll && stageCommand) {
        stageCommand->program = QStringLiteral("git");
        // The controller must supply explicit, boundary-checked paths before enqueueing.
        stageCommand->arguments.clear();
        stageCommand->workingDirectory = repoAbsPath;
        stageCommand->title = tr("暂存全部改动");
        stageCommand->kind = GitCommandKind::StageAll;
        stageCommand->repositoryIndex = repoIndex;
        stageCommand->repositoryPath = repoRelPath;
        stageCommand->stopOnFailure = true;
    }

    if (commitCommand) {
        QStringList arguments{QStringLiteral("commit")};
        if (amend) {
            arguments << QStringLiteral("--amend");
            if (message.trimmed().isEmpty())
                arguments << QStringLiteral("--no-edit");
        }
        if (!message.trimmed().isEmpty())
            arguments << QStringLiteral("-m") << message.trimmed();

        commitCommand->program = QStringLiteral("git");
        commitCommand->arguments = arguments;
        commitCommand->workingDirectory = repoAbsPath;
        commitCommand->title = amend ? tr("修订提交") : tr("创建提交");
        commitCommand->kind = GitCommandKind::Commit;
        commitCommand->repositoryIndex = repoIndex;
        commitCommand->repositoryPath = repoRelPath;
        commitCommand->stopOnFailure = true;
        commitCommand->commitOperation = true;
        commitCommand->affectedRepositories = {repoRelPath};
        commitCommand->workspacePath = workspacePath;
        commitCommand->submittedMessage = message;
    }

    return true;
}

bool CommitWorkflowService::prepareCommitAndPush(int repoIndex, const QString &repoAbsPath,
                                                 const QString &repoRelPath, const QString &workspacePath,
                                                 const QString &message, bool amend, bool stageAll,
                                                 bool isDetailsCached, bool isBusy,
                                                 const QString &remote, const QString &targetBranch,
                                                 const QString &topic,
                                                 GitCommand *stageCommand, GitCommand *commitCommand,
                                                 QString *errorMessage) const
{
    if (targetBranch.trimmed().isEmpty() || remote.trimmed().isEmpty()) {
        if (errorMessage) *errorMessage = tr("当前仓库未配置 Remote 或 Gerrit 目标分支。");
        return false;
    }

    if (!prepareCommit(repoIndex, repoAbsPath, repoRelPath, workspacePath,
                       message, amend, stageAll, isDetailsCached, isBusy,
                       stageCommand, commitCommand, errorMessage)) {
        return false;
    }

    if (commitCommand) {
        commitCommand->pushAfterCommit = true;
        commitCommand->pushRemote = remote.trimmed();
        commitCommand->pushTargetBranch = targetBranch.trimmed();
        commitCommand->pushTopic = topic.trimmed();
    }

    return true;
}

void CommitWorkflowService::onCommitCompleted(const GitCommand &command)
{
    WorkspaceSettings::clearDraft(command.workspacePath, command.repositoryPath);
    emit commitSucceeded(command.workspacePath, command.repositoryPath, command.submittedMessage);
}

QString CommitWorkflowService::loadDraft(const QString &repoRelPath, const QString &workspacePath) const
{
    return WorkspaceSettings::loadDraft(workspacePath, repoRelPath);
}

void CommitWorkflowService::saveDraft(const QString &repoRelPath, const QString &text, const QString &workspacePath)
{
    WorkspaceSettings::saveDraft(workspacePath, repoRelPath, text);
}

void CommitWorkflowService::clearDraft(const QString &repoRelPath, const QString &workspacePath)
{
    WorkspaceSettings::clearDraft(workspacePath, repoRelPath);
}
