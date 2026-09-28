import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 侧栏导航项：选中态浅色底，悬停浅灰底
Button {
    id: control

    leftPadding: 12
    rightPadding: 12
    implicitHeight: 34
    font.pixelSize: Math.round(13 * settings.fontScale)

    background: Rectangle {
        radius: 8
        color: control.highlighted ? theme.accentSoft
             : control.hovered ? theme.highlight
             : "transparent"
    }
    contentItem: Label {
        text: control.text
        color: control.highlighted ? theme.text : theme.textSoft
        font: control.font
        elide: Text.ElideRight
    }

    Theme {
        id: theme
        dark: settings.dark
    }
}
