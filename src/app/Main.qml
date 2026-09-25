import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    width: 1100
    height: 720
    visible: true
    title: qsTr("Lens")
    color: theme.background

    Theme {
        id: theme
        dark: settings.dark
    }

    Shortcut {
        sequence: StandardKey.New
        context: Qt.ApplicationShortcut
        onActivated: chat.newConversation(workdirField.text)
    }

    property bool settingsMode: false
    property string settingsCategory: "general"
    property var mcpServersWorking: []
    property int mcpSelected: 0
    property int skillsRevision: 0

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
        settings.language = languageCombo.currentValue
        settings.theme = themeCombo.currentValue
        settings.sendShortcut = sendShortcutCombo.currentValue
        root.applyMcpServers()
        settings.save()
        chat.refreshContext()
    }

    function saveSettings() {
        settings.updateProvider(settings.activeProvider,
            { "name": providerNameField.text,
              "protocol": protocolCombo.currentValue,
              "endpoint": endpointField.text,
              "apiKey": apiKeyField.text,
              "model": modelField.text,
              "serverSearch": serverSearchCheck.checked })
        root.applySettings()
    }

    // ── MCP 编辑器：字段 ↔ 工作副本 ─────────────────────────────
    function flushMcpFields() {
        if (root.mcpServersWorking.length === 0)
            return
        const i = Math.min(root.mcpSelected, root.mcpServersWorking.length - 1)
        root.mcpServersWorking[i].name = mcpNameField.text.trim()
        root.mcpServersWorking[i].command = mcpCommandField.text.trim()
        root.mcpServersWorking[i].args = mcpArgsField.text.split("\n")
            .map(s => s.trim()).filter(s => s.length > 0)
    }

    function loadMcpFields() {
        if (root.mcpServersWorking.length === 0) {
            mcpNameField.text = ""
            mcpCommandField.text = ""
            mcpArgsField.text = ""
            return
        }
        root.mcpSelected = Math.min(root.mcpSelected, root.mcpServersWorking.length - 1)
        const server = root.mcpServersWorking[root.mcpSelected]
        mcpNameField.text = server.name
        mcpCommandField.text = server.command
        mcpArgsField.text = server.args.join("\n")
    }

    function addMcpServer() {
        root.flushMcpFields()
        root.mcpServersWorking.push({ "name": qsTr("新服务器"), "command": "", "args": [] })
        root.mcpSelected = root.mcpServersWorking.length - 1
        root.loadMcpFields()
    }

    function removeMcpServer() {
        if (root.mcpServersWorking.length === 0)
            return
        root.mcpServersWorking.splice(root.mcpSelected, 1)
        root.mcpSelected = Math.max(0, root.mcpSelected - 1)
        root.loadMcpFields()
    }

    function applyMcpServers() {
        root.flushMcpFields()
        settings.setMcpServers(root.mcpServersWorking)
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
        root.mcpServersWorking = settings.mcpServers
        root.mcpSelected = 0
        root.loadMcpFields()
        systemPromptField.text = settings.systemPrompt
        languageCombo.currentIndex = languageCombo.indexOfValue(settings.language)
        themeCombo.currentIndex = themeCombo.indexOfValue(settings.theme)
        sendShortcutCombo.currentIndex = sendShortcutCombo.indexOfValue(settings.sendShortcut)
    }

    // CI 冒烟钩子：--qml-check 模拟真实设置流程并验证两轮保存回读，
    // 由 C++ 侧定时退出；配合检查 settings.json 可验证完整保存链路。
    // 若任何字段创建失败，此处会抛出 ReferenceError 并打印到 stderr。
    property int qmlCheckStage: 0

    Component.onCompleted: {
        if (Qt.application.arguments.indexOf("--qml-check") >= 0) {
            root.settingsMode = true
            root.settingsCategory = "providers"
            root.qmlCheckStage = 1
            root.loadSettingsIntoFields()
            // 第一轮：模拟用户键入并保存
            endpointField.text = "http://qml-check.example/v1"
            apiKeyField.text = "sk-qml-check"
            modelField.text = "qml-check-model"
            systemPromptField.text = "qml-check-prompt"
            root.applySettings()
            // 清空字段，确保第二轮保存的值只能来自 loadSettingsIntoFields
            // 的重新填充
            root.qmlCheckStage = 2
            endpointField.text = ""
            apiKeyField.text = ""
            modelField.text = ""
            systemPromptField.text = ""
            root.loadSettingsIntoFields()
            root.applySettings()
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
        color: theme.surface
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
                    text: root.settingsMode ? qsTr("设置") : qsTr("Lens")
                    font.pixelSize: 18
                    font.bold: true
                    color: theme.text
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
                visible: !sidebar.collapsed && !root.settingsMode
                Layout.fillWidth: true
                placeholderText: qsTr("工作文件夹（默认主目录）")
                color: theme.textSoft
                selectByMouse: true
                font.pixelSize: 12
                background: Rectangle {
                    color: theme.field
                    border.color: theme.fieldBorder
                    radius: 6
                }
            }

            Button {
                visible: !sidebar.collapsed && !root.settingsMode
                Layout.fillWidth: true
                text: qsTr("＋ 新建会话")
                onClicked: chat.newConversation(workdirField.text)
            }

            // 设置模式：分类导航
            ColumnLayout {
                visible: !sidebar.collapsed && root.settingsMode
                Layout.fillWidth: true
                spacing: 4
                Button {
                    Layout.fillWidth: true
                    highlighted: root.settingsCategory === "general"
                    text: qsTr("常规")
                    onClicked: root.settingsCategory = "general"
                }
                Button {
                    Layout.fillWidth: true
                    highlighted: root.settingsCategory === "providers"
                    text: qsTr("模型提供商")
                    onClicked: root.settingsCategory = "providers"
                }
                Button {
                    Layout.fillWidth: true
                    highlighted: root.settingsCategory === "mcp"
                    text: qsTr("MCP")
                    onClicked: root.settingsCategory = "mcp"
                }
                Button {
                    Layout.fillWidth: true
                    highlighted: root.settingsCategory === "skills"
                    text: qsTr("Skills")
                    onClicked: root.settingsCategory = "skills"
                }
                Button {
                    Layout.fillWidth: true
                    highlighted: root.settingsCategory === "shortcuts"
                    text: qsTr("快捷键")
                    onClicked: root.settingsCategory = "shortcuts"
                }
            }

            ListView {
                id: conversationList
                visible: !sidebar.collapsed && !root.settingsMode
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
                        color: parent.highlighted ? theme.highlight
                             : parent.hovered ? theme.field : "transparent"
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
                                color: theme.text
                                elide: Text.ElideRight
                                font.pixelSize: 13
                            }
                            Label {
                                Layout.fillWidth: true
                                text: workdir
                                color: theme.textDim
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
                    color: theme.textFaint
                }
            }

            // 占位：列表隐藏（折叠态或设置模式）时把按钮压到底部
            Item { Layout.fillHeight: true; visible: sidebar.collapsed || root.settingsMode }

            Button {
                id: settingsButton
                // 折叠态侧栏内宽仅 ~26px：必须始终填宽并去掉水平内边距，
                // 否则按钮按隐式宽度渲染会伸出侧栏边界
                Layout.fillWidth: true
                leftPadding: sidebar.collapsed ? 0 : 12
                rightPadding: sidebar.collapsed ? 0 : 12
                text: sidebar.collapsed ? (root.settingsMode ? "«" : "⚙")
                      : root.settingsMode ? qsTr("« 返回聊天") : qsTr("⚙ 设置")
                onClicked: {
                    if (root.settingsMode) {
                        root.settingsMode = false
                    } else {
                        root.loadSettingsIntoFields()
                        root.settingsMode = true
                    }
                }
            }
        }
    }

    // ── 主区：消息流 ───────────────────────────────────────────
    ColumnLayout {
        visible: !root.settingsMode
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
                color: theme.text
                font.pixelSize: 15
                elide: Text.ElideRight
            }
            Label {
                text: chat.streaming ? qsTr("生成中…") : ""
                color: theme.accent
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
                            color: theme.bubbleUser
                            radius: 10
                            Label {
                                id: bubble
                                anchors.centerIn: parent
                                width: Math.min(messageList.width * 0.7, 560)
                                text: model.text
                                color: theme.bubbleUserText
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
                                    color: theme.textDim
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
                                    color: theme.textFaint
                                    font.pixelSize: 11
                                }
                            }
                            Label {
                                objectName: "assistantReasoning"
                                visible: model.reasoning.length > 0 && reasoningExpanded
                                Layout.fillWidth: true
                                text: model.reasoning + (model.streaming && model.text.length === 0 ? " ▌" : "")
                                color: theme.textDim
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
                                color: theme.textSoft
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
                            color: theme.card
                            radius: 8
                            border.color: model.toolPending ? theme.accentBorder : theme.cardBorder
                            ColumnLayout {
                                id: toolColumn
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 4
                                Label {
                                    text: (model.toolPending ? "⚙ " : "✔ ")
                                                          + model.toolName
                                                          + (model.toolPending ? qsTr("　运行中…") : "")
                                    color: theme.accent
                                    font.pixelSize: 12
                                    font.bold: true
                                }
                                Label {
                                    Layout.fillWidth: true
                                    visible: model.toolArgs.length > 0
                                    text: model.toolArgs
                                    color: theme.textDim
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
                                    color: theme.success
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
                            color: theme.error
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
                    color: theme.textFaint
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
                placeholderText: chat.streaming ? qsTr("生成中…")
                    : settings.sendShortcut === "enter"
                      ? qsTr("输入消息，Enter 发送，Shift+Enter 换行")
                      : qsTr("输入消息，Ctrl+Enter 发送，Enter 换行")
                wrapMode: TextArea.Wrap
                color: theme.text
                background: Rectangle {
                    color: theme.field
                    border.color: theme.fieldBorder
                    radius: 8
                }
                Keys.onPressed: (event) => {
                    if (event.key !== Qt.Key_Return && event.key !== Qt.Key_Enter)
                        return
                    const ctrlHeld = (event.modifiers & Qt.ControlModifier) !== 0
                    const plainEnter = (event.modifiers & (Qt.ShiftModifier | Qt.ControlModifier)) === 0
                    // ctrl_enter 模式：Ctrl+Enter 发送、Enter 换行
                    // enter 模式：Enter 发送、Shift+Enter 换行
                    if (ctrlHeld || (settings.sendShortcut === "enter" && plainEnter)) {
                        root.sendAction()
                        event.accepted = true
                    }
                    // 其余组合交给 TextArea 默认行为（插入换行）
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

    // ── 设置页面：侧栏分类 常规/模型提供商/MCP/Skills ──────────
    ColumnLayout {
        visible: root.settingsMode
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: sidebar.right
        anchors.right: parent.right
        anchors.margins: 16
        spacing: 10

        Label {
            text: root.settingsCategory === "providers" ? qsTr("模型提供商")
                : root.settingsCategory === "mcp" ? qsTr("MCP")
                : root.settingsCategory === "skills" ? qsTr("Skills")
                : root.settingsCategory === "shortcuts" ? qsTr("快捷键")
                : qsTr("常规")
            font.pixelSize: 16
            font.bold: true
            color: theme.text
        }

        // ── 常规：外观、web_search、系统提示词 ───────────────────
        ColumnLayout {
            visible: root.settingsCategory === "general"
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Label { text: qsTr("语言"); color: theme.textDim; font.pixelSize: 12 }
                ComboBox {
                    id: languageCombo
                    Layout.preferredWidth: 150
                    textRole: "text"
                    valueRole: "value"
                    model: [
                        { text: qsTr("跟随系统"), value: "system" },
                        { text: qsTr("中文"), value: "zh" },
                        { text: qsTr("English"), value: "en" }
                    ]
                }
                Item { Layout.preferredWidth: 24 }
                Label { text: qsTr("主题"); color: theme.textDim; font.pixelSize: 12 }
                ComboBox {
                    id: themeCombo
                    Layout.preferredWidth: 150
                    textRole: "text"
                    valueRole: "value"
                    model: [
                        { text: qsTr("跟随系统"), value: "system" },
                        { text: qsTr("深色"), value: "dark" },
                        { text: qsTr("浅色"), value: "light" }
                    ]
                }
                Item { Layout.fillWidth: true }
            }
            Label {
                text: qsTr("web_search 搜索接口（Tavily 兼容，留空则不启用）")
                color: theme.textDim; font.pixelSize: 12
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                TextField {
                    id: webSearchEndpointField
                    Layout.fillWidth: true
                    placeholderText: qsTr("搜索端点")
                    color: theme.text
                    selectByMouse: true
                    background: SettingFieldBg
                }
                TextField {
                    id: webSearchApiKeyField
                    Layout.preferredWidth: 220
                    placeholderText: qsTr("密钥")
                    echoMode: TextInput.Password
                    color: theme.text
                    selectByMouse: true
                    background: SettingFieldBg
                }
            }
            Label { text: qsTr("自定义系统提示词（附加段落）"); color: theme.textDim; font.pixelSize: 12 }
            TextArea {
                id: systemPromptField
                Layout.fillWidth: true
                Layout.fillHeight: true
                wrapMode: TextArea.Wrap
                color: theme.text
                background: SettingFieldBg
            }
        }

        // ── 模型提供商：左侧列表，右侧编辑表单 ───────────────────
        RowLayout {
            visible: root.settingsCategory === "providers"
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 16

            ColumnLayout {
                Layout.preferredWidth: 200
                Layout.fillHeight: true
                spacing: 6
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Button {
                        Layout.fillWidth: true
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
                        Layout.fillWidth: true
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
                ListView {
                    id: providerList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 4
                    model: settings.providers

                    delegate: ItemDelegate {
                        width: providerList.width
                        highlighted: index === settings.activeProvider
                        onClicked: {
                            settings.activeProvider = index
                            root.loadSettingsIntoFields()
                        }
                        background: Rectangle {
                            color: parent.highlighted ? theme.highlight
                                 : parent.hovered ? theme.field : "transparent"
                            radius: 6
                        }
                        contentItem: Label {
                            text: modelData.name
                            color: theme.text
                            elide: Text.ElideRight
                            font.pixelSize: 13
                        }
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Label { text: qsTr("名称"); color: theme.textDim; font.pixelSize: 12 }
                    TextField {
                        id: providerNameField
                        Layout.fillWidth: true
                        color: theme.text
                        selectByMouse: true
                        background: SettingFieldBg
                    }
                    Label { text: qsTr("协议"); color: theme.textDim; font.pixelSize: 12 }
                    ComboBox {
                        id: protocolCombo
                        Layout.preferredWidth: 180
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
                    font.pixelSize: 12
                    contentItem: Label {
                        text: serverSearchCheck.text
                        color: theme.textDim
                        font.pixelSize: 12
                        leftPadding: serverSearchCheck.indicator.width + 4
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                Label {
                    text: qsTr("API 地址（含协议路径，或仅主机/根路径自动补全）")
                    color: theme.textDim; font.pixelSize: 12
                }
                TextField {
                    id: endpointField
                    Layout.fillWidth: true
                    color: theme.text
                    selectByMouse: true
                    background: SettingFieldBg
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Label { text: qsTr("API Key"); color: theme.textDim; font.pixelSize: 12 }
                    TextField {
                        id: apiKeyField
                        Layout.fillWidth: true
                        echoMode: TextInput.Password
                        color: theme.text
                        selectByMouse: true
                        background: SettingFieldBg
                    }
                    Label { text: qsTr("模型"); color: theme.textDim; font.pixelSize: 12 }
                    TextField {
                        id: modelField
                        Layout.preferredWidth: 200
                        color: theme.text
                        selectByMouse: true
                        background: SettingFieldBg
                    }
                }
                Item { Layout.fillHeight: true }
            }
        }

        // ── MCP：左侧服务器列表，右侧编辑 + 连接状态 ─────────────
        RowLayout {
            visible: root.settingsCategory === "mcp"
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 16

            ColumnLayout {
                Layout.preferredWidth: 200
                Layout.fillHeight: true
                spacing: 6
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Button {
                        Layout.fillWidth: true
                        text: qsTr("＋")
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("新增 MCP 服务器")
                        onClicked: root.addMcpServer()
                    }
                    Button {
                        Layout.fillWidth: true
                        text: qsTr("－")
                        enabled: root.mcpServersWorking.length > 0
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("删除当前服务器")
                        onClicked: root.removeMcpServer()
                    }
                }
                ListView {
                    id: mcpList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 4
                    model: root.mcpServersWorking

                    delegate: ItemDelegate {
                        width: mcpList.width
                        highlighted: index === root.mcpSelected
                        onClicked: {
                            root.flushMcpFields()
                            root.mcpSelected = index
                            root.loadMcpFields()
                        }
                        background: Rectangle {
                            color: parent.highlighted ? theme.highlight
                                 : parent.hovered ? theme.field : "transparent"
                            radius: 6
                        }
                        contentItem: Label {
                            text: modelData.name.length > 0 ? modelData.name : qsTr("（未命名）")
                            color: theme.text
                            elide: Text.ElideRight
                            font.pixelSize: 13
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
                spacing: 8
                enabled: root.mcpServersWorking.length > 0

                Label { text: qsTr("名称"); color: theme.textDim; font.pixelSize: 12 }
                TextField {
                    id: mcpNameField
                    Layout.fillWidth: true
                    color: theme.text
                    selectByMouse: true
                    background: SettingFieldBg
                }
                Label {
                    text: qsTr("启动命令（stdio 传输，如 npx、python）")
                    color: theme.textDim; font.pixelSize: 12
                }
                TextField {
                    id: mcpCommandField
                    Layout.fillWidth: true
                    color: theme.text
                    selectByMouse: true
                    background: SettingFieldBg
                }
                Label { text: qsTr("参数（每行一个）"); color: theme.textDim; font.pixelSize: 12 }
                TextArea {
                    id: mcpArgsField
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    wrapMode: TextArea.Wrap
                    font.family: "monospace"
                    font.pixelSize: 11
                    color: theme.text
                    background: SettingFieldBg
                }
                Label {
                    text: qsTr("连接状态")
                    color: theme.textDim; font.pixelSize: 12
                }
                Repeater {
                    model: chat.mcpStatus
                    Label {
                        required property var modelData
                        width: parent.width
                        text: "· MCP " + modelData.name + "　" + modelData.status
                              + qsTr("　[%1]").arg(modelData.command)
                        color: modelData.connected ? theme.success : theme.error
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                }
            }
        }

        // ── Skills：自动发现的技能清单（只读） ───────────────────
        ColumnLayout {
            visible: root.settingsCategory === "skills"
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Label {
                    Layout.fillWidth: true
                    text: qsTr("技能来自 SKILL.md，清单自动发现，正文由 Agent 按需读取")
                    color: theme.textDim
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
                ToolButton {
                    text: qsTr("刷新")
                    onClicked: root.skillsRevision++
                }
            }
            ListView {
                id: skillList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 8
                model: root.skillsRevision >= 0
                       ? chat.skillsList(chat.currentWorkdir) : []

                delegate: Rectangle {
                    width: skillList.width
                    height: skillCard.implicitHeight
                    color: theme.card
                    radius: 8
                    border.color: theme.cardBorder

                    ColumnLayout {
                        id: skillCard
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.margins: 10
                        spacing: 2

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
                                text: modelData.origin === "global"
                                      ? qsTr("全局") : qsTr("项目")
                                color: theme.textDim
                                font.pixelSize: 10
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: modelData.description.length > 0
                            text: modelData.description
                            color: theme.textSoft
                            wrapMode: Text.Wrap
                            font.pixelSize: 12
                        }
                        Label {
                            Layout.fillWidth: true
                            text: modelData.path
                            color: theme.textDim
                            font.family: "monospace"
                            font.pixelSize: 10
                            elide: Text.ElideMiddle
                        }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: skillList.count === 0
                    text: qsTr("未发现技能")
                    color: theme.textFaint
                }
            }
            Label {
                text: qsTr("全局目录：数据目录下 skills/*/SKILL.md；项目目录：工作文件夹下 .lens/skills/")
                color: theme.textFaint
                font.pixelSize: 11
            }
        }

        // ── 快捷键：发送方式可配置，其余固定 ─────────────────────
        ColumnLayout {
            visible: root.settingsCategory === "shortcuts"
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Label { text: qsTr("发送消息"); color: theme.textDim; font.pixelSize: 12 }
                ComboBox {
                    id: sendShortcutCombo
                    Layout.preferredWidth: 320
                    textRole: "text"
                    valueRole: "value"
                    model: [
                        { text: qsTr("Ctrl+Enter 发送，Enter 换行"), value: "ctrl_enter" },
                        { text: qsTr("Enter 发送，Shift+Enter 换行"), value: "enter" }
                    ]
                }
                Item { Layout.fillWidth: true }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                Label {
                    text: qsTr("固定快捷键")
                    color: theme.textDim
                    font.pixelSize: 12
                    font.bold: true
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Label {
                        text: qsTr("新建会话")
                        color: theme.text
                        font.pixelSize: 12
                        Layout.fillWidth: true
                    }
                    Label {
                        text: qsTr("Ctrl+N")
                        color: theme.textDim
                        font.family: "monospace"
                        font.pixelSize: 12
                    }
                }
                Label {
                    text: qsTr("输入框内：Enter / Shift+Enter 均可换行，取决于上方发送方式")
                    color: theme.textFaint
                    font.pixelSize: 11
                }
            }

            Item { Layout.fillHeight: true }
        }

        // ── 保存行（Skills 只读，无需保存） ──────────────────────
        RowLayout {
            visible: root.settingsCategory !== "skills"
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button {
                highlighted: true
                text: qsTr("保存")
                onClicked: root.saveSettings()
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
            color: theme.surface
            border.color: theme.fieldBorder
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
                    font.pixelSize: 16; font.bold: true; color: theme.text
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
                    color: theme.card
                    radius: 8
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
                text: qsTr("启用工具（%1）").arg(chat.contextTools.length)
                color: theme.text; font.bold: true; font.pixelSize: 13
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
                    color: theme.textDim
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
}
