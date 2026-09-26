import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 上下文检查器：上下文由什么组成、每一项来自哪里。
// 数据源：chat.contextSections / contextTools / mcpStatus / usageSummary。
Popup {
    id: inspector

    // 由宿主注入 ChatView 的 token 格式化函数
    property var formatTokens: (n) => String(n)

    modal: true
    width: 660
    height: 620
    anchors.centerIn: parent
    padding: 14
    Overlay.modal: Rectangle {
        color: settings.dark ? "#66000000" : "#33000000"
    }
    background: Rectangle {
        color: theme.surface
        border.color: theme.cardBorder
        radius: theme.radiusL
    }

    Theme {
        id: theme
        dark: settings.dark
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: qsTr("上下文检查器")
                font.pixelSize: 16; font.bold: true; color: theme.text
                Layout.fillWidth: true
            }
            ToolButton {
                text: qsTr("刷新")
                flat: true
                onClicked: chat.refreshContext()
            }
        }

        // 用量统计：数据来自服务端 usage 上报（各协议归一到 TokenUsage）
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: usageCard.implicitHeight + 20
            visible: chat.usageSummary.hasUsage
            color: theme.card
            radius: theme.radiusM
            border.color: theme.cardBorder

            ColumnLayout {
                id: usageCard
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.margins: 10
                spacing: 3

                Label {
                    text: qsTr("用量统计")
                    color: theme.accent
                    font.bold: true
                    font.pixelSize: 13
                }
                Label {
                    Layout.fillWidth: true
                    text: {
                        const u = chat.usageSummary
                        let lines = [
                            qsTr("当前上下文（最近一次输入）：%1 tokens")
                                .arg(inspector.formatTokens(u.contextTokens)),
                            qsTr("会话累计：输入 %1 / 输出 %2 / 缓存命中 %3 tokens")
                                .arg(inspector.formatTokens(u.totalPrompt))
                                .arg(inspector.formatTokens(u.totalCompletion))
                                .arg(inspector.formatTokens(u.totalCached))
                        ]
                        if (u.hasCost)
                            lines.push(qsTr("累计花费：%1")
                                .arg(Number(u.cost).toFixed(4)))
                        return lines.join("\n")
                    }
                    color: theme.textDim
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                    textFormat: Text.PlainText
                }
            }
        }

        ListView {
            id: sectionList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 8
            model: chat.contextSections
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: SlimScrollBar {}

            delegate: Rectangle {
                width: sectionList.width
                height: sectionCard.implicitHeight
                color: theme.card
                radius: theme.radiusM
                border.color: theme.cardBorder

                ColumnLayout {
                    id: sectionCard
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.margins: 10
                    spacing: 4

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: modelData.name
                            color: theme.accent
                            font.bold: true
                            font.pixelSize: 13
                        }
                        Item { Layout.fillWidth: true }
                        Label {
                            text: qsTr("来源：%1").arg(modelData.source)
                            color: theme.textDim
                            font.pixelSize: 11
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        text: modelData.content.length > 0
                              ? modelData.content : qsTr("（空，未注入）")
                        visible: sectionExpanded
                        color: modelData.content.length > 0 ? theme.textSoft : theme.textFaint
                        wrapMode: Text.Wrap
                        font.pixelSize: 12
                        textFormat: Text.PlainText
                    }
                }

                property bool sectionExpanded: index === 0
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: sectionExpanded = !sectionExpanded
                }
            }

            Text {
                anchors.centerIn: parent
                visible: sectionList.count === 0
                text: qsTr("发送消息后这里会展示系统提示词的分节组成")
                color: theme.textFaint
            }
        }

        Label {
            property int enabledCount: chat.contextTools.filter(t => t.enabled).length
            text: qsTr("启用工具（%1/%2）").arg(enabledCount).arg(chat.contextTools.length)
            color: theme.text; font.bold: true; font.pixelSize: 13
        }
        ListView {
            id: toolList
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(160, 34 * Math.max(1, chat.contextTools.length))
            clip: true
            spacing: 2
            model: chat.contextTools
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: SlimScrollBar {}

            delegate: Label {
                width: toolList.width
                text: "· " + modelData.name
                      + qsTr("　[%1]").arg(modelData.origin)
                      + (modelData.description.length > 0
                         ? "　— " + modelData.description : "")
                      + (modelData.enabled ? "" : qsTr("　（已禁用）"))
                color: modelData.enabled ? theme.textDim : theme.textFaint
                font.pixelSize: 11
                elide: Text.ElideRight
            }

            Text {
                anchors.centerIn: parent
                visible: toolList.count === 0
                text: qsTr("无")
                color: theme.textFaint
            }
        }

        ListView {
            id: mcpStatusList
            Layout.fillWidth: true
            Layout.preferredHeight: 28 * Math.min(3, Math.max(1, mcpStatusList.count))
            clip: true
            spacing: 2
            model: chat.mcpStatus
            visible: chat.mcpStatus.length > 0

            delegate: Label {
                width: mcpStatusList.width
                text: "· MCP " + modelData.name + "　"
                      + modelData.status
                      + qsTr("　[%1]").arg(modelData.command)
                color: modelData.connected ? theme.success : theme.error
                font.pixelSize: 11
                elide: Text.ElideRight
            }
        }
    }
}
