import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GerritPilot
import "../../components"

// Local branches of the active repository; click to check out.
ColumnLayout {
    id: root
    property var workspace
    spacing: 6

    function filteredBranches() {
        if (!root.workspace) return []
        const keyword = branchSearch.text.trim().toLowerCase()
        const current = root.workspace.selectedBranch
        const branches = root.workspace.availableBranches.filter(function(branch) {
            return !keyword.length || branch.toLowerCase().indexOf(keyword) !== -1
        })
        branches.sort(function(left, right) {
            if (left === current) return -1
            if (right === current) return 1
            return left.localeCompare(right)
        })
        return branches
    }

    SectionHeader {
        Layout.fillWidth: true
        Layout.leftMargin: 4
        Layout.topMargin: 4
        text: "分支"
        detail: root.workspace ? root.workspace.selectedName : ""
    }

    SearchField {
        id: branchSearch
        Layout.fillWidth: true
        Layout.rightMargin: 8
        placeholderText: "筛选分支"
    }

    ListView {
        id: branchList
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(contentHeight, 150)
        clip: true
        spacing: 1
        rightMargin: 8
        boundsBehavior: Flickable.StopAtBounds
        model: root.filteredBranches()
        ScrollBar.vertical: RightScrollBar { }
        delegate: ItemDelegate {
            id: branchDelegate
            required property string modelData
            readonly property bool isCurrent: root.workspace && modelData === root.workspace.selectedBranch
            width: branchList.width - branchList.rightMargin
            height: 30
            hoverEnabled: true
            leftPadding: 8
            rightPadding: 8
            onClicked: root.workspace.checkoutBranch(modelData)
            background: Rectangle {
                radius: Theme.radiusSmall
                color: branchDelegate.down ? Theme.pressedFill : branchDelegate.hovered ? Theme.hoverFill : "transparent"
            }
            contentItem: RowLayout {
                spacing: 8
                Icon {
                    name: branchDelegate.isCurrent ? "check" : "git-branch"
                    size: 13
                    strokeWidth: branchDelegate.isCurrent ? 2.2 : 1.6
                    color: branchDelegate.isCurrent ? Theme.accent : Theme.tertiaryText
                }
                Text {
                    text: branchDelegate.modelData
                    color: branchDelegate.isCurrent ? Theme.accent : Theme.text
                    font.pixelSize: Theme.fontSecondary
                    font.weight: branchDelegate.isCurrent ? Font.DemiBold : Font.Normal
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }
}
