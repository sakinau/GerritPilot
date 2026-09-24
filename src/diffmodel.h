#pragma once

#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

struct DiffHunk {
    int index = 0;
    QString header;
    int oldStart = 0;
    int oldCount = 0;
    int newStart = 0;
    int newCount = 0;
    QStringList lines;
    QString patch;
};

struct DiffFile {
    QString oldPath;
    QString newPath;
    bool isBinary = false;
    bool isNewFile = false;
    bool isDeleted = false;
    QString fileHeader;
    QList<DiffHunk> hunks;
};

class DiffModel
{
public:
    static DiffFile parse(const QString &diffText)
    {
        DiffFile file;
        if (diffText.isEmpty()) return file;

        const QStringList rawLines = diffText.split(QLatin1Char('\n'));
        if (rawLines.isEmpty()) return file;

        QStringList headerLines;
        int lineIdx = 0;

        // Check for binary
        if (diffText.contains(QStringLiteral("Binary files ")) || diffText.contains(QStringLiteral("GIT binary patch"))) {
            file.isBinary = true;
        }

        // Parse header lines until the first hunk line (@@)
        while (lineIdx < rawLines.size()) {
            const QString &line = rawLines.at(lineIdx);
            if (line.startsWith(QStringLiteral("@@ ")))
                break;
            headerLines.append(line);
            if (line.startsWith(QStringLiteral("--- a/"))) {
                file.oldPath = line.mid(6);
            } else if (line.startsWith(QStringLiteral("+++ b/"))) {
                file.newPath = line.mid(6);
            } else if (line.startsWith(QStringLiteral("new file mode"))) {
                file.isNewFile = true;
            } else if (line.startsWith(QStringLiteral("deleted file mode"))) {
                file.isDeleted = true;
            }
            ++lineIdx;
        }

        file.fileHeader = headerLines.join(QLatin1Char('\n'));
        if (!file.fileHeader.isEmpty())
            file.fileHeader += QLatin1Char('\n');

        // Parse hunks
        static const QRegularExpression hunkRegex(QStringLiteral(R"(^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@)"));
        DiffHunk currentHunk;
        bool inHunk = false;
        int hunkIndex = 0;

        while (lineIdx < rawLines.size()) {
            const QString &line = rawLines.at(lineIdx);
            auto match = hunkRegex.match(line);
            if (match.hasMatch()) {
                if (inHunk) {
                    currentHunk.patch = file.fileHeader + currentHunk.header + QLatin1Char('\n')
                                      + currentHunk.lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
                    file.hunks.append(currentHunk);
                }
                inHunk = true;
                currentHunk = DiffHunk();
                currentHunk.index = hunkIndex++;
                currentHunk.header = line;
                currentHunk.oldStart = match.captured(1).toInt();
                currentHunk.oldCount = match.captured(2).isEmpty() ? 1 : match.captured(2).toInt();
                currentHunk.newStart = match.captured(3).toInt();
                currentHunk.newCount = match.captured(4).isEmpty() ? 1 : match.captured(4).toInt();
            } else if (inHunk) {
                if (!line.isEmpty() || lineIdx + 1 < rawLines.size()) {
                    currentHunk.lines.append(line);
                }
            }
            ++lineIdx;
        }

        if (inHunk && !currentHunk.lines.isEmpty()) {
            currentHunk.patch = file.fileHeader + currentHunk.header + QLatin1Char('\n')
                              + currentHunk.lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
            file.hunks.append(currentHunk);
        }

        return file;
    }
};
