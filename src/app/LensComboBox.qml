import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects

ComboBox {
    id: control

    implicitHeight: 36
    leftPadding: 12
    rightPadding: 34

    background: Rectangle {
        implicitWidth: 140
        radius: theme.radiusS
        color: theme.field
        border.width: control.open || control.visualFocus ? 2 : 1
        border.color: control.open || control.visualFocus ? theme.accent
                     : control.hovered ? theme.textFaint
                     : theme.fieldBorder

        Behavior on border.color { ColorAnimation { duration: 100 } }
    }

    contentItem: Label {
        text: control.displayText
        font: control.font
        color: theme.text
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }

    indicator: Canvas {
        id: chevron
        x: control.width - width - 12
        y: (control.height - height) / 2
        width: 12
        height: 8
        rotation: control.open ? 180 : 0

        Behavior on rotation { NumberAnimation { duration: 120 } }

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.strokeStyle = theme.textDim
            ctx.lineWidth = 1.6
            ctx.lineCap = "round"
            ctx.lineJoin = "round"
            ctx.beginPath()
            ctx.moveTo(1, 1)
            ctx.lineTo(width / 2, height - 1)
            ctx.lineTo(width - 1, 1)
            ctx.stroke()
        }
        Component.onCompleted: requestPaint()
        Connections {
            target: theme
            function onDarkChanged() { chevron.requestPaint() }
        }
    }

    delegate: ItemDelegate {
        id: itemDelegate
        required property var model
        required property int index
        width: ListView.view.width
        height: 32
        leftPadding: 10
        rightPadding: 10
        highlighted: control.highlightedIndex === index
        text: control.textRole ? model[control.textRole] : model.modelData

        background: Rectangle {
            radius: theme.radiusS
            color: itemDelegate.pressed || itemDelegate.highlighted
                   || itemDelegate.hovered ? theme.accentSoft : "transparent"
        }
        contentItem: RowLayout {
            spacing: 6
            Label {
                Layout.fillWidth: true
                text: itemDelegate.text
                font: control.font
                color: control.currentIndex === index ? theme.accent : theme.text
                elide: Text.ElideRight
            }
            Label {
                visible: control.currentIndex === index
                text: "✓"
                color: theme.accent
                font.pixelSize: Math.round(12 * settings.fontScale)
            }
        }
    }

    popup: Popup {
        y: control.height + 6
        width: control.width
        margins: 6
        topPadding: 6
        bottomPadding: 6
        implicitHeight: Math.min(contentItem.implicitHeight + 12, 380)
        closePolicy: Popup.CloseOnPressOutside | Popup.CloseOnEscape

        enter: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 120 }
            NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: 120; easing.type: Easing.OutCubic }
        }

        background: Item {
            Rectangle {
                id: popupBg
                anchors.fill: parent
                radius: theme.radiusM
                color: theme.card
                border.color: theme.cardBorder
            }
            MultiEffect {
                anchors.fill: popupBg
                source: popupBg
                shadowEnabled: true
                shadowColor: settings.dark ? "#a6000000" : "#4d000000"
                blur: 0.5
                shadowVerticalOffset: 3
            }
        }

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.delegateModel
            currentIndex: control.highlightedIndex
            ScrollBar.vertical: SlimScrollBar {}
        }
    }

    Theme {
        id: theme
        dark: settings.dark
    }
}
