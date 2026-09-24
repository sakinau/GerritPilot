import QtQuick
import QtQuick.Controls
import GerritPilot

Button {
    id: root
    property string glyph: "…"
    property string toolTip: ""
    property bool emphasized: false
    implicitWidth: Theme.controlHeight
    implicitHeight: Theme.controlHeight
    hoverEnabled: true
    padding: 0
    topInset: 0
    bottomInset: 0
    leftInset: 0
    rightInset: 0

    background: Rectangle {
        radius: Theme.controlRadius
        color: root.down ? (root.emphasized ? "#0876DA" : "#DEE0E8")
                         : root.hovered ? (root.emphasized ? "#2092FF" : Theme.surfaceMuted)
                                         : (root.emphasized ? Theme.accent : "transparent")
        border.color: root.visualFocus ? Theme.accent : "transparent"
    }
    contentItem: Text {
        text: root.glyph
        color: !root.enabled ? Theme.tertiaryText : root.emphasized ? "white" : Theme.secondaryText
        font.pixelSize: root.height < 30 ? Math.max(11, root.height - 8) : 18
        font.weight: Font.Medium
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        acceptedButtons: Qt.NoButton
    }
    ToolTip.visible: hovered && toolTip.length > 0
    ToolTip.text: toolTip
    ToolTip.delay: 500
}
