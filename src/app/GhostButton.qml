import QtQuick
import QtQuick.Controls

// 次要操作按钮：自绘描边样式（不依赖控件默认样式），悬停浅底、按下更深
Button {
    id: control
    implicitHeight: 34
    font.pixelSize: Math.round(13 * settings.fontScale)

    background: Rectangle {
        radius: 8
        color: !control.enabled ? "transparent"
             : control.down ? theme.highlight
             : control.hovered ? theme.field
             : "transparent"
        border.width: 1
        border.color: control.enabled ? theme.fieldBorder : "transparent"

        Behavior on color { ColorAnimation { duration: 100 } }
    }
    contentItem: Label {
        text: control.text
        font: control.font
        color: control.enabled ? theme.textSoft : theme.textFaint
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    Theme {
        id: theme
        dark: settings.dark
    }
}
