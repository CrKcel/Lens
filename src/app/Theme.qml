import QtQuick

// 深浅两套调色板。非单例：使用处各自实例化并绑定 dark: settings.dark
//（settings 是根上下文属性，各 QML 文件都能解析），颜色随主题即时刷新。
// 除颜色外还提供圆角 token，现代风格的几何尺寸统一从这里取值。
QtObject {
    property bool dark: true

    // 圆角层级：小（输入框/按钮）、中（卡片/气泡）、大（弹窗）
    readonly property int radiusS: 8
    readonly property int radiusM: 12
    readonly property int radiusL: 16

    readonly property color background: dark ? "#131418" : "#f5f6f8"
    readonly property color surface: dark ? "#191a20" : "#ffffff"
    readonly property color field: dark ? "#23242b" : "#eef0f3"
    readonly property color fieldBorder: dark ? "#31323c" : "#d9dbe1"
    readonly property color card: dark ? "#1e1f26" : "#f7f8fa"
    readonly property color cardBorder: dark ? "#2b2c35" : "#e3e4ea"
    readonly property color highlight: dark ? "#2d2e39" : "#e6e7ee"
    readonly property color accentBorder: dark ? "#3c5f80" : "#a8cbe8"
    readonly property color text: dark ? "#e9eaee" : "#1b1c21"
    readonly property color textSoft: dark ? "#d5d6dc" : "#3a3b42"
    readonly property color textDim: dark ? "#8d8e99" : "#6b6c76"
    readonly property color textFaint: dark ? "#5c5d67" : "#9a9ba4"
    readonly property color accent: dark ? "#5b9bd9" : "#2f7fe0"
    readonly property color accentHover: dark ? "#6ea9e0" : "#4a90e6"
    readonly property color accentPressed: dark ? "#4b8ac9" : "#2670c8"
    readonly property color accentSoft: dark ? "#20334a" : "#e3eefb"
    readonly property color success: dark ? "#8fd18f" : "#2e8b47"
    readonly property color error: dark ? "#e58585" : "#c0392b"
    readonly property color errorSoft: dark ? "#39262a" : "#fbe9e7"
    readonly property color bubbleUser: dark ? "#2d5d7c" : "#2f7fe0"
    readonly property color bubbleUser2: dark ? "#25506e" : "#2670c8"
    readonly property color bubbleUserText: "#eef4f6"
    readonly property color divider: dark ? "#24252c" : "#e8e9ee"
}
