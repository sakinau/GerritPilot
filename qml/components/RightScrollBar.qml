import QtQuick
import QtQuick.Controls
import GerritPilot

ScrollBar {
    id: root

    policy: ScrollBar.AsNeeded
    implicitWidth: 8
    minimumSize: 0.06
    hoverEnabled: true
    padding: 2
    anchors.right: parent.right
    anchors.top: parent.top
    anchors.bottom: parent.bottom

    contentItem: Rectangle {
        implicitWidth: 4
        radius: width / 2
        color: root.pressed ? Theme.scrollThumbActive : Theme.scrollThumb
        opacity: root.size >= 1 ? 0 : root.active || root.hovered || root.pressed ? 0.85 : 0
    }
    background: Item { }
}
