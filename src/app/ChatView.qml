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
        // 右下角按钮条：按钮 36px + 8px 间距，文本经 bottomPadding 避开；
        // 有附件预览时再让出预览条高度
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
        // 附件预览条：缩略图 + 删除，位于文本下方、按钮条上方左侧
        Row {
            id: attachmentStrip
            objectName: "attachmentStrip"
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.leftMargin: 10
            anchors.bottomMargin: 10
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
        // 模型切换按钮：文字指示当前模型，菜单按供应商分组（子菜单 = 供应商）。
        // 选中经 chat.selectModel 切换并持久化；下一次发送生效
        AbstractButton {
            id: modelButton
            anchors.right: thinkingButton.left
            anchors.bottom: parent.bottom
            anchors.margins: 8
            anchors.rightMargin: 6
            implicitHeight: 36
            // 文本宽度用 TextMetrics 度量：elide 的 Label 的 implicitWidth 依赖
            // 自身 width，直接引用会成绑定环
            implicitWidth: Math.max(36, Math.min(modelMetrics.advanceWidth, 140) + 2 * padding)
            padding: 8
            ToolTip.visible: hovered
            ToolTip.text: qsTr("切换模型")

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
                text: settings.model
                elide: Text.ElideRight
                color: theme.text
                font.pixelSize: Math.round(12 * settings.fontScale)
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            onClicked: modelMenu.popup(modelButton, 0, modelButton.height)
        }
        TextMetrics {
            id: modelMetrics
            font: modelLabel.font
            text: settings.model
        }
        Menu {
            id: modelMenu
            Instantiator {
                model: settings.providers
                delegate: Menu {
                    id: providerMenu
                    required property int index
                    required property var modelData
                    title: modelData.name
                    Instantiator {
                        model: providerMenu.modelData.models.length > 0
                               ? providerMenu.modelData.models
                               : [providerMenu.modelData.model]
                        delegate: MenuItem {
                            required property string modelData
                            text: modelData
                            checkable: true
                            // 激活供应商勾选当前模型，其余供应商勾选各自保存的模型
                            checked: settings.activeProvider === providerMenu.index
                                         ? settings.model === modelData
                                         : providerMenu.modelData.model === modelData
                            // 单次调用进 C++ 完成切换+保存：若在此逐条改 settings，
                            // settingsChanged 触发菜单重建会销毁本 delegate（正在执行的
                            // onTriggered 的宿主），引发级联错误
                            onTriggered: chat.selectModel(providerMenu.index, modelData)
                        }
                        onObjectAdded: (index, object) => providerMenu.insertItem(index, object)
                        onObjectRemoved: (index, object) => providerMenu.removeItem(object)
                    }
                }
                onObjectAdded: (index, object) => modelMenu.insertMenu(index, object)
                onObjectRemoved: (index, object) => modelMenu.removeMenu(object)
            }
        }
        // 思考模式按钮：文字直接指示当前档位，点击弹菜单切换；会话内临时生效
        AbstractButton {
            id: thinkingButton
            anchors.right: attachButton.left
            anchors.bottom: parent.bottom
            anchors.margins: 8
            anchors.rightMargin: 6
            implicitHeight: 36
            implicitWidth: Math.max(36, thinkingLabel.implicitWidth + padding * 2)
            padding: 8
            ToolTip.visible: hovered
            ToolTip.text: qsTr("思考模式：%1").arg(thinkingMenu.currentLabel)

            background: Rectangle {
                radius: 18
                color: thinkingButton.down ? theme.accentSoft
                     : thinkingButton.hovered ? theme.accentSoft
                     : chatRoot.thinkingLevel !== "disabled" ? theme.accentSoft
                     : "transparent"
                border.color: theme.fieldBorder
                border.width: 1
            }
            contentItem: Label {
                id: thinkingLabel
                text: thinkingMenu.currentLabel
                color: chatRoot.thinkingLevel !== "disabled" ? theme.accent : theme.textFaint
                font.pixelSize: Math.round(12 * settings.fontScale)
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            onClicked: thinkingMenu.popup(thinkingButton, 0, thinkingButton.height)
        }
        Menu {
            id: thinkingMenu
            readonly property string currentLabel: {
                if (chatRoot.thinkingLevel === "low") return qsTr("低")
                if (chatRoot.thinkingLevel === "medium") return qsTr("中")
                if (chatRoot.thinkingLevel === "high") return qsTr("高")
                if (chatRoot.thinkingLevel === "max") return qsTr("最高")
                return qsTr("关闭")
            }
            component ThinkingMenuItem : MenuItem {
                property string level
                text: level === "disabled" ? qsTr("关闭")
                    : level === "low" ? qsTr("低")
                    : level === "medium" ? qsTr("中")
                    : level === "high" ? qsTr("高")
                    : qsTr("最高")
                checkable: true
                checked: chatRoot.thinkingLevel === level
                onTriggered: chatRoot.thinkingLevel = level
            }
            ThinkingMenuItem { level: "disabled" }
            ThinkingMenuItem { level: "low" }
            ThinkingMenuItem { level: "medium" }
            ThinkingMenuItem { level: "high" }
            ThinkingMenuItem { level: "max" }
        }
        // 附件选择按钮：sendButton 左侧
        AbstractButton {
            id: attachButton
            anchors.right: sendButton.left
            anchors.bottom: parent.bottom
            anchors.margins: 8
            anchors.rightMargin: 6
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
            // 限宽避免挤压按钮条：右缘固定在 modelButton 左侧，左界到附件按钮
            width: Math.min(implicitWidth, attachButton.x - 16)
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
