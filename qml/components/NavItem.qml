import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GerritPilot

Button {
    id: root
    property string glyph: "⌂"
    property string label: ""
    property bool current: false
    property bool compact: false
    implicitHeight: 42
    hoverEnabled: true
    leftPadding: 12
    rightPadding: 10
    topPadding: 0
    bottomPadding: 0

    background: Rectangle {
        radius: 11
        color: root.current ? "#D9EAFB" : root.hovered ? "#E3E4EA" : "transparent"
    }
    contentItem: Item {
        RowLayout {
            anchors.fill: parent
            spacing: 11
            Text {
            text: root.glyph
            color: root.current ? Theme.accent : Theme.secondaryText
            font.pixelSize: Theme.fontTitle
            font.weight: Font.Medium
            Layout.preferredWidth: 22
            horizontalAlignment: Text.AlignHCenter
        }
            Text {
            text: root.label
            visible: !root.compact
            color: root.current ? Theme.text : Theme.secondaryText
            font.pixelSize: Theme.fontBody
            font.weight: root.current ? Font.DemiBold : Font.Medium
            Layout.fillWidth: true
            }
        }
    }
}
