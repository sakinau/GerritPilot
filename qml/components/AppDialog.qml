import QtQuick
import QtQuick.Controls
import GerritPilot

Dialog {
    id: root
    property real preferredWidth: 500
    parent: Overlay.overlay
    width: Math.min(preferredWidth, Math.max(0, parent ? parent.width - 32 : preferredWidth))
    contentWidth: Math.max(0, width - leftPadding - rightPadding)
    contentHeight: contentItem ? contentItem.implicitHeight : 0
    height: Math.min(implicitHeight, Math.max(0, parent ? parent.height - 32 : implicitHeight))
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    modal: true
    closePolicy: Popup.CloseOnEscape
    padding: 20
    spacing: 16
    font.pixelSize: Theme.fontBody
    background: Rectangle {
        color: Theme.surfaceStrong
        radius: Theme.radiusXLarge
        border.color: Theme.separator
    }
    header: Label {
        text: root.title
        visible: text.length > 0
        leftPadding: 20
        rightPadding: 20
        topPadding: 20
        bottomPadding: 4
        color: Theme.text
        font.pixelSize: Theme.fontHeading
        font.weight: Font.DemiBold
        elide: Text.ElideRight
    }
    footer: DialogButtonBox {
        standardButtons: root.standardButtons
        visible: count > 0
        alignment: Qt.AlignRight
        spacing: 8
        leftPadding: 20
        rightPadding: 20
        topPadding: 8
        bottomPadding: 20
        background: Item { }
        delegate: PrimaryButton { secondary: true }
    }
    Overlay.modal: Rectangle { color: "#33000000" }
}
