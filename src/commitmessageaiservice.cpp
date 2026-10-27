#include "commitmessageaiservice.h"
#include "commitmessagepolicy.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
#include <QTimer>
#include <QPointer>

#include <QSettings>

void CommitMessageAiService::setRememberApiKey(bool remember)
{
    if (m_rememberApiKey == remember) return;
    m_rememberApiKey = remember;
    emit rememberApiKeyChanged();
    saveConfig();
}

CommitMessageAiService::CommitMessageAiService(QObject *parent)
    : QObject(parent)
{
    loadConfig();
}

CommitMessageAiService::~CommitMessageAiService()
{
    cancel();
    if (m_testReply) {
        QNetworkReply *reply = m_testReply;
        m_testReply = nullptr;
        reply->abort();
    }
}

void CommitMessageAiService::loadConfig()
{
    QSettings settings;
    m_apiEndpoint = settings.value(QStringLiteral("ai/endpoint")).toString();
    m_rememberApiKey = settings.value(QStringLiteral("ai/rememberApiKey"), settings.contains(QStringLiteral("ai/apiKey"))).toBool();
    m_apiKey = m_rememberApiKey ? settings.value(QStringLiteral("ai/apiKey")).toString()
                              : QString::fromLocal8Bit(qgetenv("GERRITPILOT_AI_API_KEY"));
    m_modelName = settings.value(QStringLiteral("ai/model")).toString();

}

void CommitMessageAiService::saveConfig()
{
    QSettings settings;
    settings.setValue(QStringLiteral("ai/endpoint"), m_apiEndpoint);
    settings.setValue(QStringLiteral("ai/rememberApiKey"), m_rememberApiKey);
    if (m_rememberApiKey) settings.setValue(QStringLiteral("ai/apiKey"), m_apiKey);
    else settings.remove(QStringLiteral("ai/apiKey"));
    settings.setValue(QStringLiteral("ai/model"), m_modelName);
}

void CommitMessageAiService::setBusy(bool busy)
{
    if (m_busy != busy) {
        m_busy = busy;
        emit busyChanged(m_busy);
    }
}

void CommitMessageAiService::setApiEndpoint(const QString &endpoint)
{
    const QString value = endpoint.trimmed();
    if (m_apiEndpoint != value) {
        m_apiEndpoint = value;
        m_testResult.clear();
        emit apiEndpointChanged();
        emit testResultChanged();
        saveConfig();
    }
}

void CommitMessageAiService::setApiKey(const QString &key)
{
    if (m_apiKey != key) {
        m_apiKey = key;
        m_testResult.clear();
        emit apiKeyChanged();
        emit testResultChanged();
        saveConfig();
    }
}

void CommitMessageAiService::setModelName(const QString &model)
{
    if (m_modelName != model) {
        m_modelName = model;
        m_testResult.clear();
        emit modelNameChanged();
        emit testResultChanged();
        saveConfig();
    }
}

bool CommitMessageAiService::configured() const
{
    const QUrl url = requestUrl();
    return !m_modelName.trimmed().isEmpty() && url.isValid()
        && (url.scheme() == QLatin1String("https") || url.scheme() == QLatin1String("http"))
        && !url.host().isEmpty();
}

QUrl CommitMessageAiService::requestUrl() const
{
    QUrl url(m_apiEndpoint.trimmed());
    if (m_apiEndpoint.trimmed().isEmpty()) return {};
    QString path = url.path();
    while (path.endsWith(QLatin1Char('/'))) path.chop(1);
    if (!path.endsWith(QStringLiteral("/chat/completions"))) {
        path += path.isEmpty() ? QStringLiteral("/v1/chat/completions")
                               : QStringLiteral("/chat/completions");
    }
    url.setPath(path);
    return url;
}

QNetworkRequest CommitMessageAiService::makeRequest() const
{
    QNetworkRequest request(requestUrl());
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!m_apiKey.trimmed().isEmpty())
        request.setRawHeader("Authorization", "Bearer " + m_apiKey.trimmed().toUtf8());
    return request;
}

void CommitMessageAiService::cancel()
{
    if (m_currentReply) {
        QNetworkReply *reply = m_currentReply;
        m_currentReply = nullptr;
        reply->abort();
        reply->deleteLater();
    }
    setBusy(false);
}

void CommitMessageAiService::testConnection()
{
    if (m_testReply) {
        QNetworkReply *reply = m_testReply;
        m_testReply = nullptr;
        reply->abort();
        reply->deleteLater();
    }
    if (!configured()) {
        m_testResult = tr("请填写有效的 HTTP(S) 接口地址和模型名称。");
        emit testResultChanged();
        emit testBusyChanged();
        return;
    }
    m_testResult = tr("正在测试连接…");
    emit testResultChanged();
    const QJsonObject payload{{QStringLiteral("model"), m_modelName.trimmed()},
        {QStringLiteral("messages"), QJsonArray{QJsonObject{
            {QStringLiteral("role"), QStringLiteral("user")},
            {QStringLiteral("content"), QStringLiteral("Reply with OK.")}}}}};
    m_testReply = m_nam.post(makeRequest(), QJsonDocument(payload).toJson(QJsonDocument::Compact));
    QPointer<QNetworkReply> pending(m_testReply);
    emit testBusyChanged();
    connect(m_testReply, &QNetworkReply::finished, this, [this, pending] {
        if (!pending || m_testReply != pending.data()) return;
        QNetworkReply *reply = m_testReply;
        m_testReply = nullptr;
        const QByteArray body = reply->readAll();
        const auto choices = QJsonDocument::fromJson(body).object().value(QStringLiteral("choices")).toArray();
        const QString content = choices.isEmpty() ? QString()
            : choices.first().toObject().value(QStringLiteral("message")).toObject()
                .value(QStringLiteral("content")).toString();
        m_testResult = reply->error() == QNetworkReply::NoError && !content.trimmed().isEmpty()
            ? tr("连接成功，模型已返回响应。")
            : tr("连接失败：%1").arg(reply->error() == QNetworkReply::NoError
                ? tr("响应缺少 choices[0].message.content") : reply->errorString());
        reply->deleteLater();
        emit testBusyChanged();
        emit testResultChanged();
    });
    QTimer::singleShot(15000, this, [this, pending] {
        if (!pending || m_testReply != pending.data()) return;
        QNetworkReply *reply = m_testReply;
        m_testReply = nullptr;
        reply->abort();
        reply->deleteLater();
        m_testResult = tr("连接超时（15 秒）。");
        emit testBusyChanged();
        emit testResultChanged();
    });
}

QString CommitMessageAiService::extractExistingChangeId(const QString &message)
{
    static const QRegularExpression changeIdRegex(QStringLiteral(R"((?m)^Change-Id:[ \t]*(I[0-9a-fA-F]{40})[ \t]*\r?$)"));
    const auto match = changeIdRegex.match(message);
    if (match.hasMatch())
        return match.captured(1);
    return QString();
}

QString CommitMessageAiService::validateAndNormalize(const QString &rawCandidate,
                                                     const QString &repoName,
                                                     const QString &existingMessage,
                                                     const QString &commitType,
                                                     const QString &issueId,
                                                     bool manualCompiled,
                                                     const QString &compileNote,
                                                     const QString &secondaryIssueId)
{
    QString text = rawCandidate.trimmed();
    if (text.startsWith(QStringLiteral("```"))) {
        const int newline = text.indexOf(QLatin1Char('\n'));
        if (newline >= 0) {
            text = text.mid(newline + 1).trimmed();
            if (text.endsWith(QStringLiteral("```"))) text.chop(3);
            text = text.trimmed();
        }
    }
    QJsonParseError error;
    QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        const int firstBrace = text.indexOf(QLatin1Char('{'));
        const int lastBrace = text.lastIndexOf(QLatin1Char('}'));
        if (firstBrace >= 0 && lastBrace > firstBrace) {
            const QString extracted = text.mid(firstBrace, lastBrace - firstBrace + 1);
            document = QJsonDocument::fromJson(extracted.toUtf8(), &error);
        }
    }
    if (error.error != QJsonParseError::NoError || !document.isObject()) return {};
    const QJsonObject object = document.object();
    auto singleLine = [](const QJsonValue &value, int maximum) {
        if (!value.isString()) return QString();
        const QString line = value.toString().trimmed();
        return line.isEmpty() || line.size() > maximum || line.contains(QLatin1Char('\n'))
            || line.contains(QLatin1Char('\r')) ? QString() : line;
    };
    const QString subject = singleLine(object.value(QStringLiteral("subject")), 48);
    const QString summary = singleLine(object.value(QStringLiteral("summary")), 100);
    const QJsonArray points = object.value(QStringLiteral("summaryPoints")).toArray();
    const QJsonArray changes = object.value(QStringLiteral("changes")).toArray();
    if (subject.isEmpty() || summary.isEmpty() || points.isEmpty() || points.size() > 3
        || changes.isEmpty() || changes.size() > 6) return {};
    QStringList summaryBullets;
    for (const auto &point : points) {
        const QString line = singleLine(point, 100);
        if (line.isEmpty()) return {};
        summaryBullets.append(QStringLiteral("- ") + line);
    }
    QStringList bullets;
    for (const auto &change : changes) {
        const QString line = singleLine(change, 100);
        if (line.isEmpty()) return {};
        bullets.append(QStringLiteral("- ") + line);
    }
    QString issue, secondaryHeader, issueError;
    if (!CommitMessagePolicy::prepareIssues(commitType, issueId, secondaryIssueId,
                                             &issue, &secondaryHeader, &issueError)) return {};
    const QString scope = CommitMessagePolicy::scope(repoName);
    const QString title = CommitMessagePolicy::titlePrefix(issue, commitType, scope) + subject;
    if (title.size() > 72 || title.contains(QLatin1Char('\n')) || title.contains(QLatin1Char('\r'))) return {};
    const QString verification = manualCompiled
        ? QStringLiteral("- Local build passed: %1").arg(compileNote.trimmed().isEmpty()
            ? QStringLiteral("本地已验证编译通过") : compileNote.simplified())
        : QStringLiteral("- Not run; reason: 用户未确认本地编译验证");

    const bool isFix = (commitType.toLower() == QStringLiteral("fix"));
    QStringList trailers;
    if (isFix) {
        if (!secondaryHeader.isEmpty()) {
            trailers.append(QStringLiteral("关联需求单: %1").arg(secondaryHeader));
        }
        if (!issue.isEmpty()) {
            trailers.append(QStringLiteral("关联问题单: %1").arg(issue));
        }
    } else {
        if (!issue.isEmpty()) {
            trailers.append(QStringLiteral("关联需求单: %1").arg(issue));
        }
        if (!secondaryHeader.isEmpty()) {
            trailers.append(QStringLiteral("关联问题单: %1").arg(secondaryHeader));
        }
    }
    trailers.append(QStringLiteral("Co-Authored-By: GerritPilot AI"));
    // Never adopt model-generated authorship or Change-Id; Amend keeps the existing one.
    const QString changeId = extractExistingChangeId(existingMessage);
    if (!changeId.isEmpty()) trailers.append(QStringLiteral("Change-Id: %1").arg(changeId));

    QString result = title + QStringLiteral("\n\n") + summary
        + QStringLiteral("\n\nSummary:\n") + summaryBullets.join(QLatin1Char('\n'))
        + QStringLiteral("\n\nChanges:\n") + bullets.join(QLatin1Char('\n'))
        + QStringLiteral("\n\nVerification:\n") + verification
        + QStringLiteral("\n\n") + trailers.join(QLatin1Char('\n'));
    return result;
}

QString CommitMessageAiService::issueValidationError(const QString &commitType, const QString &issueId,
                                                     const QString &secondaryIssueId) const
{
    QString error;
    CommitMessagePolicy::prepareIssues(commitType, issueId, secondaryIssueId, nullptr, nullptr, &error);
    return error;
}

void CommitMessageAiService::generate(const QString &repoName, const QString &branchName,
                                      const QString &diffText, const QStringList &changedFiles,
                                      const QString &existingMessage, const QString &commitType,
                                      const QString &issueId, bool wholeRepository,
                                      bool manualCompiled, const QString &compileNote, const QString &purpose,
                                      const QString &secondaryIssueId)
{
    cancel();
    m_lastError.clear();
    QString issueHeader, secondaryHeader;
    if (!CommitMessagePolicy::prepareIssues(commitType, issueId, secondaryIssueId,
                                             &issueHeader, &secondaryHeader, &m_lastError)) {
        emit errorOccurred(m_lastError);
        return;
    }
    const QString scopeName = CommitMessagePolicy::scope(repoName);
    const int prefixLength = static_cast<int>(CommitMessagePolicy::titlePrefix(issueHeader, commitType, scopeName).size());
    const int subjectLimit = qMin(48, 72 - prefixLength);
    if (subjectLimit < 1 || scopeName.contains(QLatin1Char('\n')) || scopeName.contains(QLatin1Char('\r'))) {
        m_lastError = tr("单号与模块前缀已占满 72 字符，请缩小提交范围或缩短模块名称。");
        emit errorOccurred(m_lastError);
        return;
    }
    if (!configured()) {
        m_lastError = tr("请先在设置中填写有效的 AI 接口地址和模型名称。");
        emit errorOccurred(m_lastError);
        return;
    }
    setBusy(true);
    m_pendingRepo = repoName;
    m_pendingExistingMsg = existingMessage;
    m_pendingCommitType = commitType;
    m_pendingIssueId = issueHeader;
    m_pendingManualCompiled = manualCompiled;
    m_pendingCompileNote = compileNote;
    m_pendingSecondaryIssueId = secondaryHeader;

    // Encode variable context as JSON so it stays separate from the policy.
    const QJsonObject context{
        {QStringLiteral("module"), scopeName},
        {QStringLiteral("branch"), branchName},
        {QStringLiteral("type"), commitType},
        {QStringLiteral("issue"), issueHeader},
        {QStringLiteral("relatedIssue"), secondaryHeader},
        {QStringLiteral("subjectMaxCharacters"), subjectLimit},
        {QStringLiteral("range"), wholeRepository ? QStringLiteral("当前仓库全部改动") : QStringLiteral("仅暂存区")},
        {QStringLiteral("purpose"), purpose.trimmed().left(300)},
        {QStringLiteral("verification"), manualCompiled ? QStringLiteral("用户确认本地编译通过") : QStringLiteral("Not run")},
        {QStringLiteral("files"), QJsonArray::fromStringList(changedFiles)},
        {QStringLiteral("diff"), diffText.left(wholeRepository ? 8000 : 6000)},
        {QStringLiteral("diffTruncated"), diffText.size() > (wholeRepository ? 8000 : 6000)}
    };
    const QString userPrompt = QStringLiteral("分析以下 JSON 数据，按系统规则返回提交说明内容 JSON：\n")
        + QString::fromUtf8(QJsonDocument(context).toJson(QJsonDocument::Compact));

    QJsonObject messageSystem;
    messageSystem[QStringLiteral("role")] = QStringLiteral("system");
    messageSystem[QStringLiteral("content")] = CommitMessagePolicy::instructions();

    QJsonObject messageUser;
    messageUser[QStringLiteral("role")] = QStringLiteral("user");
    messageUser[QStringLiteral("content")] = userPrompt;

    QJsonArray messages;
    messages.append(messageSystem);
    messages.append(messageUser);

    QJsonObject payload;
    payload[QStringLiteral("model")] = m_modelName.trimmed();
    payload[QStringLiteral("messages")] = messages;
    payload[QStringLiteral("temperature")] = 0.2;
    payload[QStringLiteral("max_tokens")] = 4096;

    const QByteArray data = QJsonDocument(payload).toJson(QJsonDocument::Compact);

    m_currentReply = m_nam.post(makeRequest(), data);
    connect(m_currentReply, &QNetworkReply::finished, this, &CommitMessageAiService::onReplyFinished);

    // Generating a full commit message is substantially slower than the short
    // connectivity probe. Keep a finite total deadline without sharing its 15s limit.
    constexpr int GenerationTimeoutMs = 120000;
    QPointer<QNetworkReply> pending(m_currentReply);
    QTimer::singleShot(GenerationTimeoutMs, this, [this, pending]() {
        if (pending && m_currentReply == pending.data()) {
            cancel();
            m_lastError = tr("AI 生成超时（120 秒）");
            emit errorOccurred(m_lastError);
        }
    });
}

void CommitMessageAiService::onReplyFinished()
{
    if (!m_currentReply || sender() != m_currentReply)
        return;

    QNetworkReply *reply = m_currentReply;
    m_currentReply = nullptr;

    if (reply->error() != QNetworkReply::NoError) {
        const QString errStr = reply->errorString();
        const QByteArray errBody = reply->readAll();
        m_lastError = QStringLiteral("API 请求失败 (%1): %2").arg(errStr, QString::fromUtf8(errBody));
        emit errorOccurred(m_lastError);

        setBusy(false);
        reply->deleteLater();
        return;
    }

    const QByteArray responseData = reply->readAll();
    reply->deleteLater();

    const QJsonDocument doc = QJsonDocument::fromJson(responseData);
    if (!doc.isObject()) {
        m_lastError = QStringLiteral("API 返回数据格式错误");
        emit errorOccurred(m_lastError);
        setBusy(false);
        return;
    }

    const QJsonObject obj = doc.object();
    const QJsonArray choices = obj.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty()) {
        m_lastError = QStringLiteral("API 未返回有效候选结果");
        emit errorOccurred(m_lastError);
        setBusy(false);
        return;
    }

    const QJsonObject firstChoice = choices.first().toObject();
    const QString finishReason = firstChoice.value(QStringLiteral("finish_reason")).toString();
    const QJsonObject message = firstChoice.value(QStringLiteral("message")).toObject();
    const QString content = message.value(QStringLiteral("content")).toString();

    if (content.trimmed().isEmpty()) {
        if (finishReason == QStringLiteral("length")) {
            m_lastError = tr("AI 思考过程消耗了过多 Token 导致生成被截断，请缩小改动范围或调整模型后重试。");
        } else {
            m_lastError = tr("API 返回的提交说明为空");
        }
        setBusy(false);
        emit errorOccurred(m_lastError);
        return;
    }

    m_lastCandidate = validateAndNormalize(content, m_pendingRepo, m_pendingExistingMsg,
                                           m_pendingCommitType, m_pendingIssueId,
                                           m_pendingManualCompiled, m_pendingCompileNote,
                                           m_pendingSecondaryIssueId);
    if (m_lastCandidate.isEmpty()) {
        m_lastError = tr("AI 返回的结构或篇幅不符合提交规范，请补充改动目的后重新生成；原说明已保留。");
        setBusy(false);
        emit errorOccurred(m_lastError);
        return;
    }
    setBusy(false);
    emit candidateReady(m_lastCandidate);
}
