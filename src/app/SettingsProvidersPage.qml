import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 模型提供商页：供应商列表 / 基本信息 / API 接入 / 模型编辑器 / 计费。
// view 回引 SettingsView（提交链路、模型清单工作副本与拉取状态所在），
// 字段经 alias 暴露给提交链路。
ScrollView {
    id: providersPage

    required property var view

    property alias providerNameField: providerNameField
    property alias protocolCombo: protocolCombo
    property alias endpointField: endpointField
    property alias apiKeyField: apiKeyField
    property alias serverSearchCheck: serverSearchCheck
    property alias inputPriceField: inputPriceField
    property alias outputPriceField: outputPriceField
    property alias cachedPriceField: cachedPriceField
    property alias modelSelect: modelSelect
    property alias modelDisplayNameField: modelDisplayNameField
    property alias modelIdField: modelIdField
    property alias modelContextField: modelContextField
    property alias modelMaxOutputField: modelMaxOutputField
    property alias modelImagesCheck: modelImagesCheck

    contentWidth: availableWidth
    contentHeight: providersContent.implicitHeight
    ScrollBar.vertical: SlimScrollBar {}

    Theme {
        id: theme
        dark: settings.dark
    }

    ColumnLayout {
        id: providersContent
        width: providersPage.availableWidth
        spacing: 12

        // 供应商选择：横向条目 + 新增/删除
        SettingsSection {
            Layout.fillWidth: true
            title: qsTr("供应商")

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                ListView {
                    id: providerList
                    Layout.fillWidth: true
                    Layout.preferredHeight: 36
                    orientation: ListView.Horizontal
                    clip: true
                    spacing: 4
                    model: settings.providers
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.horizontal: SlimScrollBar {}

                    delegate: ItemDelegate {
                        width: providerNameLabel.implicitWidth + 24
                        height: ListView.view ? ListView.view.height : 36
                        highlighted: index === settings.activeProvider
                        onClicked: providersPage.view.switchProvider(index)
                        background: Rectangle {
                            radius: theme.radiusS
                            color: parent.highlighted ? theme.accentSoft
                                 : parent.hovered ? theme.highlight
                                 : "transparent"
                        }
                        contentItem: Label {
                            id: providerNameLabel
                            text: modelData.name
                            color: highlighted ? theme.accent : theme.text
                            elide: Text.ElideRight
                            font.pixelSize: Math.round(13 * settings.fontScale)
                        }
                    }
                }

                GhostButton {
                    text: qsTr("＋")
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("新增供应商")
                    onClicked: providersPage.view.addProviderFromFields(
                                   qsTr("供应商%1").arg(settings.providers.length + 1))
                }
                GhostButton {
                    text: qsTr("－")
                    enabled: settings.providers.length > 1
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("删除当前供应商")
                    onClicked: providersPage.view.removeActiveProvider()
                }
            }
        }

        SettingsSection {
            Layout.fillWidth: true
            title: qsTr("基本信息")

            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                SettingsField {
                    Layout.fillWidth: true
                    label: qsTr("名称")
                    SettingsTextField {
                        id: providerNameField
                        Layout.fillWidth: true
                        onTextEdited: providersPage.view.scheduleCommit()
                    }
                }
                SettingsField {
                    Layout.preferredWidth: 220
                    label: qsTr("协议")
                    LensComboBox {
                        id: protocolCombo
                        Layout.fillWidth: true
                        textRole: "text"
                        valueRole: "value"
                        onActivated: providersPage.view.commitSettings()
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
                onToggled: if (!providersPage.view.loadingFields) providersPage.view.commitSettings()
                contentItem: Label {
                    text: serverSearchCheck.text
                    color: theme.textDim
                    font.pixelSize: Math.round(12 * settings.fontScale)
                    leftPadding: serverSearchCheck.indicator.width + 4
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        SettingsSection {
            Layout.fillWidth: true
            title: qsTr("API 接入")

            SettingsField {
                Layout.fillWidth: true
                label: qsTr("API 地址")
                SettingsTextField {
                    id: endpointField
                    Layout.fillWidth: true
                    onTextEdited: providersPage.view.scheduleCommit()
                }
            }
            SettingsField {
                Layout.fillWidth: true
                label: qsTr("API Key")
                SettingsTextField {
                    id: apiKeyField
                    Layout.fillWidth: true
                    echoMode: TextInput.Password
                    onTextEdited: providersPage.view.scheduleCommit()
                }
            }
        }
        // 模型编辑器：清单条目 + 每模型的显示与能力参数
        SettingsSection {
            Layout.fillWidth: true
            title: qsTr("模型")

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                LensComboBox {
                    id: modelSelect
                    Layout.fillWidth: true
                    // activated：切换编辑目标（先落盘当前字段再换）
                    onActivated: {
                        providersPage.view.flushModelFields()
                        providersPage.view.modelsSelected = currentIndex
                        providersPage.view.loadModelFields()
                    }
                }
                GhostButton {
                    text: qsTr("＋")
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("新增模型")
                    onClicked: providersPage.view.addModel()
                }
                GhostButton {
                    text: qsTr("－")
                    enabled: providersPage.view.modelsWorking.length > 0
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("删除当前选中的模型")
                    onClicked: providersPage.view.removeModel()
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                SettingsField {
                    Layout.fillWidth: true
                    label: qsTr("显示名称")
                    SettingsTextField {
                        id: modelDisplayNameField
                        Layout.fillWidth: true
                        onTextEdited: providersPage.view.scheduleCommit()
                    }
                }
                SettingsField {
                    Layout.fillWidth: true
                    label: qsTr("模型 ID")
                    SettingsTextField {
                        id: modelIdField
                        Layout.fillWidth: true
                        onTextEdited: providersPage.view.scheduleCommit()
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                SettingsField {
                    Layout.preferredWidth: 150
                    label: qsTr("上下文窗口")
                    SettingsTextField {
                        id: modelContextField
                        Layout.fillWidth: true
                        inputMethodHints: Qt.ImhDigitsOnly
                        onTextEdited: providersPage.view.scheduleCommit()
                    }
                }
                SettingsField {
                    Layout.preferredWidth: 150
                    label: qsTr("最大输出 Token")
                    SettingsTextField {
                        id: modelMaxOutputField
                        Layout.fillWidth: true
                        inputMethodHints: Qt.ImhDigitsOnly
                        onTextEdited: providersPage.view.scheduleCommit()
                    }
                }
                CheckBox {
                    id: modelImagesCheck
                    text: qsTr("启用图片输入")
                    font.pixelSize: Math.round(12 * settings.fontScale)
                    onToggled: if (!providersPage.view.loadingFields) providersPage.view.commitSettings()
                    contentItem: Label {
                        text: modelImagesCheck.text
                        color: theme.textDim
                        font.pixelSize: Math.round(12 * settings.fontScale)
                        leftPadding: modelImagesCheck.indicator.width + 4
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                Item { Layout.fillWidth: true }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                AccentButton {
                    text: qsTr("设为当前模型")
                    enabled: providersPage.view.modelsWorking.length > 0
                    onClicked: providersPage.view.setCurrentModel()
                }
                AccentButton {
                    text: chat.fetchingModels ? qsTr("获取中…") : qsTr("获取模型列表")
                    enabled: !chat.fetchingModels && endpointField.text.trim().length > 0
                    onClicked: providersPage.view.fetchModels()
                }
                Label {
                    Layout.fillWidth: true
                    text: providersPage.view.currentModelWorking.length > 0
                          ? qsTr("当前模型：%1").arg(providersPage.view.currentModelWorking)
                          : qsTr("尚未选择当前模型")
                    color: theme.textFaint; font.pixelSize: Math.round(11 * settings.fontScale)
                    elide: Text.ElideMiddle
                    horizontalAlignment: Text.AlignRight
                }
            }
            Label {
                visible: providersPage.view.modelsFetchStatus.length > 0
                text: providersPage.view.modelsFetchStatus
                color: theme.textFaint; font.pixelSize: Math.round(11 * settings.fontScale)
                elide: Text.ElideRight
            }
        }
        SettingsSection {
            Layout.fillWidth: true
            title: qsTr("计费")

            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                SettingsField {
                    Layout.fillWidth: true
                    Layout.maximumWidth: 140
                    label: qsTr("输入单价")
                    SettingsTextField {
                        id: inputPriceField
                        Layout.fillWidth: true
                        onTextEdited: providersPage.view.scheduleCommit()
                    }
                }
                SettingsField {
                    Layout.fillWidth: true
                    Layout.maximumWidth: 140
                    label: qsTr("输出单价")
                    SettingsTextField {
                        id: outputPriceField
                        Layout.fillWidth: true
                        onTextEdited: providersPage.view.scheduleCommit()
                    }
                }
                SettingsField {
                    Layout.fillWidth: true
                    Layout.maximumWidth: 140
                    label: qsTr("缓存单价")
                    SettingsTextField {
                        id: cachedPriceField
                        Layout.fillWidth: true
                        onTextEdited: providersPage.view.scheduleCommit()
                    }
                }
                Item { Layout.fillWidth: true }
            }
        }
    }
}
