#include "repositorymodel.h"

#include <QDir>
#include <QFileInfo>

RepositoryModel::RepositoryModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int RepositoryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_repositories.size();
}

QVariant RepositoryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_repositories.size())
        return {};

    const Repository &repository = m_repositories.at(index.row());
    switch (role) {
    case NameRole: return repository.name;
    case PathRole: return repository.path;
    case BranchRole: return repository.branch;
    case ChangeCountRole: return repository.changeCount;
    case AheadRole: return repository.ahead;
    case BehindRole: return repository.behind;
    case StateRole: return repository.state;
    case SelectedRole: return repository.selected;
    case IgnoredRole: return repository.ignored;
    default: return {};
    }
}

QHash<int, QByteArray> RepositoryModel::roleNames() const
{
    return {
        {NameRole, "repoName"},
        {PathRole, "repoPath"},
        {BranchRole, "branch"},
        {ChangeCountRole, "changeCount"},
        {AheadRole, "ahead"},
        {BehindRole, "behind"},
        {StateRole, "repoState"},
        {SelectedRole, "repoSelected"},
        {IgnoredRole, "repoIgnored"}
    };
}

void RepositoryModel::resetRepositories(const QStringList &paths, const QStringList &ignoredPaths)
{
    beginResetModel();
    m_repositories.clear();
    for (const QString &path : paths) {
        Repository repository;
        repository.path = path;
        repository.name = path == QStringLiteral(".")
            ? tr("根仓库")
            : QFileInfo(path).fileName();
        repository.ignored = ignoredPaths.contains(path);
        m_repositories.append(repository);
    }
    endResetModel();
}

void RepositoryModel::updateStatus(int row, const QString &branch, int changes, int ahead,
                                   int behind, const QString &state)
{
    if (row < 0 || row >= m_repositories.size())
        return;
    Repository &repository = m_repositories[row];
    repository.branch = branch.isEmpty() ? QStringLiteral("detached") : branch;
    repository.changeCount = changes;
    repository.ahead = ahead;
    repository.behind = behind;
    repository.state = state;
    const QModelIndex changed = index(row);
    emit dataChanged(changed, changed,
                     {BranchRole, ChangeCountRole, AheadRole, BehindRole, StateRole});
}

void RepositoryModel::setSelected(int row, bool selected)
{
    if (row < 0 || row >= m_repositories.size() || m_repositories[row].selected == selected)
        return;
    m_repositories[row].selected = selected;
    emit dataChanged(index(row), index(row), {SelectedRole});
}

void RepositoryModel::selectAll(bool selected)
{
    if (m_repositories.isEmpty())
        return;
    for (Repository &repository : m_repositories)
        repository.selected = selected;
    emit dataChanged(index(0), index(m_repositories.size() - 1), {SelectedRole});
}

void RepositoryModel::setIgnored(int row, bool ignored)
{
    if (row < 0 || row >= m_repositories.size() || m_repositories[row].ignored == ignored)
        return;
    m_repositories[row].ignored = ignored;
    emit dataChanged(index(row), index(row), {IgnoredRole});
}

QVariantMap RepositoryModel::summaryAt(int row) const
{
    if (row < 0 || row >= m_repositories.size()) return {};
    const auto &repository = m_repositories.at(row);
    return {{QStringLiteral("branch"), repository.branch},
            {QStringLiteral("changes"), repository.changeCount},
            {QStringLiteral("ahead"), repository.ahead},
            {QStringLiteral("behind"), repository.behind},
            {QStringLiteral("state"), repository.state}};
}

void RepositoryModel::restoreSummary(int row, const QVariantMap &summary)
{
    if (!summary.contains(QStringLiteral("branch"))) return;
    updateStatus(row, summary.value(QStringLiteral("branch")).toString(),
        qMax(0, summary.value(QStringLiteral("changes")).toInt()),
        qMax(0, summary.value(QStringLiteral("ahead")).toInt()),
        qMax(0, summary.value(QStringLiteral("behind")).toInt()),
        QStringLiteral("cached"));
}

QString RepositoryModel::pathAt(int row) const
{
    return row >= 0 && row < m_repositories.size() ? m_repositories.at(row).path : QString();
}

QString RepositoryModel::branchAt(int row) const
{
    return row >= 0 && row < m_repositories.size() ? m_repositories.at(row).branch : QString();
}

QString RepositoryModel::nameAt(int row) const
{
    return row >= 0 && row < m_repositories.size() ? m_repositories.at(row).name : QString();
}

bool RepositoryModel::ignoredAt(int row) const
{
    return row >= 0 && row < m_repositories.size() && m_repositories.at(row).ignored;
}

int RepositoryModel::indexOfPath(const QString &path) const
{
    for (int row = 0; row < m_repositories.size(); ++row) {
        if (m_repositories.at(row).path == path)
            return row;
    }
    return -1;
}

QStringList RepositoryModel::selectedPaths() const
{
    QStringList paths;
    for (const Repository &repository : m_repositories) {
        if (repository.selected)
            paths.append(repository.path);
    }
    return paths;
}

int RepositoryModel::selectedCount() const
{
    int count = 0;
    for (const Repository &repository : m_repositories)
        count += repository.selected ? 1 : 0;
    return count;
}

int RepositoryModel::changedCount() const
{
    int count = 0;
    for (const Repository &repository : m_repositories)
        count += repository.changeCount > 0 && !repository.ignored ? 1 : 0;
    return count;
}

int RepositoryModel::aheadCount() const
{
    int count = 0;
    for (const Repository &repository : m_repositories)
        count += repository.ahead > 0 && !repository.ignored ? 1 : 0;
    return count;
}
