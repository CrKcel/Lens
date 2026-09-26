import QtQuick

// 设置页输入框统一背景：聚焦时亮起 accent 描边，悬停时边框微亮。
// 作为 background 挂到 TextField/TextArea 时，parent 即控件本身，
// 可直接读取其 activeFocus / hovered 状态。
Rectangle {
    implicitHeight: 36
    radius: 8
    color: theme.field
    border.width: control.activeFocus ? 2 : 1
    border.color: control.activeFocus ? theme.accent
                 : control.hovered ? theme.textFaint
                 : theme.fieldBorder

    Behavior on border.color { ColorAnimation { duration: 100 } }

    // background 项的父级就是宿主控件
    property var control: parent

    Theme {
        id: theme
        dark: settings.dark
    }
}
