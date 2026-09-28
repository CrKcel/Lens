import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 侧栏：应用标识、工作文件夹、新建会话、设置分类导航、会话列表、
// 上下文检查器（设置按钮上方）、设置入口。折叠/展开按钮在 Main 的标题栏。
// settingsMode / settingsCategory 由 Main 持有；导航点击经 categorySelected
// 回传，设置按钮经 settingsToggleRequested 请求切换，侧栏不做业务决策。
// 展开宽度可经右缘拖拽调节（固定下限 ~ 窗口宽度的 1/3，拖到下限以下
// 自动折叠）；折叠态完全收起（宽度归零）；设置模式侧栏始终展开、不可折叠。
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
    readonly property real minExpandedWidth: 200
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
    color: theme.sidebar
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

    // 右缘拖拽手柄：调节展开宽度，范围 [固定下限, 窗口宽度/3]，
    // 拖到下限以下直接折叠。位移必须取窗口场景坐标（mapToItem(null)）：
    // 手柄自身随侧栏移动，用其局部坐标会形成反馈回路（侧栏一动 mouse.x
    // 就回缩）导致宽度振荡、页面抖动
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
        property real pressSceneX
        onPressed: (mouse) => {
            pressWidth = clampWidth(sidebarRoot.expandedWidth)
            pressSceneX = mapToItem(null, mouse.x, mouse.y).x
        }
        onPositionChanged: (mouse) => {
            const w = pressWidth + mapToItem(null, mouse.x, mouse.y).x - pressSceneX
            if (w < sidebarRoot.minExpandedWidth && !sidebarRoot.settingsMode)
                sidebarRoot.collapsed = true
            else
                sidebarRoot.expandedWidth = clampWidth(w)
        }
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
                font.pixelSize: Math.round(17 * settings.fontScale)
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
            font.pixelSize: Math.round(12 * settings.fontScale)
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

        // 新建窗口：多窗口并行——每个窗口独立绑定会话，生成互不阻塞
        Button {
            id: newWindowButton
            visible: !sidebarRoot.collapsed && !sidebarRoot.settingsMode
            Layout.fillWidth: true
            implicitHeight: 32
            font.pixelSize: Math.round(12 * settings.fontScale)
            text: qsTr("❐ 新窗口（Ctrl+Shift+N）")
            background: Rectangle {
                radius: theme.radiusS
                color: newWindowButton.down ? theme.accentSoft
                     : newWindowButton.hovered ? theme.highlight
                     : "transparent"
                border.color: theme.fieldBorder
            }
            contentItem: Label {
                text: newWindowButton.text
                font: newWindowButton.font
                color: theme.textSoft
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            onClicked: chat.newWindow()
        }

        // 会话搜索：按标题或消息内容过滤会话列表（即时生效）
        TextField {
            id: searchField
            visible: !sidebarRoot.collapsed && !sidebarRoot.settingsMode
            Layout.fillWidth: true
            placeholderText: qsTr("搜索会话")
            color: theme.textSoft
            selectByMouse: true
            font.pixelSize: Math.round(12 * settings.fontScale)
            leftPadding: 10
            rightPadding: clearButton.visible ? clearButton.width : 10
            topPadding: 6
            bottomPadding: 6
            background: Rectangle {
                color: theme.field
                border.color: searchField.activeFocus ? theme.accent : theme.fieldBorder
                radius: theme.radiusS
            }
            onTextEdited: chat.searchConversations(text)
            onActiveFocusChanged: if (activeFocus) selectAll()
            ToolButton {
                id: clearButton
                visible: searchField.text.length > 0
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: height
                height: searchField.height - 8
                flat: true
                text: qsTr("×")
                font.pixelSize: Math.round(13 * settings.fontScale)
                ToolTip.visible: hovered
                ToolTip.text: qsTr("清除搜索")
                onClicked: {
                    searchField.text = ""
                    chat.searchConversations("")
                }
            }
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
                highlighted: sidebarRoot.settingsCategory === "appearance"
                text: qsTr("外观")
                onClicked: sidebarRoot.categorySelected("appearance")
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
            // 会话行相对组头卡片的左缩进
            readonly property int groupIndent: 14

            // 按工作文件夹分组：模型输出 组头行 + 组内会话行 的扁平结构，
            // 组头是圆角卡片、可折叠，会话行相对组头缩进。
            // 模型角色经 required property 注入 delegate 根对象，两种条目
            // 放在同一个 delegate 里按 isHeader 互斥显示
            delegate: Item {
                id: delegateRoot
                width: conversationList.width
                height: isHeader ? groupHeader.height : conversationRow.height
                required property bool isHeader
                required property string workdir
                required property int groupCount
                required property string title
                required property int conversationId
                required property bool streaming

                ItemDelegate {
                    id: groupHeader
                    visible: delegateRoot.isHeader
                    width: parent.width
                    implicitHeight: 30
                    hoverEnabled: true
                    onClicked: chat.conversations.toggleGroup(delegateRoot.workdir)

                    background: Rectangle {
                        radius: theme.radiusM
                        color: groupHeader.hovered ? theme.highlight : theme.card
                        border.color: theme.cardBorder
                    }

                    contentItem: RowLayout {
                        spacing: 6
                        Label {
                            // 搜索期间条目强制展开（模型保证），箭头保持展开态
                            text: searchField.text.length === 0
                                  && chat.conversations.isGroupCollapsed(delegateRoot.workdir)
                                  ? "▸" : "▾"
                            color: theme.textDim
                            font.pixelSize: Math.round(10 * settings.fontScale)
                        }
                        Label {
                            Layout.fillWidth: true
                            text: delegateRoot.workdir
                            color: theme.textSoft
                            elide: Text.ElideMiddle
                            font.pixelSize: Math.round(11 * settings.fontScale)
                            font.bold: true
                        }
                        Label {
                            text: delegateRoot.groupCount
                            color: theme.textFaint
                            font.pixelSize: Math.round(10 * settings.fontScale)
                        }
                    }
                }

                ItemDelegate {
                    id: conversationRow
                    visible: !delegateRoot.isHeader
                    x: conversationList.groupIndent
                    width: parent.width - conversationList.groupIndent
                    hoverEnabled: true
                    highlighted: chat.currentConversationId === delegateRoot.conversationId
                    onClicked: chat.openConversation(delegateRoot.conversationId)

                    background: Rectangle {
                        radius: theme.radiusS
                        color: conversationRow.highlighted ? theme.accentSoft
                             : conversationRow.hovered ? theme.highlight
                             : "transparent"
                    }

                    contentItem: RowLayout {
                        spacing: 4
                        // 后台生成中角标：该会话正在生成（哪怕窗口不在前台看它）
                        Rectangle {
                            visible: delegateRoot.streaming
                            Layout.preferredWidth: 8
                            Layout.preferredHeight: 8
                            radius: 4
                            color: theme.accent
                            SequentialAnimation on opacity {
                                running: delegateRoot.streaming
                                loops: Animation.Infinite
                                NumberAnimation { to: 0.3; duration: 700; easing.type: Easing.InOutQuad }
                                NumberAnimation { to: 1.0; duration: 700; easing.type: Easing.InOutQuad }
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            text: delegateRoot.title
                            color: conversationRow.highlighted ? theme.accent : theme.text
                            elide: Text.ElideRight
                            font.pixelSize: Math.round(13 * settings.fontScale)
                        }
                        ToolButton {
                            text: qsTr("❐")
                            visible: conversationRow.hovered
                            flat: true
                            display: AbstractButton.TextOnly
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("在新窗口打开")
                            onClicked: chat.openConversationInNewWindow(delegateRoot.conversationId)
                        }
                        ToolButton {
                            text: qsTr("×")
                            flat: true
                            display: AbstractButton.TextOnly
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("删除会话")
                            onClicked: chat.deleteConversation(delegateRoot.conversationId)
                        }
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                visible: conversationList.count === 0
                text: searchField.text.length > 0 ? qsTr("无匹配会话") : qsTr("暂无会话")
                color: theme.textFaint
                font.pixelSize: Math.round(12 * settings.fontScale)
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
            font.pixelSize: Math.round(13 * settings.fontScale)
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
