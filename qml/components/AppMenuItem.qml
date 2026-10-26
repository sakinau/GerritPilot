import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GerritPilot

// macOS-style menu row: accent highlight with white text on hover.
MenuItem {
    id: control
    property string iconName: ""
    property bool destructive: false
    implicitHeight: 30
    leftPadding: 8
    rightPadding: 10
    hoverEnabled: true
    indicator: Item { }
    arrow: Item { }
    readonly property color foreground: !control.enabled ? Theme.quaternaryText
        : control.highlighted ? Theme.textOnAccent
        : control.destructive ? Theme.redText : Theme.text
    contentItem: RowLayout {
        spacing: 8
        Item {
            Layout.preferredWidth: 16
            Layout.preferredHeight: 16
            Icon {
                anchors.centerIn: parent
                name: control.checked ? "check" : control.iconName
                size: 14
                strokeWidth: control.checked ? 2 : 1.6
                color: control.foreground
            }
        }
        Text {
            text: control.text
            color: control.foreground
            font.pixelSize: Theme.fontBody
            elide: Text.ElideRight
            Layout.fillWidth: true
            verticalAlignment: Text.AlignVCenter
        }
    }
    background: Rectangle {
        implicitWidth: 200
        radius: Theme.radiusSmall
        color: control.highlighted && control.enabled
            ? (control.destructive ? Theme.red : Theme.accent) : "transparent"
    }
}
