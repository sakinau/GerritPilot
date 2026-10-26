import QtQuick
import GerritPilot

// Elevated content surface. Set `elevated: false` for nested panes.
Rectangle {
    id: root
    property bool elevated: false
    color: Theme.surface
    radius: Theme.radiusLarge
    border.color: Theme.separatorSoft
    border.width: 1

    Rectangle {
        visible: root.elevated
        z: -1
        anchors.fill: parent
        anchors.topMargin: 1
        anchors.bottomMargin: -1
        radius: root.radius
        color: "transparent"
        border.color: "#0D000000"
        border.width: 1
    }
}
