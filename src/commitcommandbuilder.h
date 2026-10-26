#pragma once

#include "gitservice.h"
#include <QCoreApplication>
#include <QString>
#include <QStringList>

// Stateless validation/command construction. Queueing, Hook preflight and
// completion belong to WorkspaceController; draft storage to WorkspaceSettings.
class CommitCommandBuilder final
{
    // Preserve the existing translation context without registering a QObject.
    Q_DECLARE_TR_FUNCTIONS(CommitWorkflowService)
public:
    static bool prepareCommit(int repoIndex, const QString &repoAbsPath,
                       const QString &repoRelPath, const QString &workspacePath,
                       const QString &message, bool amend, bool stageAll,
                       bool isDetailsCached, bool isBusy,
                       GitCommand *stageCommand, GitCommand *commitCommand,
                       QString *errorMessage);
};
