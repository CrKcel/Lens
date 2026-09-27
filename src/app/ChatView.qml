import QtQuick
import QtQuick.Controls
import Qt.labs.platform as NativeDialogs
import QtQuick.Layouts

// 聊天主区：消息流、输入区。
// 发送动作由 Main.sendAction 统一处理（工作文件夹在侧栏）。
ColumnLayout {
    id: chatRoot

    readonly property alias inputText: input.text
    property var attachments: [] // 待发送附件 {url, name, isImage}（文件路径/data URL）
    // 思考模式强度（disabled/low/medium/high/max）：会话内临时状态，
    // 随每次发送传给控制器，不进设置、不持久化；默认 high
    property string thinkingLevel: "high"
    // 滑块档位与思考强度的映射（滑块 0..4 ↔ 档位枚举）
    readonly property var thinkingLevels: ["disabled", "low", "medium", "high", "max"]

    function thinkingLevelLabel(level) {
        if (level === "low") return qsTr("低")
        if (level === "medium") return qsTr("中")
        if (level === "high") return qsTr("高")
        if (level === "max") return qsTr("最高")
        return qsTr("关闭")
    }

    signal sendRequested()
    signal stopRequested()

    anchors.topMargin: 16
    anchors.bottomMargin: 16
    anchors.leftMargin: 64
    anchors.rightMargin: 64
    spacing: 10

    readonly property real contentMaxWidth: 860

    function clearInput() {
        input.clear()
        chatRoot.attachments = []
    }

    // isImage 仅用于预览条选择渲染方式（缩略图/文件名 chip），真正分类在发送时
    // 由 ChatController 按魔数与文本嗅探完成
    function addAttachment(url, name, isImage) {
        if (chatRoot.attachments.length >= 8) { // 预览条容量上限，避免挤占输入区
            console.warn("附件数量已达上限（8），忽略新增")
            return
        }
        const next = chatRoot.attachments.slice()
        next.push({url: String(url), name: name || "", isImage: isImage !== false})
        chatRoot.attachments = next
    }

    function removeAttachment(index) {
        const next = chatRoot.attachments.slice()
        next.splice(index, 1)
        chatRoot.attachments = next
    }

    function formatTokens(n) {
        if (n >= 1000000)
            return (n / 1000000).toFixed(1) + "M"
        if (n >= 1000)
            return (n / 1000).toFixed(1) + "k"
        return String(n)
    }

    function formatUsageSummary() {
        const u = chat.usageSummary
        if (!u.hasUsage)
            return ""
        let text = qsTr("上下文 %1 · 累计 %2")
            .arg(chatRoot.formatTokens(u.contextTokens))
            .arg(chatRoot.formatTokens(u.totalPrompt + u.totalCompletion))
        if (u.hasCost)
            text += qsTr(" · 花费 %1").arg(Number(u.cost).toFixed(4))
        return text
    }

    Theme {
        id: theme
        dark: settings.dark
    }

    ListView {
        id: messageList
        objectName: "messageListView"
        Layout.fillWidth: true
        Layout.maximumWidth: chatRoot.contentMaxWidth
        Layout.alignment: Qt.AlignHCenter
        // 空会话时高度收为 0，让输入框经弹性 spacer 居中
        Layout.fillHeight: messageList.count > 0
        visible: messageList.count > 0
        clip: true
        spacing: 12
        model: chat.messages
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: SlimScrollBar {}

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
                    height: bubbleWrap.implicitHeight
                    Rectangle {
                        id: bubbleWrap
                        anchors.right: parent.right
                        anchors.rightMargin: 4
                        readonly property bool hasImages: model.images.length > 0
                        readonly property bool hasFiles: model.files.length > 0
                        implicitWidth: Math.min(
                            Math.max(userText.implicitWidth + 24,
                                     bubbleWrap.hasImages ? 280 : 0,
                                     bubbleWrap.hasFiles ? 260 : 0),
                            messageList.width * 0.72, 560)
                        implicitHeight: (bubbleWrap.hasImages ? userImages.height + 10 : 0)
                                        + (bubbleWrap.hasFiles ? userFiles.height + 8 : 0)
                                        + userText.implicitHeight + 22
                        radius: theme.radiusM
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: theme.bubbleUser }
                            GradientStop { position: 1.0; color: theme.bubbleUser2 }
                        }
                        Column {
                            id: userContent
                            anchors.fill: parent
                            anchors.margins: 11
                            spacing: 8
                            Grid {
                                id: userImages
                                visible: bubbleWrap.hasImages
                                columns: 2
                                columnSpacing: 6
                                rowSpacing: 6
                                Repeater {
                                    model: userImages.visible ? model.images : []
                                    delegate: Rectangle {
                                        width: 124
                                        height: 92
                                        radius: theme.radiusS
                                        color: theme.field
                                        clip: true
                                        Image {
                                            anchors.fill: parent
                                            anchors.margins: 2
                                            source: modelData
                                            fillMode: Image.PreserveAspectCrop
                                            asynchronous: true
                                        }
                                    }
                                }
                            }
                            // 文本文件附件 chip：只显示文件名（内容不展示在气泡里）
                            Flow {
                                id: userFiles
                                visible: bubbleWrap.hasFiles
                                width: userContent.width
                                spacing: 6
                                Repeater {
                                    model: userFiles.visible ? model.files : []
                                    delegate: Rectangle {
                                        required property var modelData
                                        // 限宽让 ElideMiddle 生效，超长文件名不撑破气泡
                                        width: Math.min(fileChipText.implicitWidth + 20, 240)
                                        height: 22
                                        radius: theme.radiusS
                                        color: theme.field
                                        Label {
                                            id: fileChipText
                                            anchors.centerIn: parent
                                            width: Math.min(implicitWidth, parent.width - 12)
                                            text: "📄 " + modelData.name
                                            color: theme.textDim
                                            font.pixelSize: Math.round(11 * settings.fontScale)
                                            elide: Label.ElideMiddle
                                            textFormat: Text.PlainText
                                        }
                                    }
                                }
                            }
                            Label {
                                id: userText
                                width: userContent.width
                                text: model.text
                                color: theme.bubbleUserText
                                wrapMode: Text.Wrap
                                textFormat: Text.PlainText
                                lineHeight: settings.lineSpacing
                                lineHeightMode: Text.ProportionalHeight
                                font.pixelSize: Math.round(13 * settings.fontScale)
                            }
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
                                display: AbstractButton.TextOnly
                                font.pixelSize: Math.round(10 * settings.fontScale)
                                leftPadding: 0
                                rightPadding: 0
                            }
                            Label {
                                text: qsTr("思考过程")
                                color: theme.textDim
                                font.pixelSize: Math.round(11 * settings.fontScale)
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
                                font.pixelSize: Math.round(11 * settings.fontScale)
                            }
                        }
                        Label {
                            objectName: "assistantReasoning"
                            visible: model.reasoning.length > 0 && reasoningExpanded
                            Layout.fillWidth: true
                            text: model.reasoning + (model.streaming && model.text.length === 0 ? " ▌" : "")
                            color: theme.textDim
                            font.pixelSize: Math.round(12 * settings.fontScale)
                            wrapMode: Text.Wrap
                            textFormat: Text.PlainText
                            lineHeight: settings.lineSpacing
                            lineHeightMode: Text.ProportionalHeight
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
                            lineHeight: settings.lineSpacing
                            lineHeightMode: Text.ProportionalHeight
                            font.pixelSize: Math.round(13 * settings.fontScale)
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
                        implicitHeight: toolColumn.implicitHeight + 20
                        x: 12
                        color: theme.card
                        radius: theme.radiusM
                        border.color: model.toolPending ? theme.accentBorder : theme.cardBorder

                        ColumnLayout {
                            id: toolColumn
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 4
                            RowLayout {
                                spacing: 8
                                // 状态圆点：运行中 accent 呼吸闪烁，完成 success
                                Rectangle {
                                    width: 10; height: 10; radius: 5
                                    Layout.alignment: Qt.AlignVCenter
                                    color: model.toolPending ? theme.accent : theme.success
                                    SequentialAnimation on opacity {
                                        running: model.toolPending
                                        loops: Animation.Infinite
                                        NumberAnimation { to: 0.3; duration: 600 }
                                        NumberAnimation { to: 1.0; duration: 600 }
                                    }
                                }
                                Label {
                                    text: model.toolName
                                          + (model.toolPending ? qsTr("　运行中…") : "")
                                    color: model.toolPending ? theme.accent : theme.textSoft
                                    font.pixelSize: Math.round(12 * settings.fontScale)
                                    font.bold: true
                                }
                            }
                            Label {
                                Layout.fillWidth: true
                                visible: model.toolArgs.length > 0
                                text: model.toolArgs
                                color: theme.textDim
                                font.family: "monospace"
                                font.pixelSize: Math.round(11 * settings.fontScale)
                                wrapMode: Text.Wrap
                                maximumLineCount: 6
                                elide: Text.ElideRight
                            }
                            // 工具返回的图片（如 read 读图片文件），缩略图供用户确认
                            Grid {
                                visible: model.images.length > 0
                                columns: 6
                                columnSpacing: 4
                                rowSpacing: 4
                                Repeater {
                                    model: model.images
                                    delegate: Rectangle {
                                        width: 64
                                        height: 48
                                        radius: theme.radiusS
                                        color: theme.field
                                        clip: true
                                        Image {
                                            anchors.fill: parent
                                            anchors.margins: 1
                                            source: modelData
                                            fillMode: Image.PreserveAspectCrop
                                            asynchronous: true
                                        }
                                    }
                                }
                            }
                            Label {
                                Layout.fillWidth: true
                                visible: model.text.length > 0
                                text: model.text
                                color: theme.success
                                font.family: "monospace"
                                font.pixelSize: Math.round(11 * settings.fontScale)
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
                    height: errorCard.implicitHeight
                    Rectangle {
                        id: errorCard
                        width: messageList.width - 24
                        x: 12
                        implicitHeight: errorText.implicitHeight + 20
                        radius: theme.radiusM
                        color: theme.errorSoft
                        border.color: theme.error
                        border.width: 1
                        Label {
                            id: errorText
                            anchors.fill: parent
                            anchors.margins: 10
                            text: qsTr("⚠ %1").arg(model.text)
                            color: theme.error
                            wrapMode: Text.Wrap
                            font.pixelSize: Math.round(12 * settings.fontScale)
                        }
                    }
                }
            }
        }

    }
    // 空会话占位：与输入框一起垂直居中（上下两个弹性 spacer 夹住输入区）
    Item {
        visible: messageList.count === 0
        Layout.fillWidth: true
        Layout.fillHeight: messageList.count === 0
        ColumnLayout {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            spacing: 10
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                width: 44; height: 44; radius: 22
                color: theme.card
                border.color: theme.cardBorder
                Label {
                    anchors.centerIn: parent
                    text: "◎"
                    color: theme.accent
                    font.pixelSize: Math.round(20 * settings.fontScale)
                }
            }
            Label {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("在「设置」中填入 API 地址与 Key\n发送第一条消息即自动创建会话")
                horizontalAlignment: Text.AlignHCenter
                color: theme.textFaint
                font.pixelSize: Math.round(12 * settings.fontScale)
            }
        }
    }

    // ── 输入区：圆角卡片，文本占满整行宽度，发送按钮固定右下角 ─────────
    // 默认三行文本高度 + 底部按钮条，随内容增长，最高不超过窗口四分之一
    Rectangle {
        id: inputCard
        objectName: "inputCard"
        Layout.fillWidth: true
        Layout.maximumWidth: chatRoot.contentMaxWidth
        Layout.alignment: Qt.AlignHCenter
        readonly property real lineH: input.font.pixelSize * 1.5
        readonly property real maxH: chatRoot.height / 4
        // 左下角附件按钮 + 右下角按钮条：按钮 36px + 8px 间距，文本经
        // bottomPadding 避开；有附件预览时再让出预览条高度（预览条在按钮行上方）
        readonly property real buttonStrip: sendButton.height + 8
        readonly property real bottomReserved: inputCard.buttonStrip
            + (attachmentStrip.visible ? attachmentStrip.height + 8 : 0)
        // 视口 = 高度 − topPadding − bottomPadding，故卡片需补上两侧 padding 与 16px 外边距
        implicitHeight: Math.min(
            Math.max(input.contentHeight, 3 * lineH) + input.topPadding + input.bottomPadding + 16,
            maxH)
        radius: theme.radiusM
        color: theme.field
        border.width: input.activeFocus ? 2 : 1
        border.color: input.activeFocus ? theme.accent : theme.fieldBorder

        Behavior on border.color { ColorAnimation { duration: 100 } }

        TextArea {
            id: input
            objectName: "chatInput"
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 8
            placeholderText: chat.streaming ? qsTr("生成中…")
                : settings.sendShortcut === "enter"
                  ? qsTr("输入消息，Enter 发送，Shift+Enter 换行")
                  : qsTr("输入消息，Ctrl+Enter 发送，Enter 换行")
            wrapMode: TextArea.Wrap
            color: theme.text
            font.pixelSize: Math.round(13 * settings.fontScale)
            background: null
            leftPadding: 6
            bottomPadding: inputCard.bottomReserved
            clip: true
            verticalAlignment: TextInput.AlignTop
            Keys.onPressed: (event) => {
                // 剪贴板有图片时 Ctrl+V 转为附件，不再走文本粘贴
    if (event.key === Qt.Key_V && (event.modifiers & Qt.ControlModifier) !== 0
            && chat.clipboardHasImage()) {
        chatRoot.addAttachment(chat.clipboardImageDataUrl(), qsTr("剪贴板图片.png"), true)
        event.accepted = true
        return
    }
                if (event.key !== Qt.Key_Return && event.key !== Qt.Key_Enter)
                    return
                const ctrlHeld = (event.modifiers & Qt.ControlModifier) !== 0
                const plainEnter = (event.modifiers & (Qt.ShiftModifier | Qt.ControlModifier)) === 0
                // ctrl_enter 模式：Ctrl+Enter 发送、Enter 换行
                // enter 模式：Enter 发送、Shift+Enter 换行
                if (ctrlHeld || (settings.sendShortcut === "enter" && plainEnter)) {
                    chatRoot.sendRequested()
                    event.accepted = true
                }
                // 其余组合交给 TextArea 默认行为（插入换行）
            }
        }
        // 附件预览条：缩略图 + 删除，位于底部按钮行上方左侧
        Row {
            id: attachmentStrip
            objectName: "attachmentStrip"
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.leftMargin: 10
            anchors.bottomMargin: inputCard.buttonStrip + 6
            spacing: 6
            visible: chatRoot.attachments.length > 0
            Repeater {
                model: chatRoot.attachments
                delegate: Rectangle {
                    id: stripEntry
                    required property int index
                    required property var modelData
                    width: 52
                    height: 52
                    radius: theme.radiusS
                    color: theme.field
                    clip: true
                    Image {
                        anchors.fill: parent
                        visible: stripEntry.modelData.isImage
                        source: stripEntry.modelData.isImage ? stripEntry.modelData.url : ""
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                    }
                    // 文本文件条目：文件名 chip 代替缩略图
                    Label {
                        anchors.fill: parent
                        anchors.margins: 5
                        visible: !stripEntry.modelData.isImage
                        text: stripEntry.modelData.name
                        color: theme.textDim
                        font.pixelSize: Math.round(10 * settings.fontScale)
                        wrapMode: Text.WrapAnywhere
                        elide: Label.ElideRight
                        maximumLineCount: 4
                        verticalAlignment: Text.AlignVCenter
                    }
                    // 删除角标
                    AbstractButton {
                        anchors.top: parent.top
                        anchors.right: parent.right
                        width: 16
                        height: 16
                        background: Rectangle {
                            radius: 8
                            color: parent.hovered ? theme.error : theme.cardBorder
                        }
                        contentItem: Label {
                            text: "✕"
                            color: "#ffffff"
                            font.pixelSize: Math.round(9 * settings.fontScale)
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        onClicked: chatRoot.removeAttachment(stripEntry.index)
                    }
                }
            }
        }
        // 合并按钮：模型 + 思考强度。文字指示当前模型与思考档位，点击弹出自绘
        // 弹层：上半是按供应商分组的模型清单，下半是思考强度滑块。
        // 选中模型经 chat.selectModel 切换并持久化；下一次发送生效
        AbstractButton {
            id: modelButton
            anchors.right: sendButton.left
            anchors.bottom: parent.bottom
            anchors.margins: 8
            anchors.rightMargin: 6
            implicitHeight: 36
            // 文本宽度用 TextMetrics 度量：elide 的 Label 的 implicitWidth 依赖
            // 自身 width，直接引用会成绑定环
            // 模型文字用显示名（设置里可配）：在激活供应商的清单里按 id 找条目
            readonly property string modelDisplayName: {
                const provider = settings.providers[settings.activeProvider]
                if (!provider)
                    return settings.model
                const models = provider.models.length > 0
                    ? provider.models : [{ "id": provider.model }]
                const entry = models.find(m => m.id === settings.model)
                return entry ? (entry.displayName || entry.id) : settings.model
            }
            readonly property string label: modelDisplayName
                + (chatRoot.thinkingLevel !== "disabled"
                   ? " · " + chatRoot.thinkingLevelLabel(chatRoot.thinkingLevel) : "")
            implicitWidth: Math.max(36, Math.min(modelMetrics.advanceWidth, 160) + 2 * padding)
            padding: 8
            ToolTip.visible: hovered
            ToolTip.text: qsTr("模型与思考强度")

            background: Rectangle {
                radius: 18
                color: modelButton.down ? theme.accentSoft
                     : modelButton.hovered ? theme.accentSoft
                     : "transparent"
                border.color: theme.fieldBorder
                border.width: 1
            }
            contentItem: Label {
                id: modelLabel
                text: modelButton.label
                elide: Text.ElideRight
                color: theme.text
                font.pixelSize: Math.round(12 * settings.fontScale)
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            onClicked: modelMenu.open()
        }
        TextMetrics {
            id: modelMetrics
            font: modelLabel.font
            text: modelButton.label
        }
        // 自绘弹层（不用 Menu：其 contentItem ListView 对非菜单条目的布局
        // 在动态子菜单场景下不可靠，Popup 内布局完全自控）
        Popup {
            id: modelMenu
            parent: modelButton
            // 右缘对齐按钮右缘、向左展开，向上弹出（输入框贴窗口底部）；
            // 显式 x/y 不走 Qt 的自动收边，须自行保证在窗口内
            x: parent.width - width
            y: -height - 8
            width: 264
            padding: 6
            closePolicy: Popup.CloseOnPressOutside | Popup.CloseOnEscape
            // 打开时把滑块对齐到当前档位（交互会解除 value 的声明式绑定，
            // 每次打开重设，避免外部状态变化后错位）
            onAboutToShow:
                thinkingSlider.value = chatRoot.thinkingLevels.indexOf(chatRoot.thinkingLevel)

            background: Rectangle {
                radius: theme.radiusM
                color: theme.card
                border.color: theme.cardBorder
            }

            contentItem: ColumnLayout {
                spacing: 6

                // 模型清单：供应商小标题 + 条目平铺，条目过多时内部滚动
                ScrollView {
                    id: modelsScroll
                    Layout.fillWidth: true
                    Layout.maximumHeight: 340
                    Layout.preferredHeight: modelsColumn.implicitHeight
                    contentWidth: modelsScroll.availableWidth
                    contentHeight: modelsColumn.implicitHeight
                    ScrollBar.vertical: SlimScrollBar {}
                    clip: true

                    ColumnLayout {
                        id: modelsColumn
                        width: modelsScroll.availableWidth
                        spacing: 0

                        Repeater {
                            model: settings.providers
                            delegate: ColumnLayout {
                                id: providerSection
                                required property int index
                                required property var modelData
                                readonly property var providerData: modelData
                                readonly property int providerIndex: index
                                spacing: 0

                                Label {
                                    Layout.fillWidth: true
                                    Layout.leftMargin: 8
                                    Layout.topMargin: 6
                                    text: providerSection.providerData.name
                                    color: theme.textFaint
                                    font.pixelSize: Math.round(11 * settings.fontScale)
                                    elide: Text.ElideRight
                                }
                                Repeater {
                                    // models 条目为 {id, displayName, ...}；清单为空时
                                    // 回退仅含当前模型一项
                                    model: providerSection.providerData.models.length > 0
                                           ? providerSection.providerData.models
                                           : [{ "id": providerSection.providerData.model }]
                                    delegate: AbstractButton {
                                        id: modelItem
                                        required property var modelData
                                        readonly property string modelId: modelData.id
                                        readonly property string modelTitle:
                                            modelData.displayName || modelData.id
                                        readonly property bool current:
                                            settings.activeProvider === providerSection.providerIndex
                                                ? settings.model === modelItem.modelId
                                                : providerSection.providerData.model === modelItem.modelId
                                        Layout.fillWidth: true
                                        implicitHeight: 30
                                        leftPadding: 10
                                        rightPadding: 10

                                        background: Rectangle {
                                            radius: theme.radiusS
                                            color: modelItem.pressed ? theme.accentSoft
                                                 : modelItem.hovered ? theme.accentSoft
                                                 : "transparent"
                                        }
                                        contentItem: RowLayout {
                                            spacing: 6
                                            Label {
                                                Layout.fillWidth: true
                                                text: modelItem.modelTitle
                                                elide: Text.ElideRight
                                                color: modelItem.current ? theme.accent
                                                     : theme.text
                                                font.pixelSize: Math.round(12 * settings.fontScale)
                                            }
                                            Label {
                                                visible: modelItem.current
                                                text: "✓"
                                                color: theme.accent
                                                font.pixelSize: Math.round(12 * settings.fontScale)
                                            }
                                        }
                                        // 单次调用进 C++ 完成切换+保存：若在此逐条改 settings，
                                        // settingsChanged 会重建模型列表、销毁正在执行的
                                        // onClicked 的宿主，引发级联错误——先关弹层，
                                        // 再经 callLater 推迟到事件循环执行
                                        onClicked: {
                                            modelMenu.close()
                                            Qt.callLater(chat.selectModel,
                                                         providerSection.providerIndex,
                                                         modelItem.modelId)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: theme.cardBorder
                }

                // 思考强度滑块：0..4 五档（关闭/低/中/高/最高），会话内临时生效
                Item {
                    Layout.fillWidth: true
                    implicitHeight: 36
                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 8
                        Label {
                            id: thinkingTitleLabel
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("思考")
                            color: theme.textDim
                            font.pixelSize: Math.round(12 * settings.fontScale)
                        }
                        Slider {
                            id: thinkingSlider
                            objectName: "thinkingSlider"
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - thinkingTitleLabel.width
                                   - thinkingValueLabel.width - parent.spacing * 2
                            // 自绘 background/handle 未带 implicit 尺寸时 Control 的
                            // implicitHeight 会解析为 0，Row 不拉伸子项高度 → 滑块
                            // 变成不可见的零高条目，必须显式给高
                            implicitHeight: 24
                            height: 24
                            from: 0
                            to: 4
                            stepSize: 1
                            snapMode: Slider.SnapAlways
                            onMoved: chatRoot.thinkingLevel =
                                     chatRoot.thinkingLevels[Math.round(value)]
                            // 滑块把手视觉：accent 圆点 + 细轨道
                            background: Rectangle {
                                implicitWidth: 120
                                implicitHeight: 4
                                x: thinkingSlider.leftPadding
                                y: thinkingSlider.topPadding
                                     + thinkingSlider.availableHeight / 2 - height / 2
                                width: thinkingSlider.availableWidth
                                height: 4
                                radius: 2
                                color: theme.fieldBorder
                                Rectangle {
                                    width: thinkingSlider.visualPosition * parent.width
                                    height: parent.height
                                    radius: 2
                                    color: theme.accent
                                }
                            }
                            handle: Rectangle {
                                implicitWidth: 14
                                implicitHeight: 14
                                x: thinkingSlider.leftPadding
                                   + thinkingSlider.visualPosition
                                     * (thinkingSlider.availableWidth - width)
                                y: thinkingSlider.topPadding
                                   + thinkingSlider.availableHeight / 2 - height / 2
                                width: 14
                                height: 14
                                radius: 7
                                color: theme.accent
                                border.color: theme.field
                                border.width: 2
                            }
                        }
                        Label {
                            id: thinkingValueLabel
                            anchors.verticalCenter: parent.verticalCenter
                            text: chatRoot.thinkingLevelLabel(chatRoot.thinkingLevel)
                            color: chatRoot.thinkingLevel !== "disabled"
                                   ? theme.accent : theme.textFaint
                            font.pixelSize: Math.round(12 * settings.fontScale)
                            // 定宽让滑块不随档位文字宽度跳动（"最高"最宽）
                            width: Math.round(28 * settings.fontScale)
                        }
                    }
                }
            }
        }
        // 附件选择按钮：输入框左下角
        AbstractButton {
            id: attachButton
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.margins: 8
            implicitWidth: 36
            implicitHeight: 36
            enabled: !chat.streaming
            ToolTip.visible: hovered
            ToolTip.text: qsTr("附加文件")

            background: Rectangle {
                radius: 18
                color: attachButton.down ? theme.accentSoft
                     : attachButton.hovered ? theme.accentSoft
                     : "transparent"
                border.color: theme.fieldBorder
                border.width: 1
            }
            contentItem: Label {
                text: "📎"
                font.pixelSize: Math.round(14 * settings.fontScale)
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            onClicked: imageDialog.open()
        }
        // 用量统计：输入框底部按钮左侧
        Label {
            anchors.right: modelButton.left
            anchors.rightMargin: 6
            anchors.verticalCenter: modelButton.verticalCenter
            visible: text.length > 0
            text: chatRoot.formatUsageSummary()
            color: theme.textDim
            font.pixelSize: Math.round(11 * settings.fontScale)
            elide: Text.ElideRight
            // 限宽避免挤压按钮条：右缘固定在 modelButton 左侧，左界到附件按钮；
            // 窗口过窄时可用空间为负，钳到 0
            width: Math.max(0, Math.min(implicitWidth,
                            modelButton.x - attachButton.x - attachButton.width - 12))
        }

        // 圆形发送/停止按钮：不挤占文本宽度
        AbstractButton {
            id: sendButton
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 8
            implicitWidth: 36
            implicitHeight: 36
            enabled: chat.streaming || input.text.trim().length > 0
            ToolTip.visible: hovered
            ToolTip.text: chat.streaming ? qsTr("停止") : qsTr("发送")

            background: Rectangle {
                radius: 18
                color: !sendButton.enabled ? theme.fieldBorder
                     : chat.streaming ? theme.error
                     : sendButton.down ? theme.accentPressed
                     : sendButton.hovered ? theme.accentHover
                     : theme.accent

                Behavior on color { ColorAnimation { duration: 100 } }
            }
            contentItem: Label {
                text: chat.streaming ? "⏹" : "➤"
                color: sendButton.enabled ? "#ffffff" : theme.textFaint
                font.pixelSize: Math.round(14 * settings.fontScale)
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            onClicked: chat.streaming ? chatRoot.stopRequested()
                                      : chatRoot.sendRequested()
        }

        // 原生系统文件对话框（Qt.labs.platform 走各平台原生对话框实现，
        // QtQuick.Dialogs 在 Linux 无 portal 时会回落 Qt 自绘对话框）
        NativeDialogs.FileDialog {
            id: imageDialog
            fileMode: NativeDialogs.FileDialog.OpenFiles
            nameFilters: [qsTr("图片文件 (*.png *.jpg *.jpeg *.gif *.webp *.bmp)"),
                          qsTr("文本文件 (*.txt *.md *.json *.xml *.yaml *.yml *.toml *.ini "
                               + "*.csv *.log *.html *.css *.js *.ts *.py *.c *.h *.cpp *.hpp "
                               + "*.qml *.sh *.cmake)"),
                          qsTr("所有文件 (*)")]
            onAccepted: {
                // 条目分类（isImage）按扩展名预判，发送时由 C++ 嗅探纠正
                for (const url of imageDialog.files) {
                    const name = decodeURIComponent(String(url).split("/").pop())
                    const isImage = /\.(png|jpe?g|gif|webp|bmp)$/i.test(name)
                    chatRoot.addAttachment(url, name, isImage)
                }
            }
        }
    }

    // 空会话时的下方弹性 spacer：与上方 spacer 等分剩余空间，使输入框居中
    Item {
        visible: messageList.count === 0
        Layout.fillWidth: true
        Layout.fillHeight: messageList.count === 0
    }
}
