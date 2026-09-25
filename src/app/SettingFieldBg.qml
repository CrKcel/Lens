import QtQuick

Rectangle {
    implicitHeight: 34
    radius: 6
    color: theme.field
    border.color: theme.fieldBorder

    Theme {
        id: theme
        dark: settings.dark
    }
}
