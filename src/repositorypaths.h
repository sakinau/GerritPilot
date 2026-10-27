#pragma once

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace RepositoryPaths {
inline QString gitMetadataDirectory(const QString &repository)
{
    const QString path = QDir(repository).filePath(QStringLiteral(".git"));
    if (QFileInfo(path).isDir()) return path;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    const QString content = QString::fromUtf8(file.readAll()).trimmed();
    if (!content.startsWith(QStringLiteral("gitdir:"))) return {};
    return QDir(repository).absoluteFilePath(content.mid(7).trimmed());
}

}
