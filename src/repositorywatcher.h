#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QString>
#include <QTimer>

class RepositoryWatcher final : public QObject
{
    Q_OBJECT
public:
    explicit RepositoryWatcher(QObject *parent = nullptr);

    void setActiveRepository(const QString &absolutePath);
    QString activeRepository() const { return m_activeRepositoryPath; }

    void setSuspended(bool suspended);
    bool isSuspended() const { return m_suspended; }

signals:
    void repositoryChanged();

private:
    void setupWatchPaths();
    QString resolveGitDirectory(const QString &repositoryPath) const;

    QFileSystemWatcher m_watcher;
    QTimer m_debounceTimer;
    QString m_activeRepositoryPath;
    bool m_suspended = false;
    bool m_pendingRefreshOnResume = false;
};
