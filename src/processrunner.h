#pragma once

#include <QObject>
#include <QProcess>
#include <QTimer>

// Executes one process. No Git knowledge, repository state, queue or UI policy.
class ProcessRunner final : public QObject
{
    Q_OBJECT
public:
    explicit ProcessRunner(QObject *parent = nullptr);
    bool running() const { return m_running; }
    // maxOutputPrefixBytes keeps only the beginning of stdout for bounded previews;
    // maxCapturedBytes retains a diagnostic tail for other commands and stderr.
    bool start(const QString &program, const QStringList &arguments, const QString &directory,
               int timeoutMilliseconds = 0, const QByteArray &standardInput = {},
               int maxCapturedBytes = 0, int maxOutputPrefixBytes = 0);
    void cancel();

signals:
    void outputReceived(const QByteArray &standardOutput, const QByteArray &standardError);
    void finished(int exitCode, QProcess::ExitStatus exitStatus,
                  const QByteArray &standardOutput, const QByteArray &standardError);
    void failedToStart(const QString &reason);

private:
    void drainOutput();
    QProcess m_process;
    QTimer m_deadline;
    QByteArray m_output;
    QByteArray m_errors;
    int m_maxCapturedBytes = 0;
    int m_maxOutputPrefixBytes = 0;
    bool m_running = false;
    bool m_cancelRequested = false;
};
