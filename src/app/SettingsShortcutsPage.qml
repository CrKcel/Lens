import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 快捷键页：发送方式可配置，其余固定。
// view 回引 SettingsView（提交链路所在），sendShortcutCombo 经 alias 暴露给提交链路。
ScrollView {
    id: shortcutsPage

    required property var view

    property alias sendShortcutCombo: sendShortcutCombo

    contentWidth: availableWidth
    contentHeight: shortcutsContent.implicitHeight
    ScrollBar.vertical: SlimScrollBar {}

    Theme {
        id: theme
        dark: settings.dark
    }

    // 键帽
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

    ColumnLayout {
        id: shortcutsContent
        width: shortcutsPage.availableWidth
        spacing: 12

        SettingsSection {
            Layout.fillWidth: true
            title: qsTr("发送消息")

            SettingsRow {
                Layout.fillWidth: true
                label: qsTr("发送方式")
                LensComboBox {
                    id: sendShortcutCombo
                    Layout.preferredWidth: 320
                    textRole: "text"
                    valueRole: "value"
                    onActivated: shortcutsPage.view.commitSettings()
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
