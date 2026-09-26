#include "TitleBarLayout.hpp"

#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

namespace lens {

namespace {

bool desktopMatches(const char *keyword)
{
    return qEnvironmentVariable("XDG_CURRENT_DESKTOP")
        .contains(QLatin1String(keyword), Qt::CaseInsensitive);
}

// KWin 的按钮布局存为字母串（X 为关闭键），左右分列在 kwinrc 的
// org.kde.kdecoration2 组；键缺失说明用户未改默认（右侧）
bool kwinButtonsOnLeft()
{
    QSettings kwin(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
                       + QStringLiteral("/kwinrc"),
                   QSettings::IniFormat);
    if (kwin.value(QStringLiteral("org.kde.kdecoration2/ButtonsOnLeft"))
            .toString()
            .contains(QLatin1Char('X')))
        return true;
    if (kwin.value(QStringLiteral("org.kde.kdecoration2/ButtonsOnRight"))
            .toString()
            .contains(QLatin1Char('X')))
        return false;
    return false;
}

QString commandOutput(const QString &program, const QStringList &args)
{
    QProcess process;
    process.start(program, args);
    if (!process.waitForFinished(500)
        || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return {};
    return QString::fromUtf8(process.readAllStandardOutput().trimmed());
}

// button-layout 形如 "appmenu:minimize,maximize,close"，冒号左侧在窗口左边；
// 布局里没有 close 键时回退右侧
bool parseButtonLayout(const QString &layout)
{
    QString value = layout;
    value.remove(u'\'');
    value.remove(u'"');
    value = value.trimmed();
    const int colon = value.indexOf(u':');
    const QString left = colon < 0 ? QString() : value.left(colon);
    const QString right = colon < 0 ? value : value.mid(colon + 1);
    const auto hasClose = [](const QString &tokens) {
        for (const QString &token : tokens.split(u','))
            if (token.trimmed() == QLatin1String("close"))
                return true;
        return false;
    };
    if (hasClose(left))
        return true;
    return false;
}

bool gsettingsButtonsOnLeft(const char *schema)
{
    const QString layout = commandOutput(QStringLiteral("gsettings"),
        {QStringLiteral("get"), QLatin1String(schema),
         QStringLiteral("button-layout")});
    return !layout.isEmpty() && parseButtonLayout(layout);
}

bool unixButtonsOnLeft()
{
    if (desktopMatches("KDE") || desktopMatches("Plasma"))
        return kwinButtonsOnLeft();
    if (desktopMatches("XFCE")) {
        const QString layout = commandOutput(QStringLiteral("xfconf-query"),
            {QStringLiteral("-c"), QStringLiteral("xfwm4"),
             QStringLiteral("-p"), QStringLiteral("/general/button_layout")});
        return !layout.isEmpty() && parseButtonLayout(layout);
    }
    if (desktopMatches("MATE"))
        return gsettingsButtonsOnLeft("org.mate.Marco.general");
    // GNOME / Unity / Budgie / Cinnamon 及其他 gsettings 环境
    return gsettingsButtonsOnLeft("org.gnome.desktop.wm.preferences")
        || gsettingsButtonsOnLeft("org.cinnamon.desktop.wm.preferences");
}

} // namespace

bool titleBarButtonsOnLeft()
{
#if defined(Q_OS_MACOS)
    return true;
#elif defined(Q_OS_WINDOWS)
    return false;
#else
    return unixButtonsOnLeft();
#endif
}

} // namespace lens
