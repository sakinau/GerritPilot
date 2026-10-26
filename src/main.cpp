#include "workspacecontroller.h"
#include "repoproject.h"

#include <QFont>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QSettings>
#include <QSvgRenderer>

int main(int argc, char *argv[])
{
    // Keep legacy users' settings without retaining company-specific app identity.
    QSettings currentSettings(QSettings::NativeFormat, QSettings::UserScope,
                              QStringLiteral("GerritPilot"), QStringLiteral("GerritPilot"));
    if (!currentSettings.value(QStringLiteral("legacySettingsImported"), false).toBool()) {
        QSettings legacySettings(QSettings::NativeFormat, QSettings::UserScope,
                                 QStringLiteral("MVPilot"), QStringLiteral("GerritPilot"));
        for (const QString &key : legacySettings.allKeys()) {
            if (!currentSettings.contains(key)) currentSettings.setValue(key, legacySettings.value(key));
        }
        currentSettings.setValue(QStringLiteral("legacySettingsImported"), true);
    }
    // Apply the user-selected zoom before Qt initializes screen scaling.
    // An explicit QT_SCALE_FACTOR from the desktop environment takes precedence.
    QSettings visualSettings(QSettings::NativeFormat, QSettings::UserScope,
                             QStringLiteral("GerritPilot"), QStringLiteral("GerritPilot"));
    const int scalePercent = qBound(80, visualSettings.value(QStringLiteral("Appearance/uiScalePercent"), 100).toInt(), 200);
    if (scalePercent != 100 && qEnvironmentVariableIsEmpty("QT_SCALE_FACTOR"))
        qputenv("QT_SCALE_FACTOR", QByteArray::number(scalePercent / 100.0, 'f', 2));

    QGuiApplication application(argc, argv);
    QGuiApplication::setDesktopFileName(QStringLiteral("gerritpilot"));
    QCoreApplication::setApplicationName(QStringLiteral("GerritPilot"));
    QCoreApplication::setOrganizationName(QStringLiteral("GerritPilot"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QFont appFont = application.font();
    appFont.setPointSize(10);
    appFont.setStyleHint(QFont::SansSerif, QFont::PreferAntialias);
    application.setFont(appFont);

    // Same SVG as the in-app logo (AppIcon.qml) and the installed desktop icon.
    QSvgRenderer logo(QStringLiteral(":/qt/qml/GerritPilot/qml/assets/gerritpilot.svg"));
    QIcon appIcon;
    for (const int size : {16, 24, 32, 48, 64, 128, 256}) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        logo.render(&painter);
        painter.end();
        appIcon.addPixmap(pixmap);
    }
    application.setWindowIcon(appIcon);

    WorkspaceController workspace;
    RepoProject repoProject(&workspace);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("workspace"), &workspace);
    engine.rootContext()->setContextProperty(QStringLiteral("repoProject"), &repoProject);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &application, [] { QCoreApplication::exit(-1); },
                     Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("GerritPilot"), QStringLiteral("App"));
    return application.exec();
}
