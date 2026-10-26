#include "commitcommandbuilder.h"

bool CommitCommandBuilder::prepareCommit(int repoIndex, const QString &repoAbsPath,
                                          const QString &repoRelPath, const QString &workspacePath,
                                          const QString &message, bool amend, bool stageAll,
                                          bool isDetailsCached, bool isBusy,
                                          GitCommand *stageCommand, GitCommand *commitCommand,
                                          QString *errorMessage)
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

