import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 设置视图：常规 / 模型提供商 / MCP / Skills / 快捷键五个分类页。
// 设置保存链路的字段 id 与函数集中在本文件：AppSettings 的 setter 每次都发
// settingsChanged，保存时先写的属性会把还没读到的字段绑定刷回旧值，因此
// loadSettingsIntoFields 采用一次性填充而非活绑定（详见 loadMcpFields 注释）。
// settingsCategory 由 Main 持有并注入；--qml-check 冒烟钩子入口为 runQmlCheck()。
ColumnLayout {
    id: settingsRoot

    property string settingsCategory: "general"

    // 模型清单工作副本（当前编辑中的供应商），保存时经 updateProvider 写回 models；
    // 拉取快照用于判定结果返回时表单是否已被改动（如切走供应商），过期则不应用
    property var modelsWorking: []
    property var modelsFetchSnapshot: null
    property string modelsFetchStatus: ""

    anchors.margins: 20
    spacing: 12

    Theme {
        id: theme
        dark: settings.dark
    }

    function applySettings() {
        settings.endpoint = endpointField.text
        settings.apiKey = apiKeyField.text
        settings.model = modelCombo.editText
        settings.protocol = protocolCombo.currentValue
        settings.serverSearch = serverSearchCheck.checked
        settings.systemPrompt = systemPromptField.text
        settings.webSearchEndpoint = webSearchEndpointField.text
        settings.webSearchApiKey = webSearchApiKeyField.text
        settings.language = languageCombo.currentValue
        settings.theme = themeCombo.currentValue
        settings.sendShortcut = sendShortcutCombo.currentValue
        settings.toolPreset = toolPresetCombo.currentValue
        settings.customTools = settingsRoot.customToolsWorking
        settingsRoot.applyMcpServers()
        settings.save()
        chat.refreshContext()
    }

    function saveSettings() {
        settings.updateProvider(settings.activeProvider,
            { "name": providerNameField.text,
              "protocol": protocolCombo.currentValue,
              "endpoint": endpointField.text,
              "apiKey": apiKeyField.text,
              "model": modelCombo.editText,
              "models": settingsRoot.modelsWorking,
              "serverSearch": serverSearchCheck.checked,
              "inputPrice": Number(inputPriceField.text) || 0,
              "outputPrice": Number(outputPriceField.text) || 0,
              "cachedPrice": Number(cachedPriceField.text) || 0 })
        settingsRoot.applySettings()
    }

    // ── MCP 编辑器：字段 ↔ 工作副本 ─────────────────────────────
    function flushMcpFields() {
        if (settingsRoot.mcpServersWorking.length === 0)
            return
        const i = Math.min(settingsRoot.mcpSelected, settingsRoot.mcpServersWorking.length - 1)
        settingsRoot.mcpServersWorking[i].name = mcpNameField.text.trim()
        settingsRoot.mcpServersWorking[i].command = mcpCommandField.text.trim()
        settingsRoot.mcpServersWorking[i].args = mcpArgsField.text.split("\n")
            .map(s => s.trim()).filter(s => s.length > 0)
    }

    function loadMcpFields() {
        if (settingsRoot.mcpServersWorking.length === 0) {
            mcpNameField.text = ""
            mcpCommandField.text = ""
            mcpArgsField.text = ""
            return
        }
        settingsRoot.mcpSelected = Math.min(settingsRoot.mcpSelected,
                                            settingsRoot.mcpServersWorking.length - 1)
        const server = settingsRoot.mcpServersWorking[settingsRoot.mcpSelected]
        mcpNameField.text = server.name
        mcpCommandField.text = server.command
        mcpArgsField.text = server.args.join("\n")
    }

    function addMcpServer() {
        settingsRoot.flushMcpFields()
        settingsRoot.mcpServersWorking.push({ "name": qsTr("新服务器"), "command": "", "args": [] })
        settingsRoot.mcpSelected = settingsRoot.mcpServersWorking.length - 1
        settingsRoot.loadMcpFields()
    }

    function removeMcpServer() {
        if (settingsRoot.mcpServersWorking.length === 0)
            return
        settingsRoot.mcpServersWorking.splice(settingsRoot.mcpSelected, 1)
        settingsRoot.mcpSelected = Math.max(0, settingsRoot.mcpSelected - 1)
        settingsRoot.loadMcpFields()
    }

    function applyMcpServers() {
        settingsRoot.flushMcpFields()
        settings.setMcpServers(settingsRoot.mcpServersWorking)
    }

    // ── 内置工具自定义清单：勾选 ↔ 工作副本 ─────────────────────
    function setCustomToolEnabled(name, enabled) {
        const list = settingsRoot.customToolsWorking.slice()
        const i = list.indexOf(name)
        if (enabled && i < 0) {
            list.push(name)
            settingsRoot.customToolsWorking = list
        } else if (!enabled && i >= 0) {
            list.splice(i, 1)
            settingsRoot.customToolsWorking = list
        }
    }

    // 打开时一次性填充。字段上不能挂 text: settings.xxx 之类的活绑定：
    // AppSettings 的 setter 每次都会发 settingsChanged，保存时先写的属性
    // 会把还没读到的字段绑定刷回旧值，导致只有第一个字段能保存。
    function loadSettingsIntoFields() {
        endpointField.text = settings.endpoint
        apiKeyField.text = settings.apiKey
        protocolCombo.currentIndex = protocolCombo.indexOfValue(settings.protocol)
        serverSearchCheck.checked = settings.serverSearch
        const activeProvider = settings.providers.length > 0
            ? settings.providers[settings.activeProvider] : null
        providerNameField.text = activeProvider ? activeProvider.name : ""
        inputPriceField.text = activeProvider ? String(activeProvider.inputPrice) : "0"
        outputPriceField.text = activeProvider ? String(activeProvider.outputPrice) : "0"
        cachedPriceField.text = activeProvider ? String(activeProvider.cachedPrice) : "0"
        // 模型下拉框：清单取当前供应商的 models（空则回退仅含当前模型一项）
        const models = activeProvider && activeProvider.models.length > 0
            ? activeProvider.models : (settings.model ? [settings.model] : [])
        settingsRoot.modelsWorking = models
        const currentModel = settings.model
        modelCombo.model = models
        modelCombo.editText = currentModel
        modelCombo.currentIndex = modelCombo.find(currentModel)
        settingsRoot.modelsFetchStatus = ""
        settingsRoot.modelsFetchSnapshot = null
        webSearchEndpointField.text = settings.webSearchEndpoint
        webSearchApiKeyField.text = settings.webSearchApiKey
        settingsRoot.mcpServersWorking = settings.mcpServers
        settingsRoot.mcpSelected = 0
        settingsRoot.loadMcpFields()
        systemPromptField.text = settings.systemPrompt
        languageCombo.currentIndex = languageCombo.indexOfValue(settings.language)
        themeCombo.currentIndex = themeCombo.indexOfValue(settings.theme)
        sendShortcutCombo.currentIndex = sendShortcutCombo.indexOfValue(settings.sendShortcut)
        toolPresetCombo.currentIndex = toolPresetCombo.indexOfValue(settings.toolPreset)
        settingsRoot.customToolsWorking = settings.customTools
    }

    // 从端点拉取模型清单：以当前表单值为准（未保存的修改也可拉取），
    // 记下快照，结果返回时表单已改动则不应用
    function fetchModels() {
        settingsRoot.modelsFetchSnapshot = {
            "protocol": protocolCombo.currentValue,
            "endpoint": endpointField.text,
            "apiKey": apiKeyField.text
        }
        settingsRoot.modelsFetchStatus = ""
        chat.fetchModels(protocolCombo.currentValue, endpointField.text, apiKeyField.text)
    }

    function modelsFetchStale() {
        const snap = settingsRoot.modelsFetchSnapshot
        return !snap || snap.protocol !== protocolCombo.currentValue
            || snap.endpoint !== endpointField.text || snap.apiKey !== apiKeyField.text
    }

    Connections {
        target: chat
        function onModelsFetched(models) {
            if (settingsRoot.modelsFetchStale()) {
                settingsRoot.modelsFetchStatus = qsTr("表单已改动，结果未应用")
                return
            }
            const merged = settingsRoot.modelsWorking.slice()
            for (let i = 0; i < models.length; i++) {
                if (merged.indexOf(models[i]) < 0)
                    merged.push(models[i])
            }
            const currentModel = modelCombo.editText
            settingsRoot.modelsWorking = merged
            modelCombo.model = merged
            modelCombo.editText = currentModel
            modelCombo.currentIndex = modelCombo.find(currentModel)
            settingsRoot.modelsFetchStatus = qsTr("已获取 %1 个模型").arg(models.length)
        }
        function onModelsFetchFailed(error) {
            if (settingsRoot.modelsFetchStale())
                return
            settingsRoot.modelsFetchStatus = qsTr("获取失败：%1").arg(error)
        }
    }

    // CI 冒烟钩子：--qml-check 模拟真实设置流程并验证两轮保存回读，
    // 由 C++ 侧定时退出；配合检查 settings.json 可验证完整保存链路。
    // 若任何字段创建失败，此处会抛出 ReferenceError 并打印到 stderr。
    property int qmlCheckStage: 0

    function runQmlCheck() {
        settingsRoot.qmlCheckStage = 1
        settingsRoot.loadSettingsIntoFields()
        // 第一轮：模拟用户键入并保存（saveSettings 走供应商表单写回 +
        // applySettings 的完整链路，覆盖单价等 provider 字段）
        endpointField.text = "http://qml-check.example/v1"
        apiKeyField.text = "sk-qml-check"
        modelCombo.editText = "qml-check-model"
        settingsRoot.modelsWorking = ["qml-check-model", "qml-check-model-2"]
        systemPromptField.text = "qml-check-prompt"
        inputPriceField.text = "2.5"
        outputPriceField.text = "10"
        cachedPriceField.text = "0.1"
        settingsRoot.saveSettings()
        // 清空字段，确保第二轮保存的值只能来自 loadSettingsIntoFields
        // 的重新填充
        settingsRoot.qmlCheckStage = 2
        endpointField.text = ""
        apiKeyField.text = ""
        modelCombo.editText = ""
        settingsRoot.modelsWorking = []
        systemPromptField.text = ""
        settingsRoot.loadSettingsIntoFields()
        settingsRoot.applySettings()
    }

    property var mcpServersWorking: []
    property int mcpSelected: 0
    property int skillsRevision: 0
    property var customToolsWorking: [] // preset=custom 时勾选的内置工具名，保存时写回

    Label {
        text: settingsRoot.settingsCategory === "providers" ? qsTr("模型提供商")
            : settingsRoot.settingsCategory === "mcp" ? qsTr("MCP")
            : settingsRoot.settingsCategory === "skills" ? qsTr("Skills")
            : settingsRoot.settingsCategory === "shortcuts" ? qsTr("快捷键")
            : qsTr("常规")
        font.pixelSize: 17
        font.bold: true
        color: theme.text
    }

    // ── 常规：外观、web_search、系统提示词 ───────────────────
    ColumnLayout {
        visible: settingsRoot.settingsCategory === "general"
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
        Label { text: qsTr("内置工具"); color: theme.textDim; font.pixelSize: 12 }
        ComboBox {
            id: toolPresetCombo
            Layout.preferredWidth: 200
            textRole: "text"
            valueRole: "value"
            model: [
                { text: qsTr("完整（全部工具）"), value: "full" },
                { text: qsTr("对话（仅搜索）"), value: "chat" },
                { text: qsTr("只读（read + 搜索）"), value: "read_only" },
                { text: qsTr("自定义"), value: "custom" }
            ]
            onActivated: if (currentValue === "custom" && settingsRoot.customToolsWorking.length === 0) {
                // 从 full 切到 custom：默认与 full 一致，避免空清单禁掉所有工具
                settingsRoot.customToolsWorking =
                    chat.contextTools.filter(t => t.origin === "内置").map(t => t.name)
            }
        }
        Label {
            text: qsTr("禁用后的工具不进入上下文，模型无法调用")
            color: theme.textFaint; font.pixelSize: 11
            visible: toolPresetCombo.currentValue !== "full"
        }
        Item { Layout.fillWidth: true }
        ColumnLayout {
            visible: toolPresetCombo.currentValue === "custom"
            Layout.fillWidth: true
            Layout.leftMargin: 12
            spacing: 0

            Repeater {
                model: chat.contextTools.filter(t => t.origin === "内置")

                delegate: CheckBox {
                    id: toolCheck
                    required property var modelData
                    readonly property bool enabledInCopy:
                        settingsRoot.customToolsWorking.indexOf(modelData.name) >= 0
                    text: modelData.name
                          + (modelData.description.length > 0
                             ? "　— " + modelData.description : "")
                    checked: enabledInCopy
                    onEnabledInCopyChanged: checked = enabledInCopy
                    onToggled: settingsRoot.setCustomToolEnabled(modelData.name, checked)

                    contentItem: Label {
                        text: toolCheck.text
                        color: theme.text
                        font.pixelSize: 12
                        elide: Text.ElideRight
                        leftPadding: toolCheck.indicator.width + 4
                        verticalAlignment: Text.AlignVCenter
                    }
                }
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
        visible: settingsRoot.settingsCategory === "providers"
        Layout.fillWidth: true
        Layout.fillHeight: true
        spacing: 20

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
                            "model": modelCombo.editText,
                            "models": settingsRoot.modelsWorking,
                            "serverSearch": serverSearchCheck.checked,
                            "inputPrice": Number(inputPriceField.text) || 0,
                            "outputPrice": Number(outputPriceField.text) || 0,
                            "cachedPrice": Number(cachedPriceField.text) || 0
                        })
                        settingsRoot.loadSettingsIntoFields()
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
                        settingsRoot.loadSettingsIntoFields()
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
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: SlimScrollBar {}

                delegate: ItemDelegate {
                    width: providerList.width
                    highlighted: index === settings.activeProvider
                    onClicked: {
                        settings.activeProvider = index
                        settingsRoot.loadSettingsIntoFields()
                    }
                    background: Rectangle {
                        radius: theme.radiusS
                        color: parent.highlighted ? theme.accentSoft
                             : parent.hovered ? theme.highlight
                             : "transparent"
                    }
                    contentItem: Label {
                        text: modelData.name
                        color: highlighted ? theme.accent : theme.text
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
            }
            Label {
                text: qsTr("模型（可手动输入，或从端点获取清单后选择）")
                color: theme.textDim; font.pixelSize: 12
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                ComboBox {
                    id: modelCombo
                    Layout.fillWidth: true
                    editable: true
                    selectTextByMouse: true
                }
                AccentButton {
                    text: settings.fetchingModels ? qsTr("获取中…") : qsTr("获取模型列表")
                    enabled: !settings.fetchingModels && endpointField.text.trim().length > 0
                    onClicked: settingsRoot.fetchModels()
                }
            }
            Label {
                visible: settingsRoot.modelsFetchStatus.length > 0
                text: settingsRoot.modelsFetchStatus
                color: theme.textFaint; font.pixelSize: 11
                elide: Text.ElideRight
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Label { text: qsTr("输入单价"); color: theme.textDim; font.pixelSize: 12 }
                TextField {
                    id: inputPriceField
                    Layout.preferredWidth: 90
                    color: theme.text
                    selectByMouse: true
                    background: SettingFieldBg
                }
                Label { text: qsTr("输出单价"); color: theme.textDim; font.pixelSize: 12 }
                TextField {
                    id: outputPriceField
                    Layout.preferredWidth: 90
                    color: theme.text
                    selectByMouse: true
                    background: SettingFieldBg
                }
                Label {
                    text: qsTr("（每百万 token，留空或 0 表示不计费）")
                    color: theme.textFaint; font.pixelSize: 11
                }
                Item { Layout.fillWidth: true }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Label { text: qsTr("缓存单价"); color: theme.textDim; font.pixelSize: 12 }
                TextField {
                    id: cachedPriceField
                    Layout.preferredWidth: 90
                    color: theme.text
                    selectByMouse: true
                    background: SettingFieldBg
                }
                Label {
                    text: qsTr("（可选，缓存命中部分的单价；留空或 0 时按输入单价计）")
                    color: theme.textFaint; font.pixelSize: 11
                }
                Item { Layout.fillWidth: true }
            }
            Item { Layout.fillHeight: true }
        }
    }

    // ── MCP：左侧服务器列表，右侧编辑 + 连接状态 ─────────────
    RowLayout {
        visible: settingsRoot.settingsCategory === "mcp"
        Layout.fillWidth: true
        Layout.fillHeight: true
        spacing: 20

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
                    onClicked: settingsRoot.addMcpServer()
                }
                Button {
                    Layout.fillWidth: true
                    text: qsTr("－")
                    enabled: settingsRoot.mcpServersWorking.length > 0
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("删除当前服务器")
                    onClicked: settingsRoot.removeMcpServer()
                }
            }
            ListView {
                id: mcpList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 4
                model: settingsRoot.mcpServersWorking
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: SlimScrollBar {}

                delegate: ItemDelegate {
                    width: mcpList.width
                    highlighted: index === settingsRoot.mcpSelected
                    onClicked: {
                        settingsRoot.flushMcpFields()
                        settingsRoot.mcpSelected = index
                        settingsRoot.loadMcpFields()
                    }
                    background: Rectangle {
                        radius: theme.radiusS
                        color: parent.highlighted ? theme.accentSoft
                             : parent.hovered ? theme.highlight
                             : "transparent"
                    }
                    contentItem: Label {
                        text: modelData.name.length > 0 ? modelData.name : qsTr("（未命名）")
                        color: highlighted ? theme.accent : theme.text
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
            enabled: settingsRoot.mcpServersWorking.length > 0

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
        visible: settingsRoot.settingsCategory === "skills"
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
                onClicked: settingsRoot.skillsRevision++
            }
        }
        ListView {
            id: skillList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 8
            model: settingsRoot.skillsRevision >= 0
                   ? chat.skillsList(chat.currentWorkdir) : []
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: SlimScrollBar {}

            delegate: Rectangle {
                width: skillList.width
                height: skillCard.implicitHeight
                color: theme.card
                radius: theme.radiusM
                border.color: theme.cardBorder

                ColumnLayout {
                    id: skillCard
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.margins: 12
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
        visible: settingsRoot.settingsCategory === "shortcuts"
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
        visible: settingsRoot.settingsCategory !== "skills"
        Layout.fillWidth: true
        Item { Layout.fillWidth: true }
        AccentButton {
            text: qsTr("保存")
            onClicked: settingsRoot.saveSettings()
        }
    }
}
