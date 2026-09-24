import QtQuick
import GerritPilot

Rectangle {
    id: root
    property alias text: label.text
    property color foreground: Theme.secondaryText
    property color fill: Theme.surfaceMuted
    property int horizontalPadding: 9
    implicitWidth: label.implicitWidth + horizontalPadding * 2
    implicitHeight: 24
    radius: height / 2
    color: fill

    Text {
        id: label
        anchors.centerIn: parent
        color: root.foreground
        font.pixelSize: Theme.fontSecondary
        font.weight: Font.Medium
    }
}
