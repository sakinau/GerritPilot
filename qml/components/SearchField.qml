import QtQuick
import QtQuick.Layouts
import GerritPilot

// iOS-style search field: filled capsule, leading magnifier, trailing clear button.
Rectangle {
    id: root
    property alias text: input.text
    property alias placeholderText: placeholder.text
    property alias input: input
    property color fillColor: Theme.fillTertiary
    // Trailing slot for counters / navigation (e.g. "3/12  ▲ ▼").
    default property alias trailing: trailingRow.data
    signal accepted()

    function clear() { input.text = "" }
    function focusInput() { input.forceActiveFocus() }

    implicitWidth: 220
    implicitHeight: 30
    radius: 8
    color: input.activeFocus ? Theme.surface : fillColor
    border.width: input.activeFocus ? 2 : 0
    border.color: Theme.focusRing
    Behavior on color { ColorAnimation { duration: Theme.animationFast } }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.IBeamCursor
        onPressed: mouse => { input.forceActiveFocus(); mouse.accepted = false }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 4
        spacing: 6

        Icon {
            name: "search"
            size: 14
            color: Theme.tertiaryText
            Layout.alignment: Qt.AlignVCenter
        }

        TextInput {
            id: input
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            color: Theme.text
            font.pixelSize: Theme.fontSecondary
            selectByMouse: true
            selectionColor: Theme.accentSoft
            selectedTextColor: Theme.text
            clip: true
            onAccepted: root.accepted()
            Keys.onEscapePressed: event => {
                if (text.length) { text = ""; event.accepted = true }
                else event.accepted = false
            }

            Text {
                id: placeholder
                anchors.fill: parent
                verticalAlignment: Text.AlignVCenter
                visible: !input.text.length
                color: Theme.placeholder
                font: input.font
                elide: Text.ElideRight
            }
        }

        RowLayout {
            id: trailingRow
            spacing: 2
            Layout.alignment: Qt.AlignVCenter
        }

        Rectangle {
            visible: input.text.length > 0
            Layout.preferredWidth: 16
            Layout.preferredHeight: 16
            Layout.rightMargin: 3
            Layout.alignment: Qt.AlignVCenter
            radius: 8
            color: clearMouse.containsMouse ? Theme.secondaryText : Theme.quaternaryText
            Icon { anchors.centerIn: parent; name: "x"; size: 10; strokeWidth: 2; color: Theme.surface }
            MouseArea {
                id: clearMouse
                anchors.fill: parent
                anchors.margins: -4
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: { input.text = ""; input.forceActiveFocus() }
            }
        }
    }
}
