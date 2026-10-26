import QtQuick
import QtQuick.Controls.Basic
import GerritPilot

// SplitView handle: hairline divider, grip appears on hover / drag.
// `horizontalBar` is for vertical SplitViews (the handle spans the width).
Rectangle {
    id: root
    property bool horizontalBar: false
    property bool showLine: true
    readonly property bool active: SplitHandle.pressed
    readonly property bool hovered: SplitHandle.hovered

    implicitWidth: horizontalBar ? 0 : 8
    implicitHeight: horizontalBar ? 8 : 0
    color: "transparent"

    Rectangle {
        visible: root.showLine || root.active
        anchors.centerIn: parent
        width: root.horizontalBar ? parent.width : 1
        height: root.horizontalBar ? 1 : parent.height
        color: root.active ? Theme.accent : Theme.separatorSoft
    }
    Rectangle {
        anchors.centerIn: parent
        width: root.horizontalBar ? 36 : 4
        height: root.horizontalBar ? 4 : 36
        radius: 2
        opacity: root.hovered || root.active ? 1 : 0
        color: root.active ? Theme.accent : Theme.scrollThumb
        Behavior on opacity { NumberAnimation { duration: Theme.animationFast } }
    }
}
