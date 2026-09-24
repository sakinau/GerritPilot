#include "workspacecontroller.h"
#include "repoproject.h"

#include <QFont>
#include <QIcon>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QSettings>

int main(int argc, char *argv[])
{
    // Apply the user-selected zoom before Qt initializes screen scaling.
    // An explicit QT_SCALE_FACTOR from the desktop environment takes precedence.
    QSettings visualSettings(QSettings::NativeFormat, QSettings::UserScope,
                             QStringLiteral("MVPilot"), QStringLiteral("GerritPilot"));
    const int scalePercent = qBound(80, visualSettings.value(QStringLiteral("Appearance/uiScalePercent"), 100).toInt(), 200);
    if (scalePercent != 100 && qEnvironmentVariableIsEmpty("QT_SCALE_FACTOR"))
        qputenv("QT_SCALE_FACTOR", QByteArray::number(scalePercent / 100.0, 'f', 2));

    QGuiApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("GerritPilot"));
    QCoreApplication::setOrganizationName(QStringLiteral("MVPilot"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QFont appFont = application.font();
    appFont.setPointSize(10);
    appFont.setStyleHint(QFont::SansSerif, QFont::PreferAntialias);
    application.setFont(appFont);

    QIcon appIcon;
    for (const int size : {32, 64, 128}) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.scale(size / 64.0, size / 64.0);
        QLinearGradient gradient(0, 0, 64, 64);
        gradient.setColorAt(0, QColor(QStringLiteral("#42B5FF")));
        gradient.setColorAt(1, QColor(QStringLiteral("#0A84FF")));
        painter.setPen(Qt::NoPen);
        painter.setBrush(gradient);
        painter.drawRoundedRect(QRectF(1, 1, 62, 62), 16, 16);
        painter.setPen(QPen(Qt::white, 4.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        QPainterPath branch;
        branch.moveTo(20, 20);
        branch.lineTo(20, 46);
        branch.moveTo(20, 29);
        branch.lineTo(45, 29);
        branch.lineTo(45, 43);
        painter.drawPath(branch);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::white);
        for (const QPointF point : {QPointF(20, 18), QPointF(20, 46), QPointF(45, 45)})
            painter.drawEllipse(point, 5, 5);
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
