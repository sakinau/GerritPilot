import QtQuick
import GerritPilot

// iOS segmented control with a sliding thumb.
Rectangle {
    id: root
    property var model: []
    property int currentIndex: 0
    property bool compact: false
    property int segmentPadding: compact ? 12 : 16
    signal activated(int index)

    implicitHeight: compact ? 24 : 28
    implicitWidth: segments.implicitWidth + 4
    radius: compact ? 7 : 8
    color: Theme.fillTertiary

    Rectangle {
        id: thumb
        readonly property Item target: repeater.count > root.currentIndex && root.currentIndex >= 0
            ? repeater.itemAt(root.currentIndex) : null
        visible: target !== null
        x: target ? target.x + segments.x : 0
        y: 2
        width: target ? target.width : 0
        height: root.height - 4
        radius: root.radius - 2
        color: Theme.surface
        border.width: 0.5
        border.color: "#0F000000"
        Behavior on x { NumberAnimation { duration: Theme.animationNormal; easing.type: Easing.OutCubic } }
        Behavior on width { NumberAnimation { duration: Theme.animationNormal; easing.type: Easing.OutCubic } }
        Rectangle {
            z: -1
            anchors.fill: parent
            anchors.topMargin: 1
            anchors.bottomMargin: -1
            radius: parent.radius
            color: "#14000000"
        }
    }

    Row {
        id: segments
        x: 2
        height: parent.height
        Repeater {
            id: repeater
            model: root.model
            delegate: Item {
                id: segment
                required property int index
                required property var modelData
                readonly property bool selected: root.currentIndex === index
                width: label.implicitWidth + root.segmentPadding * 2
                height: segments.height
                Text {
                    id: label
                    anchors.centerIn: parent
                    text: segment.modelData
                    color: segment.selected ? Theme.text : (mouse.containsMouse ? Theme.text : Theme.secondaryText)
                    font.pixelSize: root.compact ? Theme.fontCaption : Theme.fontSecondary
                    font.weight: segment.selected ? Font.DemiBold : Font.Medium
                }
                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (root.currentIndex !== segment.index)
                            root.activated(segment.index)
                    }
                }
            }
        }
    }
}
