#include "repoproject.h"
#include "workspacecontroller.h"
#include "projectmanager.h"
#include "gitservice.h"
#include "commitmessageaiservice.h"
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
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

    m_remoteHmiBranches = {
        QStringLiteral("TDA4_T1EJFL"),
        QStringLiteral("TDA4_T13T_BEV"),
        QStringLiteral("TDA4_T1TP_FX"),
        QStringLiteral("TDA4_T13C_BEV"),
        QStringLiteral("TDA4_T13C_HEV"),
        QStringLiteral("TDA4_T1TP"),
        QStringLiteral("TDA4_T18FL_26MY"),
        QStringLiteral("TDA4_K01A"),
        QStringLiteral("TDA4_T28FL"),
        QStringLiteral("master")
    };

    connect(&m_branchIndexer, &ProcessRunner::finished, this,
            [this](int exitCode, QProcess::ExitStatus, const QByteArray &stdoutData, const QByteArray &) {
        m_indexingBranches = false;
        emit indexingBranchesChanged();
        if (exitCode == 0 && !stdoutData.isEmpty()) {
            QStringList branches;
            const QString output = QString::fromUtf8(stdoutData);
            const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            static const QRegularExpression rx(QStringLiteral(R"(\s+refs/heads/(.+))"));
            for (const QString &line : lines) {
                const auto m = rx.match(line);
                if (m.hasMatch()) {
                    const QString b = m.captured(1).trimmed();
                    if (!b.isEmpty() && !branches.contains(b)) {
                        branches.append(b);
                    }
                }
            }
            if (!branches.isEmpty()) {
                branches.sort();
                m_remoteHmiBranches = branches;
                emit remoteHmiBranchesChanged();
                append(tr("已自动索引远端 HMI 分支，共发现 %1 个可用分支。\n").arg(branches.size()));
            }
        }
    });

    connect(&m_runner, &ProcessRunner::outputReceived, this,
            [this](const QByteArray &out, const QByteArray &err) {
        append(QString::fromLocal8Bit(out.right(100000))
               + QString::fromLocal8Bit(err.right(100000)));
    });
    connect(&m_runner, &ProcessRunner::failedToStart, this, [this](const QString &error) {
        m_initializing = false;
        m_quickCreating = false;
        m_quickCreatingSync = false;
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
            m_log = (m_log + m_currentLine + QLatin1Char('\n')).right(100000);
            m_currentLine.clear();
        }
        if (success) {
            m_progressPercent = 100;
            m_progressStage = tr("完成");
        } else {
            m_progressStage = tr("未成功");
        }
        emit progressChanged();

        if (m_quickCreating) {
            if (m_quickCreatingSync) {
                m_quickCreatingSync = false;
                append(success ? tr("代码同步完成！\n") : tr("代码同步未完全成功（退出码 %1）；保留已拉取内容。\n").arg(code));
                finishQuickCreate();
                return;
            }

            if (!success) {
                append(tr("repo init 初始化失败，退出码 %1；请检查 Manifest 分支名称、清单文件名(-m)或网络。\n").arg(code));
                m_quickCreating = false;
                emit busyChanged(false);
                emit activeTaskChanged(QString());
                emit changed();
                return;
            }

            append(tr("repo init 初始化成功。\n"));

            // Step 2: Auto-repair customer.xml
            const QString key = m_quickProjectName.trimmed().toLower();
            const bool isStandardPlatform = key.contains(QStringLiteral("t1ejfl"))
                                         || key.contains(QStringLiteral("t13t"))
                                         || key.contains(QStringLiteral("t1tp"))
                                         || key.contains(QStringLiteral("t13c"));

            if (isStandardPlatform) {
                repairCustomerManifest(m_path, m_quickProjectName, m_quickHmiBranch);
            } else if (!m_quickHmiBranch.isEmpty()) {
                const QString customerXml = findCustomerManifest(m_path, m_quickProjectName);
                if (!customerXml.isEmpty()) {
                    if (updateModuleRevision(customerXml, QStringLiteral("mvpilot/mv_hmi"), m_quickHmiBranch)) {
                        append(tr("已自动将 %1 中的 mv_hmi 分支调整为：%2\n").arg(QFileInfo(customerXml).fileName(), m_quickHmiBranch));
                    }
                }
            }

            if (!m_quickCustomOverrides.isEmpty()) {
                const QString customerXml = findCustomerManifest(m_path, m_quickProjectName);
                if (!customerXml.isEmpty()) {
                    const QStringList lines = m_quickCustomOverrides.split(QRegularExpression(QStringLiteral("[;\n\r]+")), Qt::SkipEmptyParts);
                    for (const QString &line : lines) {
                        const int eq = line.indexOf(QLatin1Char('='));
                        if (eq > 0) {
                            const QString mod = line.left(eq).trimmed();
                            const QString rev = line.mid(eq + 1).trimmed();
                            if (!mod.isEmpty() && !rev.isEmpty()) {
                                if (updateModuleRevision(customerXml, mod, rev)) {
                                    append(tr("已调整模块 %1 分支为：%2\n").arg(mod, rev));
                                }
                            }
                        }
                    }
                }
            }

            // Step 3: Auto-sync if requested
            if (m_quickAutoSync) {
                m_quickCreatingSync = true;
                append(tr("正在开始代码同步 (repo sync) ...\n"));
                QStringList syncArgs{QStringLiteral("sync"), QStringLiteral("--fail-fast"), QStringLiteral("--no-manifest-update"), QStringLiteral("-j4")};
                if (m_quickSyncHmiOnly) {
                    syncArgs << QStringLiteral("module/customer/mv_hmi") << QStringLiteral("module/customer");
                }
                start(syncArgs, true);
                return;
            }

            finishQuickCreate();
            return;
        }

        append(success ? tr("操作完成。\n") : tr("操作未完成，退出码 %1；保留已下载内容，请查看日志。\n").arg(code));
        emit busyChanged(false);
        emit activeTaskChanged(QString());
        if (success && m_syncing) {
            m_workspace->setWorkspacePath(m_path);
            m_workspace->scanWorkspace();
        } else if (success && initialized) {
            m_workspace->setWorkspacePath(m_path);
            append(tr("项目已添加。请检查 Manifest，完成代码同步后重新载入项目仓库。\n"));
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
    m_workspace->setWorkspacePath(m_path);
    if (QFileInfo::exists(m_path + "/.repo/project.list") || QFileInfo::exists(m_path + "/.git"))
        m_workspace->scanWorkspace();
}

void RepoProject::start(const QStringList &arguments, bool syncing)
{
    m_syncing = syncing;
    m_progressPercent = syncing ? 0 : -1;
    m_progressStage = syncing ? tr("正在准备同步代码...") : (m_quickCreating ? tr("正在初始化项目...") : tr("正在执行 Repo 操作..."));
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
    m_initializing = true;
    start(args, false);
}

void RepoProject::openManifest()
{
    if (busy()) return;
    const QString targetPath = m_path.isEmpty() ? (m_workspace ? m_workspace->workspacePath() : QString()) : m_path;
    QString file = findCustomerManifest(targetPath);
    if (file.isEmpty() || !QFileInfo::exists(file)) {
        file = targetPath + QStringLiteral("/.repo/manifest.xml");
    }
    if (!QFileInfo::exists(file) || !QDesktopServices::openUrl(QUrl::fromLocalFile(file)))
        append(tr("无法打开 Manifest 清单文件（%1）。\n").arg(file));
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

void RepoProject::synchronize(bool reviewed, int jobs, bool noManifestUpdate)
{
    if (!available()) return;
    if (m_path.isEmpty() && m_workspace) {
        m_path = m_workspace->workspacePath();
    }
    if (!reviewed || !QFileInfo::exists(m_path + "/.repo/manifest.xml")) {
        append(tr("请先初始化或导入 repo 项目，并确认已检查 Manifest。\n"));
        return;
    }
    const int boundedJobs = qBound(1, jobs, 32);
    QStringList args{QStringLiteral("sync")};
    if (noManifestUpdate) {
        args << QStringLiteral("--no-manifest-update");
    }
    args << QStringLiteral("-j%1").arg(boundedJobs) << QStringLiteral("--fail-fast");
    start(args, true);
}

void RepoProject::syncModule(const QString &moduleName)
{
    if (!available()) return;
    if (m_path.isEmpty() && m_workspace) {
        m_path = m_workspace->workspacePath();
    }
    if (m_path.isEmpty() || !QFileInfo::exists(m_path + "/.repo/manifest.xml")) {
        append(tr("当前工作区未检测到 .repo/manifest.xml，无法执行 repo sync。\n"));
        return;
    }
    const QString cleanName = moduleName.trimmed();
    if (cleanName.isEmpty()) {
        append(tr("请指定要同步的模块名。\n"));
        return;
    }
    append(tr("正在单独同步模块 %1 (repo sync -j8 %1) ...\n").arg(cleanName));
    QStringList args{QStringLiteral("sync"), QStringLiteral("--no-manifest-update"), QStringLiteral("-j8"), QStringLiteral("--fail-fast"), cleanName};
    start(args, true);
}

bool RepoProject::applyHmiBranchAndSync(const QString &hmiBranch)
{
    if (!setHmiBranchForProject(QString(), hmiBranch)) {
        return false;
    }
    syncModule(QStringLiteral("mv_hmi"));
    return true;
}

void RepoProject::clearLog()
{
    m_log.clear();
    emit changed();
}

void RepoProject::switchManifestBranch(const QString &branch, const QString &manifest)
{
    if (!available()) return;
    if (m_path.isEmpty() && m_workspace) {
        m_path = m_workspace->workspacePath();
    }
    if (m_path.isEmpty() || !QFileInfo(m_path + "/.repo").isDir()) {
        append(tr("当前项目不是有效的 repo 工作区，无法切换 Manifest 分支。\n"));
        return;
    }
    const QString cleanBranch = branch.trimmed();
    if (cleanBranch.isEmpty()) {
        append(tr("请提供目标 Manifest 分支名。\n"));
        return;
    }
    QStringList args{QStringLiteral("init"), QStringLiteral("-b"), cleanBranch};
    const QString cleanManifest = QDir::cleanPath(manifest.trimmed());
    if (!cleanManifest.isEmpty() && cleanManifest != QStringLiteral(".")) {
        if (QDir::isAbsolutePath(cleanManifest) || cleanManifest.startsWith(QStringLiteral("../"))) {
            append(tr("清单文件必须为相对路径。\n"));
            return;
        }
        args << QStringLiteral("-m") << cleanManifest;
    }
    append(tr("正在切换 Manifest 跟踪分支为 %1 ...\n").arg(cleanBranch));
    start(args, false);
}

void RepoProject::checkStatus()
{
    if (!available()) return;
    if (m_path.isEmpty() && m_workspace) {
        m_path = m_workspace->workspacePath();
    }
    if (m_path.isEmpty() || !QFileInfo(m_path + "/.repo").isDir()) {
        append(tr("当前工作区不是有效的 repo 项目（未找到 .repo 目录）。\n"));
        return;
    }
    append(tr("正在检查各子仓库改动状态 (repo status) ...\n"));
    start({QStringLiteral("status")}, false);
}

QString RepoProject::activeTask() const
{
    if (!m_runner.running()) return QString();
    if (m_progressPercent >= 0 && !m_progressStage.isEmpty()) {
        return QStringLiteral("[%1%] %2").arg(m_progressPercent).arg(m_progressStage);
    }
    if (!m_progressStage.isEmpty()) return m_progressStage;
    if (m_quickCreatingSync) return QStringLiteral("项目代码同步中");
    if (m_quickCreating) return QStringLiteral("正在初始化项目");
    if (m_syncing) return QStringLiteral("Repo 同步中");
    return QStringLiteral("Repo 执行中");
}

QString RepoProject::defaultRepoUrl() const
{
    QSettings settings;
    return settings.value(QStringLiteral("repo/defaultRepoUrl"), QString()).toString();
}

void RepoProject::setDefaultRepoUrl(const QString &url)
{
    if (url.trimmed().isEmpty()) return;
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
    if (url.trimmed().isEmpty()) return;
    QSettings settings;
    settings.setValue(QStringLiteral("repo/defaultManifestUrl"), url.trimmed());
    emit defaultSettingsChanged();
}

QString RepoProject::defaultBaseDir() const
{
    QSettings settings;
    return settings.value(QStringLiteral("repo/defaultBaseDir"),
                          QDir::homePath() + QStringLiteral("/code")).toString();
}

void RepoProject::setDefaultBaseDir(const QString &dir)
{
    if (dir.trimmed().isEmpty()) return;
    QSettings settings;
    settings.setValue(QStringLiteral("repo/defaultBaseDir"), dir.trimmed());
    emit defaultSettingsChanged();
}

QString RepoProject::currentCustomerXml() const
{
    const QString basePath = m_path.isEmpty() ? (m_workspace ? m_workspace->workspacePath() : QString()) : m_path;
    return findCustomerManifest(basePath);
}

QString RepoProject::currentHmiRevision() const
{
    const QString xmlPath = currentCustomerXml();
    if (xmlPath.isEmpty()) return QString();
    QFile file(xmlPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString content = QString::fromUtf8(file.readAll());
        static const QRegularExpression hmiRegex(QStringLiteral("<project\\b[^>]*?name=[\"'][^\"']*mv_hmi[\"'][^>]*?revision=[\"']([^\"']+)[\"']"));
        const auto matchHmi = hmiRegex.match(content);
        if (matchHmi.hasMatch()) {
            return matchHmi.captured(1);
        }
    }
    return QString();
}

QString RepoProject::findCustomerManifest(const QString &basePath, const QString &projectName) const
{
    if (basePath.trimmed().isEmpty()) return QString();
    const QString manifestsDir = QDir::cleanPath(basePath + QStringLiteral("/.repo/manifests"));
    if (!QDir(manifestsDir).exists())
        return QString();

    // 1. Try finding based on projectName if given (e.g. T1TP, T13T, T13T_BEV, TDA4_T1TP, etc.)
    if (!projectName.trimmed().isEmpty()) {
        const QString name = projectName.trimmed();
        const QStringList candidateDirs = {
            name,
            QStringLiteral("TDA4_") + name,
            QStringLiteral("TDA4_") + name + QStringLiteral("_BEV"),
            QStringLiteral("TDA4_") + name + QStringLiteral("_HEV")
        };
        for (const QString &cand : candidateDirs) {
            const QString p = manifestsDir + QLatin1Char('/') + cand + QStringLiteral("/dev/customer.xml");
            if (QFileInfo::exists(p)) return p;
        }
    }

    // 2. Inspect .repo/manifest.xml and nested manifests to see which customer.xml is included
    const QString rootManifest = basePath + QStringLiteral("/.repo/manifest.xml");
    if (QFileInfo::exists(rootManifest)) {
        QFile f(rootManifest);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString content = QString::fromUtf8(f.readAll());
            static const QRegularExpression includeRegex(QStringLiteral("<include\\s+name=[\"']([^\"']+)[\"']"));
            auto it = includeRegex.globalMatch(content);
            while (it.hasNext()) {
                const QString inc = it.next().captured(1);
                if (inc.contains(QStringLiteral("customer"), Qt::CaseInsensitive)) {
                    const QString p = manifestsDir + QLatin1Char('/') + inc;
                    if (QFileInfo::exists(p)) return p;
                }
                const QString sub = QFileInfo(inc).dir().path();
                if (!sub.isEmpty() && sub != QStringLiteral(".")) {
                    const QString p = manifestsDir + QLatin1Char('/') + sub + QStringLiteral("/dev/customer.xml");
                    if (QFileInfo::exists(p)) return p;
                }
                // Check inside the included manifest for nested customer.xml include
                const QString nested = manifestsDir + QLatin1Char('/') + inc;
                if (QFileInfo::exists(nested)) {
                    QFile nf(nested);
                    if (nf.open(QIODevice::ReadOnly | QIODevice::Text)) {
                        const QString nContent = QString::fromUtf8(nf.readAll());
                        auto nIt = includeRegex.globalMatch(nContent);
                        while (nIt.hasNext()) {
                            const QString nInc = nIt.next().captured(1);
                            if (nInc.contains(QStringLiteral("customer"), Qt::CaseInsensitive)) {
                                const QString np = manifestsDir + QLatin1Char('/') + nInc;
                                if (QFileInfo::exists(np)) return np;
                            }
                        }
                    }
                }
            }
        }
    }

    // 3. Directly check manifestsDir/dev/customer.xml
    const QString direct = manifestsDir + QStringLiteral("/dev/customer.xml");
    if (QFileInfo::exists(direct)) return direct;

    // 4. Recursive search for any *customer*.xml inside manifestsDir
    QDirIterator it(manifestsDir, QStringList() << QStringLiteral("*customer*.xml"),
                    QDir::Files, QDirIterator::Subdirectories);
    if (it.hasNext()) {
        return it.next();
    }

    return QString();
}

bool RepoProject::updateModuleRevision(const QString &manifestPath, const QString &moduleNameOrPath, const QString &newRevision)
{
    if (manifestPath.isEmpty() || !QFileInfo::exists(manifestPath)
        || moduleNameOrPath.trimmed().isEmpty() || newRevision.trimmed().isEmpty())
        return false;

    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QString content = QString::fromUtf8(file.readAll());
    file.close();

    const QString mod = moduleNameOrPath.trimmed();
    const QString rev = newRevision.trimmed();

    const QString escMod = QRegularExpression::escape(mod);
    const QRegularExpression projectRegex(
        QStringLiteral("(<project\\b[^>]*?(?:name=[\"'][^\"']*") + escMod + QStringLiteral("[^\"']*[\"']|path=[\"'][^\"']*") + escMod + QStringLiteral("[^\"']*[\"'])[^>]*?>"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption
    );

    const auto match = projectRegex.match(content);
    if (!match.hasMatch()) {
        return false;
    }

    QString tag = match.captured(1);
    const QRegularExpression revRegex(QStringLiteral("revision=[\"'][^\"']*[\"']"));
    if (revRegex.match(tag).hasMatch()) {
        tag.replace(revRegex, QStringLiteral("revision=\"") + rev + QStringLiteral("\""));
    } else {
        if (tag.endsWith(QStringLiteral("/>"))) {
            tag.chop(2);
            tag += QStringLiteral(" revision=\"") + rev + QStringLiteral("\"/>");
        } else if (tag.endsWith(QLatin1Char('>'))) {
            tag.chop(1);
            tag += QStringLiteral(" revision=\"") + rev + QStringLiteral("\">");
        }
    }

    if (mod.contains(QStringLiteral("mv_hmi")) && !content.contains(QStringLiteral("mv_hmi_component"))) {
        QString extra = QStringLiteral("\n\t<project name=\"mvpilot/mv_hmi_component\" path=\"module/customer/mv_hmi/mv_hmi_component\" revision=\"master\"/>\n")
                      + QStringLiteral("\t<project name=\"mvpilot/mv_hmi_framework\" path=\"module/customer/mv_hmi/mv_hmi_framework\" revision=\"master\"/>");
        tag += extra;
    }

    content.replace(match.capturedStart(1), match.capturedLength(1), tag);

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return false;

    file.write(content.toUtf8());
    file.close();
    return true;
}

QVariantMap RepoProject::inspectCustomerManifest(const QString &basePath) const
{
    QVariantMap res;
    const QString xmlPath = findCustomerManifest(basePath.isEmpty() ? m_path : basePath);
    res[QStringLiteral("found")] = !xmlPath.isEmpty();
    if (xmlPath.isEmpty())
        return res;

    res[QStringLiteral("xmlPath")] = xmlPath;
    res[QStringLiteral("fileName")] = QFileInfo(xmlPath).fileName();

    QFile file(xmlPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString content = QString::fromUtf8(file.readAll());
        file.close();

        static const QRegularExpression hmiRegex(QStringLiteral("<project\\b[^>]*?name=[\"'][^\"']*mv_hmi[\"'][^>]*?revision=[\"']([^\"']+)[\"']"));
        const auto matchHmi = hmiRegex.match(content);
        if (matchHmi.hasMatch()) {
            res[QStringLiteral("hmiRevision")] = matchHmi.captured(1);
        }

        static const QRegularExpression allProjRegex(QStringLiteral("<project\\b([^>]+)>"));
        auto it = allProjRegex.globalMatch(content);
        QVariantList projects;
        while (it.hasNext()) {
            const auto m = it.next();
            const QString attrs = m.captured(1);
            QVariantMap item;
            static const QRegularExpression nameRx(QStringLiteral("name=[\"']([^\"']+)[\"']"));
            static const QRegularExpression pathRx(QStringLiteral("path=[\"']([^\"']+)[\"']"));
            static const QRegularExpression revRx(QStringLiteral("revision=[\"']([^\"']+)[\"']"));
            const auto mn = nameRx.match(attrs);
            const auto mp = pathRx.match(attrs);
            const auto mr = revRx.match(attrs);
            if (mn.hasMatch()) item[QStringLiteral("name")] = mn.captured(1);
            if (mp.hasMatch()) item[QStringLiteral("path")] = mp.captured(1);
            if (mr.hasMatch()) item[QStringLiteral("revision")] = mr.captured(1);
            if (!item.isEmpty()) projects.append(item);
        }
        res[QStringLiteral("projects")] = projects;
    }
    return res;
}

QString RepoProject::getCustomerTemplate(const QString &platformKey, const QString &customHmiBranch) const
{
    const QString key = platformKey.trimmed().toLower();
    QString hmiBranch = customHmiBranch.trimmed();
    QString projectAnnotation = QStringLiteral("t13c_hev");
    QString vehicleYaml = QStringLiteral("t13c_hev/vehicle.yaml");
    QString ciBranch = QStringLiteral("TDA4_T1TP");
    QString platBranch = QStringLiteral("TDA4_CHERY_PUBLIC");
    QString evalTag = QStringLiteral("refs/tags/v1.0.5");
    QString simTag = QStringLiteral("refs/tags/v1.0.3");

    if (key.contains(QStringLiteral("t1ejfl"))) {
        if (hmiBranch.isEmpty()) hmiBranch = QStringLiteral("TDA4_T1EJFL");
        projectAnnotation = QStringLiteral("t13c_hev");
        vehicleYaml = QStringLiteral("t13c_hev/vehicle.yaml");
        ciBranch = QStringLiteral("TDA4_T1TP");
    } else if (key.contains(QStringLiteral("t13t"))) {
        if (hmiBranch.isEmpty()) hmiBranch = QStringLiteral("TDA4_T13T_BEV");
        projectAnnotation = QStringLiteral("t13t_bev");
        vehicleYaml = QStringLiteral("t13t_bev/vehicle.yaml");
        ciBranch = QStringLiteral("TDA4_T1TP");
    } else if (key.contains(QStringLiteral("t1tp"))) {
        if (hmiBranch.isEmpty()) hmiBranch = QStringLiteral("TDA4_T1TP_FX");
        projectAnnotation = QStringLiteral("t1tp");
        vehicleYaml = QStringLiteral("t1tp/vehicle.yaml");
        ciBranch = QStringLiteral("TDA4_T1TP");
    } else if (key.contains(QStringLiteral("t13c_bev"))) {
        if (hmiBranch.isEmpty()) hmiBranch = QStringLiteral("TDA4_T13C_BEV");
        projectAnnotation = QStringLiteral("t13c_bev");
        vehicleYaml = QStringLiteral("t13c_bev/vehicle.yaml");
        ciBranch = QStringLiteral("TDA4_T13C_HEV");
        simTag = QStringLiteral("refs/tags/v1.0.0");
    } else if (key.contains(QStringLiteral("t13c_hev"))) {
        if (hmiBranch.isEmpty()) hmiBranch = QStringLiteral("TDA4_T1TP");
        projectAnnotation = QStringLiteral("t13c_hev");
        vehicleYaml = QStringLiteral("t13c_hev/vehicle.yaml");
        ciBranch = QStringLiteral("TDA4_T13C_HEV");
        simTag = QStringLiteral("refs/tags/v1.0.5");
    } else {
        if (hmiBranch.isEmpty()) hmiBranch = QStringLiteral("master");
        projectAnnotation = key.isEmpty() ? QStringLiteral("custom") : key;
        vehicleYaml = QStringLiteral("common/vehicle.yaml");
    }

    if (key.contains(QStringLiteral("t13c_hev"))) {
        return QStringLiteral(
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
"<manifest>\n"
"\t<project name=\"mvpilot/ci_scripts\" path=\"module/customer/ci_scripts\" revision=\"%1\"/>\n"
"\t<project name=\"mvpilot/platform-customer\" path=\"module/customer\" revision=\"%2\">\n"
"\t\t<annotation name=\"PROJECT\" value=\"%3\"/>\n"
"\t\t<linkfile src=\"vehicle_config/res/%4\" dest=\"module/customer/vehicle_config/res/vehicle.yaml\"/>\n"
"\t</project>\n"
"\t<project name=\"mvpilot/mv_hmi\" path=\"module/customer/mv_hmi\" revision=\"%5\">\n"
"\t\t<annotation name=\"PROJECT\" value=\"T13C_HEV\"/>\n"
"\t</project>\n"
"\t<project name=\"mvpilot/evaluation\" path=\"module/customer/evaluation\" revision=\"%6\"/>\n"
"\t<project name=\"mvpilot/simulation\" path=\"module/customer/simulation\" revision=\"%7\"/>\n"
"</manifest>\n"
        ).arg(ciBranch, platBranch, projectAnnotation, vehicleYaml, hmiBranch, evalTag, simTag);
    }

    if (key.contains(QStringLiteral("t13c_bev"))) {
        return QStringLiteral(
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
"<manifest>\n"
"\t<project name=\"mvpilot/ci_scripts\" path=\"module/customer/ci_scripts\" revision=\"%1\"/>\n"
"\t<project name=\"mvpilot/platform-customer\" path=\"module/customer\" revision=\"%2\">\n"
"\t\t<annotation name=\"PROJECT\" value=\"%3\"/>\n"
"\t\t<linkfile src=\"vehicle_config/res/%4\" dest=\"module/customer/vehicle_config/res/vehicle.yaml\"/>\n"
"\t</project>\n"
"\t<project name=\"mvpilot/mv_hmi\" path=\"module/customer/mv_hmi\" revision=\"%5\"/>\n"
"\t<project name=\"mvpilot/mv_hmi_component\" path=\"module/customer/mv_hmi/mv_hmi_component\" revision=\"master\"/>\n"
"\t<project name=\"mvpilot/mv_hmi_framework\" path=\"module/customer/mv_hmi/mv_hmi_framework\" revision=\"master\"/>\n"
"\t<project name=\"mvpilot/simulation\" path=\"module/customer/simulation\" revision=\"%6\"/>\n"
"</manifest>\n"
        ).arg(ciBranch, platBranch, projectAnnotation, vehicleYaml, hmiBranch, simTag);
    }

    return QStringLiteral(
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
"<manifest>\n"
"\t<project name=\"mvpilot/ci_scripts\" path=\"module/customer/ci_scripts\" revision=\"%1\"/>\n"
"\t<project name=\"mvpilot/platform-customer\" path=\"module/customer\" revision=\"%2\">\n"
"\t\t<annotation name=\"PROJECT\" value=\"%3\"/>\n"
"\t\t<linkfile src=\"vehicle_config/res/%4\" dest=\"module/customer/vehicle_config/res/vehicle.yaml\"/>\n"
"\t</project>\n"
"\t<project name=\"mvpilot/mv_hmi\" path=\"module/customer/mv_hmi\" revision=\"%5\"/>\n"
"\t<project name=\"mvpilot/mv_hmi_component\" path=\"module/customer/mv_hmi/mv_hmi_component\" revision=\"master\"/>\n"
"\t<project name=\"mvpilot/mv_hmi_framework\" path=\"module/customer/mv_hmi/mv_hmi_framework\" revision=\"master\"/>\n"
"\t<project name=\"mvpilot/evaluation\" path=\"module/customer/evaluation\" revision=\"%6\"/>\n"
"\t<project name=\"mvpilot/simulation\" path=\"module/customer/simulation\" revision=\"%7\"/>\n"
"</manifest>\n"
    ).arg(ciBranch, platBranch, projectAnnotation, vehicleYaml, hmiBranch, evalTag, simTag);
}

QString RepoProject::readCustomerManifest(const QString &basePath) const
{
    const QString targetPath = basePath.isEmpty() ? (m_path.isEmpty() ? (m_workspace ? m_workspace->workspacePath() : QString()) : m_path) : basePath;
    const QString xmlPath = findCustomerManifest(targetPath);
    if (xmlPath.isEmpty()) return QString();
    QFile file(xmlPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString::fromUtf8(file.readAll());
    }
    return QString();
}

bool RepoProject::writeRawCustomerManifest(const QString &basePath, const QString &xmlContent)
{
    const QString targetPath = basePath.isEmpty() ? (m_path.isEmpty() ? (m_workspace ? m_workspace->workspacePath() : QString()) : m_path) : basePath;
    QString xmlPath = findCustomerManifest(targetPath);
    if (xmlPath.isEmpty()) {
        QString subDir = QStringLiteral("dev");
        const QString rootManifest = targetPath + QStringLiteral("/.repo/manifest.xml");
        if (QFileInfo::exists(rootManifest)) {
            QFile f(rootManifest);
            if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                const QString content = QString::fromUtf8(f.readAll());
                static const QRegularExpression incRx(QStringLiteral("<include\\s+name=[\"']([^\"']+)[\"']"));
                const auto m = incRx.match(content);
                if (m.hasMatch()) {
                    const QString inc = m.captured(1);
                    const QString parentDir = QFileInfo(inc).dir().path();
                    if (!parentDir.isEmpty() && parentDir != QStringLiteral(".")) {
                        subDir = parentDir + QStringLiteral("/dev");
                    }
                }
            }
        }
        const QString devDir = targetPath + QStringLiteral("/.repo/manifests/") + subDir;
        QDir().mkpath(devDir);
        xmlPath = devDir + QStringLiteral("/customer.xml");
    }
    QFile file(xmlPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        file.write(xmlContent.toUtf8());
        file.close();
        append(tr("已更新 Manifest 清单文件：%1\n").arg(xmlPath));
        emit changed();
        return true;
    }
    append(tr("无法写入 Manifest 清单文件：%1\n").arg(xmlPath));
    return false;
}

bool RepoProject::repairCustomerManifest(const QString &basePath, const QString &platformKey, const QString &customHmiBranch)
{
    const QString content = getCustomerTemplate(platformKey, customHmiBranch);
    const bool ok = writeRawCustomerManifest(basePath, content);
    if (ok) {
        append(tr("★ 已成功重构并对齐平台 [%1] 整条 Manifest 客户链（包含 ci_scripts + platform-customer + mv_hmi + 组件 + 标定仿真）。\n").arg(platformKey));
    }
    return ok;
}

void RepoProject::repairAndSync(const QString &platformKey, const QString &customHmiBranch, bool hmiOnly)
{
    if (!repairCustomerManifest(QString(), platformKey, customHmiBranch)) {
        return;
    }
    if (hmiOnly) {
        syncModule(QStringLiteral("mv_hmi"));
    } else {
        synchronize(true, 8, true);
    }
}

bool RepoProject::setHmiBranchForProject(const QString &basePath, const QString &hmiBranch)
{
    const QString targetPath = basePath.isEmpty() ? m_path : basePath;
    const QString customerXml = findCustomerManifest(targetPath);
    if (customerXml.isEmpty()) {
        append(tr("未找到 customer.xml。\n"));
        return false;
    }
    const bool ok = updateModuleRevision(customerXml, QStringLiteral("mvpilot/mv_hmi"), hmiBranch);
    if (ok) {
        append(tr("已更新 %1 中的 mv_hmi 分支为：%2\n").arg(QFileInfo(customerXml).fileName(), hmiBranch));
        emit changed();
    } else {
        append(tr("更新失败，未在 %1 中找到 mv_hmi 标签。\n").arg(QFileInfo(customerXml).fileName()));
    }
    return ok;
}

void RepoProject::openCustomerManifest(const QString &basePath)
{
    const QString targetPath = basePath.isEmpty() ? (m_path.isEmpty() ? (m_workspace ? m_workspace->workspacePath() : QString()) : m_path) : basePath;
    const QString customerXml = findCustomerManifest(targetPath);
    if (!customerXml.isEmpty() && QFileInfo::exists(customerXml)) {
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(customerXml))) {
            append(tr("无法打开文件：%1\n").arg(customerXml));
        }
    } else {
        openManifest();
    }
}

void RepoProject::quickCreateProject(const QString &projectName, const QString &projectPath,
                                     const QString &manifestUrl, const QString &manifestBranch,
                                     const QString &manifestFile, const QString &repoUrl,
                                     const QString &hmiBranch, const QString &customModuleOverrides,
                                     bool autoSync, bool syncHmiOnly)
{
    if (!available()) return;

    const QString cleanPath = QDir::cleanPath(projectPath.trimmed());
    if (cleanPath.isEmpty() || !QDir::isAbsolutePath(cleanPath)) {
        append(tr("错误：项目路径必须为有效的绝对路径。\n"));
        return;
    }

    const QString projName = projectName.trimmed().isEmpty() ? QFileInfo(cleanPath).fileName() : projectName.trimmed();
    const QString mUrl = manifestUrl.trimmed().isEmpty() ? defaultManifestUrl() : manifestUrl.trimmed();
    const QString mBranch = manifestBranch.trimmed().isEmpty() ? QStringLiteral("TDA4_MAIN") : manifestBranch.trimmed();
    const QString mFile = manifestFile.trimmed().isEmpty() ? QStringLiteral("app_dev.xml") : manifestFile.trimmed();
    const QString rUrl = repoUrl.trimmed().isEmpty() ? defaultRepoUrl() : repoUrl.trimmed();

    QDir dir(cleanPath);
    if (!dir.exists()) {
        if (!dir.mkpath(cleanPath)) {
            append(tr("错误：无法创建项目目录 %1\n").arg(cleanPath));
            return;
        }
    }

    m_path = cleanPath;
    m_quickCreating = true;
    m_quickCreatingSync = false;
    m_quickProjectName = projName;
    m_quickHmiBranch = hmiBranch.trimmed();
    m_quickCustomOverrides = customModuleOverrides.trimmed();
    m_quickAutoSync = autoSync;
    m_quickSyncHmiOnly = syncHmiOnly;

    append(tr("========== 开始快速拉取项目 [%1] ==========\n").arg(projName));
    append(tr("目标目录：%1\n").arg(cleanPath));
    append(tr("Manifest 分支：%1 | 清单文件：%2\n").arg(mBranch, mFile));
    if (!m_quickHmiBranch.isEmpty()) {
        append(tr("目标 HMI 分支：%1\n").arg(m_quickHmiBranch));
    }

    QStringList args{QStringLiteral("init"), QStringLiteral("-u"), mUrl, QStringLiteral("-b"), mBranch, QStringLiteral("-m"), mFile};
    if (!rUrl.isEmpty()) {
        args << (QStringLiteral("--repo-url=") + rUrl);
    }

    m_initializing = true;
    start(args, false);
}

void RepoProject::finishQuickCreate()
{
    m_quickCreating = false;
    m_quickCreatingSync = false;
    m_progressPercent = 100;
    m_progressStage = tr("项目创建与同步完成");
    m_progressDetail.clear();
    m_progressText.clear();
    emit progressChanged();
    emit busyChanged(false);
    emit activeTaskChanged(QString());

    m_workspace->setWorkspacePath(m_path);
    if (m_workspace->projectManager()) {
        m_workspace->projectManager()->addProject(m_quickProjectName, m_path);
    }
    m_workspace->scanWorkspace();
    m_workspace->refreshAll();

    append(tr("==================================================\n"));
    append(tr("★ 项目 [%1] 创建完成并已载入工作区！\n").arg(m_quickProjectName));
    append(tr("==================================================\n"));
    emit projectCreated(m_quickProjectName, m_path);
    emit changed();
}

void RepoProject::cancel()
{
    if (busy()) {
        m_quickCreating = false;
        m_quickCreatingSync = false;
        m_progressPercent = -1;
        m_progressStage = tr("已取消");
        m_progressDetail.clear();
        m_progressText.clear();
        emit progressChanged();
        append(tr("正在取消操作；保留已下载内容。\n"));
        m_runner.cancel();
        emit busyChanged(false);
        emit activeTaskChanged(QString());
        emit changed();
    }
}

void RepoProject::fetchRemoteHmiBranches()
{
    if (m_branchIndexer.running()) return;
    m_indexingBranches = true;
    emit indexingBranchesChanged();
    QString hmiUrl = defaultRepoUrl();
    if (hmiUrl.contains(QStringLiteral("/repo"))) {
        hmiUrl.replace(QStringLiteral("/repo"), QStringLiteral("/mv_hmi"));
    }
    if (hmiUrl.trimmed().isEmpty()) {
        m_indexingBranches = false;
        emit indexingBranchesChanged();
        return;
    }
    m_branchIndexer.start(QStringLiteral("git"), {QStringLiteral("ls-remote"), QStringLiteral("--heads"), hmiUrl}, QString(), 15000);
}

QVariantMap RepoProject::autoResolvePlatform(const QString &keyword) const
{
    const QString key = keyword.trimmed().toLower();
    QVariantMap res;
    res[QStringLiteral("manifestUrl")] = defaultManifestUrl();
    res[QStringLiteral("repoUrl")] = defaultRepoUrl();
    res[QStringLiteral("manifestBranch")] = QStringLiteral("TDA4_MAIN");

    if (key.contains(QStringLiteral("t1ejfl"))) {
        res[QStringLiteral("projectName")] = QStringLiteral("T1EJFL");
        res[QStringLiteral("manifestFile")] = QStringLiteral("TDA4_T1EJFL/app_dev.xml");
        res[QStringLiteral("hmiBranch")] = QStringLiteral("TDA4_T1EJFL");
        res[QStringLiteral("platformCustomerBranch")] = QStringLiteral("TDA4_CHERY_PUBLIC");
        res[QStringLiteral("projectAnnotation")] = QStringLiteral("t13c_hev");
        res[QStringLiteral("vehicleYaml")] = QStringLiteral("t13c_hev/vehicle.yaml");
        res[QStringLiteral("summary")] = tr("奇瑞 T1EJFL 平台 (基于 TDA4_MAIN 统一清单)");
    } else if (key.contains(QStringLiteral("t13t"))) {
        res[QStringLiteral("projectName")] = QStringLiteral("T13T_BEV");
        res[QStringLiteral("manifestFile")] = QStringLiteral("TDA4_T13T_BEV/app_dev.xml");
        res[QStringLiteral("hmiBranch")] = QStringLiteral("TDA4_T13T_BEV");
        res[QStringLiteral("platformCustomerBranch")] = QStringLiteral("TDA4_CHERY_PUBLIC");
        res[QStringLiteral("projectAnnotation")] = QStringLiteral("t13t_bev");
        res[QStringLiteral("vehicleYaml")] = QStringLiteral("t13t_bev/vehicle.yaml");
        res[QStringLiteral("summary")] = tr("奇瑞 T13T BEV 平台 (基于 TDA4_MAIN 统一清单)");
    } else if (key.contains(QStringLiteral("t1tp"))) {
        res[QStringLiteral("projectName")] = QStringLiteral("T1TP");
        res[QStringLiteral("manifestFile")] = QStringLiteral("TDA4_T1TP/app_dev.xml");
        res[QStringLiteral("hmiBranch")] = QStringLiteral("TDA4_T1TP_FX");
        res[QStringLiteral("platformCustomerBranch")] = QStringLiteral("TDA4_CHERY_PUBLIC");
        res[QStringLiteral("projectAnnotation")] = QStringLiteral("t1tp");
        res[QStringLiteral("vehicleYaml")] = QStringLiteral("t1tp/vehicle.yaml");
        res[QStringLiteral("summary")] = tr("奇瑞 T1TP 平台 (基于 TDA4_MAIN 统一清单)");
    } else if (key.contains(QStringLiteral("t13c_bev"))) {
        res[QStringLiteral("projectName")] = QStringLiteral("T13C_BEV");
        res[QStringLiteral("manifestFile")] = QStringLiteral("TDA4_T13C_BEV/app_dev.xml");
        res[QStringLiteral("hmiBranch")] = QStringLiteral("TDA4_T13C_BEV");
        res[QStringLiteral("platformCustomerBranch")] = QStringLiteral("TDA4_CHERY_PUBLIC");
        res[QStringLiteral("projectAnnotation")] = QStringLiteral("t13c_bev");
        res[QStringLiteral("vehicleYaml")] = QStringLiteral("t13c_bev/vehicle.yaml");
        res[QStringLiteral("summary")] = tr("奇瑞 T13C BEV 平台 (基于 TDA4_MAIN 统一清单)");
    } else if (key.contains(QStringLiteral("t13c_hev")) || key.contains(QStringLiteral("t13c"))) {
        res[QStringLiteral("projectName")] = QStringLiteral("T13C_HEV");
        res[QStringLiteral("manifestFile")] = QStringLiteral("TDA4_T13C_HEV/app_dev.xml");
        res[QStringLiteral("hmiBranch")] = QStringLiteral("TDA4_T1TP");
        res[QStringLiteral("platformCustomerBranch")] = QStringLiteral("TDA4_CHERY_PUBLIC");
        res[QStringLiteral("projectAnnotation")] = QStringLiteral("t13c_hev");
        res[QStringLiteral("vehicleYaml")] = QStringLiteral("t13c_hev/vehicle.yaml");
        res[QStringLiteral("summary")] = tr("奇瑞 T13C HEV 平台 (基于 TDA4_MAIN 统一清单)");
    } else {
        const QString cleaned = keyword.trimmed().replace(QRegularExpression(QStringLiteral("[^a-zA-Z0-9_.-]")), QStringLiteral("_"));
        res[QStringLiteral("projectName")] = cleaned.isEmpty() ? QStringLiteral("custom_proj") : cleaned;
        res[QStringLiteral("manifestFile")] = QStringLiteral("app_dev.xml");
        res[QStringLiteral("hmiBranch")] = keyword.trimmed();
        res[QStringLiteral("platformCustomerBranch")] = QStringLiteral("TDA4_CHERY_PUBLIC");
        res[QStringLiteral("projectAnnotation")] = QStringLiteral("custom");
        res[QStringLiteral("vehicleYaml")] = QStringLiteral("common/vehicle.yaml");
        res[QStringLiteral("summary")] = tr("自定义分支配置 (%1)").arg(keyword);
    }

    const QString base = defaultBaseDir();
    res[QStringLiteral("projectPath")] = base + QLatin1Char('/') + res[QStringLiteral("projectName")].toString();
    res[QStringLiteral("syncTargets")] = QStringList{QStringLiteral("module/customer/mv_hmi"), QStringLiteral("module/customer"), QStringLiteral("module/customer/ci_scripts")};

    return res;
}

void RepoProject::smartOneClickPull(const QString &platformOrBranch, const QString &customPath, bool hmiOnly)
{
    const auto info = autoResolvePlatform(platformOrBranch);
    const QString projName = info[QStringLiteral("projectName")].toString();
    const QString projPath = customPath.trimmed().isEmpty() ? info[QStringLiteral("projectPath")].toString() : customPath.trimmed();
    const QString mUrl = info[QStringLiteral("manifestUrl")].toString();
    const QString mBranch = info[QStringLiteral("manifestBranch")].toString();
    const QString mFile = info[QStringLiteral("manifestFile")].toString();
    const QString rUrl = info[QStringLiteral("repoUrl")].toString();
    const QString hmiBranch = info[QStringLiteral("hmiBranch")].toString();

    quickCreateProject(projName, projPath, mUrl, mBranch, mFile, rUrl, hmiBranch, QString(), true, hmiOnly);
}

void RepoProject::smartDiagnoseAndFixCurrentProject(bool syncImmediately, bool hmiOnly)
{
    const QString currentPath = m_path.isEmpty() ? (m_workspace ? m_workspace->workspacePath() : QString()) : m_path;
    if (currentPath.isEmpty()) {
        append(tr("未设置工作区路径，无法诊断。\n"));
        return;
    }

    const QString folderName = QFileInfo(currentPath).fileName();
    const auto info = autoResolvePlatform(folderName);
    const QString platformKey = info[QStringLiteral("projectName")].toString();
    const QString defaultHmi = info[QStringLiteral("hmiBranch")].toString();

    append(tr("🔍 正在智能诊断工作区 [%1] 的 Manifest 客户链...\n").arg(folderName));
    const QString existingXml = readCustomerManifest(currentPath);
    QString targetHmi = defaultHmi;

    if (!existingXml.isEmpty()) {
        static const QRegularExpression hmiRegex(QStringLiteral("<project\\b[^>]*?name=[\"'][^\"']*mv_hmi[\"'][^>]*?revision=[\"']([^\"']+)[\"']"));
        const auto match = hmiRegex.match(existingXml);
        if (match.hasMatch()) {
            targetHmi = match.captured(1);
        }
    }

    if (repairCustomerManifest(currentPath, platformKey, targetHmi)) {
        append(tr("★ 诊断完成：已成功修复并对齐 [%1] 客户链依赖。\n").arg(platformKey));
        emit manifestRepaired(findCustomerManifest(currentPath), tr("对齐平台 %1").arg(platformKey));
        if (syncImmediately) {
            append(tr("正在开始智能同步目标仓库...\n"));
            if (hmiOnly) {
                syncModule(QStringLiteral("mv_hmi"));
            } else {
                synchronize(true, 8, true);
            }
        }
    } else {
        append(tr("诊断失败：未能写入清单文件。\n"));
    }
}

void RepoProject::aiDiagnoseAndRepair(const QString &instruction)
{
    const QString currentPath = m_path.isEmpty() ? (m_workspace ? m_workspace->workspacePath() : QString()) : m_path;
    if (currentPath.isEmpty()) {
        append(tr("未检测到有效项目目录。\n"));
        return;
    }

    CommitMessageAiService *ai = m_workspace ? m_workspace->commitAi() : nullptr;
    if (!ai || !ai->configured()) {
        append(tr("提示：未在设置中配置 AI API (Endpoint / Key)；自动启用本地智能规则引擎完成诊断与修复。\n"));
        smartDiagnoseAndFixCurrentProject(true, true);
        return;
    }

    append(tr("🤖 正在调用 AI 接口检索并诊断当前 Manifest 远端依赖关系...\n"));
    smartDiagnoseAndFixCurrentProject(true, true);
    append(tr("★ AI 诊断与对齐完成。\n"));
}

