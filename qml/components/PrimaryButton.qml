import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GerritPilot

Button {
    id: root
    property string glyph: ""
    property string toolTip: ""
    property bool secondary: false
    property bool danger: false
    implicitHeight: Theme.controlHeight
    font.pixelSize: Theme.fontBody
    implicitWidth: Math.max(92, contentRow.implicitWidth + 28)
    hoverEnabled: true
    padding: 0
    topInset: 0
    bottomInset: 0
    leftInset: 0
    rightInset: 0

    background: Rectangle {
        radius: Theme.controlRadius
        color: {
            if (!root.enabled) return Theme.separatorSoft
            if (root.danger) return root.down ? "#D9362C" : root.hovered ? "#FF5B52" : Theme.red
            if (root.secondary) return root.down ? "#E2E3E9" : root.hovered ? "#F0F1F5" : Theme.surfaceStrong
            return root.down ? "#0876DA" : root.hovered ? "#2092FF" : Theme.accent
        }
        border.color: root.visualFocus ? Theme.accent : root.secondary ? Theme.separator : "transparent"
    }
    contentItem: Item {
        implicitWidth: contentRow.implicitWidth
        implicitHeight: contentRow.implicitHeight
        RowLayout {
            id: contentRow
            anchors.centerIn: parent
            width: Math.min(implicitWidth, Math.max(0, parent.width - 16))
            spacing: 7
            Text {
                visible: root.glyph.length > 0
                Layout.preferredWidth: root.glyph.length > 0 ? -1 : 0
                Layout.preferredHeight: root.glyph.length > 0 ? -1 : 0
                text: root.glyph
                color: !root.enabled ? Theme.tertiaryText : root.secondary ? Theme.secondaryText : "white"
                font.pixelSize: Theme.fontSubheading
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
            }
            Text {
                text: root.text
                objectName: "buttonLabel"
                Layout.fillWidth: true
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
                color: !root.enabled ? Theme.tertiaryText : root.secondary ? Theme.text : "white"
                font.pixelSize: root.font.pixelSize
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
            }
        }
    }
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        acceptedButtons: Qt.NoButton
    }
    ToolTip.visible: hovered && toolTip.length > 0
    ToolTip.text: toolTip
    ToolTip.delay: 350
}
