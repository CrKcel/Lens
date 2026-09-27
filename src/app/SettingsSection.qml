import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 卡片分节：圆角容器 + 标题 + 可选行内说明，default 子项即卡片内容。
// 高度随内容自适应，卡片内不放 fillHeight 元素。
Rectangle {
    id: sectionCard

    default property alias content: sectionColumn.data
    property alias title: sectionTitle.text
    property string hint: ""

    color: sectionTheme.card
    radius: sectionTheme.radiusM
    border.color: sectionTheme.cardBorder
    implicitHeight: sectionColumn.implicitHeight + 32

    Theme {
        id: sectionTheme
        dark: settings.dark
    }

    ColumnLayout {
        id: sectionColumn
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 16
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label {
                id: sectionTitle
                color: sectionTheme.text
                font.pixelSize: Math.round(14 * settings.fontScale)
                font.bold: true
            }
            Label {
                visible: sectionCard.hint.length > 0
                text: sectionCard.hint
                color: sectionTheme.textFaint
                font.pixelSize: Math.round(11 * settings.fontScale)
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }
    }
}
