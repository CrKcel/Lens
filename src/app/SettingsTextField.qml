import QtQuick
import QtQuick.Controls

// 设置页文本字段基座：统一配色与背景；编辑事件由使用方经
// onTextEdited 接到 SettingsView 的防抖提交（view.scheduleCommit()）
TextField {
    id: control

    color: fieldTheme.text
    selectByMouse: true
    background: SettingFieldBg {}

    Theme {
        id: fieldTheme
        dark: settings.dark
    }
}
