#pragma once

#include "processrunner.h"
#include "taskreporter.h"

class WorkspaceController;

// Import and repo init only. Daily Git pull and repo sync belong to WorkspaceController.
class RepoProject final : public TaskReporter
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString log READ log NOTIFY changed)
    Q_PROPERTY(QString path READ path NOTIFY changed)
    Q_PROPERTY(bool sshConfigExists READ sshConfigExists NOTIFY changed)
    Q_PROPERTY(QString defaultRepoUrl READ defaultRepoUrl WRITE setDefaultRepoUrl NOTIFY defaultSettingsChanged)
    Q_PROPERTY(QString defaultManifestUrl READ defaultManifestUrl WRITE setDefaultManifestUrl NOTIFY defaultSettingsChanged)
    Q_PROPERTY(int progressPercent READ progressPercent NOTIFY progressChanged)
    Q_PROPERTY(QString progressStage READ progressStage NOTIFY progressChanged)
    Q_PROPERTY(QString progressDetail READ progressDetail NOTIFY progressChanged)
    Q_PROPERTY(QString progressText READ progressText NOTIFY progressChanged)
    Q_PROPERTY(bool hasProgress READ hasProgress NOTIFY progressChanged)
public:
    explicit RepoProject(WorkspaceController *workspace, QObject *parent = nullptr);
    bool busy() const override { return m_runner.running(); }
    QString activeTask() const override;
    QString log() const { return m_log; }
    QString path() const { return m_path; }
    bool sshConfigExists() const;
    int progressPercent() const { return m_progressPercent; }
    QString progressStage() const { return m_progressStage; }
    QString progressDetail() const { return m_progressDetail; }
    QString progressText() const { return m_progressText; }
    bool hasProgress() const { return busy() && m_progressPercent >= 0; }
    QString defaultRepoUrl() const;
    void setDefaultRepoUrl(const QString &url);
    QString defaultManifestUrl() const;
    void setDefaultManifestUrl(const QString &url);
    Q_INVOKABLE void importLocal(const QString &path);
    Q_INVOKABLE void initialize(const QString &path, const QString &url,
                               const QString &branch, const QString &manifest, const QString &repoUrl);
    Q_INVOKABLE void openManifest();
    Q_INVOKABLE void openSshConfig();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clearLog();
signals:
    void changed();
    void progressChanged();
    void defaultSettingsChanged();
    void projectCreated(const QString &name, const QString &path);
private:
    bool available();
    void append(const QString &text);
    void parseProgress(const QString &text);
    static QString stripAnsi(const QString &text);
    void start(const QStringList &arguments);
    WorkspaceController *m_workspace;
    ProcessRunner m_runner;
    QString m_path;
    QString m_log;
    bool m_initializing = false;
    int m_progressPercent = -1;
    QString m_progressStage;
    QString m_progressDetail;
    QString m_progressText;
    QString m_currentLine;
};
