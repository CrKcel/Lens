import QtQuick
import QtQuick.Controls

// 窗口骨架：视图切换（聊天 / 设置）与跨视图动作（发送、设置打开）。
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

    Theme {
        id: theme
        dark: settings.dark
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

    Sidebar {
        id: sidebar
        anchors.top: parent.top
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
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: sidebar.right
        anchors.right: parent.right
        sidebarCollapsed: sidebar.collapsed
        onSendRequested: root.sendAction()
        onStopRequested: chat.stop()
    }

    SettingsView {
        id: settingsView
        visible: root.settingsMode
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: sidebar.right
        anchors.right: parent.right
        settingsCategory: root.settingsCategory
    }
}
