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
inline QString key(const QString &workspace, const QString &repository = {})
{
    return QString::fromLatin1(QCryptographicHash::hash(
        QDir::cleanPath(workspace).toUtf8() + '\0' + repository.toUtf8(), QCryptographicHash::Sha256).toHex());
}
inline QString directory()
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/repositories");
}
inline QVariantMap load(const QString &workspace, const QString &repository)
{
    QFile file(directory() + '/' + key(workspace, repository) + QStringLiteral(".json"));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 8 * 1024 * 1024) return {};
    const auto object = QJsonDocument::fromJson(file.readAll()).object();
    if (object.value(QStringLiteral("version")).toInt() != 1
        || object.value(QStringLiteral("workspace")).toString() != workspace
        || object.value(QStringLiteral("repository")).toString() != repository) return {};
    return object.value(QStringLiteral("data")).toObject().toVariantMap();
}
inline bool save(const QString &workspace, const QString &repository, const QVariantMap &data)
{
    if (repository.isEmpty() || !QDir().mkpath(directory())) return false;
    const QByteArray bytes = QJsonDocument(QJsonObject{
        {QStringLiteral("version"), 1}, {QStringLiteral("workspace"), workspace},
        {QStringLiteral("repository"), repository},
        {QStringLiteral("savedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {QStringLiteral("data"), QJsonObject::fromVariantMap(data)}
    }).toJson(QJsonDocument::Compact);
    if (bytes.size() > 8 * 1024 * 1024) return false;
    QSaveFile file(directory() + '/' + key(workspace, repository) + QStringLiteral(".json"));
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
}
