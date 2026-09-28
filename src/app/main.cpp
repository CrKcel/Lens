#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QTimer>
#include <QTranslator>

#include "AppSettings.hpp"
#include "AppStyle.hpp"
#include "ChatController.hpp"
#include "E2eDriver.hpp"
#include "TitleBarLayout.hpp"
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
#if defined(Q_OS_UNIX) && !defined(Q_OS_DARWIN)
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORMTHEME")
        && !qEnvironmentVariable("XDG_CURRENT_DESKTOP").contains(
            QLatin1String("KDE"), Qt::CaseInsensitive)
        && QFileInfo(QLibraryInfo::path(QLibraryInfo::PluginsPath)
                     + QStringLiteral("/platformthemes/libqgtk3.so"))
               .exists()) {
        qputenv("QT_QPA_PLATFORMTHEME", "gtk3");
    }
#endif
    QApplication app(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("Lens"));
    QGuiApplication::setApplicationName(QStringLiteral("Lens"));
    QGuiApplication::setApplicationVersion(QStringLiteral(LENS_VERSION));

    // 统一跨平台桌面观感；定制的控件由 Theme.qml 调色板接管
    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    const QString dataDir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);

    lens::SessionStore store(dataDir + QStringLiteral("/sessions.db"));
    if (!store.open())
        qWarning() << "Session store unavailable:" << store.lastError();

    lens::AppSettings settings(dataDir + QStringLiteral("/settings.json"));
    lens::ChatController chat(&store, &settings, dataDir);
    lens::appstyle::applyPalette(settings.dark(), settings.colorOverrides());
    lens::appstyle::applyAppFont(settings.fontScale());

    QTranslator translator;
    lens::installTranslator(settings.language(), &translator);
    QString installedLanguage = settings.language();

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("appVersion"),
                                             QGuiApplication::applicationVersion());
    engine.rootContext()->setContextProperty(QStringLiteral("chat"), &chat);
    engine.rootContext()->setContextProperty(QStringLiteral("settings"), &settings);
    // 自绘标题栏按钮方位（macOS 左上，Linux 跟随桌面环境设定）
    engine.rootContext()->setContextProperty(QStringLiteral("titleBarButtonsLeft"),
                                             lens::titleBarButtonsOnLeft());
    QObject::connect(&settings, &lens::AppSettings::settingsChanged, &app, [&] {
        const QString language = settings.language();
        if (language == installedLanguage)
            return;
        installedLanguage = language;
        lens::installTranslator(language, &translator);
        engine.retranslate();
    });
    // 主题切换时同步 QPalette（app 启动后以 settings 为准）
    QObject::connect(&settings, &lens::AppSettings::settingsChanged, &app,
                     [&settings] {
                         lens::appstyle::applyPalette(settings.dark(),
                                                      settings.colorOverrides());
                         lens::appstyle::applyAppFont(settings.fontScale());
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
