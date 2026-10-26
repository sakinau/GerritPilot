import QtQuick
import GerritPilot

// iOS activity indicator: eight fading spokes. Same `running` API as BusyIndicator.
Item {
    id: root
    property bool running: true
    property color color: Theme.secondaryText
    implicitWidth: 18
    implicitHeight: 18
    visible: running

    Item {
        id: spokes
        anchors.centerIn: parent
        width: Math.min(root.width, root.height)
        height: width
        Repeater {
            model: 8
            Rectangle {
                required property int index
                x: (spokes.width - width) / 2
                y: 0
                width: Math.max(1.5, spokes.width * 0.11)
                height: spokes.height * 0.28
                radius: width / 2
                color: root.color
                opacity: 0.25 + 0.75 * (index / 7)
                transform: Rotation {
                    origin.x: width / 2
                    origin.y: spokes.height / 2
                    angle: index * 45
                }
            }
        }
        RotationAnimator on rotation {
            running: root.running && root.visible
            from: 0
            to: 360
            duration: 900
            loops: Animation.Infinite
        }
    }
}
