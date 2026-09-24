#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QSet>

namespace GitStatusParser {

inline bool isConflict(const QString &status)
{
    return status == QStringLiteral("DD") || status == QStringLiteral("AU")
        || status == QStringLiteral("UD") || status == QStringLiteral("UA")
        || status == QStringLiteral("DU") || status == QStringLiteral("AA")
        || status == QStringLiteral("UU");
}

// porcelain v1 -z preserves whitespace and reports rename destination first.
inline QVariantList parse(const QString &output)
{
    QVariantList files;
    const QStringList records = output.split(QChar(0), Qt::KeepEmptyParts);
    for (int i = 0; i < records.size(); ++i) {
        const QString &record = records.at(i);
        if (record.size() < 4 || record.at(2) != QLatin1Char(' '))
            continue;
        const QString status = record.left(2);
        QString originalPath;
        if (status.contains('R') || status.contains('C')) {
            if (i + 1 >= records.size() || records.at(i + 1).isEmpty())
                continue;
            originalPath = records.at(++i);
        }
        const bool conflict = isConflict(status);
        const bool untracked = status == QStringLiteral("??");
        const bool ignored = status == QStringLiteral("!!");
        files.append(QVariantMap{
            {QStringLiteral("status"), status},
            {QStringLiteral("path"), record.mid(3)},
            {QStringLiteral("originalPath"), originalPath},
            {QStringLiteral("conflict"), conflict},
            {QStringLiteral("untracked"), untracked},
            {QStringLiteral("hasStagedChanges"), !conflict && !untracked && !ignored && status.at(0) != ' '},
            {QStringLiteral("hasUnstagedChanges"), !conflict && !ignored && status.at(1) != ' '}
        });
    }
    return files;
}

struct Snapshot {
    QString branch;
    bool unborn = false;
    int ahead = 0;
    int behind = 0;
    QVariantList files;
};

// porcelain v2 -z: headers and paths are NUL-delimited. Only the fixed
// metadata prefix is split on spaces; whitespace inside filenames is data.
inline Snapshot parseSnapshot(const QString &output)
{
    Snapshot result;
    QStringList fileRecords;
    QSet<QString> gitlinks;
    const auto records = output.split(QChar(0), Qt::KeepEmptyParts);
    for (int i = 0; i < records.size(); ++i) {
        const auto &record = records.at(i);
        if (record.startsWith("# branch.head ")) {
            result.branch = record.mid(14);
        } else if (record.startsWith("# branch.oid ")) {
            result.unborn = record.mid(13) == "(initial)";
        } else if (record.startsWith("# branch.ab ")) {
            const auto counts = record.mid(12).split(' ');
            if (counts.size() == 2) {
                result.ahead = qMax(0, counts[0].toInt());
                result.behind = qMax(0, -counts[1].toInt());
            }
        } else if (record.startsWith("? ")) {
            fileRecords.append("?? " + record.mid(2));
        } else if (record.startsWith("1 ") || record.startsWith("2 ") || record.startsWith("u ")) {
            const int fields = record[0] == '1' ? 8 : record[0] == '2' ? 9 : 10;
            int pathOffset = 0;
            for (int field = 0; field < fields; ++field) {
                const int space = record.indexOf(' ', pathOffset);
                if (space < 0) { pathOffset = -1; break; }
                pathOffset = space + 1;
            }
            if (pathOffset < 0 || pathOffset >= record.size()) continue;
            // Preserve repository-boundary metadata, including deleted gitlinks.
            const auto metadata = record.left(pathOffset - 1).split(' ');
            if (metadata.mid(3).contains(QStringLiteral("160000")))
                gitlinks.insert(record.mid(pathOffset));
            if (record[0] == '2' && (i + 1 >= records.size() || records[i + 1].isEmpty())) continue;
            QString status = record.mid(2, 2);
            status.replace('.', ' ');
            fileRecords.append(status + ' ' + record.mid(pathOffset));
            if (record[0] == '2') fileRecords.append(records.at(++i));
        }
    }
    result.files = parse(fileRecords.join(QChar(0)) + QChar(0));
    for (auto &file : result.files) {
        auto entry = file.toMap();
        entry.insert(QStringLiteral("gitlink"), gitlinks.contains(entry.value(QStringLiteral("path")).toString()));
        file = entry;
    }
    return result;
}

} // namespace GitStatusParser
