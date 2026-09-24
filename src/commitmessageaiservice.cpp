#include "commitmessageaiservice.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
#include <QTimer>
#include <QPointer>

#include <QSettings>

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
    m_apiKey = settings.value(QStringLiteral("ai/apiKey")).toString();
    m_modelName = settings.value(QStringLiteral("ai/model")).toString();

}

void CommitMessageAiService::saveConfig()
{
    QSettings settings;
    settings.setValue(QStringLiteral("ai/endpoint"), m_apiEndpoint);
    settings.setValue(QStringLiteral("ai/apiKey"), m_apiKey);
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
    static const QRegularExpression changeIdRegex(QStringLiteral(R"(Change-Id:\s*(I[0-9a-fA-F]{40}))"));
    const auto match = changeIdRegex.match(message);
    if (match.hasMatch())
        return match.captured(1);
    return QString();
}

QString CommitMessageAiService::validateAndNormalize(const QString &rawCandidate,
                                                     const QString &repoName,
                                                     const QString &existingMessage,
                                                     const QString &commitType,
                                                     const QString &issueId)
{
    QString text = rawCandidate.trimmed();

    // Strip Markdown code block wrappers if any
    if (text.startsWith(QStringLiteral("```"))) {
        int firstNewline = text.indexOf(QLatin1Char('\n'));
        if (firstNewline != -1)
            text = text.mid(firstNewline + 1);
        if (text.endsWith(QStringLiteral("```")))
            text.chop(3);
        text = text.trimmed();
    }

    // Process issueId tokens
    QString issueHeader;
    QString issueFooterText;
    if (issueId.trimmed().isEmpty()) {
        issueHeader = QStringLiteral("m-0");
        issueFooterText = QStringLiteral("N/A");
    } else {
        const QStringList ids = issueId.split(QRegularExpression(QStringLiteral("[,;，；\\s]+")), Qt::SkipEmptyParts);
        QStringList formattedTokens;
        for (const QString &id : ids) {
            QString token = id.trimmed();
            if (token.isEmpty()) continue;
            static const QRegularExpression pureNum(QStringLiteral("^[0-9]{4,}$"));
            if (pureNum.match(token).hasMatch())
                token = QStringLiteral("m-") + token;
            if (!formattedTokens.contains(token, Qt::CaseInsensitive))
                formattedTokens.append(token);
        }
        issueHeader = formattedTokens.isEmpty() ? QStringLiteral("m-0") : formattedTokens.join(QStringLiteral(","));
        issueFooterText = formattedTokens.isEmpty() ? QStringLiteral("N/A") : formattedTokens.join(QStringLiteral(", "));
    }

    // Enforce the subject line format: <issueHeader> <type>(<scope>): <summary>
    QStringList lines = text.split(QLatin1Char('\n'));
    if (!lines.isEmpty()) {
        QString firstLine = lines.first().trimmed();
        static const QRegularExpression titleRegex(
            QStringLiteral(R"(^(?:[a-zA-Z0-9_-]+:\s*)?(?:(?:[a-zA-Z0-9._-]+(?:\s*,\s*[a-zA-Z0-9._-]+)*)\s+)?(?:fix|feat|chg|refactor|docs|test|style|chore|add|del|merge)\([^)]+\):\s*)"),
            QRegularExpression::CaseInsensitiveOption);
        firstLine.remove(titleRegex);
        const QString scope = repoName.isEmpty() ? QStringLiteral("core") : repoName;
        lines[0] = QStringLiteral("%1 %2(%3): %4")
            .arg(issueHeader, commitType, scope, firstLine.trimmed());
        text = lines.join(QLatin1Char('\n'));
    }

    // Extract any existing Change-Id (from amend existingMessage or from generated text)
    QString changeId = extractExistingChangeId(existingMessage);
    if (changeId.isEmpty()) {
        static const QRegularExpression changeIdLine(QStringLiteral(R"((?m)^Change-Id:\s*(I[0-9a-fA-F]{40})\s*$)"));
        const auto match = changeIdLine.match(text);
        if (match.hasMatch()) {
            changeId = match.captured(1);
        }
    }

    // Clean trailers from body to avoid duplicates
    static const QRegularExpression issueLine(QStringLiteral(R"((?m)^Issue:\s*[^\n]*\n?)"));
    text.remove(issueLine);
    static const QRegularExpression coAuthorLine(QStringLiteral(R"((?m)^Co-[Aa]uthored-[Bb]y:\s*[^\n]*\n?)"));
    text.remove(coAuthorLine);
    static const QRegularExpression changeIdRegex(QStringLiteral(R"((?m)^Change-Id:\s*[^\n]*\n?)"));
    text.remove(changeIdRegex);

    text = text.trimmed();

    // Append standard Gerrit trailers in strict order
    QStringList trailers;
    trailers.append(QStringLiteral("Issue: %1").arg(issueFooterText));
    trailers.append(QStringLiteral("Co-Authored-By: AI Coding Agent"));
    if (!changeId.isEmpty()) {
        trailers.append(QStringLiteral("Change-Id: %1").arg(changeId));
    }

    text = text + QStringLiteral("\n\n") + trailers.join(QLatin1Char('\n'));
    return text.trimmed();
}

void CommitMessageAiService::generate(const QString &repoName, const QString &branchName,
                                      const QString &diffText, const QStringList &changedFiles,
                                      const QString &existingMessage, const QString &commitType,
                                      const QString &issueId, bool wholeRepository)
{
    cancel();
    m_lastError.clear();
    if (!configured()) {
        m_lastError = tr("请先在设置中填写有效的 AI 接口地址和模型名称。");
        emit errorOccurred(m_lastError);
        return;
    }
    setBusy(true);
    m_pendingRepo = repoName;
    m_pendingExistingMsg = existingMessage;
    m_pendingCommitType = commitType;
    m_pendingIssueId = issueId;

    QString issueHeader;
    if (issueId.trimmed().isEmpty()) {
        issueHeader = QStringLiteral("m-0");
    } else {
        const QStringList ids = issueId.split(QRegularExpression(QStringLiteral("[,;，；\\s]+")), Qt::SkipEmptyParts);
        QStringList formattedTokens;
        for (const QString &id : ids) {
            QString token = id.trimmed();
            if (token.isEmpty()) continue;
            static const QRegularExpression pureNum(QStringLiteral("^[0-9]{4,}$"));
            if (pureNum.match(token).hasMatch())
                token = QStringLiteral("m-") + token;
            if (!formattedTokens.contains(token, Qt::CaseInsensitive))
                formattedTokens.append(token);
        }
        issueHeader = formattedTokens.isEmpty() ? QStringLiteral("m-0") : formattedTokens.join(QStringLiteral(","));
    }

    const QString scopeName = repoName.isEmpty() ? QStringLiteral("core") : repoName;
    const QString issueSummary = issueId.trimmed().isEmpty() ? QStringLiteral("N/A") : issueHeader;

    // Prepare prompt adhering to company repo-commit skill & Gerrit standards
    QString userPrompt = QStringLiteral(
        "请根据以下代码差异（Git Diff），生成严格符合公司 repo + Gerrit 提交流程规范的 Commit Message。\n\n"
        "【上下文信息】\n"
        "- 模块名称: %1\n"
        "- 分支名称: %2\n"
        "- 提交类型: %3\n"
        "- 需求/问题单号: %4\n"
        "- 改动范围: %5\n"
        "- 涉及文件:\n%6\n\n"
        "【代码差异（可能截断）】:\n%7\n\n"
        "【严格生成规范】:\n"
        "1. 第一行 (Subject): %4 %3(%1): <简明中文主题>\n"
        "   - 中文优先，技术术语保留英文，长度严格限制在 72 字符以内。\n"
        "2. 第二段 (Summary):\n"
        "   - 概括本次提交的目的和背景，着重阐述【why 而非 what】（为什么改、解决什么业务或系统问题，而非机械罗列代码），中文优先。\n"
        "3. 第三段 (Changes):\n"
        "   - 以 'Changes:' 开头，逐条（以 '- ' 开头）列出涉及文件的具体修改要点，技术术语保留英文。\n"
        "4. 第四段 (Verification):\n"
        "   - 以 'Verification:' 开头。\n"
        "   - 若无明确的测试验证结果，必须输出：\n"
        "     - Not run; reason: 嵌入式交叉编译环境，无可用的本地构建/检查命令\n"
        "     （或 - Not run），严禁虚构或谎称已通过编译和测试。\n"
        "5. 尾部字段 (Trailers):\n"
        "   - Issue: %8\n"
        "   - Co-Authored-By: AI Coding Agent\n\n"
        "【质量与合规检查（若在 diff 中发现以下问题，请在 Changes 之后、Verification 之前以 'Notice:' 逐条客观提示开发者）】:\n"
        "- 调试残留: 是否包含 qDebug()、console.log、debugger、临时 printf 等\n"
        "- 未带单号的 TODO / FIXME / HACK\n"
        "- 硬编码密钥、Token、密码\n"
        "- 临时 mock 或桩代码\n\n"
        "请直接输出提交说明文本，不要添加任何 Markdown 代码块标记（如 ```）。"
    ).arg(scopeName,
          branchName,
          commitType,
          issueHeader,
          wholeRepository ? QStringLiteral("当前仓库全部改动（含未暂存与可读取的未跟踪文本文件）")
                          : QStringLiteral("仅暂存区"),
          changedFiles.join(QLatin1Char('\n')),
          diffText.left(wholeRepository ? 8000 : 6000),
          issueSummary);

    QJsonObject messageSystem;
    messageSystem[QStringLiteral("role")] = QStringLiteral("system");
    messageSystem[QStringLiteral("content")] = QStringLiteral(
        "你是一个严谨的车载系统与嵌入式软件工程师，精通 repo + Gerrit 提交流程与规范，擅长编写高质量、高规范性的 Git Commit Message。"
    );

    QJsonObject messageUser;
    messageUser[QStringLiteral("role")] = QStringLiteral("user");
    messageUser[QStringLiteral("content")] = userPrompt;

    QJsonArray messages;
    messages.append(messageSystem);
    messages.append(messageUser);

    QJsonObject payload;
    payload[QStringLiteral("model")] = m_modelName;
    payload[QStringLiteral("messages")] = messages;
    payload[QStringLiteral("temperature")] = 0.2;

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
    const QJsonObject message = firstChoice.value(QStringLiteral("message")).toObject();
    const QString content = message.value(QStringLiteral("content")).toString();

    if (content.trimmed().isEmpty()) {
        m_lastError = tr("API 返回的提交说明为空");
        setBusy(false);
        emit errorOccurred(m_lastError);
        return;
    }

    m_lastCandidate = validateAndNormalize(content, m_pendingRepo, m_pendingExistingMsg,
                                           m_pendingCommitType, m_pendingIssueId);
    setBusy(false);
    emit candidateReady(m_lastCandidate);
}
