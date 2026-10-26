import QtQuick
import QtQuick.Shapes
import GerritPilot
import "Icons.js" as Icons

// Vector stroke icon. `strokeWidth` is in device-independent pixels and stays
// constant regardless of `size`, matching how SF Symbols keep visual weight.
Item {
    id: root
    property string name: ""
    property color color: Theme.secondaryText
    property real size: 16
    property real strokeWidth: 1.6

    implicitWidth: size
    implicitHeight: size
    visible: name.length > 0

    Shape {
        width: 24
        height: 24
        anchors.centerIn: parent
        scale: root.size / 24
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: root.color
            strokeWidth: root.strokeWidth * 24 / Math.max(1, root.size)
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: Icons.path(root.name) }
        }
    }
}
