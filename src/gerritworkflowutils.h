#pragma once

#include "gerritservice.h"

namespace GerritWorkflow {
inline GerritPushOptions pushOptionsFromMap(const QVariantMap &map)
{
    GerritPushOptions options;
    options.topic = map.value(QStringLiteral("topic")).toString().trimmed();
    options.reviewers = GerritService::splitList(map.value(QStringLiteral("reviewers")).toString());
    options.cc = GerritService::splitList(map.value(QStringLiteral("cc")).toString());
    options.hashtags = GerritService::splitList(map.value(QStringLiteral("hashtags")).toString());
    const int workState = map.value(QStringLiteral("workState")).toInt();
    options.workState = workState == 1 ? GerritPushOptions::WorkInProgress
        : workState == 2 ? GerritPushOptions::ReadyForReview : GerritPushOptions::KeepWorkState;
    options.privateChange = map.value(QStringLiteral("private")).toBool();
    options.message = map.value(QStringLiteral("message")).toString().simplified();
    return options;
}

struct PendingCommits { int count = 0; QStringList missingChangeId; };
inline PendingCommits scanPendingCommits(const QString &output)
{
    static const QRegularExpression changeIdLine(
        QStringLiteral("(?m)^Change-Id:[ \\t]*I[0-9a-fA-F]{40}[ \\t]*$"));
    static const QRegularExpression commitHash(QStringLiteral("^[0-9a-fA-F]{40,64}$"));
    PendingCommits pending;
    const QStringList records = output.split(QChar(0));
    for (int i = 0; i + 1 < records.size(); i += 2) {
        const QString sha = records.at(i).trimmed();
        if (!commitHash.match(sha).hasMatch()) continue;
        ++pending.count;
        if (!changeIdLine.match(records.at(i + 1)).hasMatch())
            pending.missingChangeId.append(sha.left(10));
    }
    return pending;
}

inline bool isDetachedBranch(const QString &branch)
{
    return branch == QStringLiteral("(detached)") || branch == QStringLiteral("detached");
}

}
