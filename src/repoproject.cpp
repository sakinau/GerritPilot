#include "repoproject.h"
#include "workspacecontroller.h"
#include "projectmanager.h"
#include "gitservice.h"
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <QUrl>

RepoProject::RepoProject(WorkspaceController *workspace, QObject *parent)
    : TaskReporter(parent), m_workspace(workspace)
{
    if (m_workspace) {
        m_path = m_workspace->workspacePath();
        connect(m_workspace, &WorkspaceController::workspacePathChanged, this, [this]() {
            m_path = m_workspace->workspacePath();
            emit changed();
        });
    }
    connect(&m_runner, &ProcessRunner::outputReceived, this,
            [this](const QByteArray &out, const QByteArray &err) {
        append(QString::fromLocal8Bit(out.right(100000)) + QString::fromLocal8Bit(err.right(100000)));
    });
    connect(&m_runner, &ProcessRunner::failedToStart, this, [this](const QString &error) {
        m_initializing = false;
        append(tr("启动失败：%1\n").arg(error));
        emit busyChanged(false);
        emit activeTaskChanged(QString());
        emit changed();
    });
    connect(&m_runner, &ProcessRunner::finished, this,
            [this](int code, QProcess::ExitStatus status, const QByteArray &, const QByteArray &) {
        const bool success = code == 0 && status == QProcess::NormalExit;
        const bool initialized = m_initializing;
        m_initializing = false;
        if (!m_currentLine.isEmpty()) {
            const QString lastLine = m_currentLine + QLatin1Char('\n');
            m_log = (m_log + lastLine).right(100000);
            m_currentLine.clear();
            emit logMessage(lastLine);
        }
        m_progressPercent = success ? 100 : -1;
        m_progressStage = success ? tr("初始化完成") : tr("初始化未完成");
        emit progressChanged();
        append(success ? tr("repo init 完成。未修改 Manifest，未执行 repo sync。\n")
                       : tr("操作未完成，退出码 %1；保留现场，请查看日志。\n").arg(code));
        emit busyChanged(false);
        emit activeTaskChanged(QString());
        if (success && initialized && m_workspace) {
            m_workspace->projectManager()->addProject(QFileInfo(m_path).fileName(), m_path);
            emit projectCreated(QFileInfo(m_path).fileName(), m_path);
        }
        emit changed();
    });
}

QString RepoProject::stripAnsi(const QString &text)
{
    static const QRegularExpression ansiRx(QStringLiteral("\x1b\\[[0-9;]*[a-zA-Z]"));
    return QString(text).remove(ansiRx);
}

void RepoProject::parseProgress(const QString &text)
{
    static const QRegularExpression repoProgressRx(
        QStringLiteral(R"((Fetching(?: projects)?|Checking out|Updating files|Syncing)\s*:\s*(\d+)%\s*(?:\[[^\]]*\]\s*)?\((\d+)\/(\d+)\)(.*))"));
    static const QRegularExpression gitProgressRx(
        QStringLiteral(R"((Receiving objects|Resolving deltas|Counting objects|Compressing objects|Writing objects)\s*:\s*(\d+)%)"));

    bool changed = false;

    const auto repoMatch = repoProgressRx.match(text);
    if (repoMatch.hasMatch()) {
        const QString stageStr = repoMatch.captured(1);
        const int pct = repoMatch.captured(2).toInt();
        const QString done = repoMatch.captured(3);
        const QString total = repoMatch.captured(4);
        const QString extra = repoMatch.captured(5).trimmed();

        m_progressPercent = qBound(0, pct, 100);
        if (stageStr.startsWith(QStringLiteral("Fetch"))) {
            m_progressStage = tr("正在拉取代码 (Fetching)");
        } else if (stageStr.startsWith(QStringLiteral("Check"))) {
            m_progressStage = tr("正在检出工作区 (Checking out)");
        } else if (stageStr.startsWith(QStringLiteral("Updat"))) {
            m_progressStage = tr("正在更新文件 (Updating files)");
        } else {
            m_progressStage = stageStr;
        }

        m_progressDetail = QStringLiteral("(%1/%2)%3").arg(done, total, extra.isEmpty() ? QString() : (QStringLiteral(" ") + extra));
        m_progressText = QStringLiteral("%1: %2% %3").arg(m_progressStage).arg(m_progressPercent).arg(m_progressDetail);
        changed = true;
    } else {
        const auto gitMatch = gitProgressRx.match(text);
        if (gitMatch.hasMatch()) {
            const QString gitStage = gitMatch.captured(1);
            const int pct = gitMatch.captured(2).toInt();
            m_progressPercent = qBound(0, pct, 100);
            m_progressDetail = QStringLiteral("%1: %2%").arg(gitStage).arg(pct);
            m_progressText = m_progressDetail;
            changed = true;
        }
    }

    if (changed) {
        emit progressChanged();
        emit activeTaskChanged(activeTask());
    }
}

void RepoProject::append(const QString &text)
{
    const QString clean = stripAnsi(text);
    parseProgress(clean);

    QString toAppend;
    for (int i = 0; i < clean.size(); ++i) {
        const QChar c = clean.at(i);
        if (c == QLatin1Char('\r')) {
            m_currentLine.clear();
        } else if (c == QLatin1Char('\n')) {
            if (!m_currentLine.isEmpty()) {
                toAppend += m_currentLine + QLatin1Char('\n');
                m_currentLine.clear();
            } else {
                toAppend += QLatin1Char('\n');
            }
        } else {
            m_currentLine += c;
        }
    }

    if (!toAppend.isEmpty()) {
        m_log = (m_log + toAppend).right(100000);
        emit logMessage(toAppend);
        emit changed();
    }
}

bool RepoProject::available()
{
    if (busy() || m_workspace->busy()) {
        append(tr("请等待当前操作完成。\n"));
        return false;
    }
    return true;
}

void RepoProject::importLocal(const QString &path)
{
    if (!available()) return;
    const QFileInfo dir(path.trimmed());
    if (!dir.isDir() || (!QFileInfo::exists(dir.filePath() + "/.git")
                        && !QFileInfo(dir.filePath() + "/.repo").isDir())) {
        append(tr("请选择已有 .repo 或 .git 的项目根目录。\n"));
        return;
    }
    m_path = dir.canonicalFilePath();
    append(tr("已导入：%1\n").arg(m_path));
    m_workspace->projectManager()->addProject(QFileInfo(m_path).fileName(), m_path);
}

void RepoProject::start(const QStringList &arguments)
{
    m_progressPercent = -1;
    m_progressStage = tr("正在初始化项目");
    m_progressDetail.clear();
    m_progressText.clear();
    m_currentLine.clear();
    emit progressChanged();

    append(GitService::displayCommand("repo", arguments) + "\n" + tr("目录：%1\n").arg(m_path));

#ifdef Q_OS_UNIX
    QString program = QStringLiteral("python3");
    QStringList fullArgs;
    fullArgs << QStringLiteral("-u") << QStringLiteral("-c")
             << QStringLiteral("import pty, sys, os; sys.exit(os.waitstatus_to_exitcode(pty.spawn(['repo'] + sys.argv[1:])))");
    fullArgs << arguments;
    m_runner.start(program, fullArgs, m_path, 0, {}, 256 * 1024);
#else
    m_runner.start("repo", arguments, m_path, 0, {}, 256 * 1024);
#endif

    emit busyChanged(true);
    emit activeTaskChanged(activeTask());
    emit changed();
}

void RepoProject::initialize(const QString &path, const QString &url, const QString &branch,
                             const QString &manifest, const QString &repoUrl)
{
    if (!available()) return;
    const QString target = QDir::cleanPath(path.trimmed());
    const QString cleanManifest = QDir::cleanPath(manifest.trimmed());
    if (!QDir::isAbsolutePath(target) || target == "/" || url.trimmed().isEmpty()
        || branch.trimmed().isEmpty() || manifest.trimmed().isEmpty()
        || QDir::isAbsolutePath(cleanManifest) || cleanManifest == ".." || cleanManifest.startsWith("../")) {
        append(tr("请填写新项目绝对路径、Manifest URL、分支和相对清单路径。\n"));
        return;
    }
    const QFileInfo info(target);
    if (info.exists() && (!info.isDir() || info.isSymLink()
        || !QDir(target).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot).isEmpty())) {
        append(tr("初始化只允许新目录或空目录；已有项目请使用导入，不会删除或覆盖 .repo。\n"));
        return;
    }
    // Do not create nested projects inside an existing checkout.
    QDir ancestor(QFileInfo(target).absolutePath());
    do {
        if (QFileInfo::exists(ancestor.filePath(".repo")) || QFileInfo::exists(ancestor.filePath(".git"))) {
            append(tr("不能在已有 Git/repo 项目内部初始化新项目。\n"));
            return;
        }
    } while (ancestor.cdUp());
    if (!QDir().mkpath(target)) {
        append(tr("无法创建目标目录。\n"));
        return;
    }
    m_path = QFileInfo(target).canonicalFilePath();
    QStringList args{"init", "-u", url.trimmed(), "-b", branch.trimmed(), "-m", cleanManifest};
    if (!repoUrl.trimmed().isEmpty()) args << ("--repo-url=" + repoUrl.trimmed());
    setDefaultManifestUrl(url);
    setDefaultRepoUrl(repoUrl);
    m_initializing = true;
    start(args);
}

void RepoProject::openManifest()
{
    if (busy() || !m_workspace) return;
    const QString directory = QDir(m_workspace->workspacePath()).filePath(QStringLiteral(".repo/manifests"));
    if (!QFileInfo(directory).isDir() || !QDesktopServices::openUrl(QUrl::fromLocalFile(directory)))
        append(tr("无法打开 Manifest 目录：%1\n").arg(directory));
}

bool RepoProject::sshConfigExists() const
{
    const QString sshConfigPath = QDir::homePath() + QStringLiteral("/.ssh/config");
    return QFileInfo(sshConfigPath).isFile();
}

void RepoProject::openSshConfig()
{
    const QString sshDirPath = QDir::homePath() + QStringLiteral("/.ssh");
    const QString configPath = sshDirPath + QStringLiteral("/config");
    if (!QFileInfo(sshDirPath).exists()) {
        if (!QDir().mkpath(sshDirPath)
            || !QFile::setPermissions(sshDirPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner)) {
            append(tr("无法安全创建 SSH 配置目录（%1）。\n").arg(sshDirPath));
            return;
        }
    }
    if (!QFileInfo(configPath).exists()) {
        QFile config(configPath);
        if (!config.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            append(tr("无法安全创建 SSH 配置文件（%1）。\n").arg(configPath));
            return;
        }
        if (!config.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
            config.close();
            config.remove();
            append(tr("无法为 SSH 配置文件设置仅限当前用户访问的权限（%1）。\n").arg(configPath));
            return;
        }
        config.close();
        emit changed();
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(configPath)))
        append(tr("无法打开 SSH 配置文件（%1）。\n").arg(configPath));
}

void RepoProject::clearLog()
{
    m_log.clear();
    m_currentLine.clear();
    emit changed();
}

QString RepoProject::activeTask() const
{
    if (!busy()) return QString();
    return m_progressStage.isEmpty() ? tr("正在初始化项目") : m_progressStage;
}

QString RepoProject::defaultRepoUrl() const
{
    QSettings settings;
    return settings.value(QStringLiteral("repo/defaultRepoUrl"), QString()).toString();
}

void RepoProject::setDefaultRepoUrl(const QString &url)
{
    QSettings settings;
    settings.setValue(QStringLiteral("repo/defaultRepoUrl"), url.trimmed());
    emit defaultSettingsChanged();
}

QString RepoProject::defaultManifestUrl() const
{
    QSettings settings;
    return settings.value(QStringLiteral("repo/defaultManifestUrl"), QString()).toString();
}

void RepoProject::setDefaultManifestUrl(const QString &url)
{
    QSettings settings;
    settings.setValue(QStringLiteral("repo/defaultManifestUrl"), url.trimmed());
    emit defaultSettingsChanged();
}

void RepoProject::cancel()
{
    if (!busy()) return;
    append(tr("正在取消操作；保留已下载内容。\n"));
    m_runner.cancel();
}

