import QtQuick
import QtQuick.Layouts
import GerritPilot

// iOS grouped table section: caption above, rounded inset card, footnote below.
ColumnLayout {
    id: root
    property string title: ""
    property string footnote: ""
    default property alias content: body.data
    spacing: 6

    Text {
        visible: root.title.length > 0
        text: root.title
        color: Theme.secondaryText
        font.pixelSize: Theme.fontCaption
        font.weight: Font.DemiBold
        font.letterSpacing: 0.4
        Layout.leftMargin: 12
    }
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: body.implicitHeight + 24
        radius: Theme.radiusLarge
        color: Theme.surface
        border.width: 1
        border.color: Theme.separatorSoft
        ColumnLayout {
            id: body
            x: 14
            y: 12
            width: parent.width - 28
            spacing: 10
        }
    }
    Text {
        visible: root.footnote.length > 0
        text: root.footnote
        color: Theme.tertiaryText
        font.pixelSize: Theme.fontCaption
        wrapMode: Text.Wrap
        Layout.fillWidth: true
        Layout.leftMargin: 12
        Layout.rightMargin: 12
    }
}
