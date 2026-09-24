#pragma once

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class WorkspaceController;

class ProjectManager final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList projectList READ projectList NOTIFY projectListChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(QString currentProjectName READ currentProjectName NOTIFY currentProjectChanged)
    Q_PROPERTY(QString currentProjectPath READ currentProjectPath NOTIFY currentProjectChanged)
public:
    explicit ProjectManager(WorkspaceController *controller, QObject *parent = nullptr);

    QVariantList projectList() const { return m_projects; }
    int currentIndex() const { return m_currentIndex; }
    QString currentProjectName() const;
    QString currentProjectPath() const;

    Q_INVOKABLE void switchProject(int index);
    Q_INVOKABLE void addProject(const QString &name, const QString &path);
    Q_INVOKABLE void removeProject(int index);
    Q_INVOKABLE void renameProject(int index, const QString &newName);
    Q_INVOKABLE void updateCurrentIndexForPath(const QString &path);

    void loadProjects();
    void saveProjects() const;

signals:
    void projectListChanged();
    void currentIndexChanged(int index);
    void currentProjectChanged();

private:
    WorkspaceController *m_controller = nullptr;
    QVariantList m_projects;
    int m_currentIndex = -1;
};
