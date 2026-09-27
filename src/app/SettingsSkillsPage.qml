import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Skills 页：自动发现的技能清单（只读），自包含无需提交链路
ColumnLayout {
    id: skillsPage

    // 递增触发清单刷新（model 绑定引用该属性以重新求值）
    property int skillsRevision: 0

    Layout.fillWidth: true
    Layout.fillHeight: true
    spacing: 12

    Theme {
        id: theme
        dark: settings.dark
    }

    RowLayout {
        Layout.fillWidth: true
        Item { Layout.fillWidth: true }
        ToolButton {
            text: qsTr("刷新")
            onClicked: skillsPage.skillsRevision++
        }
    }
    ListView {
        id: skillList
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        spacing: 8
        model: skillsPage.skillsRevision >= 0
               ? chat.skillsList(chat.currentWorkdir) : []
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: SlimScrollBar {}

        delegate: Rectangle {
            width: skillList.width
            height: skillCard.implicitHeight
            color: theme.card
            radius: theme.radiusM
            border.color: theme.cardBorder

            ColumnLayout {
                id: skillCard
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.margins: 12
                spacing: 2

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: modelData.name
                        color: theme.accent
                        font.bold: true
                        font.pixelSize: Math.round(13 * settings.fontScale)
                    }
                    Item { Layout.fillWidth: true }
                    Label {
                        text: modelData.origin === "global"
                              ? qsTr("全局") : qsTr("项目")
                        color: theme.textDim
                        font.pixelSize: Math.round(10 * settings.fontScale)
                    }
                }
                Label {
                    Layout.fillWidth: true
                    visible: modelData.description.length > 0
                    text: modelData.description
                    color: theme.textSoft
                    wrapMode: Text.Wrap
                    font.pixelSize: Math.round(12 * settings.fontScale)
                }
                Label {
                    Layout.fillWidth: true
                    text: modelData.path
                    color: theme.textDim
                    font.family: "monospace"
                    font.pixelSize: Math.round(10 * settings.fontScale)
                    elide: Text.ElideMiddle
                }
            }
        }

        Text {
            anchors.centerIn: parent
            visible: skillList.count === 0
            text: qsTr("未发现技能")
            color: theme.textFaint
        }
    }
    Label {
        text: qsTr("全局目录：数据目录下 skills/*/SKILL.md；项目目录：工作文件夹下 .lens/skills/")
        color: theme.textFaint
        font.pixelSize: Math.round(11 * settings.fontScale)
    }
}
