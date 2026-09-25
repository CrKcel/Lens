#include <QDir>
#include <QGuiApplication>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStandardPaths>
#include <QTimer>
#include <QTranslator>

#include "AppSettings.hpp"
#include "ChatController.hpp"
#include "E2eDriver.hpp"
#include <lens/core/storage/SessionStore.hpp>

namespace lens {

static void installTranslator(const QString &language, QTranslator *translator)
{
    QGuiApplication::removeTranslator(translator);
    const bool chinese = language == QStringLiteral("zh")
        || (language == QStringLiteral("system")
            && QLocale::system().name().startsWith(QLatin1String("zh")));
    if (chinese)
        return;
    if (translator->load(QStringLiteral(":/i18n/lens_en.qm")))
        QGuiApplication::installTranslator(translator);
    else
        qWarning() << "Translation file unavailable: :/i18n/lens_en.qm";
}

} // namespace lens

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
    lens::ChatController chat(&store, &settings, dataDir);

    QTranslator translator;
    lens::installTranslator(settings.language(), &translator);
    QString installedLanguage = settings.language();

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("appVersion"),
                                             QGuiApplication::applicationVersion());
    engine.rootContext()->setContextProperty(QStringLiteral("chat"), &chat);
    engine.rootContext()->setContextProperty(QStringLiteral("settings"), &settings);
    QObject::connect(&settings, &lens::AppSettings::settingsChanged, &app, [&] {
        const QString language = settings.language();
        if (language == installedLanguage)
            return;
        installedLanguage = language;
        lens::installTranslator(language, &translator);
        engine.retranslate();
    });
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
