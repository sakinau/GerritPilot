#pragma once

#include <QObject>
#include <QString>

// Unified interface for task status reporting and logging across Git, Repo, and other background services.
class TaskReporter : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString activeTask READ activeTask NOTIFY activeTaskChanged)
public:
    explicit TaskReporter(QObject *parent = nullptr) : QObject(parent) {}
    ~TaskReporter() override = default;

    virtual bool busy() const = 0;
    virtual QString activeTask() const = 0;

signals:
    void busyChanged(bool busy);
    void activeTaskChanged(const QString &task);
    void logMessage(const QString &message);
};
