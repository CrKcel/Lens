import QtQuick

// 深浅两套调色板。非单例：使用处各自实例化并绑定 dark: settings.dark
//（settings 是根上下文属性，各 QML 文件都能解析），颜色随主题即时刷新。
QtObject {
    property bool dark: true

    readonly property color background: dark ? "#16171b" : "#f4f4f6"
    readonly property color surface: dark ? "#1d1e24" : "#ffffff"
    readonly property color field: dark ? "#24252c" : "#ececef"
    readonly property color fieldBorder: dark ? "#33343e" : "#d5d6dc"
    readonly property color card: dark ? "#1f2027" : "#f0f0f3"
    readonly property color cardBorder: dark ? "#2c2d36" : "#dcdde3"
    readonly property color highlight: dark ? "#2c2d36" : "#e2e2e8"
    readonly property color accentBorder: dark ? "#4a6d8a" : "#8ab8d8"
    readonly property color text: dark ? "#e8e8e8" : "#1c1d22"
    readonly property color textSoft: dark ? "#d8d8dc" : "#3a3b42"
    readonly property color textDim: dark ? "#8a8b94" : "#6b6c76"
    readonly property color textFaint: dark ? "#5a5b64" : "#9a9ba4"
    readonly property color accent: dark ? "#7fb4d8" : "#2a6f9e"
    readonly property color success: dark ? "#9cdc9c" : "#2e8b47"
    readonly property color error: dark ? "#e07a7a" : "#c0392b"
    readonly property color bubbleUser: "#2d5d7c"
    readonly property color bubbleUserText: "#eaf2ea"
}
