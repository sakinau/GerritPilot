import QtQuick
import QtQuick.Controls.Basic
import GerritPilot

// Borderless toolbar button: shows a fill only on hover / press, like iOS bar items.
Button {
    id: root
    property string iconName: ""
    property string glyph: ""
    property string toolTip: ""
    property bool emphasized: false
    property bool destructive: false
    property color iconColor: !root.enabled ? Theme.quaternaryText
        : root.emphasized ? Theme.textOnAccent
        : root.destructive ? Theme.red
        : root.checked ? Theme.accent : Theme.secondaryText
    readonly property real iconSize: root.height < 26 ? 14 : 16
    implicitWidth: Theme.controlHeight
    implicitHeight: Theme.controlHeight
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    padding: 0
    topInset: 0
    bottomInset: 0
    leftInset: 0
    rightInset: 0
    opacity: enabled ? 1 : 0.6

    background: Rectangle {
        radius: Math.min(Theme.controlRadius, height / 2)
        color: root.emphasized
            ? (root.down ? Theme.accentPressed : root.hovered ? Theme.accentHover : Theme.accent)
            : root.down ? Theme.pressedFill
            : root.checked ? Theme.accentTint
            : root.hovered ? Theme.hoverFill : "transparent"
        border.width: root.visualFocus ? 2 : 0
        border.color: Theme.focusRing
        Behavior on color { ColorAnimation { duration: Theme.animationFast } }
    }
    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            visible: root.iconName.length > 0
            name: root.iconName
            size: root.iconSize
            color: root.iconColor
        }
        Text {
            anchors.centerIn: parent
            visible: root.iconName.length === 0
            text: root.glyph
            color: root.iconColor
            font.pixelSize: root.height < 26 ? Theme.fontSecondary : Theme.fontHeading
            font.weight: Font.Medium
        }
    }
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        acceptedButtons: Qt.NoButton
    }
    ToolTip.visible: hovered && toolTip.length > 0
    ToolTip.text: toolTip
    ToolTip.delay: 500
    Accessible.name: toolTip
}
