import QtQuick
import QtQuick.Layouts
import GerritPilot

Item {
    id: root
    property bool checked: false
    property string text: ""
    signal toggled(bool checked)
    implicitWidth: text.length > 0 ? (row.implicitWidth + 4) : 18
    implicitHeight: Math.max(20, row.implicitHeight)

    RowLayout {
        id: row
        anchors.verticalCenter: parent.verticalCenter
        spacing: 6
        Rectangle {
            Layout.preferredWidth: 16
            Layout.preferredHeight: 16
            Layout.alignment: Qt.AlignVCenter
            radius: 4
            color: root.checked ? Theme.accent : Theme.surfaceStrong
            border.color: root.checked ? Theme.accent : Theme.separator
            Text { anchors.centerIn: parent; text: "✓"; visible: root.checked; color: "white"; font.pixelSize: Theme.fontSecondary; font.weight: Font.Bold }
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
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            root.checked = !root.checked
            root.toggled(root.checked)
        }
    }
}
