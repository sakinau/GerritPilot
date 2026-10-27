#include "gerritservice.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QXmlStreamReader>

namespace {
constexpr qint64 MaxHookBytes = 256 * 1024;
}

GerritService::GerritService(QObject *parent)
    : QObject(parent)
{
}

GitCommand GerritService::createQueryHookPathCommand(int repoIndex, const QString &repoAbsPath,
                                                    const QString &repoRelPath, GitCommandKind kind) const
{
    GitCommand command;
    command.arguments = {QStringLiteral("rev-parse"),
        QStringLiteral("--path-format=absolute"), QStringLiteral("--git-path"),
        QStringLiteral("hooks/commit-msg")};
    command.workingDirectory = repoAbsPath;
    command.repositoryIndex = repoIndex;
    command.repositoryPath = repoRelPath;
    command.kind = kind;
    command.title = tr("检查 Change-Id Hook");
    command.stopOnFailure = kind != GitCommandKind::HookStatus;
    command.timeoutMilliseconds = 5000;
    return command;
}

bool GerritService::hookSourceFromRemoteUrl(const QString &url, QString *source,
                                            QString *port, QString *error)
{
    const QString remote = url.trimmed();
    QString host;
    QString user;
    QString remotePort;
    if (remote.startsWith(QStringLiteral("http://")) || remote.startsWith(QStringLiteral("https://"))) {
        QUrl parsed(remote);
        if (!parsed.isValid() || parsed.host().isEmpty()) {
            if (error) *error = QObject::tr("推送 Remote 不是可识别的 HTTP(S) 地址，无法从 Gerrit 下载 Hook。");
            return false;
        }
        // Gerrit serves authenticated Git under <base>/a/<project>; the hook lives at <base>/tools/hooks.
        // Without "/a/" the base path is unknown, so fall back to the host root.
        const QString path = parsed.path();
        const int authenticated = path.indexOf(QStringLiteral("/a/"));
        const QString base = authenticated > 0 ? path.left(authenticated) : QString();
        QUrl hookUrl;
        hookUrl.setScheme(parsed.scheme());
        hookUrl.setHost(parsed.host());
        hookUrl.setPort(parsed.port());
        hookUrl.setPath(base + QStringLiteral("/tools/hooks/commit-msg"));
        if (source) *source = hookUrl.toString();
        if (port) port->clear();
        return true;
    }
    if (remote.startsWith(QStringLiteral("ssh://"))) {
        const QUrl parsed(remote);
        host = parsed.host();
        user = parsed.userName();
        if (parsed.port() > 0) remotePort = QString::number(parsed.port());
    } else {
        static const QRegularExpression scpUrl(
            QStringLiteral(R"(^(?:([^@:/]+)@)?([^@:/]+):[^\s]+$)"));
        const auto match = scpUrl.match(remote);
        if (match.hasMatch()) {
            user = match.captured(1);
            host = match.captured(2);
        }
    }
    static const QRegularExpression safeHost(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]*$"));
    static const QRegularExpression safeUser(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]*$"));
    if (!safeHost.match(host).hasMatch() || (!user.isEmpty() && !safeUser.match(user).hasMatch())) {
        if (error) *error = QObject::tr("推送 Remote 不是可识别的 SSH 或 HTTP(S) 地址，无法从 Gerrit 安装 Hook。");
        return false;
    }
    if (source) *source = (user.isEmpty() ? host : user + QLatin1Char('@') + host)
        + QStringLiteral(":hooks/commit-msg");
    if (port) *port = remotePort;
    return true;
}

GitCommand GerritService::createQueryHookRemoteCommand(int repoIndex, const QString &repoAbsPath,
                                                       const QString &repoRelPath,
                                                       const QString &remote) const
{
    GitCommand command;
    command.program = QStringLiteral("git");
    command.arguments = {QStringLiteral("remote"), QStringLiteral("get-url"),
                         QStringLiteral("--push"), remote};
    command.kind = GitCommandKind::HookRemoteUrl;
    command.workingDirectory = repoAbsPath;
    command.repositoryIndex = repoIndex;
    command.repositoryPath = repoRelPath;
    command.title = tr("读取 Gerrit 推送地址");
    command.stopOnFailure = true;
    return command;
}

GitCommand GerritService::createInstallHookCommand(const GitCommand &completedQueryCommand,
                                                   const QString &hookPath, const QString &temporaryPath) const
{
    GitCommand command;
    command.program = QStringLiteral("scp");
    command.arguments = {QStringLiteral("-O"), QStringLiteral("-p"),
                         QStringLiteral("-B"), QStringLiteral("-o"),
                         QStringLiteral("ConnectTimeout=8")};
    if (!completedQueryCommand.hook.port.isEmpty())
        command.arguments.append({QStringLiteral("-P"), completedQueryCommand.hook.port});
    command.arguments.append({completedQueryCommand.hook.source, temporaryPath});
    command.kind = GitCommandKind::InstallHook;
    command.workingDirectory = completedQueryCommand.workingDirectory;
    command.repositoryIndex = completedQueryCommand.repositoryIndex;
    command.repositoryPath = completedQueryCommand.repositoryPath;
    command.hook = completedQueryCommand.hook;
    command.hook.targetPath = hookPath;
    command.hook.temporaryPath = temporaryPath;
    command.title = tr("安装 Change-Id Hook");
    command.stopOnFailure = true;
    command.timeoutMilliseconds = 30000;
    return command;
}

void GerritService::finalizeHookPermissions(const QString &hookPath)
{
    if (QFile::exists(hookPath)) {
        QFile::setPermissions(hookPath, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner
                                      | QFile::ReadGroup | QFile::ExeGroup
                                      | QFile::ReadOther | QFile::ExeOther);
    }
}

bool GerritService::isHttpHookSource(const QString &source)
{
    return source.startsWith(QStringLiteral("http://")) || source.startsWith(QStringLiteral("https://"));
}

void GerritService::downloadHook(const GitCommand &context, const QString &hookPath,
                                 const QString &temporaryPath)
{
    cancelHookDownload();
    GitCommand command = context;
    command.kind = GitCommandKind::InstallHook;
    command.hook.targetPath = hookPath;
    command.hook.temporaryPath = temporaryPath;
    command.title = tr("下载 Change-Id Hook");

    QNetworkRequest request{QUrl(context.hook.source)};
    request.setTransferTimeout(30000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_network.get(request);
    m_hookReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, command] {
        if (m_hookReply == reply) m_hookReply = nullptr;
        reply->deleteLater();
        QString error;
        if (reply->error() != QNetworkReply::NoError) {
            error = tr("从 %1 下载 Hook 失败：%2").arg(command.hook.source, reply->errorString());
        } else {
            const QByteArray body = reply->read(MaxHookBytes + 1);
            // Reject login pages and other HTML so they never become an executable hook.
            if (body.size() > MaxHookBytes || !body.startsWith("#!")) {
                error = tr("%1 返回的内容不是 commit-msg 脚本；Gerrit 可能部署在子路径下或需要登录。")
                    .arg(command.hook.source);
            } else {
                QFile file(command.hook.temporaryPath);
                if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(body) != body.size())
                    error = tr("无法写入 Hook 临时文件。");
            }
        }
        emit hookDownloadFinished(command, error.isEmpty(), error);
    });
}

void GerritService::cancelHookDownload()
{
    if (!m_hookReply) return;
    auto *reply = m_hookReply.data();
    m_hookReply = nullptr;
    reply->abort();
}

GitCommand GerritService::createQueryRemoteBranchesCommand(int repoIndex, const QString &repoAbsPath,
                                                           const QString &repoRelPath,
                                                           const QString &remote) const
{
    GitCommand command;
    command.program = QStringLiteral("git");
    command.arguments = {QStringLiteral("ls-remote"), QStringLiteral("--heads"), remote};
    command.kind = GitCommandKind::RemoteBranches;
    command.workingDirectory = repoAbsPath;
    command.repositoryIndex = repoIndex;
    command.repositoryPath = repoRelPath;
    command.title = tr("查询远端目标分支 %1").arg(remote);
    return command;
}

QStringList GerritService::splitList(const QString &text)
{
    static const QRegularExpression separators(QStringLiteral("[,;\\s]+"));
    QStringList items;
    for (const QString &item : text.split(separators, Qt::SkipEmptyParts)) {
        if (!items.contains(item)) items.append(item);
    }
    return items;
}

bool GerritService::validatePushOptions(const GerritPushOptions &options, QString *error)
{
    if (!GitService::validateTopicName(options.topic, error)) return false;
    // Values are embedded in the refspec, so separators such as ',' and '%' must never reach it.
    static const QRegularExpression account(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._@-]*$"));
    for (const QString &item : options.reviewers + options.cc) {
        if (!account.match(item).hasMatch()) {
            if (error) *error = tr("评审人或抄送“%1”无效：请填写 Gerrit 用户名或邮箱，多个之间用逗号或空格分隔。").arg(item);
            return false;
        }
    }
    static const QRegularExpression hashtag(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]*$"));
    for (const QString &item : options.hashtags) {
        if (!hashtag.match(item).hasMatch()) {
            if (error) *error = tr("Hashtag“%1”无效：只能包含字母、数字、点、下划线和连字符。").arg(item);
            return false;
        }
    }
    if (options.message.size() > 500) {
        if (error) *error = tr("补丁集说明过长，请控制在 500 个字符以内。");
        return false;
    }
    return true;
}

QString GerritService::formatReviewRef(const QString &targetBranch, const GerritPushOptions &options)
{
    QStringList parts;
    if (!options.topic.trimmed().isEmpty())
        parts.append(QStringLiteral("topic=") + options.topic.trimmed());
    for (const QString &reviewer : options.reviewers)
        parts.append(QStringLiteral("r=") + reviewer);
    for (const QString &cc : options.cc)
        parts.append(QStringLiteral("cc=") + cc);
    for (const QString &hashtag : options.hashtags)
        parts.append(QStringLiteral("t=") + hashtag);
    if (options.workState == GerritPushOptions::WorkInProgress)
        parts.append(QStringLiteral("wip"));
    else if (options.workState == GerritPushOptions::ReadyForReview)
        parts.append(QStringLiteral("ready"));
    if (options.privateChange)
        parts.append(QStringLiteral("private"));
    const QString message = options.message.simplified();
    if (!message.isEmpty()) {
        // Gerrit requires these reserved characters to be percent-encoded in m=.
        parts.append(QStringLiteral("m=") + QString::fromLatin1(
            QUrl::toPercentEncoding(message, QByteArray(), QByteArrayLiteral("-._~"))));
    }
    QString reviewRef = QStringLiteral("HEAD:refs/for/") + targetBranch.trimmed();
    if (!parts.isEmpty())
        reviewRef += QLatin1Char('%') + parts.join(QLatin1Char(','));
    return reviewRef;
}

QString GerritService::pushPreview(const QString &remote, const QString &targetBranch,
                                   const GerritPushOptions &options)
{
    return QStringLiteral("git push %1 %2").arg(remote, formatReviewRef(targetBranch, options));
}

GitCommand GerritService::createPushCommand(int repoIndex, const QString &repoAbsPath,
                                            const QString &repoRelPath, const QString &remote,
                                            const QString &targetBranch, const GerritPushOptions &options) const
{
    GitCommand command;
    command.program = QStringLiteral("git");
    command.arguments = {QStringLiteral("push"), remote, formatReviewRef(targetBranch, options)};
    command.kind = GitCommandKind::GerritPush;
    command.workingDirectory = repoAbsPath;
    command.repositoryIndex = repoIndex;
    command.repositoryPath = repoRelPath;
    command.title = tr("上传到 Gerrit");
    command.stopOnFailure = true;
    command.affectedRepositories = {repoRelPath};
    return command;
}

bool GerritService::parseChangeSpec(const QString &spec, int *change, int *patchset, QString *error)
{
    const QString text = spec.trimmed();
    static const QList<QRegularExpression> patterns = {
        QRegularExpression(QStringLiteral(R"(^refs/changes/\d{2}/(\d+)/(\d+)$)")),
        QRegularExpression(QStringLiteral(R"(^(\d+)(?:\s*[/,]\s*(\d+))?$)")),
        // New UI: .../c/<project>/+/<change>[/<patchset>]; old UI: .../#/c/<change>[/<patchset>]
        QRegularExpression(QStringLiteral(R"(^https?://\S*?/\+/(\d+)(?:/(\d+))?(?:[/?#]\S*)?$)")),
        QRegularExpression(QStringLiteral(R"(^https?://\S*?/#/c/(\d+)(?:/(\d+))?(?:[/?#]\S*)?$)")),
        QRegularExpression(QStringLiteral(R"(^https?://[^/\s]+/(?:c/)?(\d+)(?:/(\d+))?/?$)")),
    };
    for (const QRegularExpression &pattern : patterns) {
        const auto match = pattern.match(text);
        if (!match.hasMatch()) continue;
        bool ok = false;
        const int number = match.captured(1).toInt(&ok);
        const int revision = match.captured(2).isEmpty() ? 0 : match.captured(2).toInt();
        if (!ok || number <= 0 || revision < 0) break;
        if (change) *change = number;
        if (patchset) *patchset = revision;
        return true;
    }
    if (error)
        *error = tr("无法识别 Change：请输入 Change 号（如 12345 或 12345/3）、refs/changes/... 引用或 Gerrit 评审链接。");
    return false;
}

QString GerritService::changeRef(int change, int patchset)
{
    return QStringLiteral("refs/changes/%1/%2/%3")
        .arg(change % 100, 2, 10, QLatin1Char('0'))
        .arg(change)
        .arg(patchset > 0 ? QString::number(patchset) : QStringLiteral("*"));
}

int GerritService::latestPatchset(const QString &lsRemoteOutput, int change)
{
    const QRegularExpression ref(QStringLiteral(R"(\trefs/changes/\d{2}/%1/(\d+)$)").arg(change));
    int latest = 0;
    for (const QString &line : lsRemoteOutput.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const auto match = ref.match(line.trimmed());
        if (match.hasMatch()) latest = qMax(latest, match.captured(1).toInt());
    }
    return latest;
}

QString GerritService::branchFromRevision(const QString &revision)
{
    QString branch = revision.trimmed();
    if (branch.startsWith(QStringLiteral("refs/heads/")))
        branch = branch.mid(11);
    static const QRegularExpression sha(QStringLiteral("^[0-9a-fA-F]{40,64}$"));
    if (branch.startsWith(QStringLiteral("refs/")) || sha.match(branch).hasMatch()
        || !GitService::validateBranchName(branch))
        return {};
    return branch;
}

QHash<QString, GerritService::ManifestTarget> GerritService::parseManifestTargets(const QByteArray &xml)
{
    // Follows repo upload: dest-branch (project, then default) wins over revision
    // (project, remote, default); upstream is the last resort for pinned revisions.
    QHash<QString, QString> remoteRevisions;
    QString defaultRemote;
    QString defaultRevision;
    QString defaultDestBranch;
    struct Project { QString path; QString remote; QString revision; QString destBranch; QString upstream; };
    QList<Project> projects;
    QStringList parents;
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isEndElement() && reader.name() == QLatin1String("project")) {
            if (!parents.isEmpty()) parents.removeLast();
            continue;
        }
        if (!reader.isStartElement()) continue;
        const auto attributes = reader.attributes();
        const auto value = [&attributes](const char *name) {
            return attributes.value(QLatin1String(name)).toString().trimmed();
        };
        if (reader.name() == QLatin1String("remote")) {
            remoteRevisions.insert(value("name"), value("revision"));
        } else if (reader.name() == QLatin1String("default")) {
            defaultRemote = value("remote");
            defaultRevision = value("revision");
            defaultDestBranch = value("dest-branch");
        } else if (reader.name() == QLatin1String("project")) {
            QString path = value("path");
            if (path.isEmpty()) path = value("name");
            // Nested projects are relative to their parent checkout.
            if (!parents.isEmpty()) path = parents.last() + QLatin1Char('/') + path;
            path = QDir::cleanPath(path);
            parents.append(path);
            projects.append({path, value("remote"), value("revision"), value("dest-branch"), value("upstream")});
        }
    }
    QHash<QString, ManifestTarget> targets;
    if (reader.hasError()) return targets;
    for (const Project &project : projects) {
        ManifestTarget target;
        target.remote = project.remote.isEmpty() ? defaultRemote : project.remote;
        QString revision = project.revision;
        if (revision.isEmpty()) revision = remoteRevisions.value(target.remote);
        if (revision.isEmpty()) revision = defaultRevision;
        const QString destBranch = project.destBranch.isEmpty() ? defaultDestBranch : project.destBranch;
        target.revision = destBranch.isEmpty() ? revision : destBranch;
        target.branch = branchFromRevision(target.revision);
        if (target.branch.isEmpty())
            target.branch = branchFromRevision(project.upstream);
        targets.insert(project.path, target);
    }
    return targets;
}

QString GerritService::parseGerritError(const QByteArray &stderrData, const QString &defaultDiagnostic)
{
    const QString stderrLower = QString::fromLocal8Bit(stderrData).toLower();
    QString errorMsg;
    const QString credentials = credentialHint(stderrData);
    if (!credentials.isEmpty()) {
        errorMsg = credentials;
    } else if (stderrLower.contains(QStringLiteral("permission denied")) || stderrLower.contains(QStringLiteral("authentication failed"))) {
        errorMsg = tr("远端拒绝访问：可能是身份认证或项目权限问题。请检查 SSH 密钥或凭据。");
    } else if (stderrLower.contains(QStringLiteral("missing change-id"))) {
        errorMsg = tr("提交缺少 Change-Id。请为当前仓库安装 Hook，并修订缺少 Change-Id 的提交后重试。");
    } else if (stderrLower.contains(QStringLiteral("non-fast-forward")) || stderrLower.contains(QStringLiteral("fetch first"))) {
        errorMsg = tr("远端分支包含未同步的新提交（非快进），请先拉取或同步代码。");
    } else if (stderrLower.contains(QStringLiteral("does not exist")) || stderrLower.contains(QStringLiteral("not found"))) {
        errorMsg = tr("远端报告资源不存在，请检查原始错误中的仓库或目标分支。");
    } else if (stderrLower.contains(QStringLiteral("no new changes"))) {
        errorMsg = tr("Gerrit 提示：当前提交与已有的 Change 相同，没有新的改动需要推送。");
    } else if (stderrLower.contains(QStringLiteral("prohibited by gerrit"))) {
        errorMsg = tr("Gerrit 拒绝推送：可能是直接推送到了受保护的分支（需推送到 refs/for/ 分支）。");
    } else {
        errorMsg = defaultDiagnostic;
    }

    if (errorMsg != defaultDiagnostic && !defaultDiagnostic.isEmpty())
        errorMsg += tr("\n\n原始错误：\n") + defaultDiagnostic;

    return errorMsg;
}

QString GerritService::extractReviewUrl(const QByteArray &stdoutData, const QByteArray &stderrData)
{
    const QString combined = QString::fromLocal8Bit(stdoutData) + QLatin1Char('\n') + QString::fromLocal8Bit(stderrData);
    static const QRegularExpression reviewUrlRegex(QStringLiteral(R"((https?://[^\s<>"']+/(?:c/[^\s<>"']+/)?\+/\d+|https?://[^\s<>"']+/#/c/\d+))"));
    const auto match = reviewUrlRegex.match(combined);
    if (match.hasMatch())
        return match.captured(1);
    return QString();
}

QString GerritService::credentialHint(const QByteArray &stderrData)
{
    // Prompts are disabled for GUI processes, so HTTP remotes without a stored credential fail here.
    const QString stderrLower = QString::fromLocal8Bit(stderrData).toLower();
    if (!stderrLower.contains(QStringLiteral("could not read username"))
        && !stderrLower.contains(QStringLiteral("could not read password"))
        && !stderrLower.contains(QStringLiteral("terminal prompts disabled"))
        && !stderrLower.contains(QStringLiteral("returned error: 401")))
        return {};
    return tr("HTTPS 远端需要已保存的 Git 凭据（本程序不会弹出密码输入框）。请按以下步骤配置一次：\n"
              "1. 在 Gerrit 网页“Settings → HTTP Credentials”生成 HTTP 密码（不是登录密码）；\n"
              "2. 在终端执行：git config --global credential.helper store\n"
              "   （或使用 libsecret 等更安全的凭据助手）；\n"
              "3. 在终端进入该仓库执行一次 git fetch，按提示输入 Gerrit 用户名和 HTTP 密码；\n"
              "4. 回到本程序重试。");
}

bool GerritService::checkSshConfigured(const QString &host)
{
    const QString configPath = QDir::homePath() + QStringLiteral("/.ssh/config");
    QFile file(configPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    const QString content = QString::fromUtf8(file.readAll());
    return content.contains(host);
}

QString GerritService::sshConfigTemplate(const QString &username, const QString &host, int port)
{
    const QString effectiveUser = username.trimmed().isEmpty() ? QStringLiteral("<你的工号或用户名>") : username.trimmed();
    return QStringLiteral(
        "# === Gerrit SSH Configuration ===\n"
        "Host %1\n"
        "    HostName %1\n"
        "    Port %2\n"
        "    User %3\n"
        "    IdentityFile ~/.ssh/id_rsa\n"
        "    PreferredAuthentications publickey\n"
        "    ServerAliveInterval 60\n"
        "    ServerAliveCountMax 3\n"
    ).arg(host).arg(port).arg(effectiveUser);
}
