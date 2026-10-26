import QtQuick
import QtQuick.Controls.Basic
import GerritPilot

Dialog {
    id: root
    property real preferredWidth: 500
    // Renders the accept button in red for irreversible actions.
    property bool destructive: false
    parent: Overlay.overlay
    width: Math.min(preferredWidth, Math.max(0, parent ? parent.width - 32 : preferredWidth))
    contentWidth: Math.max(0, width - leftPadding - rightPadding)
    contentHeight: contentItem ? contentItem.implicitHeight : 0
    height: Math.min(implicitHeight, Math.max(0, parent ? parent.height - 32 : implicitHeight))
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    modal: true
    closePolicy: Popup.CloseOnEscape
    padding: 22
    topPadding: 12
    spacing: 16
    font.pixelSize: Theme.fontBody
    background: FloatingSurface {
        radius: Theme.radiusXLarge
        elevation: 10
    }
    header: Label {
        text: root.title
        visible: text.length > 0
        leftPadding: 22
        rightPadding: 22
        topPadding: 20
        bottomPadding: 2
        color: Theme.text
        font.pixelSize: Theme.fontHeading
        font.weight: Font.DemiBold
        elide: Text.ElideRight
    }
    footer: DialogButtonBox {
        standardButtons: root.standardButtons
        visible: count > 0
        alignment: Qt.AlignRight
        // Cancel on the left, the affirmative action on the right (Apple HIG).
        buttonLayout: DialogButtonBox.MacLayout
        spacing: 8
        leftPadding: 22
        rightPadding: 22
        topPadding: 6
        bottomPadding: 20
        background: Item { }
        delegate: PrimaryButton {
            kind: DialogButtonBox.buttonRole === DialogButtonBox.AcceptRole
                  || DialogButtonBox.buttonRole === DialogButtonBox.YesRole
                ? (root.destructive ? "danger" : "filled") : "gray"
        }
    }
    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.animationFast }
        NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: Theme.animationNormal; easing.type: Easing.OutCubic }
    }
    // No exit transition: a fading modal scrim would swallow the next click.
    Overlay.modal: Rectangle { color: Theme.scrim }
}
