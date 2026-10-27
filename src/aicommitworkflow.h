#pragma once

#include "repositoryworkflow.h"
#include "commitmessageaiservice.h"

// Owns only the in-flight source collection request. Model configuration and
// generated candidates stay in CommitMessageAiService; drafts stay in settings.
class AiCommitWorkflow final : public RepositoryWorkflow {
    Q_OBJECT
public:
    AiCommitWorkflow(const WorkflowRepositoryAccess &repositories, CommitMessageAiService &service)
        : RepositoryWorkflow(repositories), m_commitAi(service) {}
    void generate(const QString &existingMessage, int scope, const QString &commitType,
                  const QString &issueId, bool manualCompiled, const QString &compileNote,
                  const QString &purpose, const QString &secondaryIssueId);
    void reset();
    bool handleResult(const GitCommandResult &result);
private:
    CommitMessageAiService &m_commitAi;
    quint64 m_aiRequest = 0;
    QString m_aiExistingMessage, m_aiCommitType, m_aiIssueId, m_aiSecondaryIssueId;
    bool m_aiManualCompiled = false;
    QString m_aiCompileNote, m_aiPurpose;
    QStringList m_aiChangedFiles, m_aiUntrackedFiles;
};
