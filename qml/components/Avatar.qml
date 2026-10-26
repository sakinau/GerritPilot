import QtQuick
import GerritPilot

// Circular initials avatar with a stable color per name.
Rectangle {
    id: root
    property string name: ""
    property real size: 24
    implicitWidth: size
    implicitHeight: size
    radius: size / 2
    color: Theme.avatarColor(name)
    readonly property string initials: {
        const value = String(name || "").trim()
        if (!value.length) return "?"
        const parts = value.split(/[\s._-]+/).filter(p => p.length)
        if (/^[A-Za-z]/.test(value) && parts.length > 1)
            return (parts[0][0] + parts[1][0]).toUpperCase()
        return /^[A-Za-z]/.test(value) ? value.slice(0, 2).toUpperCase() : value.slice(0, 1)
    }
    Text {
        anchors.centerIn: parent
        text: root.initials
        color: Theme.textOnAccent
        font.pixelSize: Math.round(root.size * 0.42)
        font.weight: Font.DemiBold
    }
}
