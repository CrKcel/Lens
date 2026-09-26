import QtQuick
import QtQuick.Controls

// 细圆角滚动条：平时淡出，悬停/拖动时加深
ScrollBar {
    id: control
    implicitWidth: 10
    implicitHeight: 10
    visible: size < 1.0

    background: null
    contentItem: Rectangle {
        implicitWidth: 4
        implicitHeight: 4
        radius: 2
        color: control.pressed ? theme.textDim
             : control.hovered ? theme.textFaint
             : theme.fieldBorder
        opacity: control.pressed || control.hovered ? 1.0 : 0.55

        Behavior on opacity { NumberAnimation { duration: 120 } }
    }

    Theme {
        id: theme
        dark: settings.dark
    }
}
