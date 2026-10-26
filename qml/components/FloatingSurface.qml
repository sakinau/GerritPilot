import QtQuick
import GerritPilot

// Rounded surface for dialogs, menus and popovers. The soft shadow is built
// from stacked translucent layers so it also renders on the software backend.
Rectangle {
    id: root
    property int elevation: 6
    radius: Theme.radiusLarge
    color: Theme.surface
    border.width: 1
    border.color: "#14000000"

    Repeater {
        model: root.elevation
        Rectangle {
            required property int index
            readonly property real spread: (index + 1) * 1.6
            z: -1
            x: -spread
            y: -spread + (index + 1) * 0.9
            width: root.width + spread * 2
            height: root.height + spread * 2
            radius: root.radius + spread
            color: "#000000"
            opacity: 0.022
        }
    }
}
