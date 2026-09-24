import QtQuick
import QtQuick.Controls
import GerritPilot

ToolButton {
    id: control
    property string toolTip: ""
    property bool destructive: false
    implicitWidth: 28
    implicitHeight: 28
    padding: 0
    topInset: 0
    bottomInset: 0
    leftInset: 0
    rightInset: 0
    hoverEnabled: true
    opacity: enabled ? 1 : 0.4
    background: Rectangle {
        radius: 5
        color: control.down ? Theme.separatorSoft
             : control.hovered ? Theme.surfaceMuted : "transparent"
    }
    contentItem: Text {
        text: control.text
        color: control.destructive ? Theme.red : Theme.secondaryText
        font.pixelSize: 15
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
    ToolTip.delay: 400
    Accessible.name: toolTip
}
