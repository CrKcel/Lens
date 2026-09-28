import QtQuick
import QtQuick.Controls

// 现代自绘开关（iOS 风格滑块，不依赖控件默认样式）：药丸轨道 + 圆形滑块，
// 颜色与位置带过渡动画。开启态轨道取 accent（实底），滑块用 background 形成
// 反差；关闭态轨道取 field，滑块取 textFaint。悬停轨道边框微亮，按下滑块
// 微缩。键盘操作由 AbstractButton 提供（空格切换）。
AbstractButton {
    id: control

    checkable: true

    implicitWidth: Math.round(44 * settings.fontScale)
    implicitHeight: Math.round(26 * settings.fontScale)

    readonly property int knobSize: height - 6
    readonly property int knobMargin: 3

    opacity: enabled ? 1.0 : 0.5

    background: Rectangle {
        radius: height / 2
        color: control.checked ? theme.accent : theme.field
        border.width: control.checked ? 0 : 1
        border.color: control.hovered ? theme.textFaint : theme.fieldBorder

        Behavior on color { ColorAnimation { duration: 150 } }
        Behavior on border.color { ColorAnimation { duration: 100 } }

        Rectangle {
            x: control.checked
               ? control.width - width - control.knobMargin : control.knobMargin
            y: (parent.height - height) / 2
            width: control.knobSize
            height: control.knobSize * (control.pressed ? 0.9 : 1.0)
            radius: height / 2
            color: control.checked ? theme.background : theme.textFaint

            Behavior on x { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
            Behavior on color { ColorAnimation { duration: 150 } }
            Behavior on height { NumberAnimation { duration: 100 } }
        }
    }

    Theme {
        id: theme
        dark: settings.dark
    }
}
