#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

class RepositoryModel final : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        PathRole,
        BranchRole,
        ChangeCountRole,
        AheadRole,
        BehindRole,
        StateRole,
        SelectedRole,
        IgnoredRole
    };

    struct Repository {
        QString name;
        QString path;
        QString branch = QStringLiteral("—");
        int changeCount = 0;
        int ahead = 0;
        int behind = 0;
        QString state = QStringLiteral("waiting");
        bool selected = false;
        bool ignored = false;
    };

    explicit RepositoryModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void resetRepositories(const QStringList &paths, const QStringList &ignoredPaths = {});
    void updateStatus(int row, const QString &branch, int changes, int ahead, int behind,
                      const QString &state);
    void setSelected(int row, bool selected);
    void selectAll(bool selected);
    void setIgnored(int row, bool ignored);
    QString pathAt(int row) const;
    QVariantMap summaryAt(int row) const;
    void restoreSummary(int row, const QVariantMap &summary);
    QString branchAt(int row) const;
    QString nameAt(int row) const;
    bool ignoredAt(int row) const;
    int indexOfPath(const QString &path) const;
    QStringList selectedPaths() const;
    int selectedCount() const;
    int changedCount() const;
    int aheadCount() const;

private:
    QList<Repository> m_repositories;
};
