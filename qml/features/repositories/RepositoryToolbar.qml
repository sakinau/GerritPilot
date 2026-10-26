import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GerritPilot
import "../../components"

// Window-style toolbar: repository identity, view switcher and repository actions.
Rectangle {
    id: root
    property var workspace
    property int currentTab: 0
    readonly property bool compactActions: width < 860
    readonly property bool hasRepository: workspace && workspace.selectedPath.length > 0

    signal tabRequested(int index)
    signal branchRequested()
    signal settingsRequested()
    signal pushRequested()
    signal commandLogRequested()
    signal stashRequested()

    implicitHeight: 54
    color: Theme.toolbar
    radius: Theme.radiusLarge

    // Square off the bottom corners and draw the hairline.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: root.radius
        color: root.color
    }
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.separatorSoft
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 12
        spacing: 10

        Rectangle {
            Layout.preferredWidth: 30
            Layout.preferredHeight: 30
            Layout.alignment: Qt.AlignVCenter
            radius: 8
            color: Theme.accentTint
            Icon { anchors.centerIn: parent; name: "folder-git"; size: 17; color: Theme.accent }
        }

        ColumnLayout {
            Layout.alignment: Qt.AlignVCenter
            Layout.maximumWidth: 260
            spacing: 0
            Text {
                objectName: "repositoryName"
                text: root.hasRepository ? root.workspace.selectedName : "未选择仓库"
                color: Theme.text
                font.pixelSize: Theme.fontSubheading
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                Layout.fillWidth: true
                HoverHandler { id: nameHover }
                ToolTip.visible: nameHover.hovered && root.hasRepository
                ToolTip.text: root.workspace ? (root.workspace.workspacePath + "/" + root.workspace.selectedPath) : ""
                ToolTip.delay: 500
            }
            Text {
                text: root.hasRepository ? root.workspace.selectedPath : "从左侧选择一个仓库"
                color: Theme.tertiaryText
                font.pixelSize: Theme.fontCaption
                elide: Text.ElideMiddle
                Layout.fillWidth: true
            }
        }

        // Branch chip
        Rectangle {
            objectName: "headerBranchButton"
            visible: root.hasRepository
            Layout.alignment: Qt.AlignVCenter
            implicitWidth: branchRow.implicitWidth + 18
            implicitHeight: 26
            radius: 13
            color: branchMouse.pressed ? "#33007AFF" : branchMouse.containsMouse ? "#29007AFF" : Theme.accentTint
            Behavior on color { ColorAnimation { duration: Theme.animationFast } }
            RowLayout {
                id: branchRow
                anchors.centerIn: parent
                spacing: 4
                Icon { name: "git-branch"; size: 13; strokeWidth: 1.8; color: Theme.accent }
                Text {
                    text: root.workspace ? root.workspace.selectedBranch : ""
                    color: Theme.accent
                    font.pixelSize: Theme.fontSecondary
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    Layout.maximumWidth: 180
                }
                Icon { name: "chevron-down"; size: 12; strokeWidth: 2; color: Theme.accent }
            }
            MouseArea {
                id: branchMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                ToolTip.visible: containsMouse
                ToolTip.text: "本地分支管理（切换 / 新建 / 删除）"
                ToolTip.delay: 500
                onClicked: root.branchRequested()
            }
        }

        Item { Layout.fillWidth: true; Layout.minimumWidth: 8 }

        SegmentedControl {
            Layout.alignment: Qt.AlignVCenter
            model: ["工作区改动", "提交历史"]
            currentIndex: root.currentTab
            onActivated: index => root.tabRequested(index)
        }

        Item { Layout.fillWidth: true; Layout.minimumWidth: 8 }

        RowLayout {
            spacing: 2
            Layout.alignment: Qt.AlignVCenter

            IconButton {
                iconName: "refresh"
                toolTip: "刷新当前仓库状态"
                enabled: root.workspace && !root.workspace.busy
                onClicked: root.workspace.refreshActive()
            }
            IconButton {
                visible: !root.compactActions
                iconName: "archive"
                toolTip: "贮藏管理"
                enabled: root.workspace && !root.workspace.busy && root.hasRepository
                onClicked: root.stashRequested()
            }
            IconButton {
                visible: !root.compactActions
                iconName: root.workspace && root.workspace.selectedIgnored ? "eye" : "eye-off"
                toolTip: root.workspace && root.workspace.selectedIgnored ? "恢复仓库显示" : "隐藏当前仓库（可在设置中恢复）"
                enabled: root.hasRepository
                onClicked: root.workspace.setActiveRepositoryIgnored(!root.workspace.selectedIgnored)
            }
            IconButton {
                iconName: "terminal"
                toolTip: "命令日志"
                onClicked: root.commandLogRequested()
            }
            IconButton {
                iconName: "settings"
                toolTip: "设置（Ctrl+,）"
                onClicked: root.settingsRequested()
            }
            IconButton {
                visible: root.compactActions
                iconName: "more-horizontal"
                toolTip: "仓库选项"
                onClicked: repositoryMenu.popup(0, height + 4)
                AppMenu {
                    id: repositoryMenu
                    AppMenuItem {
                        text: "贮藏管理…"
                        iconName: "archive"
                        enabled: root.hasRepository
                        onTriggered: root.stashRequested()
                    }
                    AppMenuItem {
                        text: root.workspace && root.workspace.selectedIgnored ? "恢复显示此仓库" : "隐藏此仓库"
                        iconName: root.workspace && root.workspace.selectedIgnored ? "eye" : "eye-off"
                        enabled: root.hasRepository
                        onTriggered: root.workspace.setActiveRepositoryIgnored(!root.workspace.selectedIgnored)
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: 1
                Layout.preferredHeight: 20
                Layout.leftMargin: 6
                Layout.rightMargin: 6
                color: Theme.separatorSoft
            }

            PrimaryButton {
                objectName: "headerPushBtn"
                iconName: "upload"
                text: "推送"
                toolTip: "推送当前仓库到 Gerrit 评审（Ctrl+Enter）"
                implicitHeight: 30
                enabled: root.workspace && !root.workspace.busy && root.hasRepository
                onClicked: root.pushRequested()
            }
        }
    }
}
