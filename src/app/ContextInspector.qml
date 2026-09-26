import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 上下文检查器：侧栏内嵌面板（Main 侧栏设置按钮上方），展示上下文分节组成、
// 工具与 MCP 状态、用量统计。数据源：chat.contextSections / contextTools /
// mcpStatus / usageSummary。折叠/展开由宿主控制（Sidebar 经 expanded 属性，
// 展开前先调 chat.refreshContext() 拉取快照）。
Rectangle {
    id: inspector

    property bool expanded: false

    function formatTokens(n) {
        if (n >= 1000000)
            return (n / 1000000).toFixed(1) + "M"
        if (n >= 1000)
            return (n / 1000).toFixed(1) + "k"
        return String(n)
    }

    color: theme.card
    radius: theme.radiusM
    border.color: theme.cardBorder

    Theme {
        id: theme
        dark: settings.dark
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                text: qsTr("上下文检查器")
                font.pixelSize: Math.round(13 * settings.fontScale)
                font.bold: true
                color: theme.accent
            }
            ToolButton {
                text: qsTr("刷新")
                flat: true
                font.pixelSize: Math.round(11 * settings.fontScale)
                onClicked: chat.refreshContext()
            }
        }

        // 用量统计：数据来自服务端 usage 上报（各协议归一到 TokenUsage）
        Label {
            Layout.fillWidth: true
            visible: chat.usageSummary.hasUsage
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
            font.pixelSize: Math.round(11 * settings.fontScale)
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
        }

        ListView {
            id: sectionList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 6
            model: chat.contextSections
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: SlimScrollBar {}

            delegate: Rectangle {
                width: sectionList.width
                height: sectionCard.implicitHeight
                color: theme.surface
                radius: theme.radiusS
                border.color: theme.cardBorder

                ColumnLayout {
                    id: sectionCard
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.margins: 8
                    spacing: 3

                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: modelData.name
                            color: theme.accent
                            font.bold: true
                            font.pixelSize: Math.round(12 * settings.fontScale)
                        }
                        Item { Layout.fillWidth: true }
                        Label {
                            text: qsTr("来源：%1").arg(modelData.source)
                            color: theme.textDim
                            font.pixelSize: Math.round(10 * settings.fontScale)
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        text: modelData.content.length > 0
                              ? modelData.content : qsTr("（空，未注入）")
                        visible: sectionExpanded
                        color: modelData.content.length > 0 ? theme.textSoft : theme.textFaint
                        wrapMode: Text.Wrap
                        font.pixelSize: Math.round(11 * settings.fontScale)
                        textFormat: Text.PlainText
                    }
                }

                property bool sectionExpanded: false
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
                font.pixelSize: Math.round(11 * settings.fontScale)
                wrapMode: Text.Wrap
                width: parent.width - 20
            }
        }

        Label {
            property int enabledCount: chat.contextTools.filter(t => t.enabled).length
            text: qsTr("启用工具（%1/%2）").arg(enabledCount).arg(chat.contextTools.length)
            color: theme.text
            font.bold: true
            font.pixelSize: Math.round(11 * settings.fontScale)
        }
        ListView {
            id: toolList
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(64, 18 * Math.max(1, chat.contextTools.length))
            clip: true
            spacing: 2
            model: chat.contextTools
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: SlimScrollBar {}

            delegate: Label {
                width: toolList.width
                text: "· " + modelData.name
                      + qsTr("　[%1]").arg(modelData.origin)
                      + (modelData.enabled ? "" : qsTr("　（已禁用）"))
                color: modelData.enabled ? theme.textDim : theme.textFaint
                font.pixelSize: Math.round(10 * settings.fontScale)
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
            Layout.preferredHeight: 18 * Math.min(2, Math.max(1, mcpStatusList.count))
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
                font.pixelSize: Math.round(10 * settings.fontScale)
                elide: Text.ElideRight
            }
        }
    }
}
