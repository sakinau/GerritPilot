#include "repositorywatcher.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QQueue>

RepositoryWatcher::RepositoryWatcher(QObject *parent)
    : QObject(parent)
{
    m_debounceTimer.setSingleShot(true);
    m_debounceTimer.setInterval(100);
    connect(&m_debounceTimer, &QTimer::timeout, this, [this] {
        if (!m_suspended)
            emit repositoryChanged();
    });

    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString &path) {
        // Re-add file in case of atomic replacement (e.g. editors replacing index, HEAD or source file)
        if (QFileInfo::exists(path)) {
            if (!m_watcher.files().contains(path))
                m_watcher.addPath(path);
        }
        if (m_suspended) {
            const QString gitDir = resolveGitDirectory(m_activeRepositoryPath);
            if (!gitDir.isEmpty() && path.startsWith(gitDir)) {
                // Internal git metadata touched during Git operation - ignore
                return;
            }
            m_pendingRefreshOnResume = true;
            return;
        }
        m_debounceTimer.start();
    });

    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString &path) {
        if (m_suspended) {
            const QString gitDir = resolveGitDirectory(m_activeRepositoryPath);
            if (!gitDir.isEmpty() && path.startsWith(gitDir)) {
                return;
            }
            // Directory changes during suspended operations are already covered by
            // WorkspaceController's operation completion refresh.
            return;
        }
        m_debounceTimer.start();
    });
}

QString RepositoryWatcher::resolveGitDirectory(const QString &repositoryPath) const
{
    if (repositoryPath.isEmpty()) return QString();
    const QString gitPath = QDir(repositoryPath).filePath(QStringLiteral(".git"));
    QFileInfo info(gitPath);
    if (info.isDir()) {
        return gitPath;
    }
    if (info.isFile()) {
        QFile file(gitPath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString content = QString::fromUtf8(file.readAll()).trimmed();
            if (content.startsWith(QStringLiteral("gitdir:"))) {
                const QString relOrAbs = content.mid(7).trimmed();
                return QDir(repositoryPath).absoluteFilePath(relOrAbs);
            }
        }
    }
    return QString();
}

static bool isIgnoredDirName(const QString &name)
{
    return name == QLatin1String(".git")
        || name == QLatin1String(".repo")
        || name.startsWith(QLatin1String("build"))
        || name == QLatin1String("node_modules")
        || name == QLatin1String(".cache")
        || name == QLatin1String(".idea")
        || name == QLatin1String(".vscode")
        || name == QLatin1String(".gradle")
        || name == QLatin1String(".cargo")
        || name == QLatin1String("target")
        || name == QLatin1String("dist")
        || name == QLatin1String("__pycache__");
}

void RepositoryWatcher::setupWatchPaths()
{
    if (m_activeRepositoryPath.isEmpty() || !QDir(m_activeRepositoryPath).exists())
        return;

    // Architectural Note: Working tree filesystem watching is intentionally bounded to
    // MaxTotalWatchedDirectories (40) and MaxDepth (4) to prevent inotify descriptor exhaustion
    // and unbounded CPU/memory overhead on large monorepos / Android multi-gigabyte trees.
    // Changes in deep paths (> 4 levels) or beyond the 40 directory threshold are not guaranteed
    // to trigger filesystem notifications. Full state reconciliation is handled by:
    // (1) Application window re-activation via requestActiveRefresh() in App.qml
    // (2) Explicit user manual refresh (Ctrl+R / F5 / header & sidebar refresh buttons)
    // (3) Preflight checks before Git write operations (commit, stage, discard, push)
    constexpr int MaxTotalWatchedDirectories = 40;
    constexpr int MaxDepth = 4;

    const QStringList existingFiles = m_watcher.files();
    QStringList existingDirs = m_watcher.directories();

    // 1. Resolve and watch Git metadata
    const QString gitDirStr = resolveGitDirectory(m_activeRepositoryPath);
    if (!gitDirStr.isEmpty() && QDir(gitDirStr).exists()) {
        const QDir gitDir(gitDirStr);
        const QString headFile = gitDir.filePath(QStringLiteral("HEAD"));
        if (QFileInfo::exists(headFile) && !existingFiles.contains(headFile))
            m_watcher.addPath(headFile);

        const QString indexFile = gitDir.filePath(QStringLiteral("index"));
        if (QFileInfo::exists(indexFile) && !existingFiles.contains(indexFile))
            m_watcher.addPath(indexFile);

        const QString refsHeadsDir = gitDir.filePath(QStringLiteral("refs/heads"));
        if (QDir(refsHeadsDir).exists() && !existingDirs.contains(refsHeadsDir)
            && m_watcher.directories().size() < MaxTotalWatchedDirectories) {
            m_watcher.addPath(refsHeadsDir);
            existingDirs.append(refsHeadsDir);
        }

        // If worktree .git file exists, also watch it
        const QString dotGitFile = QDir(m_activeRepositoryPath).filePath(QStringLiteral(".git"));
        if (QFileInfo(dotGitFile).isFile() && !existingFiles.contains(dotGitFile))
            m_watcher.addPath(dotGitFile);
    }

    // 2. BFS working tree watching up to MaxDepth and MaxTotalWatchedDirectories
    if (!existingDirs.contains(m_activeRepositoryPath)
        && m_watcher.directories().size() < MaxTotalWatchedDirectories) {
        m_watcher.addPath(m_activeRepositoryPath);
        existingDirs.append(m_activeRepositoryPath);
    }

    struct DirNode {
        QString path;
        int depth;
    };
    QQueue<DirNode> queue;
    queue.enqueue({m_activeRepositoryPath, 0});

    while (!queue.isEmpty() && m_watcher.directories().size() < MaxTotalWatchedDirectories) {
        const DirNode current = queue.dequeue();
        if (current.depth >= MaxDepth)
            continue;

        QDir dir(current.path);
        const QFileInfoList subdirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable);
        for (const QFileInfo &sub : subdirs) {
            if (m_watcher.directories().size() >= MaxTotalWatchedDirectories)
                break;
            const QString subName = sub.fileName();
            if (isIgnoredDirName(subName))
                continue;

            const QString subPath = sub.absoluteFilePath();
            if (!existingDirs.contains(subPath)) {
                m_watcher.addPath(subPath);
                existingDirs.append(subPath);
            }
            if (current.depth + 1 < MaxDepth) {
                queue.enqueue({subPath, current.depth + 1});
            }
        }
    }
}

void RepositoryWatcher::setActiveRepository(const QString &absolutePath)
{
    if (m_activeRepositoryPath == absolutePath)
        return;
    m_debounceTimer.stop();
    m_pendingRefreshOnResume = false;
    const QStringList existingFiles = m_watcher.files();
    if (!existingFiles.isEmpty())
        m_watcher.removePaths(existingFiles);
    const QStringList existingDirs = m_watcher.directories();
    if (!existingDirs.isEmpty())
        m_watcher.removePaths(existingDirs);

    m_activeRepositoryPath = absolutePath;
    setupWatchPaths();
}

void RepositoryWatcher::setSuspended(bool suspended)
{
    if (m_suspended == suspended)
        return;
    m_suspended = suspended;
    if (suspended) {
        m_debounceTimer.stop();
    } else {
        setupWatchPaths();
        if (m_pendingRefreshOnResume) {
            m_pendingRefreshOnResume = false;
            m_debounceTimer.start();
        }
    }
}
