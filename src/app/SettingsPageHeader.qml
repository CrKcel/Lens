import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 设置页页头：分类标题 + 一句话说明（说明为空则不显示）
ColumnLayout {
    id: pageHeader

    property alias title: pageTitle.text
    property alias description: pageDesc.text
    spacing: 2

    Theme {
        id: headerTheme
        dark: settings.dark
    }

    Label {
        id: pageTitle
        color: headerTheme.text
        font.pixelSize: Math.round(18 * settings.fontScale)
        font.bold: true
    }
    Label {
        id: pageDesc
        visible: text.length > 0
        color: headerTheme.textDim
        font.pixelSize: Math.round(12 * settings.fontScale)
    }
}
