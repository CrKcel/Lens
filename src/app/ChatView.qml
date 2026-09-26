import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 聊天主区：标题行（含流式状态与用量）、消息流、输入区、上下文检查器。
// 发送动作由 Main.sendAction 统一处理（工作文件夹在侧栏）。
ColumnLayout {
    id: chatRoot

    readonly property alias inputText: input.text

    signal sendRequested()
    signal stopRequested()

    anchors.margins: 16
    spacing: 10

    function clearInput() {
        input.clear()
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

    RowLayout {
        Layout.fillWidth: true
        spacing: 10
        Label {
            Layout.fillWidth: true
            text: chat.currentConversationId === 0
                  ? qsTr("新会话")
                  : chat.currentTitle
            color: theme.text
            font.pixelSize: 16
            font.bold: true
            elide: Text.ElideRight
        }
        // 生成中状态：accent 药丸 + 呼吸动画
        Rectangle {
            visible: chat.streaming
            implicitWidth: streamingLabel.implicitWidth + 18
            implicitHeight: 24
            radius: 12
            color: theme.accentSoft

            Label {
                id: streamingLabel
                anchors.centerIn: parent
                text: qsTr("生成中…")
                color: theme.accent
                font.pixelSize: 11

                SequentialAnimation on opacity {
                    running: chat.streaming
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.4; duration: 700; easing.type: Easing.InOutQuad }
                    NumberAnimation { to: 1.0; duration: 700; easing.type: Easing.InOutQuad }
                }
            }
        }
        Label {
            visible: text.length > 0
            text: chatRoot.formatUsageSummary()
            color: theme.textDim
            font.pixelSize: 11
        }
        ToolButton {
            text: qsTr("上下文")
            flat: true
            font.pixelSize: 12
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
                        implicitWidth: Math.min(userText.implicitWidth + 24,
                                                messageList.width * 0.72, 560)
                        implicitHeight: userText.implicitHeight + 22
                        radius: theme.radiusM
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: theme.bubbleUser }
                            GradientStop { position: 1.0; color: theme.bubbleUser2 }
                        }
                        Label {
                            id: userText
                            anchors.fill: parent
                            anchors.margins: 11
                            text: model.text
                            color: theme.bubbleUserText
                            wrapMode: Text.Wrap
                            font.pixelSize: 13
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
                            font.pixelSize: 13
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
                                    font.pixelSize: 12
                                    font.bold: true
                                }
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
                            font.pixelSize: 12
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
                    font.pixelSize: 20
                }
            }
            Label {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("在「设置」中填入 API 地址与 Key\n发送第一条消息即自动创建会话")
                horizontalAlignment: Text.AlignHCenter
                color: theme.textFaint
                font.pixelSize: 12
            }
        }
    }

    // ── 输入区：圆角卡片，文本占满整行宽度，发送按钮固定右下角 ─────────
    // 默认三行文本高度 + 底部按钮条，随内容增长，最高不超过窗口四分之一
    Rectangle {
        id: inputCard
        objectName: "inputCard"
        Layout.fillWidth: true
        readonly property real lineH: input.font.pixelSize * 1.5
        readonly property real maxH: chatRoot.height / 4
        // 右下角按钮条：按钮 36px + 8px 间距，文本经 bottomPadding 避开
        readonly property real buttonStrip: sendButton.height + 8
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
            font.pixelSize: 13
            background: null
            leftPadding: 6
            bottomPadding: inputCard.buttonStrip
            clip: true
            verticalAlignment: TextInput.AlignTop
            Keys.onPressed: (event) => {
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
                font.pixelSize: 14
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            onClicked: chat.streaming ? chatRoot.stopRequested()
                                      : chatRoot.sendRequested()
        }
    }

    // 空会话时的下方弹性 spacer：与上方 spacer 等分剩余空间，使输入框居中
    Item {
        visible: messageList.count === 0
        Layout.fillWidth: true
        Layout.fillHeight: messageList.count === 0
    }

    ContextInspector {
        id: contextPopup
        formatTokens: chatRoot.formatTokens
    }
}
