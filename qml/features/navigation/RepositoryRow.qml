import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GerritPilot
import "../../components"

// Repository source-list row. The active repository is filled with the accent color.
ItemDelegate {
    id: row
    required property int index
    required property string repoName
    required property string repoPath
    required property string branch
    required property int changeCount
    required property int ahead
    required property bool repoSelected
    property var workspace

    readonly property bool active: workspace && workspace.selectedPath === repoPath

    height: 46
    hoverEnabled: true
    leftPadding: 8
    rightPadding: 8
    topPadding: 4
    bottomPadding: 4
    onClicked: workspace.activateRepository(index)

    background: Rectangle {
        radius: Theme.radiusMedium - 2
        color: row.active ? Theme.accent : row.down ? Theme.pressedFill : row.hovered ? Theme.hoverFill : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.animationFast } }
    }

    contentItem: RowLayout {
        spacing: 8
        FlatCheckBox {
            controlled: true
            checked: row.repoSelected
            checkedColor: row.active ? "#4DFFFFFF" : Theme.accent
            uncheckedBorder: row.active ? "#99FFFFFF" : Theme.separator
            Layout.preferredWidth: 16
            Layout.alignment: Qt.AlignVCenter
            onToggled: {
                if (row.workspace && typeof row.workspace.toggleRepository === "function")
                    row.workspace.toggleRepository(row.index)
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: 2
            Text {
                text: row.repoName
                color: row.active ? Theme.textOnAccent : Theme.text
                font.pixelSize: Theme.fontBody
                font.weight: row.active ? Font.DemiBold : Font.Medium
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 4
                Icon {
                    name: "git-branch"
                    size: 11
                    strokeWidth: 1.8
                    color: row.active ? "#D9FFFFFF" : Theme.tertiaryText
                }
                Text {
                    text: row.branch
                    color: row.active ? "#E6FFFFFF" : Theme.secondaryText
                    font.pixelSize: Theme.fontCaption
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }
        }
        Badge {
            visible: row.changeCount > 0
            text: row.changeCount
            tone: "orange"
            inverted: row.active
            Layout.alignment: Qt.AlignVCenter
        }
        Badge {
            visible: row.ahead > 0
            text: row.ahead
            iconName: "arrow-up"
            tone: "purple"
            inverted: row.active
            Layout.alignment: Qt.AlignVCenter
        }
    }
}
