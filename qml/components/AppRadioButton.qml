import QtQuick
import QtQuick.Controls.Basic
import GerritPilot

RadioButton {
    id: control
    font.pixelSize: Theme.fontSecondary
    spacing: 7
    hoverEnabled: true
    indicator: Rectangle {
        implicitWidth: 16
        implicitHeight: 16
        x: control.leftPadding
        y: (control.height - height) / 2
        radius: 8
        color: control.checked ? Theme.accent : Theme.surface
        border.width: control.checked ? 0 : 1.5
        border.color: control.hovered && control.enabled ? Theme.accent : Theme.separator
        opacity: control.enabled ? 1 : 0.5
        Rectangle {
            anchors.centerIn: parent
            width: 6
            height: 6
            radius: 3
            color: Theme.textOnAccent
            visible: control.checked
        }
    }
    contentItem: Text {
        leftPadding: control.indicator.width + control.spacing
        text: control.text
        font: control.font
        color: control.enabled ? Theme.text : Theme.tertiaryText
        verticalAlignment: Text.AlignVCenter
    }
}
