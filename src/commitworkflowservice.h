#pragma once

#include "gitservice.h"
#include "workspacesettings.h"
#include <QObject>
#include <QString>
#include <QStringList>

class CommitWorkflowService final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool pendingPush READ isPendingPush NOTIFY pendingPushChanged)
public:
    explicit CommitWorkflowService(QObject *parent = nullptr);

    bool isPendingPush() const { return m_pendingPush; }
    void setPendingPush(bool pending);

    // Validation & Command preparation
    bool prepareCommit(int repoIndex, const QString &repoAbsPath,
                       const QString &repoRelPath, const QString &workspacePath,
                       const QString &message, bool amend, bool stageAll,
                       bool isDetailsCached, bool isBusy,
                       GitCommand *stageCommand, GitCommand *commitCommand,
                       QString *errorMessage) const;

    bool prepareCommitAndPush(int repoIndex, const QString &repoAbsPath,
                              const QString &repoRelPath, const QString &workspacePath,
                              const QString &message, bool amend, bool stageAll,
                              bool isDetailsCached, bool isBusy,
                              const QString &remote, const QString &targetBranch,
                              const QString &topic,
                              GitCommand *stageCommand, GitCommand *commitCommand,
                              QString *errorMessage) const;

    void onCommitCompleted(const GitCommand &command);

    // Draft management
    QString loadDraft(const QString &repoRelPath, const QString &workspacePath) const;
    void saveDraft(const QString &repoRelPath, const QString &text, const QString &workspacePath);
    void clearDraft(const QString &repoRelPath, const QString &workspacePath);

signals:
    void commitSucceeded(const QString &workspacePath, const QString &repositoryPath,
                         const QString &submittedMessage);
    void commitFailed(const QString &title, const QString &message);
    void pendingPushChanged(bool pending);

private:
    bool m_pendingPush = false;
};
