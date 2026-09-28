import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// MCP 页：左侧服务器列表，右侧编辑 + 连接状态。
// view 回引 SettingsView（提交链路与 mcpServersWorking 工作副本所在），
// 编辑字段经 alias 暴露给提交链路。
RowLayout {
    id: mcpPage

    required property var view

    property alias mcpNameField: mcpNameField
    property alias mcpCommandField: mcpCommandField
    property alias mcpArgsField: mcpArgsField

    Layout.fillWidth: true
    Layout.fillHeight: true
    spacing: 20

    Theme {
        id: theme
        dark: settings.dark
    }

    ColumnLayout {
        Layout.preferredWidth: 200
        Layout.fillHeight: true
        spacing: 6
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            GhostButton {
                Layout.fillWidth: true
                text: qsTr("＋")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("新增 MCP 服务器")
                onClicked: mcpPage.view.addMcpServer()
            }
            GhostButton {
                Layout.fillWidth: true
                text: qsTr("－")
                enabled: mcpPage.view.mcpServersWorking.length > 0
                ToolTip.visible: hovered
                ToolTip.text: qsTr("删除当前服务器")
                onClicked: mcpPage.view.removeMcpServer()
            }
        }
        ListView {
            id: mcpList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 4
            model: mcpPage.view.mcpServersWorking
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: SlimScrollBar {}

            delegate: ItemDelegate {
                width: mcpList.width
                highlighted: index === mcpPage.view.mcpSelected
                onClicked: {
                    mcpPage.view.flushMcpFields()
                    mcpPage.view.mcpSelected = index
                    mcpPage.view.loadMcpFields()
                    mcpPage.view.scheduleCommit()
                }
                background: Rectangle {
                    radius: theme.radiusS
                    color: parent.highlighted ? theme.accentSoft
                         : parent.hovered ? theme.highlight
                         : "transparent"
                }
                contentItem: Label {
                    text: modelData.name.length > 0 ? modelData.name : qsTr("（未命名）")
                    color: highlighted ? theme.accent : theme.text
                    elide: Text.ElideRight
                    font.pixelSize: Math.round(13 * settings.fontScale)
                }
            }

            Text {
                anchors.centerIn: parent
                visible: mcpList.count === 0
                text: qsTr("未配置 MCP 服务器")
                color: theme.textFaint
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        spacing: 14
        enabled: mcpPage.view.mcpServersWorking.length > 0

        SettingsField {
            Layout.fillWidth: true
            label: qsTr("名称")
            TextField {
                id: mcpNameField
                Layout.fillWidth: true
                color: theme.text
                selectByMouse: true
                background: SettingFieldBg {}
                onTextEdited: mcpPage.view.scheduleCommit()
            }
        }
        SettingsField {
            Layout.fillWidth: true
            label: qsTr("启动命令")
            hint: qsTr("stdio 传输，如 npx、python")
            TextField {
                id: mcpCommandField
                Layout.fillWidth: true
                color: theme.text
                selectByMouse: true
                background: SettingFieldBg {}
                onTextEdited: mcpPage.view.scheduleCommit()
            }
        }
        SettingsField {
            Layout.fillWidth: true
            Layout.fillHeight: true
            label: qsTr("参数（每行一个）")
            TextArea {
                id: mcpArgsField
                Layout.fillWidth: true
                Layout.fillHeight: true
                wrapMode: TextArea.Wrap
                font.family: "monospace"
                font.pixelSize: Math.round(11 * settings.fontScale)
                color: theme.text
                background: SettingFieldBg {}
                onTextEdited: mcpPage.view.scheduleCommit()
            }
        }
        Label { text: qsTr("连接状态"); color: theme.textDim; font.pixelSize: Math.round(12 * settings.fontScale) }
        Repeater {
            model: chat.mcpStatus
            Label {
                required property var modelData
                width: parent.width
                text: "· MCP " + modelData.name + "　" + modelData.status
                      + qsTr("　[%1]").arg(modelData.command)
                color: modelData.connected ? theme.success : theme.error
                font.pixelSize: Math.round(11 * settings.fontScale)
                elide: Text.ElideRight
            }
        }
    }
}
