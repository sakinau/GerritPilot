import QtQuick
import QtQuick.Layouts
import GerritPilot

// Small grouped-list caption with optional trailing controls.
RowLayout {
    id: root
    property string text: ""
    property string detail: ""
    default property alias trailing: trailingRow.data
    spacing: 4

    Text {
        text: root.text
        color: Theme.secondaryText
        font.pixelSize: Theme.fontCaption
        font.weight: Font.DemiBold
        font.letterSpacing: 0.4
        elide: Text.ElideRight
        Layout.alignment: Qt.AlignVCenter
        Layout.preferredWidth: implicitWidth
        Layout.minimumWidth: 0
    }
    // Detail absorbs the free space and elides first.
    Text {
        text: root.detail
        color: Theme.tertiaryText
        font.pixelSize: Theme.fontCaption
        elide: Text.ElideRight
        Layout.alignment: Qt.AlignVCenter
        Layout.fillWidth: true
        Layout.preferredWidth: 0
        Layout.minimumWidth: 0
    }
    RowLayout {
        id: trailingRow
        spacing: 0
        Layout.alignment: Qt.AlignVCenter
    }
}
