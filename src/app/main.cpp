#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStandardPaths>
#include <QTimer>

#include "AppSettings.hpp"
#include "ChatController.hpp"
#include "E2eDriver.hpp"
#include <lens/core/storage/SessionStore.hpp>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("Lens"));
    QGuiApplication::setApplicationName(QStringLiteral("Lens"));
    QGuiApplication::setApplicationVersion(QStringLiteral(LENS_VERSION));

    const QString dataDir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);

    lens::SessionStore store(dataDir + QStringLiteral("/sessions.db"));
    if (!store.open())
        qWarning() << "Session store unavailable:" << store.lastError();

    lens::AppSettings settings(dataDir + QStringLiteral("/settings.json"));
    lens::ChatController chat(&store, &settings);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("appVersion"),
                                             QGuiApplication::applicationVersion());
    engine.rootContext()->setContextProperty(QStringLiteral("chat"), &chat);
    engine.rootContext()->setContextProperty(QStringLiteral("settings"), &settings);
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    QObject::connect(&engine, &QQmlEngine::quit, &app, &QCoreApplication::quit);

    lens::runE2eIfNeeded(engine, &chat, &settings);

    if (QCoreApplication::arguments().contains(QStringLiteral("--qml-check")))
        QTimer::singleShot(1500, &app, [] { QCoreApplication::exit(0); });
    engine.loadFromModule("Lens.app", "Main");
    return app.exec();
}
