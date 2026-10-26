#pragma once

#include <QList>
#include <QRegularExpression>
#include <QString>

struct DiffRange {
    qsizetype start = 0;
    qsizetype end = 0;
};

struct DiffIndex {
    qsizetype headerEnd = 0;
    bool isBinary = false;
    QList<DiffRange> hunks;
};

class DiffModel
{
public:
    // Metadata only: no split line list, code copies, or per-hunk patch strings.
    static DiffIndex index(const QString &text)
    {
        DiffIndex result;
        result.isBinary = text.contains(QStringLiteral("Binary files "))
            || text.contains(QStringLiteral("GIT binary patch"));
        static const QRegularExpression header(QStringLiteral(R"(^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@)"));
        qsizetype cursor = 0;
        while (cursor < text.size()) {
            const qsizetype newline = text.indexOf(QLatin1Char('\n'), cursor);
            const qsizetype end = newline < 0 ? text.size() : newline;
            if (end - cursor >= 3 && text.at(cursor) == QLatin1Char('@')
                && text.at(cursor + 1) == QLatin1Char('@') && text.at(cursor + 2) == QLatin1Char(' ')
                && header.match(text.mid(cursor, end - cursor)).hasMatch()) {
                if (result.hunks.isEmpty()) result.headerEnd = cursor;
                else result.hunks.last().end = cursor;
                result.hunks.append(DiffRange{cursor, text.size()});
            }
            cursor = newline < 0 ? text.size() : newline + 1;
        }
        return result;
    }

    static QString patch(const QString &text, const DiffIndex &index, int hunk)
    {
        if (index.isBinary || hunk < 0 || hunk >= index.hunks.size()) return {};
        const auto &range = index.hunks.at(hunk);
        if (range.start < index.headerEnd || range.end > text.size() || range.end <= range.start) return {};
        QString result = text.left(index.headerEnd) + text.mid(range.start, range.end - range.start);
        if (!result.endsWith(QLatin1Char('\n'))) result += QLatin1Char('\n');
        return result;
    }
};
