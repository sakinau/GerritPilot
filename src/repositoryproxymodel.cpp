#include "repositoryproxymodel.h"

#include "repositorymodel.h"

RepositoryProxyModel::RepositoryProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
    setSortCaseSensitivity(Qt::CaseInsensitive);
}

void RepositoryProxyModel::setFilterMode(int mode)
{
    mode = qBound(static_cast<int>(AllRepositories), mode,
                  static_cast<int>(AheadRepositories));
    if (m_filterMode == mode)
        return;
    m_filterMode = mode;
    invalidate();
    emit filterModeChanged();
}

void RepositoryProxyModel::setFilterText(const QString &text)
{
    const QString normalized = text.trimmed();
    if (m_filterText == normalized)
        return;
    m_filterText = normalized;
    invalidate();
    emit filterTextChanged();
}

int RepositoryProxyModel::sourceRow(int proxyRow) const
{
    if (proxyRow < 0 || proxyRow >= rowCount())
        return -1;
    return mapToSource(index(proxyRow, 0)).row();
}

int RepositoryProxyModel::proxyRow(int sourceRow) const
{
    if (sourceRow < 0 || !sourceModel() || sourceRow >= sourceModel()->rowCount())
        return -1;
    const QModelIndex srcIndex = sourceModel()->index(sourceRow, 0);
    const QModelIndex proxyIndex = mapFromSource(srcIndex);
    return proxyIndex.isValid() ? proxyIndex.row() : -1;
}

bool RepositoryProxyModel::filterAcceptsRow(int sourceRow,
                                            const QModelIndex &sourceParent) const
{
    const QModelIndex sourceIndex = sourceModel()->index(sourceRow, 0, sourceParent);
    const bool ignored = sourceIndex.data(RepositoryModel::IgnoredRole).toBool();
    if (ignored)
        return false;
    if (!m_filterText.isEmpty()) {
        const Qt::CaseSensitivity sensitivity = Qt::CaseInsensitive;
        bool matched = false;
        if (m_filterText.contains(QLatin1Char('/')) || m_filterText.contains(QLatin1Char('\\'))) {
            // User explicitly typed a path separator, match against full path or branch
            matched = sourceIndex.data(RepositoryModel::PathRole).toString()
                         .contains(m_filterText, sensitivity)
                   || sourceIndex.data(RepositoryModel::BranchRole).toString()
                         .contains(m_filterText, sensitivity);
        } else {
            // User typed a keyword, match against repository name or branch name.
            // Do NOT match intermediate parent directories in path (e.g. searching 'framework'
            // should not match unrelated repos inside module/framework/* like libmos or mit_public).
            matched = sourceIndex.data(RepositoryModel::NameRole).toString()
                         .contains(m_filterText, sensitivity)
                   || sourceIndex.data(RepositoryModel::BranchRole).toString()
                         .contains(m_filterText, sensitivity);
        }
        if (!matched)
            return false;
    }
    if (m_filterMode == ModifiedRepositories)
        return sourceIndex.data(RepositoryModel::ChangeCountRole).toInt() > 0;
    if (m_filterMode == AheadRepositories)
        return sourceIndex.data(RepositoryModel::AheadRole).toInt() > 0;
    return true;
}

bool RepositoryProxyModel::lessThan(const QModelIndex &left,
                                    const QModelIndex &right) const
{
    if (!m_filterText.isEmpty()) {
        const auto matchScore = [this](const QModelIndex &index) {
            const QString name = index.data(RepositoryModel::NameRole).toString();
            const QString branch = index.data(RepositoryModel::BranchRole).toString();
            if (name.compare(m_filterText, Qt::CaseInsensitive) == 0)
                return 0; // exact name match
            if (name.startsWith(m_filterText, Qt::CaseInsensitive))
                return 1; // name prefix match
            if (name.contains(m_filterText, Qt::CaseInsensitive))
                return 2; // name contains
            if (branch.compare(m_filterText, Qt::CaseInsensitive) == 0)
                return 3; // exact branch match
            if (branch.contains(m_filterText, Qt::CaseInsensitive))
                return 4; // branch contains
            return 5;
        };
        const int leftScore = matchScore(left);
        const int rightScore = matchScore(right);
        if (leftScore != rightScore)
            return leftScore < rightScore;
    }

    const auto rank = [](const QModelIndex &index) {
        if (index.data(RepositoryModel::IgnoredRole).toBool()) return 4;
        if (index.data(RepositoryModel::ChangeCountRole).toInt() > 0) return 0;
        if (index.data(RepositoryModel::AheadRole).toInt() > 0) return 1;
        if (index.data(RepositoryModel::StateRole).toString() == QStringLiteral("clean")) return 2;
        return 3;
    };
    const int leftRank = rank(left);
    const int rightRank = rank(right);
    if (leftRank != rightRank)
        return leftRank < rightRank;
    return QString::localeAwareCompare(left.data(RepositoryModel::NameRole).toString(),
                                       right.data(RepositoryModel::NameRole).toString()) < 0;
}
