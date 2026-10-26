#pragma once

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QVariantMap>

namespace RepositoryCache {
enum class Section { Summary, Files, Branches, History };
inline QString sectionName(Section section)
{
    switch (section) {
    case Section::Summary: return QStringLiteral("summary");
    case Section::Files: return QStringLiteral("files");
    case Section::Branches: return QStringLiteral("branches");
    case Section::History: return QStringLiteral("history");
    }
    return {};
}
inline qint64 sizeLimit(Section section)
{
    switch (section) {
    case Section::Summary: return 64 * 1024;
    case Section::Files: return 8 * 1024 * 1024;
    case Section::Branches: return 1024 * 1024;
    case Section::History: return 256 * 1024;
    }
    return 0;
}
inline QString key(const QString &workspace, const QString &repository = {})
{
    return QString::fromLatin1(QCryptographicHash::hash(
        QDir::cleanPath(workspace).toUtf8() + '\0' + repository.toUtf8(), QCryptographicHash::Sha256).toHex());
}
inline QString directory()
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/repositories");
}
inline QString filePath(const QString &workspace, const QString &repository, Section section)
{
    return directory() + '/' + key(workspace, repository) + '.' + sectionName(section) + QStringLiteral(".json");
}
inline QVariantMap load(const QString &workspace, const QString &repository, Section section)
{
    if (repository.isEmpty()) return {};
    QFile file(filePath(workspace, repository, section));
    if (!file.open(QIODevice::ReadOnly) || file.size() > sizeLimit(section)) return {};
    const auto object = QJsonDocument::fromJson(file.readAll()).object();
    if (object.value(QStringLiteral("version")).toInt() != 2
        || object.value(QStringLiteral("section")).toString() != sectionName(section)
        || object.value(QStringLiteral("workspace")).toString() != workspace
        || object.value(QStringLiteral("repository")).toString() != repository) return {};
    return object.value(QStringLiteral("data")).toObject().toVariantMap();
}
inline bool save(const QString &workspace, const QString &repository, Section section, const QVariantMap &data)
{
    if (repository.isEmpty() || !QDir().mkpath(directory())) return false;
    const QByteArray bytes = QJsonDocument(QJsonObject{
        {QStringLiteral("version"), 2}, {QStringLiteral("workspace"), workspace},
        {QStringLiteral("section"), sectionName(section)},
        {QStringLiteral("repository"), repository},
        {QStringLiteral("savedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {QStringLiteral("data"), QJsonObject::fromVariantMap(data)}
    }).toJson(QJsonDocument::Compact);
    if (bytes.size() > sizeLimit(section)) return false;
    QSaveFile file(filePath(workspace, repository, section));
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
}
