#pragma once

#include "gitstatusparser.h"
#include <QMap>

struct DiscardPlan {
    bool valid = false;
    QString rejectedPath;
    QStringList tracked;
    QStringList untracked;
};

// Plan the entire selection before allowing any mutation. Conflicts, stale paths
// and staged-only changes are never interpreted as untracked files.
inline DiscardPlan planDiscard(const QStringList &paths, const QVariantList &files)
{
    DiscardPlan plan;
    if (paths.isEmpty()) return plan;
    QMap<QString, QString> statuses;
    for (const auto &file : files) {
        const auto entry = file.toMap();
        statuses.insert(entry.value(QStringLiteral("path")).toString(),
                        entry.value(QStringLiteral("status")).toString());
    }
    for (const auto &path : paths) {
        const QString status = statuses.value(path);
        if (status.size() != 2 || GitStatusParser::isConflict(status)
            || status == QStringLiteral("!!")
            || (status != QStringLiteral("??") && status.at(1) == ' ')) {
            return {false, path, {}, {}};
        }
        if (status == QStringLiteral("??")) plan.untracked.append(path);
        else plan.tracked.append(path);
    }
    plan.tracked.removeDuplicates();
    plan.untracked.removeDuplicates();
    plan.valid = true;
    return plan;
}
