import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 字段组：标签在上，控件在下
ColumnLayout {
    id: settingField

    default property alias content: fieldControls.data
    property alias label: fieldLabel.text
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
    }
    RowLayout {
        id: fieldControls
        Layout.fillWidth: true
        Layout.fillHeight: true
        spacing: 8
    }
}
