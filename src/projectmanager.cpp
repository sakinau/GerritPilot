#include "projectmanager.h"
#include "workspacecontroller.h"
#include <QDir>
#include <QFileInfo>
#include <QSettings>

ProjectManager::ProjectManager(WorkspaceController *controller, QObject *parent)
    : QObject(parent)
    , m_controller(controller)
{
    loadProjects();
}

QString ProjectManager::currentProjectName() const
{
    if (m_currentIndex >= 0 && m_currentIndex < m_projects.size()) {
        return m_projects.at(m_currentIndex).toMap().value(QStringLiteral("name")).toString();
    }
    if (m_controller) {
        const QString path = m_controller->workspacePath();
        if (!path.isEmpty()) return QFileInfo(path).fileName();
    }
    return QStringLiteral("未选择项目");
}

QString ProjectManager::currentProjectPath() const
{
    if (m_currentIndex >= 0 && m_currentIndex < m_projects.size()) {
        return m_projects.at(m_currentIndex).toMap().value(QStringLiteral("path")).toString();
    }
    return m_controller ? m_controller->workspacePath() : QString();
}

void ProjectManager::loadProjects()
{
    QSettings settings;
    const QByteArray jsonBytes = settings.value(QStringLiteral("projects/list")).toByteArray();
    if (!jsonBytes.isEmpty()) {
        const QJsonDocument doc = QJsonDocument::fromJson(jsonBytes);
        if (doc.isArray()) {
            m_projects.clear();
            const QJsonArray arr = doc.array();
            for (const QJsonValue &val : arr) {
                if (val.isObject()) {
                    m_projects.append(val.toObject().toVariantMap());
                }
            }
        }
    }

    // Determine current index matching controller's current workspace path
    if (m_controller && !m_controller->workspacePath().isEmpty()) {
        updateCurrentIndexForPath(m_controller->workspacePath());
    } else {
        m_currentIndex = settings.value(QStringLiteral("projects/currentIndex"), -1).toInt();
        if (m_currentIndex < 0 || m_currentIndex >= m_projects.size())
            m_currentIndex = m_projects.isEmpty() ? -1 : 0;
    }

    emit projectListChanged();
    emit currentIndexChanged(m_currentIndex);
    emit currentProjectChanged();
}

void ProjectManager::saveProjects() const
{
    QSettings settings;
    QJsonArray arr;
    for (const QVariant &item : m_projects) {
        arr.append(QJsonObject::fromVariantMap(item.toMap()));
    }
    settings.setValue(QStringLiteral("projects/list"), QJsonDocument(arr).toJson(QJsonDocument::Compact));
    settings.setValue(QStringLiteral("projects/currentIndex"), m_currentIndex);
}

void ProjectManager::updateCurrentIndexForPath(const QString &path)
{
    if (path.trimmed().isEmpty()) return;
    const QString clean = QDir::cleanPath(path.trimmed());
    if (clean.isEmpty())
        return;

    for (int i = 0; i < m_projects.size(); ++i) {
        if (QDir::cleanPath(m_projects.at(i).toMap().value(QStringLiteral("path")).toString()) == clean) {
            if (m_currentIndex != i) {
                m_currentIndex = i;
                saveProjects();
                emit currentIndexChanged(m_currentIndex);
                emit currentProjectChanged();
            }
            return;
        }
    }

    if (QDir(clean).exists()) {
        QVariantMap proj;
        proj[QStringLiteral("name")] = QFileInfo(clean).fileName();
        proj[QStringLiteral("path")] = clean;
        m_projects.prepend(proj);
        m_currentIndex = 0;
        saveProjects();
        emit projectListChanged();
        emit currentIndexChanged(m_currentIndex);
        emit currentProjectChanged();
    }
}

void ProjectManager::switchProject(int index)
{
    if (index < 0 || index >= m_projects.size())
        return;

    const QString targetPath = m_projects.at(index).toMap().value(QStringLiteral("path")).toString();
    if (m_controller && m_controller->pushState() == 1
        && QDir::cleanPath(targetPath) != QDir::cleanPath(m_controller->workspacePath()))
        return;

    m_currentIndex = index;
    saveProjects();

    if (m_controller && !targetPath.isEmpty()) {
        m_controller->setWorkspacePath(targetPath);
        if (QFileInfo(QDir(targetPath).filePath(QStringLiteral(".repo/project.list"))).isFile()
            || QFileInfo(QDir(targetPath).filePath(QStringLiteral(".git"))).exists()) {
            m_controller->scanWorkspace();
            m_controller->refreshAll();
        }
    }

    emit currentIndexChanged(m_currentIndex);
    emit currentProjectChanged();
}

void ProjectManager::addProject(const QString &name, const QString &path)
{
    const QString cleanPath = QDir::cleanPath(path.trimmed());
    if (cleanPath.isEmpty())
        return;

    const QString projName = name.trimmed().isEmpty() ? QFileInfo(cleanPath).fileName() : name.trimmed();

    // Check if path already exists
    for (int i = 0; i < m_projects.size(); ++i) {
        if (QDir::cleanPath(m_projects.at(i).toMap().value(QStringLiteral("path")).toString()) == cleanPath) {
            switchProject(i);
            return;
        }
    }

    QVariantMap proj;
    proj[QStringLiteral("name")] = projName;
    proj[QStringLiteral("path")] = cleanPath;
    m_projects.append(proj);
    saveProjects();

    emit projectListChanged();
    switchProject(m_projects.size() - 1);
}

void ProjectManager::removeProject(int index)
{
    if (index < 0 || index >= m_projects.size())
        return;

    m_projects.removeAt(index);
    if (m_currentIndex >= m_projects.size())
        m_currentIndex = m_projects.isEmpty() ? -1 : m_projects.size() - 1;

    saveProjects();
    emit projectListChanged();
    emit currentIndexChanged(m_currentIndex);
    emit currentProjectChanged();

    if (!m_projects.isEmpty()) {
        switchProject(m_currentIndex);
    }
}

void ProjectManager::renameProject(int index, const QString &newName)
{
    if (index < 0 || index >= m_projects.size() || newName.trimmed().isEmpty())
        return;

    QVariantMap proj = m_projects.at(index).toMap();
    proj[QStringLiteral("name")] = newName.trimmed();
    m_projects[index] = proj;

    saveProjects();
    emit projectListChanged();
    if (index == m_currentIndex) {
        emit currentProjectChanged();
    }
}
