#pragma once

#include <QColor>
#include <QString>
#include <QVariantMap>

namespace lens::appstyle {

// 调色板可覆盖的颜色 token 白名单。token 名与 Theme.qml 的属性、外观页
// SettingsAppearancePage 的 paletteTokens 清单对应（三处一致由
// test_app_settings 的 paletteTokensMatchQml 行为用例锁定）
bool isValidPaletteToken(const QString &token);

// Fusion 基础样式下未定制的控件（ComboBox 弹层、ToolTip 等）读 QPalette，
// 需与 Theme.qml 的深浅色保持同步，否则深色主题下会闪出浅色控件。
// Highlight/Link 用与 Theme.qml accent 同源的黑白灰；其余角色同样接入
// 外观页的调色板覆盖（token 对应 Theme.qml 的属性名）
void applyPalette(bool dark, const QVariantMap &overrides);

// 应用默认字体随设置缩放：QML 里显式 font.pixelSize 的文本走 settings.fontScale，
// 未显式指定的控件（ComboBox 弹层、按钮等）读应用字体，两处需同步缩放
void applyAppFont(double scale);

} // namespace lens::appstyle
