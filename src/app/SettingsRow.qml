import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 设置行：标签居左、控件居右
RowLayout {
    id: settingRow

    default property alias content: rowControls.data
    property alias label: rowLabel.text
    spacing: 12

    Theme {
        id: rowTheme
        dark: settings.dark
    }

    Label {
        id: rowLabel
        color: rowTheme.text
        font.pixelSize: Math.round(13 * settings.fontScale)
    }
    Item { Layout.fillWidth: true }
    RowLayout {
        id: rowControls
        spacing: 8
    }
}
