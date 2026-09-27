import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 设置视图：常规 / 外观 / 模型提供商 / MCP / Skills / 快捷键六个分类页。
// 本文件持有提交链路（loadSettingsIntoFields / commitSettings 与全部工作副本
// 状态）与页头；六个分类页拆在 SettingsXxxPage.qml，页面经 `view` 属性回引
// 本文件、经 property alias 把输入控件暴露给提交链路。改动即时生效、无保存
// 按钮：下拉/勾选等离散控件改动立即提交，文本字段经 textEdited 触发防抖提交，
// 退出设置页/关窗时经 commitPending 冲刷未落地的防抖提交。AppSettings 的
// setter 每次都发 settingsChanged，若字段挂活绑定，写入时会把还没读到的
// 字段刷回旧值，因此 loadSettingsIntoFields 采用一次性填充而非活绑定。
// settingsCategory 由 Main 持有并注入；--qml-check 冒烟钩子入口为 runQmlCheck()。
ColumnLayout {
    id: settingsRoot

    property string settingsCategory: "general"
    // loadSettingsIntoFields / 拉取结果回填期间为 true：程序性赋值触发的
    // onToggled / onEditTextChanged 不当作用户改动提交
    property bool loadingFields: false

    // 模型清单工作副本（当前编辑中的供应商）：每项为
    // {id, displayName, contextWindow, maxOutputTokens, images}，保存时经
    // updateProvider 写回 models；modelsSelected 是编辑器当前选中的条目，
    // currentModelWorking 是"设为当前"的目标模型 id（写回 provider.model）。
    // 拉取快照用于判定结果返回时表单是否已被改动（如切走供应商），过期则不应用
    property var modelsWorking: []
    property int modelsSelected: 0
    property string currentModelWorking: ""
    property var modelsFetchSnapshot: null
    property string modelsFetchStatus: ""

    // MCP 服务器工作副本：编辑器选中项 mcpSelected，提交时经 applyMcpServers 写回
    property var mcpServersWorking: []
    property int mcpSelected: 0

    // preset=custom 时勾选的内置工具名，保存时写回
    property var customToolsWorking: []

    // 工作副本对应的供应商下标：字段只在进入设置模式时加载一次，若期间
    // 激活供应商被外部改变（聊天弹层 selectModel），提交时写回的是加载时
    // 的供应商，否则会把 A 供应商的配置（含模型清单）写进 B
    property int loadedProviderIndex: -1

    // 激活供应商在设置页之外被切换（弹层 selectModel 连发两次 settingsChanged：
    // 先切供应商再设模型）：重载排队到事件循环，等写方完整结束后同步工作副本，
    // 避免读到半更新状态
    Connections {
        target: settings
        function onSettingsChanged() {
            if (settings.activeProvider !== settingsRoot.loadedProviderIndex
                    && !settingsRoot.loadingFields)
                Qt.callLater(settingsRoot.reloadFieldsIfDrifted)
        }
    }

    function reloadFieldsIfDrifted() {
        if (settings.activeProvider !== settingsRoot.loadedProviderIndex)
            settingsRoot.loadSettingsIntoFields()
    }

    anchors.margins: 20
    spacing: 12

    Theme {
        id: theme
        dark: settings.dark
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
        const language = generalPage.languageCombo.currentValue
        const theme = appearancePage.themeCombo.currentValue
        const fontScale = appearancePage.fontScaleCombo.currentValue
        const lineSpacing = appearancePage.lineSpacingCombo.currentValue
        const sendShortcut = shortcutsPage.sendShortcutCombo.currentValue
        const toolPreset = generalPage.toolPresetCombo.currentValue
        const languageChanged = language !== settings.language
        // 写回工作副本所属的供应商而非当前激活的：激活供应商可能在字段加载后
        // 被聊天弹层 selectModel 改变，按当前值写会把 A 的配置写进 B。
        // 字段从未加载时（loadedProviderIndex = -1）跳过写回：此时 modelsWorking
        // 为空，写回会清空该供应商的模型清单
        if (settingsRoot.loadedProviderIndex >= 0
                && settingsRoot.loadedProviderIndex < settings.providers.length) {
            settingsRoot.flushModelFields()
            settings.updateProvider(settingsRoot.loadedProviderIndex,
                { "name": providersPage.providerNameField.text,
                  "protocol": providersPage.protocolCombo.currentValue,
                  "endpoint": providersPage.endpointField.text,
                  "apiKey": providersPage.apiKeyField.text,
                  "model": settingsRoot.currentModelWorking,
                  "models": settingsRoot.modelsWorking,
                  "serverSearch": providersPage.serverSearchCheck.checked,
                  "inputPrice": Number(providersPage.inputPriceField.text) || 0,
                  "outputPrice": Number(providersPage.outputPriceField.text) || 0,
                  "cachedPrice": Number(providersPage.cachedPriceField.text) || 0 })
        }
        settings.systemPrompt = generalPage.systemPromptField.text
        settings.webSearchEndpoint = generalPage.webSearchEndpointField.text
        settings.webSearchApiKey = generalPage.webSearchApiKeyField.text
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
        settingsRoot.mcpServersWorking[i].name = mcpPage.mcpNameField.text.trim()
        settingsRoot.mcpServersWorking[i].command = mcpPage.mcpCommandField.text.trim()
        settingsRoot.mcpServersWorking[i].args = mcpPage.mcpArgsField.text.split("\n")
            .map(s => s.trim()).filter(s => s.length > 0)
    }

    function loadMcpFields() {
        if (settingsRoot.mcpServersWorking.length === 0) {
            mcpPage.mcpNameField.text = ""
            mcpPage.mcpCommandField.text = ""
            mcpPage.mcpArgsField.text = ""
            return
        }
        settingsRoot.mcpSelected = Math.min(settingsRoot.mcpSelected,
                                            settingsRoot.mcpServersWorking.length - 1)
        const server = settingsRoot.mcpServersWorking[settingsRoot.mcpSelected]
        mcpPage.mcpNameField.text = server.name
        mcpPage.mcpCommandField.text = server.command
        mcpPage.mcpArgsField.text = server.args.join("\n")
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

    // ── 模型编辑器：字段 ↔ 工作副本 ─────────────────────────────
    function makeModel(id) {
        return { "id": id, "displayName": "", "contextWindow": 0,
                 "maxOutputTokens": 0, "images": true }
    }

    // 下拉框条目文本：有显示名时并列展示，便于对照请求用的模型 id
    function modelLabel(model) {
        return model.displayName.length > 0
            ? model.displayName + "（" + model.id + "）" : model.id
    }

    function refreshModelSelect() {
        settingsRoot.modelsSelected = settingsRoot.modelsWorking.length > 0
            ? Math.min(settingsRoot.modelsSelected, settingsRoot.modelsWorking.length - 1) : 0
        providersPage.modelSelect.model = settingsRoot.modelsWorking.map(settingsRoot.modelLabel)
        providersPage.modelSelect.currentIndex = settingsRoot.modelsSelected
    }

    function loadModelFields() {
        settingsRoot.loadingFields = true
        const model = settingsRoot.modelsWorking.length > 0
            ? settingsRoot.modelsWorking[settingsRoot.modelsSelected] : null
        providersPage.modelDisplayNameField.text = model ? model.displayName : ""
        providersPage.modelIdField.text = model ? model.id : ""
        providersPage.modelContextField.text = model ? String(model.contextWindow) : "0"
        providersPage.modelMaxOutputField.text = model ? String(model.maxOutputTokens) : "0"
        providersPage.modelImagesCheck.checked = model ? model.images : true
        settingsRoot.loadingFields = false
    }

    // 把编辑字段写回工作副本当前条目；若改的是当前模型且改了 id，跟随更新
    function flushModelFields() {
        if (settingsRoot.modelsWorking.length === 0)
            return
        const i = Math.min(settingsRoot.modelsSelected, settingsRoot.modelsWorking.length - 1)
        const model = settingsRoot.modelsWorking[i]
        const oldId = model.id
        model.displayName = providersPage.modelDisplayNameField.text.trim()
        model.id = providersPage.modelIdField.text.trim()
        model.contextWindow = parseInt(providersPage.modelContextField.text) || 0
        model.maxOutputTokens = parseInt(providersPage.modelMaxOutputField.text) || 0
        model.images = providersPage.modelImagesCheck.checked
        if (settingsRoot.currentModelWorking === oldId && model.id.length > 0)
            settingsRoot.currentModelWorking = model.id
    }

    function addModel() {
        settingsRoot.flushModelFields()
        settingsRoot.modelsWorking.push(settingsRoot.makeModel(""))
        settingsRoot.modelsSelected = settingsRoot.modelsWorking.length - 1
        settingsRoot.refreshModelSelect()
        settingsRoot.loadModelFields()
        providersPage.modelIdField.forceActiveFocus()
        // 等用户填完模型 id 再提交（空 id 的条目不落盘）
    }

    function removeModel() {
        if (settingsRoot.modelsWorking.length === 0)
            return
        settingsRoot.flushModelFields()
        const removedId = settingsRoot.modelsWorking[settingsRoot.modelsSelected].id
        settingsRoot.modelsWorking.splice(settingsRoot.modelsSelected, 1)
        if (removedId.length > 0 && settingsRoot.currentModelWorking === removedId) {
            // 删掉的是当前模型：顺延到相邻条目
            const next = settingsRoot.modelsWorking.length > 0
                ? settingsRoot.modelsWorking[Math.max(0, settingsRoot.modelsSelected - 1)].id
                : ""
            settingsRoot.currentModelWorking = next
        }
        settingsRoot.modelsSelected = Math.max(0, settingsRoot.modelsSelected - 1)
        settingsRoot.refreshModelSelect()
        settingsRoot.loadModelFields()
        settingsRoot.commitSettings()
    }

    function setCurrentModel() {
        settingsRoot.flushModelFields()
        if (settingsRoot.modelsWorking.length === 0)
            return
        const model = settingsRoot.modelsWorking[settingsRoot.modelsSelected]
        if (model.id.length > 0 && settingsRoot.currentModelWorking !== model.id) {
            settingsRoot.currentModelWorking = model.id
            settingsRoot.scheduleCommit()
        }
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
        settingsRoot.loadedProviderIndex = settings.activeProvider
        providersPage.endpointField.text = settings.endpoint
        providersPage.apiKeyField.text = settings.apiKey
        providersPage.protocolCombo.currentIndex =
            providersPage.protocolCombo.indexOfValue(settings.protocol)
        providersPage.serverSearchCheck.checked = settings.serverSearch
        const activeProvider = settings.providers.length > 0
            ? settings.providers[settings.activeProvider] : null
        providersPage.providerNameField.text = activeProvider ? activeProvider.name : ""
        providersPage.inputPriceField.text = activeProvider ? String(activeProvider.inputPrice) : "0"
        providersPage.outputPriceField.text = activeProvider ? String(activeProvider.outputPrice) : "0"
        providersPage.cachedPriceField.text = activeProvider ? String(activeProvider.cachedPrice) : "0"
        // 模型编辑器：清单取当前供应商的 models；当前模型不在清单（老配置手输）
        // 时补一个条目，保证它可编辑可见
        let models = activeProvider && activeProvider.models.length > 0
            ? activeProvider.models.slice() : []
        if (settings.model.length > 0 && !models.some(m => m.id === settings.model))
            models.unshift(settingsRoot.makeModel(settings.model))
        settingsRoot.modelsWorking = models
        settingsRoot.currentModelWorking = settings.model
        settingsRoot.modelsSelected = 0
        settingsRoot.refreshModelSelect()
        settingsRoot.loadModelFields()
        settingsRoot.modelsFetchStatus = ""
        settingsRoot.modelsFetchSnapshot = null
        generalPage.webSearchEndpointField.text = settings.webSearchEndpoint
        generalPage.webSearchApiKeyField.text = settings.webSearchApiKey
        settingsRoot.mcpServersWorking = settings.mcpServers
        settingsRoot.mcpSelected = 0
        settingsRoot.loadMcpFields()
        generalPage.systemPromptField.text = settings.systemPrompt
        generalPage.languageCombo.currentIndex =
            generalPage.languageCombo.indexOfValue(settings.language)
        appearancePage.themeCombo.currentIndex =
            appearancePage.themeCombo.indexOfValue(settings.theme)
        appearancePage.fontScaleCombo.currentIndex =
            appearancePage.fontScaleCombo.indexOfValue(settings.fontScale)
        appearancePage.lineSpacingCombo.currentIndex =
            appearancePage.lineSpacingCombo.indexOfValue(settings.lineSpacing)
        shortcutsPage.sendShortcutCombo.currentIndex =
            shortcutsPage.sendShortcutCombo.indexOfValue(settings.sendShortcut)
        generalPage.toolPresetCombo.currentIndex =
            generalPage.toolPresetCombo.indexOfValue(settings.toolPreset)
        settingsRoot.customToolsWorking = settings.customTools
        settingsRoot.loadingFields = false
    }

    // 从端点拉取模型清单：以当前表单值为准（未保存的修改也可拉取），
    // 记下快照，结果返回时表单已改动则不应用
    function fetchModels() {
        settingsRoot.modelsFetchSnapshot = {
            "protocol": providersPage.protocolCombo.currentValue,
            "endpoint": providersPage.endpointField.text,
            "apiKey": providersPage.apiKeyField.text
        }
        settingsRoot.modelsFetchStatus = ""
        chat.fetchModels(providersPage.protocolCombo.currentValue,
                         providersPage.endpointField.text,
                         providersPage.apiKeyField.text)
    }

    function modelsFetchStale() {
        const snap = settingsRoot.modelsFetchSnapshot
        return !snap || snap.protocol !== providersPage.protocolCombo.currentValue
            || snap.endpoint !== providersPage.endpointField.text
            || snap.apiKey !== providersPage.apiKeyField.text
    }

    Connections {
        target: chat
        function onModelsFetched(models) {
            if (settingsRoot.modelsFetchStale()) {
                settingsRoot.modelsFetchStatus = qsTr("表单已改动，结果未应用")
                return
            }
            settingsRoot.flushModelFields()
            // 已拉到的清单与工作副本合并：已有条目（含其参数）不动，仅追加新 id
            const merged = settingsRoot.modelsWorking.slice()
            let added = 0
            for (let i = 0; i < models.length; i++) {
                if (!merged.some(m => m.id === models[i])) {
                    merged.push(settingsRoot.makeModel(models[i]))
                    added++
                }
            }
            settingsRoot.modelsWorking = merged
            settingsRoot.refreshModelSelect()
            settingsRoot.loadModelFields()
            settingsRoot.modelsFetchStatus =
                qsTr("已获取 %1 个模型（新增 %2 个）").arg(models.length).arg(added)
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

    // ── 页头 ─────────────────────────────────────────────────
    SettingsPageHeader {
        title: settingsRoot.settingsCategory === "providers" ? qsTr("模型提供商")
            : settingsRoot.settingsCategory === "mcp" ? qsTr("MCP")
            : settingsRoot.settingsCategory === "skills" ? qsTr("Skills")
            : settingsRoot.settingsCategory === "shortcuts" ? qsTr("快捷键")
            : settingsRoot.settingsCategory === "appearance" ? qsTr("外观")
            : qsTr("常规")
        description: settingsRoot.settingsCategory === "providers" ? qsTr("管理模型供应商与接入参数")
            : settingsRoot.settingsCategory === "mcp" ? qsTr("经 stdio 连接 Model Context Protocol 服务器")
            : settingsRoot.settingsCategory === "skills" ? qsTr("技能来自 SKILL.md，清单自动发现，正文由 Agent 按需读取")
            : settingsRoot.settingsCategory === "shortcuts" ? qsTr("配置消息的发送方式")
            : settingsRoot.settingsCategory === "appearance" ? qsTr("主题、配色与阅读体验")
            : qsTr("语言、联网搜索与系统提示词")
    }

    // ── 常规：语言 / 联网搜索 / 内置工具 / 系统提示词 ─────────
    SettingsGeneralPage {
        id: generalPage
        view: settingsRoot
        visible: settingsRoot.settingsCategory === "general"
        Layout.fillWidth: true
        Layout.fillHeight: true
    }

    // ── 外观：主题 / 配色 / 阅读体验 / 调色板 ─────────────────
    SettingsAppearancePage {
        id: appearancePage
        view: settingsRoot
        visible: settingsRoot.settingsCategory === "appearance"
        Layout.fillWidth: true
        Layout.fillHeight: true
    }

    // ── 模型提供商：上下布局，圆角卡片分节 ───────────────────
    SettingsProvidersPage {
        id: providersPage
        view: settingsRoot
        visible: settingsRoot.settingsCategory === "providers"
        Layout.fillWidth: true
        Layout.fillHeight: true
    }

    // ── MCP：左侧服务器列表，右侧编辑 + 连接状态 ─────────────
    SettingsMcpPage {
        id: mcpPage
        view: settingsRoot
        visible: settingsRoot.settingsCategory === "mcp"
    }

    // ── Skills：自动发现的技能清单（只读） ───────────────────
    SettingsSkillsPage {
        id: skillsPage
        visible: settingsRoot.settingsCategory === "skills"
    }

    // ── 快捷键：发送方式可配置，其余固定 ─────────────────────
    SettingsShortcutsPage {
        id: shortcutsPage
        view: settingsRoot
        visible: settingsRoot.settingsCategory === "shortcuts"
        Layout.fillWidth: true
        Layout.fillHeight: true
    }
}
