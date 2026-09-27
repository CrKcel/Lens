import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 设置视图：常规 / 模型提供商 / MCP / Skills / 快捷键五个分类页。
// 设置提交链路的字段 id 与函数集中在本文件。改动即时生效、无保存按钮：
// 下拉/勾选等离散控件改动立即提交，文本字段经 textEdited 触发防抖提交，
// 退出设置页/关窗时经 commitPending 冲刷未落地的防抖提交。AppSettings 的
// setter 每次都发 settingsChanged，若字段挂活绑定，写入时会把还没读到的
// 字段刷回旧值，因此 loadSettingsIntoFields 采用一次性填充而非活绑定。
// settingsCategory 由 Main 持有并注入；--qml-check 冒烟钩子入口为 runQmlCheck()。
//
// 排版组件（inline component）：SettingsPageHeader 页头、SettingsSection
// 卡片分节、SettingsRow 标签居左/控件居右的设置行、SettingsField 标签在上
// 的字段组、KeyCap 快捷键键帽。所有 qsTr 都写在本文件（组件经 property
// 接收文本），保证翻译上下文始终是 SettingsView。
ColumnLayout {
    id: settingsRoot

    property string settingsCategory: "general"
    // loadSettingsIntoFields / 拉取结果回填期间为 true：程序性赋值触发的
    // onToggled / onEditTextChanged 不当作用户改动提交
    property bool loadingFields: false

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

    // ── 排版组件 ─────────────────────────────────────────────

    // 页头：分类标题 + 一句话说明（说明为空则不显示）
    component SettingsPageHeader: ColumnLayout {
        id: pageHeader

        property alias title: pageTitle.text
        property alias description: pageDesc.text
        spacing: 2

        Theme {
            id: headerTheme
            dark: settings.dark
        }

        Label {
            id: pageTitle
            color: headerTheme.text
            font.pixelSize: Math.round(18 * settings.fontScale)
            font.bold: true
        }
        Label {
            id: pageDesc
            visible: text.length > 0
            color: headerTheme.textDim
            font.pixelSize: Math.round(12 * settings.fontScale)
        }
    }

    // 卡片分节：圆角容器 + 标题 + 可选行内说明，default 子项即卡片内容。
    // 高度随内容自适应，卡片内不放 fillHeight 元素。
    component SettingsSection: Rectangle {
        id: sectionCard

        default property alias content: sectionColumn.data
        property alias title: sectionTitle.text
        property string hint: ""

        color: sectionTheme.card
        radius: sectionTheme.radiusM
        border.color: sectionTheme.cardBorder
        implicitHeight: sectionColumn.implicitHeight + 32

        Theme {
            id: sectionTheme
            dark: settings.dark
        }

        ColumnLayout {
            id: sectionColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 16
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Label {
                    id: sectionTitle
                    color: sectionTheme.text
                    font.pixelSize: Math.round(14 * settings.fontScale)
                    font.bold: true
                }
                Label {
                    visible: sectionCard.hint.length > 0
                    text: sectionCard.hint
                    color: sectionTheme.textFaint
                    font.pixelSize: Math.round(11 * settings.fontScale)
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }
        }
    }

    component SettingsRow: RowLayout {
        id: settingRow

        default property alias content: rowControls.data
        property alias label: rowLabel.text
        spacing: 12

        Theme {
            id: rowTheme
            dark: settings.dark
        }

        Label {
            id: rowLabel
            color: rowTheme.text
            font.pixelSize: Math.round(13 * settings.fontScale)
        }
        Item { Layout.fillWidth: true }
        RowLayout {
            id: rowControls
            spacing: 8
        }
    }

    component SettingsField: ColumnLayout {
        id: settingField

        default property alias content: fieldControls.data
        property alias label: fieldLabel.text
        property string hint: ""
        spacing: 6

        Theme {
            id: fieldTheme
            dark: settings.dark
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label {
                id: fieldLabel
                color: fieldTheme.text
                font.pixelSize: Math.round(13 * settings.fontScale)
            }
            Label {
                visible: settingField.hint.length > 0
                text: settingField.hint
                color: fieldTheme.textFaint
                font.pixelSize: Math.round(11 * settings.fontScale)
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }
        RowLayout {
            id: fieldControls
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8
        }
    }

    component KeyCap: Rectangle {
        id: keyCap

        property alias text: keyCapText.text

        implicitWidth: keyCapText.implicitWidth + 20
        implicitHeight: Math.round(24 * settings.fontScale)
        radius: keyCapTheme.radiusS
        color: keyCapTheme.field
        border.color: keyCapTheme.fieldBorder

        Theme {
            id: keyCapTheme
            dark: settings.dark
        }

        Label {
            id: keyCapText
            anchors.centerIn: parent
            color: keyCapTheme.textSoft
            font.family: "monospace"
            font.pixelSize: Math.round(11 * settings.fontScale)
        }
    }

    // 防抖提交：文本字段每次编辑重启，停止输入 600ms 后提交
    Timer {
        id: commitTimer
        interval: 600
        onTriggered: settingsRoot.commitSettings()
    }

    function scheduleCommit() {
        commitTimer.restart()
    }

    // 退出设置页 / 关窗时调用：有未落地的防抖提交则立即提交
    function commitPending() {
        if (commitTimer.running)
            settingsRoot.commitSettings()
    }

    // 把全部字段写回 AppSettings、持久化并刷新上下文。各下拉框的当前值先取
    // 快照：写 language 会触发 engine.retranslate()，重置各 ComboBox 的
    // model 绑定，之后再读 currentValue 已不是用户所选。
    // commitSettings 读全部字段（含未显示分类页的），字段必须先经
    // loadSettingsIntoFields 填充（Main 在进入设置模式时调用）
    function commitSettings() {
        commitTimer.stop()
        const protocol = protocolCombo.currentValue
        const model = modelCombo.editText
        const language = languageCombo.currentValue
        const theme = themeCombo.currentValue
        const fontScale = fontScaleCombo.currentValue
        const lineSpacing = lineSpacingCombo.currentValue
        const sendShortcut = sendShortcutCombo.currentValue
        const toolPreset = toolPresetCombo.currentValue
        const languageChanged = language !== settings.language
        settings.updateProvider(settings.activeProvider,
            { "name": providerNameField.text,
              "protocol": protocol,
              "endpoint": endpointField.text,
              "apiKey": apiKeyField.text,
              "model": model,
              "models": settingsRoot.modelsWorking,
              "serverSearch": serverSearchCheck.checked,
              "inputPrice": Number(inputPriceField.text) || 0,
              "outputPrice": Number(outputPriceField.text) || 0,
              "cachedPrice": Number(cachedPriceField.text) || 0 })
        settings.systemPrompt = systemPromptField.text
        settings.webSearchEndpoint = webSearchEndpointField.text
        settings.webSearchApiKey = webSearchApiKeyField.text
        settings.language = language
        settings.theme = theme
        settings.fontScale = fontScale
        settings.lineSpacing = lineSpacing
        settings.sendShortcut = sendShortcut
        settings.toolPreset = toolPreset
        settings.customTools = settingsRoot.customToolsWorking
        settingsRoot.applyMcpServers()
        settings.save()
        chat.refreshContext()
        if (languageChanged)
            settingsRoot.loadSettingsIntoFields() // retranslate 重置了下拉框，重新回填
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
        settingsRoot.scheduleCommit() // 等用户填完命令再提交（空命令的服务器不落盘）
    }

    function removeMcpServer() {
        if (settingsRoot.mcpServersWorking.length === 0)
            return
        settingsRoot.flushMcpFields()
        settingsRoot.mcpServersWorking.splice(settingsRoot.mcpSelected, 1)
        settingsRoot.mcpSelected = Math.max(0, settingsRoot.mcpSelected - 1)
        settingsRoot.loadMcpFields()
        settingsRoot.commitSettings()
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
        } else {
            return
        }
        settingsRoot.commitSettings()
    }

    // 打开时一次性填充。字段上不能挂 text: settings.xxx 之类的活绑定：
    // AppSettings 的 setter 每次都会发 settingsChanged，提交时先写的属性
    // 会把还没读到的字段绑定刷回旧值，导致只有第一个字段能保存。
    // loadingFields 置位期间，程序性赋值触发的 onToggled / onEditTextChanged
    // 不当作用户改动提交。
    function loadSettingsIntoFields() {
        settingsRoot.loadingFields = true
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
        fontScaleCombo.currentIndex = fontScaleCombo.indexOfValue(settings.fontScale)
        lineSpacingCombo.currentIndex = lineSpacingCombo.indexOfValue(settings.lineSpacing)
        sendShortcutCombo.currentIndex = sendShortcutCombo.indexOfValue(settings.sendShortcut)
        toolPresetCombo.currentIndex = toolPresetCombo.indexOfValue(settings.toolPreset)
        settingsRoot.customToolsWorking = settings.customTools
        settingsRoot.loadingFields = false
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
            settingsRoot.loadingFields = true // 回填触发 onEditTextChanged 不算用户改动
            settingsRoot.modelsWorking = merged
            modelCombo.model = merged
            modelCombo.editText = currentModel
            modelCombo.currentIndex = modelCombo.find(currentModel)
            settingsRoot.loadingFields = false
            settingsRoot.modelsFetchStatus = qsTr("已获取 %1 个模型").arg(models.length)
            settingsRoot.scheduleCommit() // 合并结果即时写回供应商
        }
        function onModelsFetchFailed(error) {
            if (settingsRoot.modelsFetchStale())
                return
            settingsRoot.modelsFetchStatus = qsTr("获取失败：%1").arg(error)
        }
    }

    // CI 冒烟钩子：--qml-check 走一遍设置的加载/提交/回读链路，
    // 不向字段写入任何值；由 C++ 侧定时退出。
    // 若任何字段创建失败，此处会抛出 ReferenceError 并打印到 stderr。
    function runQmlCheck() {
        settingsRoot.loadSettingsIntoFields()
        settingsRoot.commitSettings()
        settingsRoot.loadSettingsIntoFields()
    }

    property var mcpServersWorking: []
    property int mcpSelected: 0
    property int skillsRevision: 0
    property var customToolsWorking: [] // preset=custom 时勾选的内置工具名，保存时写回

    // ── 页头 ─────────────────────────────────────────────────
    SettingsPageHeader {
        title: settingsRoot.settingsCategory === "providers" ? qsTr("模型提供商")
            : settingsRoot.settingsCategory === "mcp" ? qsTr("MCP")
            : settingsRoot.settingsCategory === "skills" ? qsTr("Skills")
            : settingsRoot.settingsCategory === "shortcuts" ? qsTr("快捷键")
            : qsTr("常规")
        description: settingsRoot.settingsCategory === "providers" ? qsTr("管理模型供应商与接入参数")
            : settingsRoot.settingsCategory === "mcp" ? qsTr("经 stdio 连接 Model Context Protocol 服务器")
            : settingsRoot.settingsCategory === "skills" ? qsTr("技能来自 SKILL.md，清单自动发现，正文由 Agent 按需读取")
            : settingsRoot.settingsCategory === "shortcuts" ? qsTr("配置消息的发送方式")
            : qsTr("语言、主题与阅读体验")
    }

    // ── 常规：外观 / 联网搜索 / 内置工具 / 系统提示词 ─────────
    ScrollView {
        id: generalScroll
        visible: settingsRoot.settingsCategory === "general"
        Layout.fillWidth: true
        Layout.fillHeight: true
        contentWidth: availableWidth
        contentHeight: generalContent.implicitHeight
        ScrollBar.vertical: SlimScrollBar {}

        ColumnLayout {
            id: generalContent
            width: generalScroll.availableWidth
            spacing: 12

            SettingsSection {
                Layout.fillWidth: true
                title: qsTr("外观")

                SettingsRow {
                    Layout.fillWidth: true
                    label: qsTr("语言")
                    ComboBox {
                        id: languageCombo
                        Layout.preferredWidth: 170
                        textRole: "text"
                        valueRole: "value"
                        onActivated: settingsRoot.commitSettings()
                        model: [
                            { text: qsTr("跟随系统"), value: "system" },
                            { text: qsTr("中文"), value: "zh" },
                            { text: qsTr("English"), value: "en" }
                        ]
                    }
                }
                SettingsRow {
                    Layout.fillWidth: true
                    label: qsTr("主题")
                    ComboBox {
                        id: themeCombo
                        Layout.preferredWidth: 170
                        textRole: "text"
                        valueRole: "value"
                        onActivated: settingsRoot.commitSettings()
                        model: [
                            { text: qsTr("跟随系统"), value: "system" },
                            { text: qsTr("深色"), value: "dark" },
                            { text: qsTr("浅色"), value: "light" }
                        ]
                    }
                }
                SettingsRow {
                    Layout.fillWidth: true
                    label: qsTr("字体大小")
                    ComboBox {
                        id: fontScaleCombo
                        Layout.preferredWidth: 170
                        textRole: "text"
                        valueRole: "value"
                        onActivated: settingsRoot.commitSettings()
                        model: [
                            { text: qsTr("小（85%）"), value: 0.85 },
                            { text: qsTr("标准（100%）"), value: 1.0 },
                            { text: qsTr("大（115%）"), value: 1.15 },
                            { text: qsTr("特大（130%）"), value: 1.3 },
                            { text: qsTr("最大（150%）"), value: 1.5 }
                        ]
                    }
                }
                SettingsRow {
                    Layout.fillWidth: true
                    label: qsTr("行间距")
                    ComboBox {
                        id: lineSpacingCombo
                        Layout.preferredWidth: 170
                        textRole: "text"
                        valueRole: "value"
                        onActivated: settingsRoot.commitSettings()
                        model: [
                            { text: qsTr("紧凑（100%）"), value: 1.0 },
                            { text: qsTr("标准（115%）"), value: 1.15 },
                            { text: qsTr("宽松（130%）"), value: 1.3 },
                            { text: qsTr("特宽（150%）"), value: 1.5 }
                        ]
                    }
                }
            }

            SettingsSection {
                Layout.fillWidth: true
                title: qsTr("联网搜索")
                hint: qsTr("web_search 搜索接口（Tavily 兼容，留空则不启用）")

                SettingsField {
                    Layout.fillWidth: true
                    label: qsTr("搜索端点")
                    TextField {
                        id: webSearchEndpointField
                        Layout.fillWidth: true
                        placeholderText: qsTr("搜索端点")
                        color: theme.text
                        selectByMouse: true
                        background: SettingFieldBg
                        onTextEdited: settingsRoot.scheduleCommit()
                    }
                }
                SettingsField {
                    Layout.fillWidth: true
                    label: qsTr("密钥")
                    TextField {
                        id: webSearchApiKeyField
                        Layout.fillWidth: true
                        placeholderText: qsTr("密钥")
                        echoMode: TextInput.Password
                        color: theme.text
                        selectByMouse: true
                        background: SettingFieldBg
                        onTextEdited: settingsRoot.scheduleCommit()
                    }
                }
            }

            SettingsSection {
                Layout.fillWidth: true
                title: qsTr("内置工具")

                SettingsRow {
                    Layout.fillWidth: true
                    label: qsTr("预设")
                    ComboBox {
                        id: toolPresetCombo
                        Layout.preferredWidth: 220
                        textRole: "text"
                        valueRole: "value"
                        model: [
                            { text: qsTr("完整（全部工具）"), value: "full" },
                            { text: qsTr("对话（仅搜索）"), value: "chat" },
                            { text: qsTr("只读（read + 搜索）"), value: "read_only" },
                            { text: qsTr("自定义"), value: "custom" }
                        ]
                        onActivated: {
                            if (currentValue === "custom" && settingsRoot.customToolsWorking.length === 0) {
                                // 从 full 切到 custom：默认与 full 一致，避免空清单禁掉所有工具
                                settingsRoot.customToolsWorking =
                                    chat.contextTools.filter(t => t.origin === "内置").map(t => t.name)
                            }
                            settingsRoot.commitSettings()
                        }
                    }
                }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("禁用后的工具不进入上下文，模型无法调用")
                    color: theme.textFaint; font.pixelSize: Math.round(11 * settings.fontScale)
                    visible: toolPresetCombo.currentValue !== "full"
                }
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
                                font.pixelSize: Math.round(12 * settings.fontScale)
                                elide: Text.ElideRight
                                leftPadding: toolCheck.indicator.width + 4
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }
                }
            }

            SettingsSection {
                Layout.fillWidth: true
                title: qsTr("系统提示词")
                hint: qsTr("作为身份提示词，未填时使用内置")

                TextArea {
                    id: systemPromptField
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.round(160 * settings.fontScale)
                    wrapMode: TextArea.Wrap
                    color: theme.text
                    background: SettingFieldBg
                    onTextEdited: settingsRoot.scheduleCommit()
                }
            }
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
                        settings.save()
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
                        settings.save()
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
                        settingsRoot.commitSettings() // 表单值写回当前激活供应商后再切换
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
                        font.pixelSize: Math.round(13 * settings.fontScale)
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 14

            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                SettingsField {
                    Layout.fillWidth: true
                    label: qsTr("名称")
                    TextField {
                        id: providerNameField
                        Layout.fillWidth: true
                        color: theme.text
                        selectByMouse: true
                        background: SettingFieldBg
                        onTextEdited: settingsRoot.scheduleCommit()
                    }
                }
                SettingsField {
                    Layout.preferredWidth: 220
                    label: qsTr("协议")
                    ComboBox {
                        id: protocolCombo
                        Layout.fillWidth: true
                        textRole: "text"
                        valueRole: "value"
                        onActivated: settingsRoot.commitSettings()
                        model: [
                            { text: qsTr("chat completions"), value: "chat_completions" },
                            { text: qsTr("responses"), value: "responses" },
                            { text: qsTr("anthropic"), value: "anthropic" }
                        ]
                    }
                }
            }
            CheckBox {
                id: serverSearchCheck
                text: qsTr("服务端联网搜索（供应商支持时启用）")
                font.pixelSize: Math.round(12 * settings.fontScale)
                onToggled: if (!settingsRoot.loadingFields) settingsRoot.commitSettings()
                contentItem: Label {
                    text: serverSearchCheck.text
                    color: theme.textDim
                    font.pixelSize: Math.round(12 * settings.fontScale)
                    leftPadding: serverSearchCheck.indicator.width + 4
                    verticalAlignment: Text.AlignVCenter
                }
            }
            SettingsField {
                Layout.fillWidth: true
                label: qsTr("API 地址")
                hint: qsTr("含协议路径，或仅主机/根路径自动补全")
                TextField {
                    id: endpointField
                    Layout.fillWidth: true
                    color: theme.text
                    selectByMouse: true
                    background: SettingFieldBg
                    onTextEdited: settingsRoot.scheduleCommit()
                }
            }
            SettingsField {
                Layout.fillWidth: true
                label: qsTr("API Key")
                TextField {
                    id: apiKeyField
                    Layout.fillWidth: true
                    echoMode: TextInput.Password
                    color: theme.text
                    selectByMouse: true
                    background: SettingFieldBg
                    onTextEdited: settingsRoot.scheduleCommit()
                }
            }
            SettingsField {
                Layout.fillWidth: true
                label: qsTr("模型")
                hint: qsTr("可手动输入，或从端点获取清单后选择")
                ComboBox {
                    id: modelCombo
                    Layout.fillWidth: true
                    editable: true
                    selectTextByMouse: true
                    // activated：从清单选中即提交；editText：手动输入走防抖
                    onActivated: settingsRoot.commitSettings()
                    onEditTextChanged: if (!settingsRoot.loadingFields)
                                           settingsRoot.scheduleCommit()
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
                color: theme.textFaint; font.pixelSize: Math.round(11 * settings.fontScale)
                elide: Text.ElideRight
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                SettingsField {
                    Layout.fillWidth: true
                    Layout.maximumWidth: 140
                    label: qsTr("输入单价")
                    TextField {
                        id: inputPriceField
                        Layout.fillWidth: true
                        color: theme.text
                        selectByMouse: true
                        background: SettingFieldBg
                        onTextEdited: settingsRoot.scheduleCommit()
                    }
                }
                SettingsField {
                    Layout.fillWidth: true
                    Layout.maximumWidth: 140
                    label: qsTr("输出单价")
                    TextField {
                        id: outputPriceField
                        Layout.fillWidth: true
                        color: theme.text
                        selectByMouse: true
                        background: SettingFieldBg
                        onTextEdited: settingsRoot.scheduleCommit()
                    }
                }
                SettingsField {
                    Layout.fillWidth: true
                    Layout.maximumWidth: 140
                    label: qsTr("缓存单价")
                    TextField {
                        id: cachedPriceField
                        Layout.fillWidth: true
                        color: theme.text
                        selectByMouse: true
                        background: SettingFieldBg
                        onTextEdited: settingsRoot.scheduleCommit()
                    }
                }
                Item { Layout.fillWidth: true }
            }
            Label {
                text: qsTr("每百万 token 单价，留空或 0 表示不计费；缓存单价留空或 0 时按输入单价计")
                color: theme.textFaint; font.pixelSize: Math.round(11 * settings.fontScale)
                wrapMode: Text.Wrap
                Layout.fillWidth: true
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
                        settingsRoot.scheduleCommit()
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
                        font.pixelSize: Math.round(13 * settings.fontScale)
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
            spacing: 14
            enabled: settingsRoot.mcpServersWorking.length > 0

            SettingsField {
                Layout.fillWidth: true
                label: qsTr("名称")
                TextField {
                    id: mcpNameField
                    Layout.fillWidth: true
                    color: theme.text
                    selectByMouse: true
                    background: SettingFieldBg
                    onTextEdited: settingsRoot.scheduleCommit()
                }
            }
            SettingsField {
                Layout.fillWidth: true
                label: qsTr("启动命令")
                hint: qsTr("stdio 传输，如 npx、python")
                TextField {
                    id: mcpCommandField
                    Layout.fillWidth: true
                    color: theme.text
                    selectByMouse: true
                    background: SettingFieldBg
                    onTextEdited: settingsRoot.scheduleCommit()
                }
            }
            SettingsField {
                Layout.fillWidth: true
                Layout.fillHeight: true
                label: qsTr("参数（每行一个）")
                TextArea {
                    id: mcpArgsField
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    wrapMode: TextArea.Wrap
                    font.family: "monospace"
                    font.pixelSize: Math.round(11 * settings.fontScale)
                    color: theme.text
                    background: SettingFieldBg
                    onTextEdited: settingsRoot.scheduleCommit()
                }
            }
            Label { text: qsTr("连接状态"); color: theme.textDim; font.pixelSize: Math.round(12 * settings.fontScale) }
            Repeater {
                model: chat.mcpStatus
                Label {
                    required property var modelData
                    width: parent.width
                    text: "· MCP " + modelData.name + "　" + modelData.status
                          + qsTr("　[%1]").arg(modelData.command)
                    color: modelData.connected ? theme.success : theme.error
                    font.pixelSize: Math.round(11 * settings.fontScale)
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
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
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
                            font.pixelSize: Math.round(13 * settings.fontScale)
                        }
                        Item { Layout.fillWidth: true }
                        Label {
                            text: modelData.origin === "global"
                                  ? qsTr("全局") : qsTr("项目")
                            color: theme.textDim
                            font.pixelSize: Math.round(10 * settings.fontScale)
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: modelData.description.length > 0
                        text: modelData.description
                        color: theme.textSoft
                        wrapMode: Text.Wrap
                        font.pixelSize: Math.round(12 * settings.fontScale)
                    }
                    Label {
                        Layout.fillWidth: true
                        text: modelData.path
                        color: theme.textDim
                        font.family: "monospace"
                        font.pixelSize: Math.round(10 * settings.fontScale)
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
            font.pixelSize: Math.round(11 * settings.fontScale)
        }
    }

    // ── 快捷键：发送方式可配置，其余固定 ─────────────────────
    ScrollView {
        id: shortcutsScroll
        visible: settingsRoot.settingsCategory === "shortcuts"
        Layout.fillWidth: true
        Layout.fillHeight: true
        contentWidth: availableWidth
        contentHeight: shortcutsContent.implicitHeight
        ScrollBar.vertical: SlimScrollBar {}

        ColumnLayout {
            id: shortcutsContent
            width: shortcutsScroll.availableWidth
            spacing: 12

            SettingsSection {
                Layout.fillWidth: true
                title: qsTr("发送消息")

                SettingsRow {
                    Layout.fillWidth: true
                    label: qsTr("发送方式")
                    ComboBox {
                        id: sendShortcutCombo
                        Layout.preferredWidth: 320
                        textRole: "text"
                        valueRole: "value"
                        onActivated: settingsRoot.commitSettings()
                        model: [
                            { text: qsTr("Ctrl+Enter 发送，Enter 换行"), value: "ctrl_enter" },
                            { text: qsTr("Enter 发送，Shift+Enter 换行"), value: "enter" }
                        ]
                    }
                }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("输入框内：Enter / Shift+Enter 均可换行，取决于上方发送方式")
                    color: theme.textFaint
                    font.pixelSize: Math.round(11 * settings.fontScale)
                    wrapMode: Text.Wrap
                }
            }

            SettingsSection {
                Layout.fillWidth: true
                title: qsTr("固定快捷键")

                SettingsRow {
                    Layout.fillWidth: true
                    label: qsTr("新建会话")
                    KeyCap { text: qsTr("Ctrl+N") }
                }
            }
        }
    }
}
