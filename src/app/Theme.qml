import QtQuick

// 深浅两套调色板。非单例：使用处各自实例化并绑定 dark: settings.dark
//（settings 是根上下文属性，各 QML 文件都能解析），颜色随主题即时刷新。
// 除颜色外还提供圆角 token，现代风格的几何尺寸统一从这里取值。
// 全部颜色 token 都可被外观页的调色板覆盖（settings.colorOverrides，按深/浅
// 主题分组经 ov() 查表），未覆盖走内置默认值。
QtObject {
    property bool dark: true

    // 调色板覆盖：外观页可按深/浅主题覆盖每个颜色 token（settings.colorOverrides
    // 结构 {"dark": {token: "#hex"}, "light": {...}}），未覆盖的 token 走内置默认值。
    // 覆盖项变化经 settingsChanged 触发各绑定重新求值，即时生效
    readonly property var paletteOverrides:
        settings.colorOverrides[dark ? "dark" : "light"] ?? ({})
    function ov(name, fallback) {
        const value = paletteOverrides[name]
        return (typeof value === "string" && value.length > 0) ? value : fallback
    }

    // 圆角层级：小（输入框/按钮）、中（卡片/气泡）、大（弹窗）
    readonly property int radiusS: 8
    readonly property int radiusM: 12
    readonly property int radiusL: 16

    readonly property color background: ov("background", dark ? "#131418" : "#f5f6f8")
    readonly property color surface: ov("surface", dark ? "#191a20" : "#ffffff")
    readonly property color sidebar: ov("sidebar", dark ? "#2b2b2b" : "#ececee")
    readonly property color field: ov("field", dark ? "#23242b" : "#eef0f3")
    readonly property color fieldBorder: ov("fieldBorder", dark ? "#31323c" : "#d9dbe1")
    readonly property color card: ov("card", dark ? "#1e1f26" : "#f7f8fa")
    readonly property color cardBorder: ov("cardBorder", dark ? "#2b2c35" : "#e3e4ea")
    readonly property color highlight: ov("highlight", dark ? "#2d2e39" : "#e6e7ee")
    readonly property color text: ov("text", dark ? "#e9eaee" : "#1b1c21")
    readonly property color textSoft: ov("textSoft", dark ? "#d5d6dc" : "#3a3b42")
    readonly property color textDim: ov("textDim", dark ? "#8d8e99" : "#6b6c76")
    readonly property color textFaint: ov("textFaint", dark ? "#5c5d67" : "#9a9ba4")
    // 强调色系列：黑白灰设计，深色主题取近白、浅色主题取近黑（实底按钮文字
    // 用 theme.background 形成反差）；main.cpp 的 QPalette Highlight/Link
    // 用同一灰阶同步。每个颜色同样可被调色板覆盖（ov 第一参数）
    readonly property color accent: ov("accent", dark ? "#e4e5ea" : "#26272d")
    readonly property color accentHover: ov("accentHover", dark ? "#f0f1f4" : "#3a3b42")
    readonly property color accentPressed: ov("accentPressed", dark ? "#c9cbd3" : "#101116")
    readonly property color accentSoft: ov("accentSoft", dark ? "#2a2b32" : "#eceef1")
    readonly property color accentBorder: ov("accentBorder", dark ? "#4a4b55" : "#c4c6cd")
    readonly property color success: ov("success", dark ? "#8fd18f" : "#2e8b47")
    readonly property color error: ov("error", dark ? "#e58585" : "#c0392b")
    readonly property color errorSoft: ov("errorSoft", dark ? "#39262a" : "#fbe9e7")
    readonly property color bubbleUser: ov("bubbleUser", dark ? "#33343c" : accent)
    readonly property color bubbleUser2: ov("bubbleUser2", dark ? "#2b2c33" : accentPressed)
    readonly property color bubbleUserText: ov("bubbleUserText", "#eff0f2")
    readonly property color divider: ov("divider", dark ? "#24252c" : "#e8e9ee")

    // token 名 → 当前生效颜色（外观页调色板色块的展示数据源）
    readonly property var tokens: ({
        "background": background, "surface": surface, "sidebar": sidebar,
        "field": field, "fieldBorder": fieldBorder, "card": card,
        "cardBorder": cardBorder, "highlight": highlight, "text": text,
        "textSoft": textSoft, "textDim": textDim, "textFaint": textFaint,
        "accent": accent, "accentHover": accentHover, "accentPressed": accentPressed,
        "accentSoft": accentSoft, "accentBorder": accentBorder, "success": success,
        "error": error, "errorSoft": errorSoft, "bubbleUser": bubbleUser,
        "bubbleUser2": bubbleUser2, "bubbleUserText": bubbleUserText, "divider": divider
    })
}
