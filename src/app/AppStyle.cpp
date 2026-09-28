#include "AppStyle.hpp"

#include <QFont>
#include <QGuiApplication>
#include <QPalette>
#include <QSet>

namespace lens {
namespace {

// 调色板可覆盖的颜色 token 白名单（与 Theme.qml 的属性名、外观页的
// paletteTokens 清单一致，见 AppStyle.hpp 注释）
const QSet<QString> &paletteTokens()
{
    static const QSet<QString> kTokens = {
        QStringLiteral("background"), QStringLiteral("surface"), QStringLiteral("sidebar"),
        QStringLiteral("field"), QStringLiteral("fieldBorder"), QStringLiteral("card"),
        QStringLiteral("cardBorder"), QStringLiteral("highlight"), QStringLiteral("text"),
        QStringLiteral("textSoft"), QStringLiteral("textDim"), QStringLiteral("textFaint"),
        QStringLiteral("accent"), QStringLiteral("accentHover"), QStringLiteral("accentPressed"),
        QStringLiteral("accentSoft"), QStringLiteral("accentBorder"), QStringLiteral("success"),
        QStringLiteral("error"), QStringLiteral("errorSoft"), QStringLiteral("bubbleUser"),
        QStringLiteral("bubbleUser2"), QStringLiteral("bubbleUserText"), QStringLiteral("divider")
    };
    return kTokens;
}

} // namespace

namespace appstyle {

bool isValidPaletteToken(const QString &token)
{
    return paletteTokens().contains(token);
}

void applyPalette(bool dark, const QVariantMap &overrides)
{
    const QVariantMap tokens =
        overrides.value(dark ? QStringLiteral("dark") : QStringLiteral("light")).toMap();
    const auto ov = [&tokens](const char *token, const QColor &fallback) {
        const QColor overridden(tokens.value(QLatin1String(token)).toString());
        return overridden.isValid() ? overridden : fallback;
    };
    QPalette p;
    // 黑白灰设计：选中/链接色取中灰，白字在其上仍可读，深浅两套通用
    const QColor highlight = ov("accent", QColor(0x55, 0x56, 0x60));
    if (dark) {
        p.setColor(QPalette::Window, ov("background", QColor(0x13, 0x14, 0x18)));
        p.setColor(QPalette::WindowText, ov("text", QColor(0xe9, 0xea, 0xee)));
        p.setColor(QPalette::Base, ov("field", QColor(0x23, 0x24, 0x2b)));
        p.setColor(QPalette::AlternateBase, ov("card", QColor(0x1e, 0x1f, 0x26)));
        p.setColor(QPalette::Text, ov("text", QColor(0xe9, 0xea, 0xee)));
        p.setColor(QPalette::Button, ov("field", QColor(0x23, 0x24, 0x2b)));
        p.setColor(QPalette::ButtonText, ov("text", QColor(0xe9, 0xea, 0xee)));
        p.setColor(QPalette::ToolTipBase, ov("card", QColor(0x1e, 0x1f, 0x26)));
        p.setColor(QPalette::ToolTipText, ov("textSoft", QColor(0xd5, 0xd6, 0xdc)));
        p.setColor(QPalette::Highlight, highlight);
        p.setColor(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));
        p.setColor(QPalette::PlaceholderText, ov("textFaint", QColor(0x5c, 0x5d, 0x67)));
        p.setColor(QPalette::Light, ov("fieldBorder", QColor(0x31, 0x32, 0x3c)));
        p.setColor(QPalette::Mid, ov("cardBorder", QColor(0x2b, 0x2c, 0x35)));
        p.setColor(QPalette::Dark, ov("surface", QColor(0x19, 0x1a, 0x20)));
        p.setColor(QPalette::Link, highlight);
    } else {
        p.setColor(QPalette::Window, ov("background", QColor(0xf5, 0xf6, 0xf8)));
        p.setColor(QPalette::WindowText, ov("text", QColor(0x1b, 0x1c, 0x21)));
        p.setColor(QPalette::Base, ov("field", QColor(0xff, 0xff, 0xff)));
        p.setColor(QPalette::AlternateBase, ov("card", QColor(0xf0, 0xf1, 0xf4)));
        p.setColor(QPalette::Text, ov("text", QColor(0x1b, 0x1c, 0x21)));
        p.setColor(QPalette::Button, ov("field", QColor(0xee, 0xf0, 0xf3)));
        p.setColor(QPalette::ButtonText, ov("text", QColor(0x1b, 0x1c, 0x21)));
        p.setColor(QPalette::ToolTipBase, ov("surface", QColor(0xff, 0xff, 0xff)));
        p.setColor(QPalette::ToolTipText, ov("textSoft", QColor(0x3a, 0x3b, 0x42)));
        p.setColor(QPalette::Highlight, highlight);
        p.setColor(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));
        p.setColor(QPalette::PlaceholderText, ov("textFaint", QColor(0x9a, 0x9b, 0xa4)));
        p.setColor(QPalette::Light, ov("surface", QColor(0xff, 0xff, 0xff)));
        p.setColor(QPalette::Mid, ov("fieldBorder", QColor(0xd9, 0xdb, 0xe1)));
        p.setColor(QPalette::Dark, QColor(0xb9, 0xbb, 0xc4));
        p.setColor(QPalette::Link, highlight);
    }
    QGuiApplication::setPalette(p);
}

void applyAppFont(double scale)
{
    static const double basePointSize = [] {
        const double size = QGuiApplication::font().pointSizeF();
        return size > 0 ? size : 10.0;
    }();
    QFont font = QGuiApplication::font();
    font.setPointSizeF(basePointSize * scale);
    QGuiApplication::setFont(font);
}

} // namespace appstyle

} // namespace lens
