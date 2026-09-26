import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 侧栏导航项：选中态 accent 左侧竖条 + 浅色底，悬停浅灰底
Button {
    id: control
    property string glyph: ""

    leftPadding: 12
    rightPadding: 12
    implicitHeight: 34
    font.pixelSize: 13

    background: Rectangle {
        radius: 8
        color: control.highlighted ? theme.accentSoft
             : control.hovered ? theme.highlight
             : "transparent"

        Rectangle {
            visible: control.highlighted
            width: 3
            radius: 1.5
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.margins: 7
            color: theme.accent
        }
    }
    contentItem: RowLayout {
        spacing: 8
        Label {
            visible: control.glyph.length > 0
            text: control.glyph
            color: control.highlighted ? theme.accent : theme.textDim
            font.pixelSize: 14
        }
        Label {
            Layout.fillWidth: true
            text: control.text
            color: control.highlighted ? theme.text : theme.textSoft
            font: control.font
            elide: Text.ElideRight
        }
    }

    Theme {
        id: theme
        dark: settings.dark
    }
}
