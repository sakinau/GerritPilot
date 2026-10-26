import QtQuick
import QtQuick.Controls.Basic
import GerritPilot

// Stepper: [ − ] value [ + ] in a single capsule.
SpinBox {
    id: control
    property string suffix: ""
    implicitWidth: 120
    implicitHeight: 30
    font.pixelSize: Theme.fontSecondary
    editable: false
    leftPadding: 30
    rightPadding: 30

    background: Rectangle {
        radius: Theme.controlRadius
        color: Theme.fillTertiary
    }
    contentItem: Text {
        text: control.textFromValue(control.value, control.locale) + control.suffix
        font: control.font
        color: Theme.text
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    down.indicator: Rectangle {
        x: 2
        y: 2
        width: 26
        height: control.height - 4
        radius: Theme.controlRadius - 2
        color: control.down.pressed ? Theme.surface : control.down.hovered ? "#80FFFFFF" : "transparent"
        opacity: control.value > control.from ? 1 : 0.35
        Icon { anchors.centerIn: parent; name: "minus"; size: 13; strokeWidth: 2; color: Theme.text }
    }
    up.indicator: Rectangle {
        x: control.width - width - 2
        y: 2
        width: 26
        height: control.height - 4
        radius: Theme.controlRadius - 2
        color: control.up.pressed ? Theme.surface : control.up.hovered ? "#80FFFFFF" : "transparent"
        opacity: control.value < control.to ? 1 : 0.35
        Icon { anchors.centerIn: parent; name: "plus"; size: 13; strokeWidth: 2; color: Theme.text }
    }
}
