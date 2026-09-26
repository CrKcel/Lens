import QtQuick
import QtQuick.Controls

// 窗口骨架：自绘标题栏 + 视图切换（聊天 / 设置）与跨视图动作（发送、设置打开）。
// 契约：sendAction 必须保留在根对象（E2eDriver 经 invokeMethod 调用），
// --qml-check 走 settingsView.runQmlCheck()（设置保存链路冒烟）。
ApplicationWindow {
    id: root
    width: 1120
    height: 740
    minimumWidth: 760
    minimumHeight: 520
    visible: true
    title: qsTr("Lens")
    color: theme.background
    // 自绘标题栏：去掉系统边框，拖拽/缩放/贴靠经 startSystemMove/startSystemResize
    // 走窗口系统的原生实现，保留系统级贴靠分屏
    flags: Qt.Window | Qt.FramelessWindowHint

    readonly property bool maximized: visibility === Window.Maximized
    // 窗口按钮在左上（macOS / Linux 桌面环境设定）时顺序为 关闭、最小化、最大化
    readonly property bool buttonsLeft: titleBarButtonsLeft
    readonly property int buttonW: 44

    function toggleMaximize() {
        if (maximized)
            root.showNormal()
        else
            root.showMaximized()
    }

    Theme {
        id: theme
        dark: settings.dark
    }

    component ResizeEdge: MouseArea {
        id: resizeEdge
        property int edges: 0
        z: 99
        enabled: !root.maximized
        cursorShape: {
            const horizontal = edges & (Qt.LeftEdge | Qt.RightEdge)
            const vertical = edges & (Qt.TopEdge | Qt.BottomEdge)
            if (horizontal && vertical)
                return (edges & Qt.TopEdge) === (edges & Qt.LeftEdge)
                        ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor
            return horizontal ? Qt.SizeHorCursor : Qt.SizeVerCursor
        }
        onPressed: (mouse) => root.startSystemResize(edges)
    }

    Shortcut {
        // StandardKey.New 在部分平台映射多个键序，sequence 只绑第一个会告警
        sequences: [StandardKey.New]
        context: Qt.ApplicationShortcut
        onActivated: chat.newConversation(sidebar.workdirText)
    }

    property bool settingsMode: false
    property string settingsCategory: "general"

    function sendAction() {
        const text = chatView.inputText.trim()
        if (text.length === 0)
            return
        // 无会话时由控制器自动创建（工作文件夹取侧栏输入）；图片附件与思考强度
        // （会话内临时状态）一并传给控制器
        chat.send(chatView.inputText, sidebar.workdirText, chatView.attachments,
                  chatView.thinkingLevel)
        chatView.clearInput()
    }

    Component.onCompleted: {
        if (Qt.application.arguments.indexOf("--qml-check") >= 0) {
            root.settingsMode = true
            root.settingsCategory = "providers"
            settingsView.runQmlCheck()
        }
    }

    // 自绘标题栏：拖拽移动、双击最大化/还原、窗口三键（方位见 buttonsLeft）。
    // 背景透明、z 高于侧栏：侧栏背景直接顶到窗口上缘实现无缝衔接，
    // 侧栏内容经 topInset 让出这条拖拽带
    Rectangle {
        id: titleBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 40
        color: "transparent"
        z: 2

        DragHandler {
            target: null
            acceptedButtons: Qt.LeftButton
            onActiveChanged: if (active) {
                // 最大化状态下拖拽先还原，再交给系统移动
                if (root.maximized)
                    root.showNormal()
                root.startSystemMove()
            }
        }
        TapHandler {
            acceptedButtons: Qt.LeftButton
            onDoubleTapped: root.toggleMaximize()
        }

        // 标题栏按钮：图形由实例内子项绘制（最小化横线 / 最大化方框 / 关闭斜线），
        // 关闭键悬停红色，其余用主题高亮；按钮居左时排列顺序镜像为
        // 关闭、最小化、最大化（macOS 交通灯次序）
        component TitleButton: Rectangle {
            id: button
            property bool danger: false
            property alias hovered: hover.hovered
            signal activated()
            width: root.buttonW
            color: {
                if (!hover.hovered)
                    return "transparent"
                if (hover.pressed)
                    return danger ? Qt.darker("#e81123", 1.3) : theme.accentPressed
                return danger ? "#e81123" : theme.highlight
            }
            HoverHandler { id: hover }
            TapHandler { onTapped: button.activated() }
        }

        // 聊天标题：内容区左上（侧栏右侧）；限宽 elide，给右侧窗口按钮留位。
        // sidebar 是 titleBar 的兄弟而非父子，不能锚定，用 x 绑定跟随其宽度；
        // 折叠后侧栏宽度归零，取与折叠按钮右缘的较大者避免重叠
        Label {
            id: chatTitle
            x: Math.max(sidebar.width + 14,
                        sidebarToggle.x + sidebarToggle.width + 8)
            anchors.verticalCenter: parent.verticalCenter
            visible: !root.settingsMode
            width: Math.min(implicitWidth, chatTitleMax)
            readonly property real chatTitleMax:
                titleBar.width - chatTitle.x - 16
                - (streamingPill.visible ? streamingPill.width + 10 : 0)
                - (root.buttonsLeft ? 0 : 3 * root.buttonW + 10)
            text: chat.currentConversationId === 0 ? qsTr("新会话") : chat.currentTitle
            color: theme.text
            font.pixelSize: Math.round(14 * settings.fontScale)
            font.bold: true
            elide: Text.ElideRight
        }

        // 生成中状态：accent 药丸 + 呼吸动画，紧随聊天标题
        Rectangle {
            id: streamingPill
            visible: chat.streaming && !root.settingsMode
            implicitWidth: streamingLabel.implicitWidth + 18
            implicitHeight: 24
            radius: 12
            color: theme.accentSoft
            anchors.left: chatTitle.right
            anchors.leftMargin: 10
            anchors.verticalCenter: parent.verticalCenter

            Label {
                id: streamingLabel
                anchors.centerIn: parent
                text: qsTr("生成中…")
                color: theme.accent
                font.pixelSize: Math.round(11 * settings.fontScale)

                SequentialAnimation on opacity {
                    running: chat.streaming
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.4; duration: 700; easing.type: Easing.InOutQuad }
                    NumberAnimation { to: 1.0; duration: 700; easing.type: Easing.InOutQuad }
                }
            }
        }

        // 侧栏折叠/展开：右对齐于标题栏的侧栏段，随侧栏宽度滑动；
        // 折叠后停在窗口按钮（居左时）右侧
        AbstractButton {
            id: sidebarToggle
            anchors.verticalCenter: parent.verticalCenter
            x: Math.max(root.buttonsLeft ? 3 * root.buttonW + 6 : 16,
                        sidebar.width - width - 6)
            visible: !root.settingsMode
            implicitWidth: 28
            implicitHeight: 28
            ToolTip.visible: hovered
            ToolTip.text: sidebar.collapsed ? qsTr("展开会话列表") : qsTr("折叠会话列表")

            background: Rectangle {
                radius: 14
                color: sidebarToggle.down ? theme.accentSoft
                     : sidebarToggle.hovered ? theme.highlight
                     : "transparent"
            }
            contentItem: Label {
                text: sidebar.collapsed ? "»" : "«"
                color: theme.textDim
                font.pixelSize: Math.round(13 * settings.fontScale)
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            onClicked: sidebar.collapsed = !sidebar.collapsed
        }

        TitleButton {
            id: closeButton
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            x: root.buttonsLeft ? 0 : parent.width - width
            onActivated: root.close()
            ToolTip.visible: closeButton.hovered
            ToolTip.delay: 600
            ToolTip.text: qsTr("关闭")
            Rectangle {
                anchors.centerIn: parent
                width: 11
                height: 1
                rotation: 45
                color: parent.hovered ? "#ffffff" : theme.text
            }
            Rectangle {
                anchors.centerIn: parent
                width: 11
                height: 1
                rotation: -45
                color: parent.hovered ? "#ffffff" : theme.text
            }
        }

        TitleButton {
            id: minimizeButton
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            x: root.buttonsLeft ? closeButton.width
                                : parent.width - 2 * closeButton.width
            onActivated: root.showMinimized()
            ToolTip.visible: minimizeButton.hovered
            ToolTip.delay: 600
            ToolTip.text: qsTr("最小化")
            Rectangle {
                anchors.centerIn: parent
                width: 10
                height: 1
                color: parent.hovered ? "#ffffff" : theme.text
            }
        }

        TitleButton {
            id: maximizeButton
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            x: root.buttonsLeft ? 2 * closeButton.width
                                : parent.width - 3 * closeButton.width
            onActivated: root.toggleMaximize()
            ToolTip.visible: maximizeButton.hovered
            ToolTip.delay: 600
            ToolTip.text: root.maximized ? qsTr("还原") : qsTr("最大化")
            // 最大化：单层方框；还原：前后两层叠框
            Rectangle {
                visible: !root.maximized
                anchors.centerIn: parent
                width: 9
                height: 9
                color: "transparent"
                border.width: 1
                border.color: parent.hovered ? "#ffffff" : theme.text
            }
            Rectangle {
                visible: root.maximized
                anchors.centerIn: parent
                anchors.horizontalCenterOffset: -2
                anchors.verticalCenterOffset: 2
                width: 8
                height: 8
                color: titleBar.color
                border.width: 1
                border.color: parent.hovered ? "#ffffff" : theme.text
            }
            Rectangle {
                visible: root.maximized
                anchors.centerIn: parent
                anchors.horizontalCenterOffset: 2
                anchors.verticalCenterOffset: -2
                width: 8
                height: 8
                color: "transparent"
                border.width: 1
                border.color: parent.hovered ? "#ffffff" : theme.text
            }
        }
    }

    // 边缘与四角缩放热区
    ResizeEdge {
        edges: Qt.TopEdge
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 5
    }
    ResizeEdge {
        edges: Qt.BottomEdge
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 5
    }
    ResizeEdge {
        edges: Qt.LeftEdge
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 5
    }
    ResizeEdge {
        edges: Qt.RightEdge
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 5
    }
    ResizeEdge {
        edges: Qt.TopEdge | Qt.LeftEdge
        anchors.top: parent.top
        anchors.left: parent.left
        width: 14
        height: 14
    }
    ResizeEdge {
        edges: Qt.TopEdge | Qt.RightEdge
        anchors.top: parent.top
        anchors.right: parent.right
        width: 14
        height: 14
    }
    ResizeEdge {
        edges: Qt.BottomEdge | Qt.LeftEdge
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        width: 14
        height: 14
    }
    ResizeEdge {
        edges: Qt.BottomEdge | Qt.RightEdge
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        width: 14
        height: 14
    }

    // 标题栏与内容区的分隔线（只覆盖内容区，侧栏以自身右缘分隔线收边）
    Rectangle {
        anchors.top: titleBar.bottom
        anchors.left: sidebar.right
        anchors.right: parent.right
        height: 1
        color: theme.divider
        z: 98
    }

    Sidebar {
        id: sidebar
        anchors.top: parent.top
        topInset: titleBar.height
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        settingsMode: root.settingsMode
        settingsCategory: root.settingsCategory
        onSettingsToggleRequested: {
            if (root.settingsMode) {
                root.settingsMode = false
            } else {
                settingsView.loadSettingsIntoFields()
                root.settingsMode = true
            }
        }
        onCategorySelected: (category) => root.settingsCategory = category
    }

    ChatView {
        id: chatView
        visible: !root.settingsMode
        anchors.top: titleBar.bottom
        anchors.bottom: parent.bottom
        anchors.left: sidebar.right
        anchors.right: parent.right
        onSendRequested: root.sendAction()
        onStopRequested: chat.stop()
    }

    SettingsView {
        id: settingsView
        visible: root.settingsMode
        anchors.top: titleBar.bottom
        anchors.bottom: parent.bottom
        anchors.left: sidebar.right
        anchors.right: parent.right
        settingsCategory: root.settingsCategory
    }
}
