import QtQuick
import QtQuick.Layouts
import GerritPilot

// Inline status message (info / warning / danger / success / neutral).
// Child items become actions laid out in a wrapping row under the text.
Rectangle {
    id: root
    property string tone: "info"
    property string title: ""
    property string message: ""
    property string iconName: (Theme.tones[tone] || Theme.tones.info).icon
    property bool showIcon: true
    property bool actionsRightAligned: true
    default property alias actions: actionFlow.data
    readonly property var colors: Theme.tones[tone] || Theme.tones.info
    readonly property color foreground: colors.foreground

    implicitHeight: column.implicitHeight + 20
    radius: Theme.radiusMedium
    color: colors.background
    border.color: colors.border
    border.width: 1
    clip: true

    ColumnLayout {
        id: column
        x: 12
        y: 10
        width: parent.width - 24
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            visible: root.title.length > 0 || root.message.length > 0
            Icon {
                visible: root.showIcon
                name: root.iconName
                size: 16
                color: root.foreground
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 0
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    visible: root.title.length > 0
                    text: root.title
                    color: root.foreground
                    font.pixelSize: Theme.fontSecondary
                    font.weight: Font.DemiBold
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                Text {
                    visible: root.message.length > 0
                    text: root.message
                    color: root.title.length > 0 ? Theme.secondaryText : root.foreground
                    font.pixelSize: root.title.length > 0 ? Theme.fontCaption : Theme.fontSecondary
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }
        }

        Flow {
            id: actionFlow
            Layout.fillWidth: true
            visible: children.length > 0
            spacing: 8
            layoutDirection: root.actionsRightAligned ? Qt.RightToLeft : Qt.LeftToRight
        }
    }
}
