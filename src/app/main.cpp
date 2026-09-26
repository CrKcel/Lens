#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QLocale>
#include <QPalette>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
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

// Fusion 基础样式下未定制的控件（ComboBox 弹层、ToolTip 等）读 QPalette，
// 需与 Theme.qml 的深浅色保持同步，否则深色主题下会闪出浅色控件。
static void applyPalette(bool dark)
{
    QPalette p;
    if (dark) {
        p.setColor(QPalette::Window, QColor(0x13, 0x14, 0x18));
        p.setColor(QPalette::WindowText, QColor(0xe9, 0xea, 0xee));
        p.setColor(QPalette::Base, QColor(0x23, 0x24, 0x2b));
        p.setColor(QPalette::AlternateBase, QColor(0x1e, 0x1f, 0x26));
        p.setColor(QPalette::Text, QColor(0xe9, 0xea, 0xee));
        p.setColor(QPalette::Button, QColor(0x23, 0x24, 0x2b));
        p.setColor(QPalette::ButtonText, QColor(0xe9, 0xea, 0xee));
        p.setColor(QPalette::ToolTipBase, QColor(0x1e, 0x1f, 0x26));
        p.setColor(QPalette::ToolTipText, QColor(0xd5, 0xd6, 0xdc));
        p.setColor(QPalette::Highlight, QColor(0x5b, 0x9b, 0xd9));
        p.setColor(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));
        p.setColor(QPalette::PlaceholderText, QColor(0x5c, 0x5d, 0x67));
        p.setColor(QPalette::Light, QColor(0x31, 0x32, 0x3c));
        p.setColor(QPalette::Mid, QColor(0x2b, 0x2c, 0x35));
        p.setColor(QPalette::Dark, QColor(0x19, 0x1a, 0x20));
        p.setColor(QPalette::Link, QColor(0x5b, 0x9b, 0xd9));
    } else {
        p.setColor(QPalette::Window, QColor(0xf5, 0xf6, 0xf8));
        p.setColor(QPalette::WindowText, QColor(0x1b, 0x1c, 0x21));
        p.setColor(QPalette::Base, QColor(0xff, 0xff, 0xff));
        p.setColor(QPalette::AlternateBase, QColor(0xf0, 0xf1, 0xf4));
        p.setColor(QPalette::Text, QColor(0x1b, 0x1c, 0x21));
        p.setColor(QPalette::Button, QColor(0xee, 0xf0, 0xf3));
        p.setColor(QPalette::ButtonText, QColor(0x1b, 0x1c, 0x21));
        p.setColor(QPalette::ToolTipBase, QColor(0xff, 0xff, 0xff));
        p.setColor(QPalette::ToolTipText, QColor(0x3a, 0x3b, 0x42));
        p.setColor(QPalette::Highlight, QColor(0x2f, 0x7f, 0xe0));
        p.setColor(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));
        p.setColor(QPalette::PlaceholderText, QColor(0x9a, 0x9b, 0xa4));
        p.setColor(QPalette::Light, QColor(0xff, 0xff, 0xff));
        p.setColor(QPalette::Mid, QColor(0xd9, 0xdb, 0xe1));
        p.setColor(QPalette::Dark, QColor(0xb9, 0xbb, 0xc4));
        p.setColor(QPalette::Link, QColor(0x2f, 0x7f, 0xe0));
    }
    QGuiApplication::setPalette(p);
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
    lens::applyPalette(settings.dark());

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
    // 主题切换时同步 QPalette（app 启动后以 settings 为准）
    QObject::connect(&settings, &lens::AppSettings::settingsChanged, &app,
                     [&settings] { lens::applyPalette(settings.dark()); });
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
