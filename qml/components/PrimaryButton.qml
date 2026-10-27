import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GerritPilot

// iOS button styles:
//   filled  - solid accent (default)       gray   - neutral fill
//   tinted  - accent tint with accent text plain  - text only, fill on hover
//   danger  - solid red                     dangerTinted - red tint with red text
Button {
    id: root
    property string glyph: ""
    property string iconName: ""
    property string toolTip: ""
    property bool compact: false
    property string kind: "filled"

    readonly property bool solid: kind === "filled" || kind === "danger"
    readonly property color foreground: {
        if (!root.enabled) return Theme.quaternaryText
        switch (root.kind) {
        case "filled":
        case "danger": return Theme.textOnAccent
        case "gray": return Theme.text
        case "dangerTinted": return Theme.redText
        default: return Theme.accent
        }
    }

    implicitHeight: compact ? Theme.controlHeightSmall : Theme.controlHeight
    font.pixelSize: compact ? Theme.fontSecondary : Theme.fontBody
    implicitWidth: Math.max(compact ? 0 : 84, contentRow.implicitWidth + (compact ? 20 : 28))
    hoverEnabled: true
    padding: 0
    // Basic style binds horizontalPadding separately; zero it so width math is exact.
    horizontalPadding: 0
    verticalPadding: 0
    topInset: 0
    bottomInset: 0
    leftInset: 0
    rightInset: 0

    background: Rectangle {
        radius: Math.min(Theme.controlRadius, height / 2)
        color: {
            if (!root.enabled) return root.kind === "plain" ? "transparent" : Theme.fillQuaternary
            switch (root.kind) {
            case "danger": return root.down ? Theme.redPressed : root.hovered ? Theme.redHover : Theme.red
            case "gray": return root.down ? Theme.fillPrimary : root.hovered ? Theme.fillSecondary : Theme.fillTertiary
            case "tinted": return root.down ? "#33007AFF" : root.hovered ? "#29007AFF" : Theme.accentTint
            case "dangerTinted": return root.down ? "#33FF3B30" : root.hovered ? "#29FF3B30" : "#1AFF3B30"
            case "plain": return root.down ? Theme.pressedFill : root.hovered ? Theme.hoverFill : "transparent"
            default: return root.down ? Theme.accentPressed : root.hovered ? Theme.accentHover : Theme.accent
            }
        }
        border.width: root.visualFocus ? 2 : 0
        border.color: Theme.focusRing
        Behavior on color { ColorAnimation { duration: Theme.animationFast } }
    }
    contentItem: Item {
        implicitWidth: contentRow.implicitWidth
        implicitHeight: contentRow.implicitHeight
        RowLayout {
            id: contentRow
            anchors.centerIn: parent
            width: Math.min(implicitWidth, Math.max(0, parent.width - 8))
            spacing: root.compact ? 4 : 6
            Icon {
                visible: root.iconName.length > 0
                name: root.iconName
                size: root.compact ? 13 : 15
                strokeWidth: root.solid ? 1.9 : 1.7
                color: root.foreground
                Layout.alignment: Qt.AlignVCenter
            }
            Text {
                visible: root.glyph.length > 0 && root.iconName.length === 0
                Layout.preferredWidth: visible ? -1 : 0
                Layout.preferredHeight: visible ? -1 : 0
                text: root.glyph
                color: root.foreground
                font.pixelSize: Theme.fontSubheading
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
            }
            Text {
                text: root.text
                objectName: "buttonLabel"
                visible: text.length > 0
                Layout.fillWidth: true
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
                color: root.foreground
                font.pixelSize: root.font.pixelSize
                font.weight: root.solid ? Font.DemiBold : Font.Medium
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
