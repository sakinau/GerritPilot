#pragma once

#include "gitservice.h"
#include <QHash>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QObject>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

class GerritService final : public QObject
{
    Q_OBJECT
public:
    explicit GerritService(QObject *parent = nullptr);


    // Commit Hook Management
    GitCommand createQueryHookPathCommand(int repoIndex, const QString &repoAbsPath,
                                         const QString &repoRelPath, GitCommandKind kind) const;
    static bool hookSourceFromRemoteUrl(const QString &url, QString *source,
                                        QString *port, QString *error);
    GitCommand createQueryHookRemoteCommand(int repoIndex, const QString &repoAbsPath,
                                             const QString &repoRelPath,
                                             const QString &remote) const;
    GitCommand createInstallHookCommand(const GitCommand &completedQueryCommand,
                                        const QString &hookPath, const QString &temporaryPath) const;
    static void finalizeHookPermissions(const QString &hookPath);
    // HTTP(S) remotes download /tools/hooks/commit-msg instead of using scp.
    static bool isHttpHookSource(const QString &source);
    void downloadHook(const GitCommand &context, const QString &hookPath, const QString &temporaryPath);
    void cancelHookDownload();

    // Remote Branches & Push target
    GitCommand createQueryRemoteBranchesCommand(int repoIndex, const QString &repoAbsPath,
                                                const QString &repoRelPath,
                                                const QString &remote) const;
    static QStringList splitList(const QString &text);
    static bool validatePushOptions(const GerritPushOptions &options, QString *error);
    static QString formatReviewRef(const QString &targetBranch, const GerritPushOptions &options);
    static QString pushPreview(const QString &remote, const QString &targetBranch,
                               const GerritPushOptions &options);
    GitCommand createPushCommand(int repoIndex, const QString &repoAbsPath,
                                 const QString &repoRelPath, const QString &remote,
                                 const QString &targetBranch, const GerritPushOptions &options) const;

    // Change download: accepts a number, number/patchset, refs/changes/... or a review URL.
    static bool parseChangeSpec(const QString &spec, int *change, int *patchset, QString *error);
    static QString changeRef(int change, int patchset);
    static int latestPatchset(const QString &lsRemoteOutput, int change);

    // Repo manifest: the review branch of each project, keyed by checkout path.
    struct ManifestTarget {
        QString remote;
        QString branch;   // Empty when the manifest pins a tag or SHA.
        QString revision; // Raw value shown when no branch can be derived.
    };
    static QHash<QString, ManifestTarget> parseManifestTargets(const QByteArray &xml);
    static QString branchFromRevision(const QString &revision);

    // Output parsing and diagnostics
    static QString parseGerritError(const QByteArray &stderrData, const QString &defaultDiagnostic);
    static QString extractReviewUrl(const QByteArray &stdoutData, const QByteArray &stderrData);
    static QString credentialHint(const QByteArray &stderrData);

    // SSH Configuration check and template
    static bool checkSshConfigured(const QString &host);
    static QString sshConfigTemplate(const QString &username,
                                     const QString &host,
                                     int port = 29418);

signals:
    void hookDownloadFinished(const GitCommand &context, bool success, const QString &error);

private:
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_hookReply;
};
