import QtQuick
import QtQuick.Layouts
import GerritPilot

Item {
    id: root
    property bool checked: false
    property bool controlled: false
    property string text: ""
    property color checkedColor: Theme.accent
    property color uncheckedBorder: Theme.separator
    signal toggled(bool checked)
    implicitWidth: text.length > 0 ? (row.implicitWidth + 4) : 18
    implicitHeight: Math.max(20, row.implicitHeight)
    opacity: enabled ? 1 : 0.5

    RowLayout {
        id: row
        anchors.verticalCenter: parent.verticalCenter
        spacing: 7
        Rectangle {
            Layout.preferredWidth: 16
            Layout.preferredHeight: 16
            Layout.alignment: Qt.AlignVCenter
            radius: 5
            color: root.checked ? root.checkedColor : Theme.surface
            border.width: root.checked ? 0 : 1.5
            border.color: checkMouse.containsMouse ? Theme.accent : root.uncheckedBorder
            Behavior on color { ColorAnimation { duration: Theme.animationFast } }
            Icon {
                anchors.centerIn: parent
                visible: root.checked
                name: "check"
                size: 12
                strokeWidth: 2.2
                color: Theme.textOnAccent
            }
        }
        Text {
            visible: root.text.length > 0
            text: root.text
            color: Theme.text
            font.pixelSize: Theme.fontSecondary
            Layout.alignment: Qt.AlignVCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    MouseArea {
        id: checkMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            const nextChecked = !root.checked
            if (!root.controlled)
                root.checked = nextChecked
            root.toggled(nextChecked)
        }
    }
}
