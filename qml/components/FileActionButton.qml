import QtQuick
import QtQuick.Controls.Basic
import GerritPilot

// Compact row action (stage, discard, open). `text` is kept as a glyph fallback.
ToolButton {
    id: control
    property string iconName: ""
    property string toolTip: ""
    property bool destructive: false
    implicitWidth: 26
    implicitHeight: 26
    padding: 0
    topInset: 0
    bottomInset: 0
    leftInset: 0
    rightInset: 0
    hoverEnabled: true
    opacity: enabled ? 1 : 0.4
    background: Rectangle {
        radius: Theme.radiusSmall
        color: control.down ? (control.destructive ? Theme.redSoft : Theme.pressedFill)
             : control.hovered ? (control.destructive ? Theme.redSoft : Theme.hoverFill) : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.animationFast } }
    }
    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            visible: control.iconName.length > 0
            name: control.iconName
            size: control.height >= 30 ? 16 : 14
            color: control.destructive ? Theme.red : control.hovered ? Theme.text : Theme.secondaryText
        }
        Text {
            anchors.centerIn: parent
            visible: control.iconName.length === 0
            text: control.text
            color: control.destructive ? Theme.red : Theme.secondaryText
            font.pixelSize: 15
        }
    }
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        acceptedButtons: Qt.NoButton
    }
    ToolTip.visible: hovered && toolTip.length > 0
    ToolTip.text: toolTip
    ToolTip.delay: 400
    Accessible.name: toolTip
}
