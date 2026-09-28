import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.platform as Labs

// 外观页：主题 / 配色 / 阅读体验 / 调色板。
// view 回引 SettingsView（提交链路所在），字段经 alias 暴露给提交链路。
ScrollView {
    id: appearancePage

    required property var view

    property alias themeCombo: themeCombo
    property alias fontScaleCombo: fontScaleCombo
    property alias lineSpacingCombo: lineSpacingCombo

    // 调色板可编辑的颜色 token：key 与 Theme.qml 的属性名一一对应
    // （与 Theme.qml 属性名、AppSettings::isValidPaletteToken 三处一起改）
    readonly property var paletteTokens: [
        { key: "background", text: qsTr("背景") },
        { key: "surface", text: qsTr("表面") },
        { key: "sidebar", text: qsTr("侧栏") },
        { key: "field", text: qsTr("输入框") },
        { key: "fieldBorder", text: qsTr("输入框边框") },
        { key: "card", text: qsTr("卡片") },
        { key: "cardBorder", text: qsTr("卡片边框") },
        { key: "highlight", text: qsTr("高亮") },
        { key: "text", text: qsTr("正文文本") },
        { key: "textSoft", text: qsTr("次要文本") },
        { key: "textDim", text: qsTr("弱化文本") },
        { key: "textFaint", text: qsTr("最弱文本") },
        { key: "accent", text: qsTr("强调色") },
        { key: "accentHover", text: qsTr("强调色悬停") },
        { key: "accentPressed", text: qsTr("强调色按下") },
        { key: "accentSoft", text: qsTr("柔和强调底色") },
        { key: "accentBorder", text: qsTr("强调色边框") },
        { key: "success", text: qsTr("成功") },
        { key: "error", text: qsTr("错误") },
        { key: "errorSoft", text: qsTr("错误底色") },
        { key: "bubbleUser", text: qsTr("用户气泡") },
        { key: "bubbleUser2", text: qsTr("用户气泡渐变") },
        { key: "bubbleUserText", text: qsTr("气泡文字") },
        { key: "divider", text: qsTr("分隔线") }
    ]
    readonly property bool hasColorOverrides: {
        const overrides = settings.colorOverrides
        return Object.keys(overrides).some(
            mode => Object.keys(overrides[mode] ?? {}).length > 0)
    }

    contentWidth: availableWidth
    contentHeight: appearanceContent.implicitHeight
    ScrollBar.vertical: SlimScrollBar {}

    Theme {
        id: theme
        dark: settings.dark
    }

    // 调色板色块：展示某 token 在深/浅主题下的生效颜色，点击弹颜色选择器修改
    component PaletteSwatch: Rectangle {
        id: paletteSwatch

        property string mode
        property string token
        property color value

        width: 22
        height: 22
        radius: 6
        color: value
        border.color: paletteHover.hovered ? theme.accent : theme.fieldBorder
        border.width: paletteHover.hovered ? 2 : 1

        HoverHandler { id: paletteHover }
        TapHandler {
            onTapped: appearancePage.pickPaletteColor(paletteSwatch.mode,
                                                      paletteSwatch.token,
                                                      paletteSwatch.value)
        }
        ToolTip.visible: paletteHover.hovered
        ToolTip.delay: 400
        ToolTip.text: (paletteSwatch.mode === "dark" ? qsTr("深色") : qsTr("浅色"))
                      + " · " + paletteSwatch.value
    }

    // 原生颜色选择器（Qt.labs.platform）：选中即写覆盖并持久化
    Labs.ColorDialog {
        id: paletteColorDialog

        property string mode
        property string token

        title: qsTr("选择颜色")
        onAccepted: appearancePage.view.commitColorOverride(mode, token, String(color))
    }

    // 调色板色块：弹出颜色选择器修改对应 token（mode = dark | light）
    function pickPaletteColor(mode, token, value) {
        paletteColorDialog.mode = mode
        paletteColorDialog.token = token
        paletteColorDialog.color = value
        paletteColorDialog.open()
    }

    ColumnLayout {
        id: appearanceContent
        width: appearancePage.availableWidth
        spacing: 12

        SettingsSection {
            Layout.fillWidth: true
            title: qsTr("主题与配色")

            SettingsRow {
                Layout.fillWidth: true
                label: qsTr("主题")
                LensComboBox {
                    id: themeCombo
                    Layout.preferredWidth: 170
                    textRole: "text"
                    valueRole: "value"
                    onActivated: appearancePage.view.commitSettings()
                    model: [
                        { text: qsTr("跟随系统"), value: "system" },
                        { text: qsTr("深色"), value: "dark" },
                        { text: qsTr("浅色"), value: "light" }
                    ]
                }
            }
        }

        SettingsSection {
            Layout.fillWidth: true
            title: qsTr("阅读体验")

            SettingsRow {
                Layout.fillWidth: true
                label: qsTr("字体大小")
                LensComboBox {
                    id: fontScaleCombo
                    Layout.preferredWidth: 170
                    textRole: "text"
                    valueRole: "value"
                    onActivated: appearancePage.view.commitSettings()
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
                LensComboBox {
                    id: lineSpacingCombo
                    Layout.preferredWidth: 170
                    textRole: "text"
                    valueRole: "value"
                    onActivated: appearancePage.view.commitSettings()
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
            title: qsTr("调色板")

            Theme {
                id: darkPreviewTheme
                dark: true
            }
            Theme {
                id: lightPreviewTheme
                dark: false
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6

                Repeater {
                    model: appearancePage.paletteTokens

                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 8

                        Label {
                            text: modelData.text
                            color: theme.text
                            font.pixelSize: Math.round(12 * settings.fontScale)
                        }
                        Item { Layout.fillWidth: true }
                        PaletteSwatch {
                            mode: "dark"
                            token: modelData.key
                            value: darkPreviewTheme.tokens[modelData.key]
                        }
                        PaletteSwatch {
                            mode: "light"
                            token: modelData.key
                            value: lightPreviewTheme.tokens[modelData.key]
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                AccentButton {
                    enabled: appearancePage.hasColorOverrides
                    text: qsTr("恢复默认调色板")
                    onClicked: appearancePage.view.resetColorOverrides()
                }
            }
        }
    }
}
