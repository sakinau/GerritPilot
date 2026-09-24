#pragma once

#include "processrunner.h"
#include "taskreporter.h"
#include <QVariantMap>

class WorkspaceController;

// Project lifecycle is separate from daily per-repository Git operations.
class RepoProject final : public TaskReporter
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString log READ log NOTIFY changed)
    Q_PROPERTY(QString path READ path NOTIFY changed)
    Q_PROPERTY(bool sshConfigExists READ sshConfigExists NOTIFY changed)
    Q_PROPERTY(QString defaultRepoUrl READ defaultRepoUrl WRITE setDefaultRepoUrl NOTIFY defaultSettingsChanged)
    Q_PROPERTY(QString defaultManifestUrl READ defaultManifestUrl WRITE setDefaultManifestUrl NOTIFY defaultSettingsChanged)
    Q_PROPERTY(QString defaultBaseDir READ defaultBaseDir WRITE setDefaultBaseDir NOTIFY defaultSettingsChanged)
    Q_PROPERTY(bool quickCreating READ quickCreating NOTIFY changed)
    Q_PROPERTY(QString currentCustomerXml READ currentCustomerXml NOTIFY changed)
    Q_PROPERTY(QString currentHmiRevision READ currentHmiRevision NOTIFY changed)
    Q_PROPERTY(QStringList remoteHmiBranches READ remoteHmiBranches NOTIFY remoteHmiBranchesChanged)
    Q_PROPERTY(bool indexingBranches READ indexingBranches NOTIFY indexingBranchesChanged)
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
    bool quickCreating() const { return m_quickCreating; }

    int progressPercent() const { return m_progressPercent; }
    QString progressStage() const { return m_progressStage; }
    QString progressDetail() const { return m_progressDetail; }
    QString progressText() const { return m_progressText; }
    bool hasProgress() const { return busy() && m_progressPercent >= 0; }

    QString defaultRepoUrl() const;
    void setDefaultRepoUrl(const QString &url);
    QString defaultManifestUrl() const;
    void setDefaultManifestUrl(const QString &url);
    QString defaultBaseDir() const;
    void setDefaultBaseDir(const QString &dir);

    QString currentCustomerXml() const;
    QString currentHmiRevision() const;
    QStringList remoteHmiBranches() const { return m_remoteHmiBranches; }
    bool indexingBranches() const { return m_indexingBranches; }

    Q_INVOKABLE void fetchRemoteHmiBranches();
    Q_INVOKABLE QVariantMap autoResolvePlatform(const QString &keyword) const;
    Q_INVOKABLE void smartOneClickPull(const QString &platformOrBranch, const QString &customPath = QString(), bool hmiOnly = true);
    Q_INVOKABLE void smartDiagnoseAndFixCurrentProject(bool syncImmediately = true, bool hmiOnly = true);
    Q_INVOKABLE void aiDiagnoseAndRepair(const QString &instruction = QString());

    Q_INVOKABLE void importLocal(const QString &path);
    Q_INVOKABLE void initialize(const QString &path, const QString &url,
                                const QString &branch, const QString &manifest, const QString &repoUrl);
    Q_INVOKABLE void quickCreateProject(const QString &projectName, const QString &projectPath,
                                        const QString &manifestUrl, const QString &manifestBranch,
                                        const QString &manifestFile, const QString &repoUrl,
                                        const QString &hmiBranch, const QString &customModuleOverrides,
                                        bool autoSync, bool syncHmiOnly);
    Q_INVOKABLE QString findCustomerManifest(const QString &basePath, const QString &projectName = QString()) const;
    Q_INVOKABLE bool updateModuleRevision(const QString &manifestPath, const QString &moduleNameOrPath, const QString &newRevision);
    Q_INVOKABLE QVariantMap inspectCustomerManifest(const QString &basePath = QString()) const;
    Q_INVOKABLE bool setHmiBranchForProject(const QString &basePath, const QString &hmiBranch);
    Q_INVOKABLE bool applyHmiBranchAndSync(const QString &hmiBranch);
    Q_INVOKABLE void syncModule(const QString &moduleName);
    Q_INVOKABLE QString getCustomerTemplate(const QString &platformKey, const QString &customHmiBranch = QString()) const;
    Q_INVOKABLE bool repairCustomerManifest(const QString &basePath, const QString &platformKey, const QString &customHmiBranch = QString());
    Q_INVOKABLE QString readCustomerManifest(const QString &basePath = QString()) const;
    Q_INVOKABLE bool writeRawCustomerManifest(const QString &basePath, const QString &xmlContent);
    Q_INVOKABLE void repairAndSync(const QString &platformKey, const QString &customHmiBranch = QString(), bool hmiOnly = false);
    Q_INVOKABLE void openCustomerManifest(const QString &basePath = QString());
    Q_INVOKABLE void openManifest();
    Q_INVOKABLE void openSshConfig();
    Q_INVOKABLE void synchronize(bool reviewed) { synchronize(reviewed, 8, true); }
    Q_INVOKABLE void synchronize(bool reviewed, int jobs) { synchronize(reviewed, jobs, true); }
    Q_INVOKABLE void synchronize(bool reviewed, int jobs, bool noManifestUpdate);
    Q_INVOKABLE void switchManifestBranch(const QString &branch, const QString &manifest = QString());
    Q_INVOKABLE void checkStatus();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clearLog();
signals:
    void changed();
    void progressChanged();
    void defaultSettingsChanged();
    void remoteHmiBranchesChanged();
    void indexingBranchesChanged();
    void manifestRepaired(const QString &xmlPath, const QString &summary);
    void projectCreated(const QString &name, const QString &path);
private:
    bool available();
    void append(const QString &text);
    void parseProgress(const QString &text);
    static QString stripAnsi(const QString &text);
    void start(const QStringList &arguments, bool syncing);
    void finishQuickCreate();

    WorkspaceController *m_workspace;
    ProcessRunner m_runner;
    ProcessRunner m_branchIndexer;
    QString m_path;
    QString m_log;
    bool m_syncing = false;
    bool m_initializing = false;

    // Real-time progress state
    int m_progressPercent = -1;
    QString m_progressStage;
    QString m_progressDetail;
    QString m_progressText;
    QString m_currentLine;

    // Remote branches cache
    QStringList m_remoteHmiBranches;
    bool m_indexingBranches = false;

    // Quick-create state
    bool m_quickCreating = false;
    bool m_quickCreatingSync = false;
    QString m_quickProjectName;
    QString m_quickHmiBranch;
    QString m_quickCustomOverrides;
    bool m_quickAutoSync = false;
    bool m_quickSyncHmiOnly = false;
};
