import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 字段组：标签在上（可带行内说明），控件在下
ColumnLayout {
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
