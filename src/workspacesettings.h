#pragma once

#include "repositorycache.h"
#include <QDir>
#include <QRegularExpression>
#include <QSettings>
#include <QString>
#include <QStringList>

class WorkspaceSettings
{
public:
    struct RepositoryConfig {
        QString remote = QStringLiteral("origin");
        QString targetBranch;
        bool targetBranchConfirmed = false;
        QString topic;
        QString gerritHost;
        QString gerritUser;
        int gerritPort = 29418;
    };

    static QString hiddenRepositoriesKey(const QString &workspace)
    {
        return QStringLiteral("workspaces/") + RepositoryCache::key(workspace)
            + QStringLiteral("/ignoredRepositories");
    }

    static QString repositorySettingsKey(const QString &workspace, const QString &repository)
    {
        return QStringLiteral("repositorySettings/") + RepositoryCache::key(workspace, repository) + '/';
    }

    static QString draftKey(const QString &workspace, const QString &repository)
    {
        return QStringLiteral("drafts/") + RepositoryCache::key(workspace, repository);
    }

    static void migrateLegacySettings(const QString &workspace)
    {
        QSettings settings;
        if (!settings.value(QStringLiteral("repositorySettingsMigrated"), false).toBool()) {
            const QString previousRepository = settings.value(QStringLiteral("cacheSelection/")
                + RepositoryCache::key(workspace)).toString();
            if (!previousRepository.isEmpty()) {
                const QString prefix = repositorySettingsKey(workspace, previousRepository);
                for (const QString &key : {QStringLiteral("remote"), QStringLiteral("targetBranch"),
                        QStringLiteral("topic"), QStringLiteral("gerritHost"),
                        QStringLiteral("gerritPort"), QStringLiteral("gerritUser")}) {
                    if (settings.contains(key) && !settings.contains(prefix + key))
                        settings.setValue(prefix + key, settings.value(key));
                }
            }
            settings.setValue(QStringLiteral("repositorySettingsMigrated"), true);
        }

        const QString hiddenKey = hiddenRepositoriesKey(workspace);
        if (!settings.value(QStringLiteral("hiddenRepositoriesMigrated"), false).toBool()) {
            if (!settings.contains(hiddenKey))
                settings.setValue(hiddenKey, settings.value(QStringLiteral("ignoredRepositories")));
            settings.setValue(QStringLiteral("hiddenRepositoriesMigrated"), true);
        }
    }

    static RepositoryConfig loadRepositoryConfig(const QString &workspace, const QString &repository)
    {
        QSettings settings;
        RepositoryConfig config;
        const QString prefix = repositorySettingsKey(workspace, repository);
        config.remote = settings.value(prefix + QStringLiteral("remote"), QStringLiteral("origin")).toString();
        // Old releases copied the last target from other repositories into this key.
        // Require a fresh confirmation in the push dialog before trusting it.
        config.targetBranchConfirmed = settings.value(prefix + QStringLiteral("targetBranchConfirmed"), false).toBool();
        if (config.targetBranchConfirmed)
            config.targetBranch = settings.value(prefix + QStringLiteral("targetBranch")).toString().trimmed();
        config.topic = settings.value(prefix + QStringLiteral("topic")).toString();
        config.gerritHost = settings.value(prefix + QStringLiteral("gerritHost")).toString();
        config.gerritPort = settings.value(prefix + QStringLiteral("gerritPort"), 29418).toInt();
        config.gerritUser = settings.value(prefix + QStringLiteral("gerritUser"),
                                           QString::fromLocal8Bit(qgetenv("USER"))).toString();
        return config;
    }

    static void saveRepositoryConfig(const QString &workspace, const QString &repository,
                                     const RepositoryConfig &config)
    {
        if (repository.isEmpty()) return;
        QSettings settings;
        const QString prefix = repositorySettingsKey(workspace, repository);
        settings.setValue(prefix + QStringLiteral("remote"), config.remote);
        if (config.targetBranchConfirmed) {
            settings.setValue(prefix + QStringLiteral("targetBranch"), config.targetBranch.trimmed());
            settings.setValue(prefix + QStringLiteral("targetBranchConfirmed"), true);
        }
        settings.setValue(prefix + QStringLiteral("topic"), config.topic);
        settings.setValue(prefix + QStringLiteral("gerritHost"), config.gerritHost);
        settings.setValue(prefix + QStringLiteral("gerritPort"), config.gerritPort);
        settings.setValue(prefix + QStringLiteral("gerritUser"), config.gerritUser);
    }

    static QStringList loadIgnoredRepositories(const QString &workspace)
    {
        QSettings settings;
        return settings.value(hiddenRepositoriesKey(workspace)).toStringList();
    }

    static void saveIgnoredRepositories(const QString &workspace, const QStringList &ignored)
    {
        QSettings settings;
        settings.setValue(hiddenRepositoriesKey(workspace), ignored);
    }

    static QString loadDraft(const QString &workspace, const QString &repository)
    {
        if (workspace.isEmpty() || repository.isEmpty()) return {};
        QSettings settings;
        return settings.value(draftKey(workspace, repository)).toString();
    }

    static void saveDraft(const QString &workspace, const QString &repository, const QString &draft)
    {
        if (workspace.isEmpty() || repository.isEmpty()) return;
        QSettings settings;
        const QString key = draftKey(workspace, repository);
        if (draft.trimmed().isEmpty()) {
            settings.remove(key);
        } else {
            settings.setValue(key, draft);
        }
    }

    static void clearDraft(const QString &workspace, const QString &repository)
    {
        if (workspace.isEmpty() || repository.isEmpty()) return;
        QSettings settings;
        settings.remove(draftKey(workspace, repository));
    }

    // Deduce Gerrit connection settings (host, port, user) from a Git remote URL
    static void deduceFromRemoteUrl(const QString &url, RepositoryConfig &config)
    {
        if (url.isEmpty()) return;
        // Match ssh://[user@]host[:port]/path
        static const QRegularExpression sshUrl(QStringLiteral(R"(^ssh://(?:([^@]+)@)?([^:/]+)(?::(\d+))?/)"));
        auto match = sshUrl.match(url);
        if (match.hasMatch()) {
            if (config.gerritUser.isEmpty() && match.capturedLength(1) > 0)
                config.gerritUser = match.captured(1);
            if (config.gerritHost.isEmpty() && match.capturedLength(2) > 0)
                config.gerritHost = match.captured(2);
            if (match.capturedLength(3) > 0)
                config.gerritPort = match.captured(3).toInt();
            return;
        }

        // Match scp-like syntax [user@]host:path
        static const QRegularExpression scpUrl(QStringLiteral(R"(^(?:([^@]+)@)?([^:/]+):)"));
        match = scpUrl.match(url);
        if (match.hasMatch()) {
            if (config.gerritUser.isEmpty() && match.capturedLength(1) > 0)
                config.gerritUser = match.captured(1);
            if (config.gerritHost.isEmpty() && match.capturedLength(2) > 0)
                config.gerritHost = match.captured(2);
        }
    }
};
