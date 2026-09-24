#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QRegularExpression>

namespace GitHistoryParser {
// Four NUL-separated fields per commit. Git inserts a newline between records;
// strip that separator only from the hash, never from the author or subject.
inline QVariantList parse(const QString &output)
{
    QVariantList commits;
    const auto fields = output.split(QChar(0));
    static const QRegularExpression hash(QStringLiteral("^(?:[0-9a-fA-F]{40}|[0-9a-fA-F]{64})$"));
    for (int i = 0; i + 3 < fields.size(); i += 4) {
        const QString revision = fields[i].trimmed();
        if (!hash.match(revision).hasMatch()) continue;
        commits.append(QVariantMap{{QStringLiteral("revision"), revision},
            {QStringLiteral("shortRevision"), revision.left(10)},
            {QStringLiteral("author"), fields[i + 1]},
            {QStringLiteral("date"), fields[i + 2]},
            {QStringLiteral("subject"), fields[i + 3]}});
    }
    return commits;
}
}
