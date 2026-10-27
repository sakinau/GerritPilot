#include "aicommitworkflow.h"
#include "commitmessagepolicy.h"
#include "gitqueries.h"
#include "gitstatusparser.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace {
QString untrackedAiPreview(const QString &repository, const QStringList &paths)
{
    QString preview;
    int remaining = 4096;
    for (const QString &path : paths) {
        if (remaining <= 0) break;
        const QFileInfo info(QDir(repository).filePath(path));
        if (!info.isFile() || info.isSymLink() || info.size() > 256 * 1024) continue;
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) continue;
        const QByteArray bytes = file.read(qMin<qint64>(2048, remaining));
        if (bytes.contains('\0')) continue;
        const QString content = QString::fromUtf8(bytes);
        preview += QStringLiteral("\n--- /dev/null\n+++ b/%1\n").arg(path);
        for (const QString &line : content.split(QLatin1Char('\n')))
            preview += QLatin1Char('+') + line + QLatin1Char('\n');
        if (file.size() > bytes.size()) preview += QStringLiteral("[新增文件内容已截断]\n");
        remaining -= bytes.size();
    }
    return preview;
}

}

void AiCommitWorkflow::generate(const QString &existingMessage, int scope,
                                                const QString &commitType, const QString &issueId,
                                                bool manualCompiled, const QString &compileNote,
                                                const QString &purpose, const QString &secondaryIssueId)
{
    const auto repository = m_repositories.activeRepositoryContext();
    if (repository.index < 0) return;
    if (m_repositories.busy()) {
        fail(tr("AI 提交说明"), tr("Git 正在处理其他任务，请稍候再试。"), WorkflowErrorType::Ai);
        return;
    }
    if (scope != 0 && scope != 1) {
        fail(tr("AI 提交说明"), tr("请选择有效的改动范围。"), WorkflowErrorType::Ai);
        return;
    }
    static const QStringList allowedTypes = {
        QStringLiteral("fix"),
        QStringLiteral("feat"),
        QStringLiteral("chg"),
        QStringLiteral("refactor"),
        QStringLiteral("docs"),
        QStringLiteral("style"),
        QStringLiteral("test"),
        QStringLiteral("chore"),
        QStringLiteral("add"),
        QStringLiteral("del"),
        QStringLiteral("merge")
    };
    const QString normalizedType = commitType.trimmed().toLower();
    if (!allowedTypes.contains(normalizedType)) {
        fail(tr("AI 提交说明"), tr("请选择有效的提交类型（如 fix, feat, chg, refactor 等）。"), WorkflowErrorType::Ai);
        return;
    }
    QString normalizedIssue, normalizedSecondaryIssue, issueError;
    if (!CommitMessagePolicy::prepareIssues(normalizedType, issueId, secondaryIssueId,
                                             &normalizedIssue, &normalizedSecondaryIssue, &issueError)) {
        fail(tr("AI 提交说明"), issueError, WorkflowErrorType::Ai);
        return;
    }
    if (!m_commitAi.configured()) {
        fail(tr("AI 提交说明"), tr("请先在设置中填写有效的 AI 接口地址和模型名称。"), WorkflowErrorType::Ai);
        return;
    }
    m_aiExistingMessage = existingMessage;
    m_aiCommitType = normalizedType;
    m_aiIssueId = normalizedIssue;
    m_aiSecondaryIssueId = normalizedSecondaryIssue;
    m_aiManualCompiled = manualCompiled;
    m_aiCompileNote = compileNote;
    m_aiPurpose = purpose.trimmed().left(300);
    m_aiChangedFiles.clear();
    m_aiUntrackedFiles.clear();
    ++m_aiRequest;
    GitCommand command;
    command.program = QStringLiteral("git");
    command.arguments = scope == 0
        ? QStringList{QStringLiteral("diff"), QStringLiteral("--cached"),
                      QStringLiteral("--name-only"), QStringLiteral("-z"), QStringLiteral("--no-renames")}
        : GitQueries::files();
    command.workingDirectory = repository.directory;
    command.repositoryIndex = repository.index;
    command.repositoryPath = repository.path;
    command.title = scope == 0 ? tr("读取暂存文件") : tr("读取仓库改动文件");
    command.kind = scope == 0 ? GitCommandKind::AiStagedFiles : GitCommandKind::AiWorkspaceFiles;
    command.diffRequest = m_aiRequest;
    emit commandRequested(command);
}

void AiCommitWorkflow::reset()
{
    ++m_aiRequest;
    m_commitAi.cancel();
    m_aiChangedFiles.clear();
    m_aiUntrackedFiles.clear();
}

bool AiCommitWorkflow::handleResult(const GitCommandResult &result)
{
    const auto &completed = result.command;
    if (completed.kind != GitCommandKind::AiStagedFiles && completed.kind != GitCommandKind::AiStagedDiff
        && completed.kind != GitCommandKind::AiWorkspaceFiles && completed.kind != GitCommandKind::AiWorkspaceDiff)
        return false;
    if (!isActive(completed) || completed.diffRequest != m_aiRequest || result.status == ExecutionStatus::Canceled)
        return true;
    if (result.status != ExecutionStatus::Success) {
        fail(completed.title, result.diagnosticMessage, WorkflowErrorType::Ai, {}, &completed);
        return true;
    }
    const auto repository = m_repositories.activeRepositoryContext();
    const QString output = QString::fromLocal8Bit(result.standardOutput);
    switch (completed.kind) {
    case GitCommandKind::AiStagedFiles: {
        QStringList files;
        for (const QString &path : output.split(QChar(0), Qt::SkipEmptyParts)) {
            if (m_repositories.isOwnRepositoryPath(completed.repositoryIndex, path)) files.append(path);
        }
        files.removeDuplicates();
        if (files.isEmpty()) {
            fail(tr("AI 提交说明"), tr("当前仓库没有可用于生成说明的暂存文件。"), WorkflowErrorType::Ai);
            break;
        }
        GitCommand diff;
        diff.program = QStringLiteral("git");
        diff.arguments = {QStringLiteral("--literal-pathspecs"), QStringLiteral("diff"), QStringLiteral("--cached"),
            QStringLiteral("--no-renames"), QStringLiteral("--no-ext-diff"),
            QStringLiteral("--no-textconv"), QStringLiteral("--no-color"),
            QStringLiteral("--unified=4"), QStringLiteral("--")};
        diff.arguments.append(files);
        diff.workingDirectory = completed.workingDirectory;
        diff.repositoryIndex = completed.repositoryIndex;
        diff.repositoryPath = completed.repositoryPath;
        diff.title = tr("读取暂存区 Diff");
        diff.kind = GitCommandKind::AiStagedDiff;
        diff.diffRequest = completed.diffRequest;
        diff.paths = files;
        diff.operationId = completed.operationId;
        emit commandRequested(diff);
        break;
    }
    case GitCommandKind::AiStagedDiff:
        if (output.trimmed().isEmpty()) {
            fail(tr("AI 提交说明"), tr("暂存区 Diff 为空，未生成提交说明。"), WorkflowErrorType::Ai);
        } else {
            m_commitAi.generate(repository.name, repository.branch, output, completed.paths,
                                m_aiExistingMessage, m_aiCommitType, m_aiIssueId, false,
                                m_aiManualCompiled, m_aiCompileNote, m_aiPurpose, m_aiSecondaryIssueId);
        }
        break;
    case GitCommandKind::AiWorkspaceFiles: {
        const auto changed = m_repositories.ownRepositoryFiles(completed.repositoryIndex, GitStatusParser::parse(output));
        QStringList trackedPaths;
        for (const QVariant &value : changed) {
            const QVariantMap entry = value.toMap();
            const QString path = entry.value(QStringLiteral("path")).toString();
            if (path.isEmpty()) continue;
            m_aiChangedFiles.append(path);
            if (entry.value(QStringLiteral("untracked")).toBool()) {
                m_aiUntrackedFiles.append(path);
            } else {
                trackedPaths.append(path);
                const QString original = entry.value(QStringLiteral("originalPath")).toString();
                if (!original.isEmpty()) trackedPaths.append(original);
            }
        }
        m_aiChangedFiles.removeDuplicates();
        trackedPaths.removeDuplicates();
        if (m_aiChangedFiles.isEmpty()) {
            fail(tr("AI 提交说明"), tr("当前仓库没有改动文件。"), WorkflowErrorType::Ai);
            break;
        }
        if (trackedPaths.isEmpty()) {
            const QString preview = untrackedAiPreview(completed.workingDirectory, m_aiUntrackedFiles);
            if (preview.trimmed().isEmpty()) {
                fail(tr("AI 提交说明"), tr("未找到可供生成说明的文本改动。"), WorkflowErrorType::Ai);
                break;
            }
            m_commitAi.generate(repository.name, repository.branch, preview, m_aiChangedFiles,
                                m_aiExistingMessage, m_aiCommitType, m_aiIssueId, true,
                                m_aiManualCompiled, m_aiCompileNote, m_aiPurpose, m_aiSecondaryIssueId);
            break;
        }
        GitCommand diff;
        diff.program = QStringLiteral("git");
        diff.arguments = {QStringLiteral("--literal-pathspecs"), QStringLiteral("diff"), QStringLiteral("HEAD"),
                          QStringLiteral("--no-renames"), QStringLiteral("--no-ext-diff"),
                          QStringLiteral("--no-textconv"), QStringLiteral("--no-color"),
                          QStringLiteral("--unified=4"), QStringLiteral("--")};
        diff.arguments.append(trackedPaths);
        diff.workingDirectory = completed.workingDirectory;
        diff.repositoryIndex = completed.repositoryIndex;
        diff.repositoryPath = completed.repositoryPath;
        diff.title = tr("读取仓库 Diff");
        diff.kind = GitCommandKind::AiWorkspaceDiff;
        diff.diffRequest = completed.diffRequest;
        diff.operationId = completed.operationId;
        emit commandRequested(diff);
        break;
    }
    case GitCommandKind::AiWorkspaceDiff: {
        const QString preview = untrackedAiPreview(completed.workingDirectory, m_aiUntrackedFiles);
        const QString combined = output.left(6000) + preview;
        if (combined.trimmed().isEmpty()) {
            fail(tr("AI 提交说明"), tr("仓库 Diff 为空，未生成提交说明。"), WorkflowErrorType::Ai);
        } else {
            m_commitAi.generate(repository.name, repository.branch, combined, m_aiChangedFiles,
                                m_aiExistingMessage, m_aiCommitType, m_aiIssueId, true,
                                m_aiManualCompiled, m_aiCompileNote, m_aiPurpose, m_aiSecondaryIssueId);
        }
        break;
    }
    default: break;
    }
    return true;
}
