import QtQuick
import GerritPilot

// Floating surfaces only: at most three shadow layers, including on the
// software backend. Closed surfaces release their decorative items.
Rectangle {
    id: root
    property int elevation: 6
    property bool shadowEnabled: true
    readonly property int shadowElevation: Math.max(0, Math.min(10, elevation))
    readonly property int shadowLayerCount: visible && shadowEnabled ? Math.min(3, shadowElevation) : 0
    radius: Theme.radiusLarge
    color: Theme.surface
    border.width: 1
    border.color: "#14000000"

    Repeater {
        model: root.shadowLayerCount
        Rectangle {
            required property int index
            readonly property real depth: (index + 1) * root.shadowElevation / Math.max(1, root.shadowLayerCount)
            readonly property real spread: depth * 1.6
            z: -1
            x: -spread
            y: -spread + depth * 0.9
            width: root.width + spread * 2
            height: root.height + spread * 2
            radius: root.radius + spread
            color: "#000000"
            opacity: 1 - Math.pow(1 - 0.022, root.shadowElevation / Math.max(1, root.shadowLayerCount))
        }
    }
}
