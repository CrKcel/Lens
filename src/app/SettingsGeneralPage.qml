import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 常规页：语言 / 内置工具 / 环境信息 / 系统提示词。
// view 回引 SettingsView（提交链路与工作副本所在），字段经 alias 暴露给提交链路。
ScrollView {
    id: generalPage

    required property var view

    property alias languageCombo: languageCombo
    property alias toolPresetCombo: toolPresetCombo
    property alias maxRetriesCombo: maxRetriesCombo
    property alias systemPromptField: systemPromptField
    property alias environmentPromptCheck: environmentPromptCheck

    contentWidth: availableWidth
    contentHeight: generalContent.implicitHeight
    ScrollBar.vertical: SlimScrollBar {}

    Theme {
        id: theme
        dark: settings.dark
    }

    ColumnLayout {
        id: generalContent
        width: generalPage.availableWidth
        spacing: 12

        SettingsSection {
            Layout.fillWidth: true
            title: qsTr("语言")

            SettingsRow {
                Layout.fillWidth: true
                label: qsTr("界面语言")
                LensComboBox {
                    id: languageCombo
                    Layout.preferredWidth: 170
                    textRole: "text"
                    valueRole: "value"
                    onActivated: generalPage.view.commitSettings()
                    model: [
                        { text: qsTr("跟随系统"), value: "system" },
                        { text: qsTr("中文"), value: "zh" },
                        { text: qsTr("English"), value: "en" }
                    ]
                }
            }
        }

        SettingsSection {
            Layout.fillWidth: true
            title: qsTr("内置工具")
            hint: qsTr("联网搜索由供应商的服务端搜索能力提供，在模型提供商页按供应商开启")

            SettingsRow {
                Layout.fillWidth: true
                label: qsTr("预设")
                LensComboBox {
                    id: toolPresetCombo
                    Layout.preferredWidth: 220
                    textRole: "text"
                    valueRole: "value"
                    model: [
                        { text: qsTr("完整（全部工具）"), value: "full" },
                        { text: qsTr("对话（无内置工具）"), value: "chat" },
                        { text: qsTr("只读（仅 read）"), value: "read_only" },
                        { text: qsTr("自定义"), value: "custom" }
                    ]
                    onActivated: {
                        if (currentValue === "custom" && generalPage.view.customToolsWorking.length === 0) {
                            // 从 full 切到 custom：默认与 full 一致，避免空清单禁掉所有工具
                            generalPage.view.customToolsWorking =
                                chat.contextTools.filter(t => t.origin === "内置").map(t => t.name)
                        }
                        generalPage.view.commitSettings()
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
                            generalPage.view.customToolsWorking.indexOf(modelData.name) >= 0
                        text: modelData.name
                              + (modelData.description.length > 0
                                 ? "　— " + modelData.description : "")
                        checked: enabledInCopy
                        onEnabledInCopyChanged: checked = enabledInCopy
                        onToggled: generalPage.view.setCustomToolEnabled(modelData.name, checked)

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
            title: qsTr("自动重试")
            hint: qsTr("连接断开或供应商繁忙（限流/过载/超时）时按退避自动重发，鉴权等失败不重试")

            SettingsRow {
                Layout.fillWidth: true
                label: qsTr("重试次数")
                LensComboBox {
                    id: maxRetriesCombo
                    Layout.preferredWidth: 170
                    textRole: "text"
                    valueRole: "value"
                    onActivated: generalPage.view.commitSettings()
                    model: [
                        { text: qsTr("关闭"), value: 0 },
                        { text: qsTr("1 次"), value: 1 },
                        { text: qsTr("2 次"), value: 2 },
                        { text: qsTr("3 次"), value: 3 },
                        { text: qsTr("5 次"), value: 5 }
                    ]
                }
            }
        }

        SettingsSection {
            Layout.fillWidth: true
            title: qsTr("环境信息")
            hint: qsTr("向模型注入运行环境（操作系统、日期、工作文件夹、Git 状态）")

            SettingsRow {
                Layout.fillWidth: true
                label: qsTr("注入环境提示词")
                ToggleSwitch {
                    id: environmentPromptCheck
                    checked: true
                    onToggled: if (!generalPage.view.loadingFields) generalPage.view.commitSettings()
                }
            }
        }

        SettingsSection {
            Layout.fillWidth: true
            title: qsTr("系统提示词")
            hint: qsTr("留空则不注入任何系统提示词")

            TextArea {
                id: systemPromptField
                Layout.fillWidth: true
                Layout.preferredHeight: Math.round(160 * settings.fontScale)
                wrapMode: TextArea.Wrap
                color: theme.text
                background: SettingFieldBg {}
                onTextEdited: generalPage.view.scheduleCommit()
            }
        }
    }
}
