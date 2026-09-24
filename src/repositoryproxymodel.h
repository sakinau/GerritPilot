#pragma once

#include <QSortFilterProxyModel>
#include <QString>

class RepositoryProxyModel final : public QSortFilterProxyModel
{
    Q_OBJECT
    Q_PROPERTY(int filterMode READ filterMode WRITE setFilterMode NOTIFY filterModeChanged)
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)

public:
    enum FilterMode { AllRepositories = 0, ModifiedRepositories = 1, AheadRepositories = 2 };
    Q_ENUM(FilterMode)

    explicit RepositoryProxyModel(QObject *parent = nullptr);

    int filterMode() const { return m_filterMode; }
    void setFilterMode(int mode);
    QString filterText() const { return m_filterText; }
    void setFilterText(const QString &text);
    Q_INVOKABLE int sourceRow(int proxyRow) const;
    Q_INVOKABLE int proxyRow(int sourceRow) const;

signals:
    void filterModeChanged();
    void filterTextChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    int m_filterMode = AllRepositories;
    QString m_filterText;
};
