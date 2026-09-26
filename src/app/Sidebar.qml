import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 侧栏：应用标识、工作文件夹、新建会话、设置分类导航、会话列表、
// 上下文检查器（设置按钮上方）、设置入口。折叠/展开按钮在 Main 的标题栏。
// settingsMode / settingsCategory 由 Main 持有；导航点击经 categorySelected
// 回传，设置按钮经 settingsToggleRequested 请求切换，侧栏不做业务决策。
// 展开宽度可经右缘拖拽调节（窗口宽度的 1/5 ~ 1/3）；折叠态完全收起
// （宽度归零）；设置模式侧栏始终展开、不可折叠。
Rectangle {
    id: sidebarRoot

    property bool settingsMode: false
    property string settingsCategory: "general"
    readonly property alias workdirText: workdirField.text

    signal settingsToggleRequested()
    signal categorySelected(string category)

    property bool collapsed: false
    property real expandedWidth: 264
    // 顶部让出自绘标题栏的拖拽带（Main 里绑定 titleBar.height），
    // 侧栏背景直接顶到窗口上缘
    property real topInset: 0
    readonly property real minExpandedWidth: parent.width / 5
    readonly property real maxExpandedWidth: parent.width / 3
    function clampWidth(w) {
        return Math.max(minExpandedWidth, Math.min(maxExpandedWidth, w))
    }
    width: collapsed ? 0 : clampWidth(expandedWidth)
    onSettingsModeChanged: if (settingsMode) collapsed = false
    // z 高于 ChatView：折叠态的悬浮展开按钮才能盖在聊天区上
    z: 1
    Behavior on width {
        enabled: !resizeHandle.pressed
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

    // 右缘拖拽手柄：调节展开宽度，范围 [窗口宽度/5, 窗口宽度/3]
    MouseArea {
        id: resizeHandle
        visible: !sidebarRoot.collapsed
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: sidebarRoot.topInset
        anchors.bottom: parent.bottom
        width: 6
        cursorShape: Qt.SizeHorCursor
        property real pressWidth
        property real pressX
        onPressed: (mouse) => {
            pressWidth = sidebarRoot.expandedWidth
            pressX = mouse.x
        }
        onPositionChanged: (mouse) =>
            sidebarRoot.expandedWidth = sidebarRoot.clampWidth(pressWidth + mouse.x - pressX)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        anchors.topMargin: 10 + sidebarRoot.topInset
        spacing: 8
        visible: !sidebarRoot.collapsed

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

        // 占位：列表隐藏（设置模式）时把按钮压到底部
        Item { Layout.fillHeight: true; visible: sidebarRoot.settingsMode }

        // 上下文检查器（内嵌面板）与开关：位于设置按钮之上，仅聊天模式显示。
        // 展开时先刷新上下文快照
        ContextInspector {
            id: contextPanel
            visible: !sidebarRoot.settingsMode && contextPanel.expanded
            Layout.fillWidth: true
            Layout.preferredHeight: contextPanel.expanded ? 340 : 0
        }
        NavButton {
            visible: !sidebarRoot.settingsMode
            Layout.fillWidth: true
            highlighted: contextPanel.expanded
            text: contextPanel.expanded ? qsTr("收起上下文") : qsTr("上下文")
            onClicked: {
                if (!contextPanel.expanded)
                    chat.refreshContext()
                contextPanel.expanded = !contextPanel.expanded
            }
        }

        Button {
            id: settingsButton
            Layout.fillWidth: true
            implicitHeight: 34
            font.pixelSize: 13
            text: sidebarRoot.settingsMode ? qsTr("« 返回聊天") : qsTr("⚙ 设置")
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
