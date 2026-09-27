import QtQuick
import QtQuick.Controls

// 主操作按钮：accent 实底、圆角，悬停/按下有明暗层次。
// 黑白灰设计下 accent 深浅主题互为反色，文字取背景色保持对比
Button {
    id: control
    implicitHeight: 34
    font.pixelSize: Math.round(13 * settings.fontScale)

    background: Rectangle {
        radius: 8
        color: !control.enabled ? theme.field
             : control.down ? theme.accentPressed
             : control.hovered ? theme.accentHover
             : theme.accent
        border.width: control.enabled ? 0 : 1
        border.color: theme.fieldBorder

        Behavior on color { ColorAnimation { duration: 100 } }
    }
    contentItem: Label {
        text: control.text
        font: control.font
        color: control.enabled ? theme.background : theme.textFaint
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    Theme {
        id: theme
        dark: settings.dark
    }
}
