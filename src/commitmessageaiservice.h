#pragma once

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

class CommitMessageAiService final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString lastCandidate READ lastCandidate NOTIFY candidateReady)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorOccurred)
    Q_PROPERTY(QString apiEndpoint READ apiEndpoint WRITE setApiEndpoint NOTIFY apiEndpointChanged)
    Q_PROPERTY(QString apiKey READ apiKey WRITE setApiKey NOTIFY apiKeyChanged)
    Q_PROPERTY(QString modelName READ modelName WRITE setModelName NOTIFY modelNameChanged)
    Q_PROPERTY(bool testBusy READ testBusy NOTIFY testBusyChanged)
    Q_PROPERTY(QString testResult READ testResult NOTIFY testResultChanged)
public:
    explicit CommitMessageAiService(QObject *parent = nullptr);
    ~CommitMessageAiService() override;

    bool busy() const { return m_busy; }
    QString lastCandidate() const { return m_lastCandidate; }
    QString lastError() const { return m_lastError; }

    QString apiEndpoint() const { return m_apiEndpoint; }
    void setApiEndpoint(const QString &endpoint);

    QString apiKey() const { return m_apiKey; }
    void setApiKey(const QString &key);

    QString modelName() const { return m_modelName; }
    void setModelName(const QString &model);
    bool testBusy() const { return m_testReply != nullptr; }
    QString testResult() const { return m_testResult; }
    bool configured() const;

    Q_INVOKABLE void loadConfig();
    Q_INVOKABLE void saveConfig();

    Q_INVOKABLE void generate(const QString &repoName, const QString &branchName,
                              const QString &diffText, const QStringList &changedFiles,
                              const QString &existingMessage, const QString &commitType,
                              const QString &issueId, bool wholeRepository);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void testConnection();

    // Verification and validation of candidate commit message
    static QString validateAndNormalize(const QString &rawCandidate,
                                                     const QString &repoName,
                                                     const QString &existingMessage,
                                                     const QString &commitType,
                                                     const QString &issueId);

signals:
    void busyChanged(bool busy);
    void candidateReady(const QString &candidate);
    void errorOccurred(const QString &error);
    void apiEndpointChanged();
    void apiKeyChanged();
    void modelNameChanged();
    void testBusyChanged();
    void testResultChanged();

private slots:
    void onReplyFinished();

private:
    void setBusy(bool busy);
    static QString extractExistingChangeId(const QString &message);
    QUrl requestUrl() const;
    QNetworkRequest makeRequest() const;

    QNetworkAccessManager m_nam;
    QNetworkReply *m_currentReply = nullptr;
    QNetworkReply *m_testReply = nullptr;
    bool m_busy = false;
    QString m_lastCandidate;
    QString m_lastError;
    QString m_apiEndpoint;
    QString m_apiKey;
    QString m_modelName;
    QString m_testResult;

    // Context saved for reply processing
    QString m_pendingRepo;
    QString m_pendingExistingMsg;
    QString m_pendingCommitType;
    QString m_pendingIssueId;
};
