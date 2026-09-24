#include "processrunner.h"
#ifdef Q_OS_UNIX
#include <signal.h>
#endif

namespace {
void appendCaptured(QByteArray &buffer, const QByteArray &chunk, int limit)
{
    if (limit <= 0) {
        buffer += chunk;
    } else if (chunk.size() >= limit) {
        buffer = chunk.right(limit);
    } else {
        buffer += chunk;
        if (buffer.size() > limit)
            buffer.remove(0, buffer.size() - limit);
    }
}
}

ProcessRunner::ProcessRunner(QObject *parent) : QObject(parent)
{
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    // No terminal is available: surface authentication failures instead of
    // leaving an invisible askpass dialog waiting. Keep custom SSH commands intact.
    env.insert(QStringLiteral("GIT_ASKPASS"), QStringLiteral("/bin/false"));
    env.insert(QStringLiteral("SSH_ASKPASS"), QStringLiteral("/bin/false"));
    env.insert(QStringLiteral("SSH_ASKPASS_REQUIRE"), QStringLiteral("never"));
    m_process.setProcessEnvironment(env);
#ifdef Q_OS_UNIX
    m_process.setUnixProcessParameters(QProcess::UnixProcessFlag::CreateNewSession);
#endif
    connect(&m_process, &QProcess::started, this, [this] {
        // A cancel requested during Starting must also reach the new process group.
        if (m_cancelRequested) cancel();
    });
    m_deadline.setSingleShot(true);
    connect(&m_deadline, &QTimer::timeout, this, [this] {
        if (!m_running || m_process.state() == QProcess::NotRunning) return;
        const QByteArray diagnostic = tr("Process timed out after %1 ms.\n")
            .arg(m_deadline.interval()).toLocal8Bit();
        appendCaptured(m_errors, diagnostic, m_maxCapturedBytes);
        emit outputReceived({}, diagnostic);
        cancel();
    });
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &ProcessRunner::drainOutput);
    connect(&m_process, &QProcess::readyReadStandardError, this, &ProcessRunner::drainOutput);
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this](int code, QProcess::ExitStatus status) {
            m_deadline.stop();
            drainOutput();
            // Completion handlers may synchronously launch the next process.
            const QByteArray output = m_output;
            const QByteArray errors = m_errors;
            QMetaObject::invokeMethod(this, [this, code, status, output, errors] {
                m_running = false;
                emit finished(code, status, output, errors);
            }, Qt::QueuedConnection);
        });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        m_deadline.stop();
        const QString reason = m_process.errorString();
        // QProcess is still unwinding its failed-start path in this signal.
        // Notify clients next turn so a reentrant start cannot be overwritten.
        QMetaObject::invokeMethod(this, [this, reason] {
            m_running = false;
            emit failedToStart(reason);
        }, Qt::QueuedConnection);
    });
}

bool ProcessRunner::start(const QString &program, const QStringList &arguments, const QString &directory,
                          int timeoutMilliseconds, const QByteArray &standardInput,
                          int maxCapturedBytes, int maxOutputPrefixBytes)
{
    if (m_running) return false;
    m_output.clear();
    m_errors.clear();
    m_maxCapturedBytes = qMax(0, maxCapturedBytes);
    m_maxOutputPrefixBytes = qMax(0, maxOutputPrefixBytes);
    m_running = true;
    m_cancelRequested = false;
    if (timeoutMilliseconds > 0) m_deadline.start(timeoutMilliseconds);
    else m_deadline.stop();
    m_process.setWorkingDirectory(directory);
    m_process.start(program, arguments);
    if (!standardInput.isEmpty()) {
        m_process.write(standardInput);
    }
    m_process.closeWriteChannel();
    return true;
}

void ProcessRunner::cancel()
{
    m_deadline.stop();
    if (!m_running) return;
    m_cancelRequested = true;
    // Only signal a group whose leader is our currently running child. Never
    // use zero (the application's own process group) or a cached/reused PID.
    if (m_process.state() == QProcess::NotRunning) return;
#ifdef Q_OS_UNIX
    const qint64 child = m_process.processId();
    if (child > 1 && ::kill(-static_cast<pid_t>(child), SIGKILL) == 0) return;
#endif
    m_process.kill();
}

void ProcessRunner::drainOutput()
{
    const QByteArray output = m_process.readAllStandardOutput();
    const QByteArray errors = m_process.readAllStandardError();
    if (m_maxOutputPrefixBytes > 0) {
        const int available = m_maxOutputPrefixBytes - m_output.size();
        if (available > 0) m_output += output.left(available);
    } else {
        appendCaptured(m_output, output, m_maxCapturedBytes);
    }
    appendCaptured(m_errors, errors, m_maxCapturedBytes);
    if (!output.isEmpty() || !errors.isEmpty()) emit outputReceived(output, errors);
}
