#pragma once

#include "gitservice.h"
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

    // Remote Branches & Push target
    GitCommand createQueryRemoteBranchesCommand(int repoIndex, const QString &repoAbsPath,
                                                const QString &repoRelPath,
                                                const QString &remote) const;
    static QString formatReviewRef(const QString &targetBranch, const QString &topic);
    static QString pushPreview(const QString &remote, const QString &targetBranch, const QString &topic);
    GitCommand createPushCommand(int repoIndex, const QString &repoAbsPath,
                                 const QString &repoRelPath, const QString &remote,
                                 const QString &targetBranch, const QString &topic) const;

    // Output parsing and diagnostics
    static QString parseGerritError(const QByteArray &stderrData, const QString &defaultDiagnostic);
    static QString extractReviewUrl(const QByteArray &stdoutData, const QByteArray &stderrData);

    // SSH Configuration check and template
    static bool checkSshConfigured(const QString &host);
    static QString sshConfigTemplate(const QString &username,
                                     const QString &host,
                                     int port = 29418);

};
