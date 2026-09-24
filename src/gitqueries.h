#pragma once

#include <QStringList>

// Pure query definitions: no process execution, UI state or persistence.
namespace GitQueries {
inline QStringList files()
{
    return {QStringLiteral("status"), QStringLiteral("--porcelain=v1"),
            QStringLiteral("-z"), QStringLiteral("--untracked-files=all")};
}
inline QStringList summary()
{
    return {QStringLiteral("status"), QStringLiteral("--porcelain=v2"),
            QStringLiteral("--branch"), QStringLiteral("-z"), QStringLiteral("--untracked-files=all")};
}
inline QStringList commitScope()
{
    auto arguments = summary();
    // Local ignore settings must not conceal staged gitlinks from the guard.
    arguments.append(QStringLiteral("--ignore-submodules=none"));
    return arguments;
}
inline QStringList history(int limit = 30, int skip = 0, const QString &anchor = {})
{
    QStringList arguments{QStringLiteral("log"),
            QStringLiteral("--no-merges"),
            QStringLiteral("--pretty=format:%H%x00%an%x00%aI%x00%s%x00"),
            QStringLiteral("-%1").arg(qMax(1, limit)),
            QStringLiteral("--skip=%1").arg(qMax(0, skip))};
    if (!anchor.isEmpty()) arguments.append(anchor);
    arguments.append(QStringLiteral("--"));
    return arguments;
}
inline QStringList branches()
{
    return {QStringLiteral("for-each-ref"), QStringLiteral("--sort=-committerdate"),
            QStringLiteral("--format=%(refname)"), QStringLiteral("refs/heads"), QStringLiteral("refs/remotes")};
}
} // namespace GitQueries
