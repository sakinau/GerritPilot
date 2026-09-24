#include "gerritservice.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QUrl>

GerritService::GerritService(QObject *parent)
    : QObject(parent)
{
}

void GerritService::setLastReviewUrl(const QString &url)
{
    if (m_lastReviewUrl != url) {
        m_lastReviewUrl = url;
        emit lastReviewUrlChanged(m_lastReviewUrl);
    }
}

bool GerritService::checkHasCommitHook(const QString &repoAbsPath)
{
    const QFileInfo hook(effectiveHookPath(repoAbsPath));
    return hook.isFile() && hook.isExecutable();
}

QString GerritService::effectiveHookPath(const QString &repoAbsPath)
{
    if (repoAbsPath.isEmpty()) return {};
    QProcess process;
    process.setWorkingDirectory(repoAbsPath);
    process.start(QStringLiteral("git"), {QStringLiteral("rev-parse"),
        QStringLiteral("--path-format=absolute"), QStringLiteral("--git-path"),
        QStringLiteral("hooks/commit-msg")});
    if (!process.waitForStarted(1000) || !process.waitForFinished(1000)
        || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return {};
    return QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
}

bool GerritService::hookSourceFromRemoteUrl(const QString &url, QString *source,
                                            QString *port, QString *error)
{
    const QString remote = url.trimmed();
    QString host;
    QString user;
    QString remotePort;
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
        if (error) *error = QObject::tr("推送 Remote 不是可识别的 SSH 地址，无法从 Gerrit 安装 Hook。");
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
                                                   const QString &hookPath) const
{
    GitCommand command;
    command.program = QStringLiteral("scp");
    command.arguments = {QStringLiteral("-O"), QStringLiteral("-p"),
                         QStringLiteral("-B"), QStringLiteral("-o"),
                         QStringLiteral("ConnectTimeout=8")};
    if (!completedQueryCommand.paths.value(1).isEmpty())
        command.arguments.append({QStringLiteral("-P"), completedQueryCommand.paths.value(1)});
    command.arguments.append({completedQueryCommand.paths.value(0), hookPath});
    command.kind = GitCommandKind::InstallHook;
    command.workingDirectory = completedQueryCommand.workingDirectory;
    command.repositoryIndex = completedQueryCommand.repositoryIndex;
    command.repositoryPath = completedQueryCommand.repositoryPath;
    command.paths = {hookPath};
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

QString GerritService::formatReviewRef(const QString &targetBranch, const QString &topic)
{
    QString reviewRef = QStringLiteral("HEAD:refs/for/") + targetBranch.trimmed();
    if (!topic.trimmed().isEmpty())
        reviewRef += QStringLiteral("%topic=") + topic.trimmed();
    return reviewRef;
}

QString GerritService::pushPreview(const QString &remote, const QString &targetBranch, const QString &topic)
{
    return QStringLiteral("git push %1 %2").arg(remote, formatReviewRef(targetBranch, topic));
}

GitCommand GerritService::createPushCommand(int repoIndex, const QString &repoAbsPath,
                                            const QString &repoRelPath, const QString &remote,
                                            const QString &targetBranch, const QString &topic) const
{
    GitCommand command;
    command.program = QStringLiteral("git");
    command.arguments = {QStringLiteral("push"), remote, formatReviewRef(targetBranch, topic)};
    command.kind = GitCommandKind::GerritPush;
    command.workingDirectory = repoAbsPath;
    command.repositoryIndex = repoIndex;
    command.repositoryPath = repoRelPath;
    command.title = tr("上传到 Gerrit");
    command.stopOnFailure = true;
    command.affectedRepositories = {repoRelPath};
    return command;
}

QString GerritService::parseGerritError(const QByteArray &stderrData, const QString &defaultDiagnostic)
{
    const QString stderrLower = QString::fromLocal8Bit(stderrData).toLower();
    QString errorMsg;
    if (stderrLower.contains(QStringLiteral("permission denied")) || stderrLower.contains(QStringLiteral("authentication failed"))) {
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
