import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    width: 1100
    height: 720
    visible: true
    title: qsTr("Lens")
    color: "#16171b"

    function sendAction() {
        if (input.text.trim().length === 0)
            return
        // 无会话时由控制器自动创建（工作文件夹取侧栏输入）
        chat.send(input.text, workdirField.text)
        input.clear()
    }

    function applySettings() {
        settings.endpoint = endpointField.text
        settings.apiKey = apiKeyField.text
        settings.model = modelField.text
        settings.systemPrompt = systemPromptField.text
        settings.save()
    }

    // 打开时一次性填充。字段上不能挂 text: settings.xxx 之类的活绑定：
    // AppSettings 的 setter 每次都会发 settingsChanged，保存时先写的属性
    // 会把还没读到的字段绑定刷回旧值，导致只有第一个字段能保存。
    function loadSettingsIntoFields() {
        endpointField.text = settings.endpoint
        apiKeyField.text = settings.apiKey
        modelField.text = settings.model
        systemPromptField.text = settings.systemPrompt
    }

    // CI 冒烟钩子：--qml-check 模拟真实设置流程并验证两轮保存回读，
    // 由 C++ 侧定时退出；配合检查 settings.json 可验证完整保存链路。
    // 若任何字段创建失败，此处会抛出 ReferenceError 并打印到 stderr。
    property int qmlCheckStage: 0

    Component.onCompleted: {
        if (Qt.application.arguments.indexOf("--qml-check") >= 0) {
            qmlCheckStage = 1
            settingsPopup.open()
        }
    }

    // ── 侧栏：可折叠，新建置顶，设置在左下角 ───────────────────
    Rectangle {
        id: sidebar
        property bool collapsed: false
        width: collapsed ? 46 : 260
        Behavior on width {
            NumberAnimation { duration: 120; easing.type: Easing.OutQuad }
        }
        color: "#1d1e24"
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: parent.left

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                Label {
                    visible: !sidebar.collapsed
                    Layout.fillWidth: true
                    text: qsTr("Lens")
                    font.pixelSize: 18
                    font.bold: true
                    color: "#e8e8e8"
                }
                ToolButton {
                    id: sidebarToggle
                    text: sidebar.collapsed ? "»" : "«"
                    flat: true
                    ToolTip.visible: hovered
                    ToolTip.text: sidebar.collapsed ? qsTr("展开会话列表") : qsTr("折叠会话列表")
                    onClicked: sidebar.collapsed = !sidebar.collapsed
                }
            }

            TextField {
                id: workdirField
                visible: !sidebar.collapsed
                Layout.fillWidth: true
                placeholderText: qsTr("工作文件夹（默认主目录）")
                color: "#d8d8dc"
                selectByMouse: true
                font.pixelSize: 12
                background: Rectangle {
                    color: "#24252c"
                    border.color: "#33343e"
                    radius: 6
                }
            }

            Button {
                visible: !sidebar.collapsed
                Layout.fillWidth: true
                text: qsTr("＋ 新建会话")
                onClicked: chat.newConversation(workdirField.text)
            }

            ListView {
                id: conversationList
                visible: !sidebar.collapsed
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 4
                model: chat.conversations

                delegate: ItemDelegate {
                    width: conversationList.width
                    highlighted: chat.currentConversationId === conversationId
                    onClicked: chat.openConversation(conversationId)

                    background: Rectangle {
                        color: parent.highlighted ? "#2c2d36"
                             : parent.hovered ? "#24252c" : "transparent"
                        radius: 6
                    }

                    contentItem: RowLayout {
                        spacing: 4
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Label {
                                Layout.fillWidth: true
                                text: title
                                color: "#e8e8e8"
                                elide: Text.ElideRight
                                font.pixelSize: 13
                            }
                            Label {
                                Layout.fillWidth: true
                                text: workdir
                                color: "#7c7d86"
                                elide: Text.ElideMiddle
                                font.pixelSize: 10
                            }
                        }
                        ToolButton {
                            text: qsTr("×")
                            flat: true
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
                    color: "#5a5b64"
                }
            }

            // 占位：列表隐藏（折叠态）时把设置按钮压到底部
            Item { Layout.fillHeight: true; visible: sidebar.collapsed }

            Button {
                id: settingsButton
                Layout.fillWidth: !sidebar.collapsed
                text: sidebar.collapsed ? "⚙" : qsTr("⚙ 设置")
                onClicked: settingsPopup.open()
            }
        }
    }

    // ── 主区：消息流 ───────────────────────────────────────────
    ColumnLayout {
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: sidebar.right
        anchors.right: parent.right
        anchors.margins: 12
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                text: chat.currentConversationId === 0
                      ? qsTr("新会话")
                      : chat.currentTitle
                color: "#e8e8e8"
                font.pixelSize: 15
                elide: Text.ElideRight
            }
            Label {
                text: chat.streaming ? qsTr("生成中…") : ""
                color: "#7fb4d8"
            }
        }

        ListView {
            id: messageList
            objectName: "messageListView"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 10
            model: chat.messages

            property bool nearBottom: true
            onContentYChanged:
                nearBottom = (contentY + height >= contentHeight - 60)
            onCountChanged: Qt.callLater(() => positionViewAtEnd())
            onContentHeightChanged: {
                // 流式增量不断增高内容：只有当用户本来就停留在底部时才跟随滚动
                if (nearBottom)
                    Qt.callLater(() => positionViewAtEnd())
            }

            delegate: DelegateChooser {
                role: "kind"
                DelegateChoice {
                    roleValue: 0 // 用户
                    Item {
                        width: messageList.width
                        height: bubble.implicitHeight + 18
                        Rectangle {
                            anchors.right: parent.right
                            width: bubble.width + 20
                            height: bubble.implicitHeight + 18
                            color: "#2d5d7c"
                            radius: 10
                            Label {
                                id: bubble
                                anchors.centerIn: parent
                                width: Math.min(messageList.width * 0.7, 560)
                                text: model.text
                                color: "#eaf2ea"
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }
                DelegateChoice {
                    roleValue: 1 // 助手
                    Item {
                        width: messageList.width
                        height: assistantColumn.implicitHeight

                        // 思考过程：reasoning_content 流式累积，默认折叠
                        ColumnLayout {
                            id: assistantColumn
                            x: 12
                            width: parent.width - 24
                            spacing: 4

                            RowLayout {
                                visible: model.reasoning.length > 0
                                spacing: 2
                                ToolButton {
                                    id: reasoningToggle
                                    text: reasoningExpanded ? "▾" : "▸"
                                    flat: true
                                    font.pixelSize: 10
                                    leftPadding: 0
                                    rightPadding: 0
                                }
                                Label {
                                    text: qsTr("思考过程")
                                    color: "#8a8b94"
                                    font.pixelSize: 11
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: reasoningExpanded = !reasoningExpanded
                                    }
                                }
                                Label {
                                    visible: model.streaming && model.text.length === 0
                                    text: qsTr("（生成中…）")
                                    color: "#5a5b64"
                                    font.pixelSize: 11
                                }
                            }
                            Label {
                                objectName: "assistantReasoning"
                                visible: model.reasoning.length > 0 && reasoningExpanded
                                Layout.fillWidth: true
                                text: model.reasoning + (model.streaming && model.text.length === 0 ? " ▌" : "")
                                color: "#7c7d86"
                                font.pixelSize: 12
                                wrapMode: Text.Wrap
                                textFormat: Text.PlainText
                            }

                            Label {
                                id: assistantText
                                objectName: "assistantContent"
                                visible: model.text.length > 0
                                Layout.fillWidth: true
                                text: model.text + (model.streaming && model.text.length > 0 ? " ▌" : "")
                                color: "#d8d8dc"
                                wrapMode: Text.Wrap
                                textFormat: Text.MarkdownText
                            }
                        }

                        property bool reasoningExpanded: false
                    }
                }
                DelegateChoice {
                    roleValue: 2 // 工具调用
                    Item {
                        width: messageList.width
                        height: toolCard.implicitHeight
                        Rectangle {
                            id: toolCard
                            width: messageList.width - 24
                            implicitHeight: toolColumn.implicitHeight + 16
                            x: 12
                            color: "#1f2027"
                            radius: 8
                            border.color: model.toolPending ? "#4a6d8a" : "#2c2d36"
                            ColumnLayout {
                                id: toolColumn
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 4
                                Label {
                                    text: (model.toolPending ? "⚙ " : "✔ ")
                                                          + model.toolName
                                                          + (model.toolPending ? qsTr("　运行中…") : "")
                                    color: "#7fb4d8"
                                    font.pixelSize: 12
                                    font.bold: true
                                }
                                Label {
                                    Layout.fillWidth: true
                                    visible: model.toolArgs.length > 0
                                    text: model.toolArgs
                                    color: "#8a8b94"
                                    font.family: "monospace"
                                    font.pixelSize: 11
                                    wrapMode: Text.Wrap
                                    maximumLineCount: 6
                                    elide: Text.ElideRight
                                }
                                Label {
                                    Layout.fillWidth: true
                                    visible: model.text.length > 0
                                    text: model.text
                                    color: "#9cdc9c"
                                    font.family: "monospace"
                                    font.pixelSize: 11
                                    wrapMode: Text.Wrap
                                    maximumLineCount: 12
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }
                }
                DelegateChoice {
                    roleValue: 3 // 错误
                    Item {
                        width: messageList.width
                        height: errorText.implicitHeight
                        Label {
                            id: errorText
                            width: messageList.width - 24
                            text: qsTr("⚠ %1").arg(model.text)
                            color: "#e07a7a"
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }

            ColumnLayout {
                anchors.centerIn: parent
                visible: messageList.count === 0
                spacing: 6
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("在「设置」中填入 API 地址与 Key\n发送第一条消息即自动创建会话")
                    horizontalAlignment: Text.AlignHCenter
                    color: "#5a5b64"
                }
            }
        }

        // ── 输入区 ────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            TextArea {
                id: input
                objectName: "chatInput"
                Layout.fillWidth: true
                placeholderText: chat.streaming ? qsTr("生成中…") : qsTr("输入消息，Enter 发送，Shift+Enter 换行")
                wrapMode: TextArea.Wrap
                color: "#e8e8e8"
                background: Rectangle {
                    color: "#24252c"
                    border.color: "#33343e"
                    radius: 8
                }
                Keys.onPressed: (event) => {
                    if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                        && (event.modifiers & Qt.ShiftModifier) === 0) {
                        root.sendAction()
                        event.accepted = true
                    }
                }
            }
            Button {
                text: chat.streaming ? qsTr("停止") : qsTr("发送")
                enabled: chat.streaming || input.text.trim().length > 0
                highlighted: !chat.streaming
                onClicked: chat.streaming ? chat.stop() : root.sendAction()
            }
        }
    }

    // ── 设置弹窗 ──────────────────────────────────────────────
    Popup {
        id: settingsPopup
        modal: true
        width: 480
        height: 440
        anchors.centerIn: parent
        onOpened: {
            root.loadSettingsIntoFields()
            if (root.qmlCheckStage === 1) {
                // 第一轮：模拟用户键入并保存
                endpointField.text = "http://qml-check.example/v1"
                apiKeyField.text = "sk-qml-check"
                modelField.text = "qml-check-model"
                systemPromptField.text = "qml-check-prompt"
                root.applySettings()
                root.qmlCheckStage = 2
                // 清空字段，确保第二轮保存的值只能来自 onOpened 的重新填充
                endpointField.text = ""
                apiKeyField.text = ""
                modelField.text = ""
                systemPromptField.text = ""
                settingsPopup.close()
                settingsPopup.open()
            } else if (root.qmlCheckStage === 2) {
                // 第二轮：先把字段清空再打开——onOpened 必须从 settings
                // 重新填充，否则此轮会把空串写回导致校验失败
                root.applySettings()
            }
        }
        background: Rectangle {
            color: "#1d1e24"
            border.color: "#33343e"
            radius: 10
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 8

            Label { text: qsTr("设置"); font.pixelSize: 16; font.bold: true; color: "#e8e8e8" }
            Label { text: qsTr("API 地址（chat completions）"); color: "#8a8b94"; font.pixelSize: 11 }
            TextField {
                id: endpointField
                Layout.fillWidth: true
                color: "#e8e8e8"
                selectByMouse: true
                background: SettingFieldBg
            }
            Label { text: qsTr("API Key"); color: "#8a8b94"; font.pixelSize: 11 }
            TextField {
                id: apiKeyField
                Layout.fillWidth: true
                echoMode: TextInput.Password
                color: "#e8e8e8"
                selectByMouse: true
                background: SettingFieldBg
            }
            Label { text: qsTr("模型"); color: "#8a8b94"; font.pixelSize: 11 }
            TextField {
                id: modelField
                Layout.fillWidth: true
                color: "#e8e8e8"
                selectByMouse: true
                background: SettingFieldBg
            }
            Label { text: qsTr("自定义系统提示词（附加段落）"); color: "#8a8b94"; font.pixelSize: 11 }
            TextArea {
                id: systemPromptField
                Layout.fillWidth: true
                Layout.fillHeight: true
                wrapMode: TextArea.Wrap
                color: "#e8e8e8"
                background: SettingFieldBg
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Button {
                    text: qsTr("取消")
                    onClicked: settingsPopup.close()
                }
                Button {
                    highlighted: true
                    text: qsTr("保存")
                    onClicked: {
                        root.applySettings()
                        settingsPopup.close()
                    }
                }
            }
        }
    }
}
