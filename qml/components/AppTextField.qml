import QtQuick
import QtQuick.Controls.Basic
import GerritPilot

TextField {
    id: control
    implicitHeight: Theme.controlHeight
    leftPadding: 10
    rightPadding: 10
    font.pixelSize: Theme.fontSecondary
    color: Theme.text
    placeholderTextColor: Theme.placeholder
    selectionColor: Theme.accentSoft
    selectedTextColor: Theme.text
    selectByMouse: true
    background: Rectangle {
        radius: Theme.controlRadius
        color: control.enabled ? Theme.surface : Theme.surfaceMuted
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? Theme.focusRing : Theme.separator
    }
}
