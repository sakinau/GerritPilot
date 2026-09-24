#include "gitservice.h"
#include <QRegularExpression>
#include <utility>
#include <QUrl>

GitService::GitService(QObject *parent)
    : TaskReporter(parent)
{
    m_waitNotice.setInterval(15000);
    connect(&m_waitNotice, &QTimer::timeout, this, [this] {
        if (m_runner.running())
            emit logMessage(tr("仍在执行 · %1 · 已用 %2 秒（可取消）")
                .arg(m_current.title).arg(m_elapsed.elapsed() / 1000));
    });
    connect(&m_runner, &ProcessRunner::outputReceived,
            this, &GitService::onProcessOutput);
    connect(&m_runner, &ProcessRunner::finished,
            this, &GitService::onProcessFinished);
    connect(&m_runner, &ProcessRunner::failedToStart,
            this, &GitService::onProcessError);
}

bool GitService::busy() const
{
    return m_runner.running() || !m_queue.isEmpty();
}

void GitService::cancelOperation(quint64 id)
{
    GitCommand command;
    command.operationId = id;
    command.stopOnFailure = true;
    cancelDependentCommands(command);
}

QString GitService::displayCommand(const QString &program, const QStringList &arguments)
{
    QStringList escaped;
    for (QString value : arguments) {
        if (value.startsWith("https://") || value.startsWith("http://"))
            value = QUrl(value).toString(QUrl::RemoveUserInfo);
        if (value.contains(QRegularExpression(QStringLiteral("[\\s\"']")))) {
            value.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
            value = QLatin1Char('\'') + value + QLatin1Char('\'');
        }
        escaped.append(value);
    }
    return program + QLatin1Char(' ') + escaped.join(QLatin1Char(' '));
}

bool GitService::validateBranchName(const QString &branch, QString *error)
{
    const QString name = branch.trimmed();
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._/-]*$"));
    const bool invalid = name.isEmpty() || !pattern.match(name).hasMatch()
        || name.contains(QStringLiteral("..")) || name.contains(QStringLiteral("//"))
        || name.endsWith(QLatin1Char('/')) || name.endsWith(QLatin1Char('.'))
        || name.endsWith(QStringLiteral(".lock"));
    if (invalid && error)
        *error = QStringLiteral("分支名只能包含字母、数字、点、下划线、斜杠和连字符，且不能包含连续分隔符。");
    return !invalid;
}

bool GitService::validateTopicName(const QString &topic, QString *error)
{
    const QString name = topic.trimmed();
    if (name.isEmpty()) return true;
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]*$"));
    const bool valid = pattern.match(name).hasMatch();
    if (!valid && error)
        *error = QStringLiteral("Topic 只能包含字母、数字、点、下划线和连字符。");
    return valid;
}

void GitService::enqueue(GitCommand command)
{
    // Deduplicate preview/transient requests in the queue
    if (command.kind == GitCommandKind::RevisionFiles) {
        for (int i = m_queue.size() - 1; i >= 0; --i) {
            if (m_queue.at(i).kind == GitCommandKind::RevisionFiles)
                m_queue.removeAt(i);
        }
    } else if (command.kind == GitCommandKind::RevisionDiff) {
        for (int i = m_queue.size() - 1; i >= 0; --i) {
            if (m_queue.at(i).kind == GitCommandKind::RevisionDiff)
                m_queue.removeAt(i);
        }
    } else if (command.kind == GitCommandKind::Diff) {
        // A preview is replaceable; mutations and their status refreshes are not.
        for (int i = m_queue.size() - 1; i >= 0; --i) {
            if (m_queue.at(i).kind == GitCommandKind::Diff)
                m_queue.removeAt(i);
        }
    }

    m_queue.enqueue(std::move(command));
    emit busyChanged(busy());
    startNext();
}

void GitService::stop()
{
    ++m_currentGeneration;
    m_queue.clear();
    m_runner.cancel();
    m_activeTask.clear();
    emit activeTaskChanged(m_activeTask);
    emit busyChanged(busy());
    emit logMessage(QStringLiteral("操作已停止"));
}

void GitService::clearQueue()
{
    ++m_currentGeneration;
    m_queue.clear();
    if (!m_runner.running()) {
        m_activeTask.clear();
        emit activeTaskChanged(m_activeTask);
        emit busyChanged(busy());
    }
}

void GitService::startNext()
{
    if (m_runner.running() || m_queue.isEmpty()) {
        if (!m_runner.running() && m_queue.isEmpty()) {
            if (!m_activeTask.isEmpty()) {
                m_activeTask.clear();
                emit activeTaskChanged(m_activeTask);
            }
            emit busyChanged(busy());
        }
        return;
    }

    m_current = m_queue.dequeue();
    m_activeTask = m_current.title;

    const QString repoPrefix = !m_current.repositoryPath.isEmpty()
        ? QStringLiteral("[%1] ").arg(m_current.repositoryPath)
        : QString();
    const QString cmdText = QStringLiteral("› ") + repoPrefix + displayCommand(m_current.program, m_current.arguments);
    emit logMessage(cmdText);
    emit logMessage(tr("工作目录：%1").arg(m_current.workingDirectory));
    emit commandStarted(m_current);

    // Bound read-only queries with 2 minutes; general network/disk operations with 10 minutes safeguard
    int timeout = m_current.timeoutMilliseconds;
    if (timeout <= 0) {
        timeout = (m_current.kind != GitCommandKind::General) ? 120000 : 600000;
    }

    // Only status/diff/history and other structured queries need complete output.
    // Pull and general write operations already stream their log; retain a bounded
    // tail for diagnostics instead of keeping every progress line in memory.
    const bool boundedCapture = m_current.kind == GitCommandKind::PullExecution
        || m_current.kind == GitCommandKind::RepoSync
        || m_current.kind == GitCommandKind::General;
    m_runner.start(m_current.program, m_current.arguments, m_current.workingDirectory,
                   timeout, m_current.standardInput, boundedCapture ? 256 * 1024 : 0,
                   (m_current.kind == GitCommandKind::AiStagedDiff
                    || m_current.kind == GitCommandKind::AiWorkspaceDiff) ? 64 * 1024 : 0);
    m_elapsed.start();
    m_waitNotice.start();
    emit activeTaskChanged(m_activeTask);
    emit busyChanged(busy());
}

void GitService::onProcessOutput(const QByteArray &output, const QByteArray &errors)
{
    // Keep machine-readable -z output and large diffs out of the command panel.
    emit outputReceived((m_current.kind == GitCommandKind::General
                         || m_current.kind == GitCommandKind::RepoSync) ? output : QByteArray(), errors);
}

void GitService::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus,
                                   const QByteArray &output, const QByteArray &errors)
{
    m_waitNotice.stop();
    emit logMessage(tr("进程结束 · %1 · 退出码 %2 · %3 · 用时 %4 秒")
        .arg(m_current.title).arg(exitCode)
        .arg(exitStatus == QProcess::NormalExit ? tr("正常退出") : tr("异常/取消/超时"))
        .arg(m_elapsed.elapsed() / 1000));
    const GitCommand completed = m_current;
    if (completed.generation != m_currentGeneration) {
        GitCommandResult result;
        result.command = completed;
        result.status = ExecutionStatus::Canceled;
        emit commandFinished(result);
        startNext();
        return;
    }

    const bool success = exitStatus == QProcess::NormalExit
        && (exitCode == 0 || (completed.differenceExitCode && exitCode == 1
                             && errors.trimmed().isEmpty()));

    GitCommandResult result;
    result.command = completed;
    result.exitCode = exitCode;
    result.exitStatus = exitStatus;
    result.standardOutput = output;
    result.standardError = errors;

    if (success) {
        result.status = ExecutionStatus::Success;
        if (completed.kind == GitCommandKind::General)
            emit logMessage(QStringLiteral("✓ 完成 · %1").arg(completed.title));
    } else {
        result.status = ExecutionStatus::Failure;
        emit logMessage(QStringLiteral("✗ 失败 · %1").arg(completed.title));
        cancelDependentCommands(completed);
        const QString diagnostic = QString::fromLocal8Bit(errors);
        result.diagnosticMessage = diagnostic.isEmpty()
            ? (output.isEmpty() ? QStringLiteral("命令执行失败") : QString::fromLocal8Bit(output))
            : diagnostic;
    }

    emit commandFinished(result);
    startNext();
}

void GitService::onProcessError(const QString &reason)
{
    m_waitNotice.stop();
    const GitCommand failed = m_current;
    cancelDependentCommands(failed);

    GitCommandResult result;
    result.command = failed;
    result.status = ExecutionStatus::Failure;
    result.diagnosticMessage = QStringLiteral("无法启动 %1：%2").arg(failed.program, reason);
    emit logMessage(result.diagnosticMessage);

    emit commandFinished(result);

    m_activeTask.clear();
    emit activeTaskChanged(m_activeTask);
    emit busyChanged(busy());
    startNext();
}

void GitService::cancelDependentCommands(const GitCommand &command)
{
    if (!command.stopOnFailure || command.operationId == 0) return;
    for (int i = m_queue.size() - 1; i >= 0; --i) {
        if (m_queue.at(i).operationId == command.operationId)
            m_queue.removeAt(i);
    }
    emit dependentCommandsCanceled(command.operationId);
}
