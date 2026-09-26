import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 侧栏：应用标识、工作文件夹、新建会话、设置分类导航、会话列表、设置入口。
// settingsMode / settingsCategory 由 Main 持有；导航点击经 categorySelected
// 回传，设置按钮经 settingsToggleRequested 请求切换，侧栏不做业务决策。
Rectangle {
    id: sidebarRoot

    property bool settingsMode: false
    property string settingsCategory: "general"
    readonly property alias workdirText: workdirField.text

    signal settingsToggleRequested()
    signal categorySelected(string category)

    property bool collapsed: false
    width: collapsed ? 52 : 264
    Behavior on width {
        NumberAnimation { duration: 140; easing.type: Easing.OutCubic }
    }
    color: theme.surface
    anchors.top: parent.top
    anchors.bottom: parent.bottom
    anchors.left: parent.left

    Theme {
        id: theme
        dark: settings.dark
    }

    // 右侧分隔线
    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: theme.divider
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            // 应用标识：accent 圆点
            Rectangle {
                width: 12; height: 12; radius: 6
                color: theme.accent
                Layout.alignment: Qt.AlignVCenter
            }
            Label {
                visible: !sidebarRoot.collapsed
                Layout.fillWidth: true
                text: sidebarRoot.settingsMode ? qsTr("设置") : qsTr("Lens")
                font.pixelSize: 17
                font.bold: true
                color: theme.text
            }
            ToolButton {
                text: sidebarRoot.collapsed ? "»" : "«"
                flat: true
                display: AbstractButton.TextOnly
                font.pixelSize: 13
                ToolTip.visible: hovered
                ToolTip.text: sidebarRoot.collapsed ? qsTr("展开会话列表") : qsTr("折叠会话列表")
                onClicked: sidebarRoot.collapsed = !sidebarRoot.collapsed
            }
        }

        TextField {
            id: workdirField
            visible: !sidebarRoot.collapsed && !sidebarRoot.settingsMode
            Layout.fillWidth: true
            placeholderText: qsTr("工作文件夹（默认主目录）")
            color: theme.textSoft
            selectByMouse: true
            font.pixelSize: 12
            leftPadding: 10
            rightPadding: 10
            background: Rectangle {
                color: theme.field
                border.color: workdirField.activeFocus ? theme.accent : theme.fieldBorder
                radius: theme.radiusS
            }
        }

        AccentButton {
            visible: !sidebarRoot.collapsed && !sidebarRoot.settingsMode
            Layout.fillWidth: true
            text: qsTr("＋ 新建会话")
            onClicked: chat.newConversation(sidebarRoot.workdirText)
        }

        // 设置模式：分类导航
        ColumnLayout {
            visible: !sidebarRoot.collapsed && sidebarRoot.settingsMode
            Layout.fillWidth: true
            spacing: 4
            NavButton {
                Layout.fillWidth: true
                highlighted: sidebarRoot.settingsCategory === "general"
                text: qsTr("常规")
                onClicked: sidebarRoot.categorySelected("general")
            }
            NavButton {
                Layout.fillWidth: true
                highlighted: sidebarRoot.settingsCategory === "providers"
                text: qsTr("模型提供商")
                onClicked: sidebarRoot.categorySelected("providers")
            }
            NavButton {
                Layout.fillWidth: true
                highlighted: sidebarRoot.settingsCategory === "mcp"
                text: qsTr("MCP")
                onClicked: sidebarRoot.categorySelected("mcp")
            }
            NavButton {
                Layout.fillWidth: true
                highlighted: sidebarRoot.settingsCategory === "skills"
                text: qsTr("Skills")
                onClicked: sidebarRoot.categorySelected("skills")
            }
            NavButton {
                Layout.fillWidth: true
                highlighted: sidebarRoot.settingsCategory === "shortcuts"
                text: qsTr("快捷键")
                onClicked: sidebarRoot.categorySelected("shortcuts")
            }
        }

        ListView {
            id: conversationList
            visible: !sidebarRoot.collapsed && !sidebarRoot.settingsMode
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 4
            model: chat.conversations
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: SlimScrollBar {}

            delegate: ItemDelegate {
                width: conversationList.width
                highlighted: chat.currentConversationId === conversationId
                onClicked: chat.openConversation(conversationId)

                background: Rectangle {
                    radius: theme.radiusS
                    color: parent.highlighted ? theme.accentSoft
                         : parent.hovered ? theme.highlight
                         : "transparent"

                    Rectangle {
                        visible: parent.parent.highlighted
                        width: 3
                        radius: 1.5
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        anchors.margins: 6
                        color: theme.accent
                    }
                }

                contentItem: RowLayout {
                    spacing: 4
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        Label {
                            Layout.fillWidth: true
                            text: title
                            color: highlighted ? theme.accent : theme.text
                            elide: Text.ElideRight
                            font.pixelSize: 13
                        }
                        Label {
                            Layout.fillWidth: true
                            text: workdir
                            color: theme.textFaint
                            elide: Text.ElideMiddle
                            font.pixelSize: 10
                        }
                    }
                    ToolButton {
                        text: qsTr("×")
                        flat: true
                        display: AbstractButton.TextOnly
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("删除会话")
                        onClicked: chat.deleteConversation(conversationId)
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                visible: conversationList.count === 0
                text: qsTr("暂无会话")
                color: theme.textFaint
            }
        }

        // 占位：列表隐藏（折叠态或设置模式）时把按钮压到底部
        Item { Layout.fillHeight: true; visible: sidebarRoot.collapsed || sidebarRoot.settingsMode }

        Button {
            id: settingsButton
            // 折叠态侧栏内宽仅 ~32px：必须始终填宽并去掉水平内边距，
            // 否则按钮按隐式宽度渲染会伸出侧栏边界
            Layout.fillWidth: true
            leftPadding: sidebarRoot.collapsed ? 0 : 12
            rightPadding: sidebarRoot.collapsed ? 0 : 12
            implicitHeight: 34
            font.pixelSize: 13
            text: sidebarRoot.collapsed ? (sidebarRoot.settingsMode ? "«" : "⚙")
                  : sidebarRoot.settingsMode ? qsTr("« 返回聊天") : qsTr("⚙ 设置")
            background: Rectangle {
                radius: theme.radiusS
                color: settingsButton.down ? theme.accentSoft
                     : settingsButton.hovered ? theme.highlight
                     : "transparent"
            }
            contentItem: Label {
                text: settingsButton.text
                font: settingsButton.font
                color: theme.textSoft
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            onClicked: sidebarRoot.settingsToggleRequested()
        }
    }
}
