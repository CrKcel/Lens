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
        settings.protocol = protocolCombo.currentValue
        settings.serverSearch = serverSearchCheck.checked
        settings.systemPrompt = systemPromptField.text
        settings.webSearchEndpoint = webSearchEndpointField.text
        settings.webSearchApiKey = webSearchApiKeyField.text
        // MCP 服务器：JSON 数组 [{name, command, args:[]}]，解析失败则不改动
        try {
            const servers = JSON.parse(mcpField.text)
            if (Array.isArray(servers))
                settings.setMcpServers(servers)
            mcpField.color = "#e8e8e8"
        } catch (e) {
            mcpField.color = "#e07a7a"
        }
        settings.save()
        chat.refreshContext()
    }

    // 打开时一次性填充。字段上不能挂 text: settings.xxx 之类的活绑定：
    // AppSettings 的 setter 每次都会发 settingsChanged，保存时先写的属性
    // 会把还没读到的字段绑定刷回旧值，导致只有第一个字段能保存。
    function loadSettingsIntoFields() {
        endpointField.text = settings.endpoint
        apiKeyField.text = settings.apiKey
        modelField.text = settings.model
        protocolCombo.currentIndex = protocolCombo.indexOfValue(settings.protocol)
        serverSearchCheck.checked = settings.serverSearch
        providerNameField.text = settings.providers.length > 0
            ? settings.providers[settings.activeProvider].name : ""
        webSearchEndpointField.text = settings.webSearchEndpoint
        webSearchApiKeyField.text = settings.webSearchApiKey
        mcpField.text = JSON.stringify(settings.mcpServers, null, 2)
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
            ToolButton {
                text: qsTr("上下文")
                flat: true
                ToolTip.visible: hovered
                ToolTip.text: qsTr("查看当前上下文组成")
                onClicked: {
                    chat.refreshContext()
                    contextPopup.open()
                }
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
        width: 520
        height: 620
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

            // 供应商选择与管理
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Label { text: qsTr("供应商"); color: "#8a8b94"; font.pixelSize: 11 }
                ComboBox {
                    id: providerCombo
                    Layout.fillWidth: true
                    textRole: "name"
                    model: settings.providers
                    currentIndex: settings.activeProvider
                    onActivated: (index) => {
                        settings.activeProvider = index
                        root.loadSettingsIntoFields()
                    }
                }
                Button {
                    text: qsTr("＋")
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("新增供应商（复制当前配置）")
                    onClicked: {
                        settings.addProvider({
                            "name": qsTr("供应商%1").arg(settings.providers.length + 1),
                            "protocol": protocolCombo.currentValue,
                            "endpoint": endpointField.text,
                            "apiKey": apiKeyField.text,
                            "model": modelField.text,
                            "serverSearch": serverSearchCheck.checked
                        })
                        root.loadSettingsIntoFields()
                    }
                }
                Button {
                    text: qsTr("－")
                    enabled: settings.providers.length > 1
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("删除当前供应商")
                    onClicked: {
                        settings.removeProvider(settings.activeProvider)
                        root.loadSettingsIntoFields()
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Label { text: qsTr("名称"); color: "#8a8b94"; font.pixelSize: 11 }
                TextField {
                    id: providerNameField
                    Layout.fillWidth: true
                    color: "#e8e8e8"
                    selectByMouse: true
                    background: SettingFieldBg
                }
                ComboBox {
                    id: protocolCombo
                    Layout.preferredWidth: 170
                    textRole: "text"
                    valueRole: "value"
                    model: [
                        { text: qsTr("chat completions"), value: "chat_completions" },
                        { text: qsTr("responses"), value: "responses" },
                        { text: qsTr("anthropic"), value: "anthropic" }
                    ]
                }
            }
            CheckBox {
                id: serverSearchCheck
                text: qsTr("服务端联网搜索（供应商支持时启用）")
                font.pixelSize: 11
                contentItem: Label {
                    text: serverSearchCheck.text
                    color: "#8a8b94"
                    font.pixelSize: 11
                    leftPadding: serverSearchCheck.indicator.width + 4
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Label { text: qsTr("API 地址（含协议路径，或仅主机/根路径自动补全）"); color: "#8a8b94"; font.pixelSize: 11 }
            TextField {
                id: endpointField
                Layout.fillWidth: true
                color: "#e8e8e8"
                selectByMouse: true
                background: SettingFieldBg
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
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
                    Layout.preferredWidth: 180
                    color: "#e8e8e8"
                    selectByMouse: true
                    background: SettingFieldBg
                }
            }
            Label {
                text: qsTr("web_search 搜索接口（Tavily 兼容，留空则不启用）")
                color: "#8a8b94"; font.pixelSize: 11
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                TextField {
                    id: webSearchEndpointField
                    Layout.fillWidth: true
                    placeholderText: qsTr("搜索端点")
                    color: "#e8e8e8"
                    selectByMouse: true
                    background: SettingFieldBg
                }
                TextField {
                    id: webSearchApiKeyField
                    Layout.preferredWidth: 180
                    placeholderText: qsTr("密钥")
                    echoMode: TextInput.Password
                    color: "#e8e8e8"
                    selectByMouse: true
                    background: SettingFieldBg
                }
            }
            Label {
                text: qsTr("MCP 服务器（JSON 数组：name / command / args）")
                color: "#8a8b94"; font.pixelSize: 11
            }
            TextArea {
                id: mcpField
                Layout.fillWidth: true
                Layout.preferredHeight: 72
                wrapMode: TextArea.Wrap
                font.family: "monospace"
                font.pixelSize: 11
                color: "#e8e8e8"
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
                        settings.updateProvider(settings.activeProvider,
                            { "name": providerNameField.text,
                              "protocol": protocolCombo.currentValue,
                              "endpoint": endpointField.text,
                              "apiKey": apiKeyField.text,
                              "model": modelField.text,
                              "serverSearch": serverSearchCheck.checked })
                        root.applySettings()
                        settingsPopup.close()
                    }
                }
            }
        }
    }

    // ── 上下文检查器：上下文由什么组成、每一项来自哪里 ──────────
    Popup {
        id: contextPopup
        modal: true
        width: 640
        height: 600
        anchors.centerIn: parent
        background: Rectangle {
            color: "#1d1e24"
            border.color: "#33343e"
            radius: 10
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: qsTr("上下文检查器")
                    font.pixelSize: 16; font.bold: true; color: "#e8e8e8"
                    Layout.fillWidth: true
                }
                ToolButton {
                    text: qsTr("刷新")
                    flat: true
                    onClicked: chat.refreshContext()
                }
            }

            ListView {
                id: sectionList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 8
                model: chat.contextSections

                delegate: Rectangle {
                    width: sectionList.width
                    height: sectionCard.implicitHeight
                    color: "#1f2027"
                    radius: 8
                    border.color: "#2c2d36"

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
                                color: "#7fb4d8"
                                font.bold: true
                                font.pixelSize: 13
                            }
                            Item { Layout.fillWidth: true }
                            Label {
                                text: qsTr("来源：%1").arg(modelData.source)
                                color: "#7c7d86"
                                font.pixelSize: 11
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            text: modelData.content.length > 0
                                  ? modelData.content : qsTr("（空，未注入）")
                            visible: sectionExpanded
                            color: modelData.content.length > 0 ? "#d8d8dc" : "#5a5b64"
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
                    color: "#5a5b64"
                }
            }

            Label {
                text: qsTr("启用工具（%1）").arg(chat.contextTools.length)
                color: "#e8e8e8"; font.bold: true; font.pixelSize: 13
            }
            ListView {
                id: toolList
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(160, 34 * Math.max(1, chat.contextTools.length))
                clip: true
                spacing: 2
                model: chat.contextTools

                delegate: Label {
                    width: toolList.width
                    text: "· " + modelData.name
                          + qsTr("　[%1]").arg(modelData.origin)
                          + (modelData.description.length > 0
                             ? "　— " + modelData.description : "")
                    color: "#9a9ba4"
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }

                Text {
                    anchors.centerIn: parent
                    visible: toolList.count === 0
                    text: qsTr("无")
                    color: "#5a5b64"
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
                    color: modelData.connected ? "#9cdc9c" : "#e07a7a"
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
            }
        }
    }
}
