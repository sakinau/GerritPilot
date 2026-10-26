import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GerritPilot

// Checkable capsule used for view options ("忽略空白").
Button {
    id: root
    property string iconName: ""
    property string toolTip: ""
    checkable: true
    hoverEnabled: true
    implicitHeight: 24
    implicitWidth: row.implicitWidth + 18
    padding: 0
    topInset: 0
    bottomInset: 0
    leftInset: 0
    rightInset: 0
    background: Rectangle {
        radius: height / 2
        color: root.checked ? Theme.accentTint
            : root.down ? Theme.pressedFill : root.hovered ? Theme.hoverFill : "transparent"
        border.width: root.checked ? 0 : 1
        border.color: Theme.separatorSoft
        Behavior on color { ColorAnimation { duration: Theme.animationFast } }
    }
    contentItem: Item {
        RowLayout {
            id: row
            anchors.centerIn: parent
            spacing: 4
            Icon {
                visible: root.iconName.length > 0 || root.checked
                name: root.checked ? "check" : root.iconName
                size: 12
                strokeWidth: 2
                color: root.checked ? Theme.accent : Theme.secondaryText
            }
            Text {
                text: root.text
                color: root.checked ? Theme.accent : Theme.secondaryText
                font.pixelSize: Theme.fontCaption
                font.weight: root.checked ? Font.DemiBold : Font.Medium
            }
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
}
