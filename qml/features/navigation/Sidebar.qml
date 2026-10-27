import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GerritPilot
import "../../components"

// Source-list sidebar: brand, project switcher, repositories, branches, status.
Rectangle {
    id: root
    clip: true

    property var workspace
    property bool collapsed: false

    signal toggleRequested()
    signal syncRequested()
    signal batchPushRequested()

    function openProjectManager(tabIndex) {
        if (typeof tabIndex === "number")
            projectManagerDialog.currentTab = tabIndex
        projectManagerDialog.open()
    }

    color: Theme.sidebar

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: root.collapsed ? 0 : 12
        anchors.rightMargin: root.collapsed ? 0 : 4
        anchors.topMargin: 14
        anchors.bottomMargin: 12
        spacing: 10

        // Brand
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: root.collapsed ? 0 : 4
            Layout.bottomMargin: 2
            spacing: 10
            visible: !root.collapsed
            AppIcon {
                implicitWidth: 30
                implicitHeight: 30
                Layout.alignment: Qt.AlignVCenter
            }
            ColumnLayout {
                spacing: 0
                Layout.fillWidth: true
                Text {
                    text: "GerritPilot"
                    color: Theme.text
                    font.pixelSize: Theme.fontHeading
                    font.weight: Font.Bold
                }
                Text {
                    text: "repo · Gerrit 工作台"
                    color: Theme.tertiaryText
                    font.pixelSize: Theme.fontCaption
                }
            }
            IconButton {
                iconName: "panel-left"
                toolTip: "收起侧栏（Ctrl+B）"
                implicitWidth: 28
                implicitHeight: 28
                onClicked: root.toggleRequested()
            }
        }

        // Collapsed rail
        ColumnLayout {
            visible: root.collapsed
            Layout.alignment: Qt.AlignHCenter
            spacing: 8
            AppIcon {
                implicitWidth: 34
                implicitHeight: 34
                Layout.alignment: Qt.AlignHCenter
                Layout.bottomMargin: 6
            }
            IconButton {
                Layout.alignment: Qt.AlignHCenter
                iconName: "panel-left"
                toolTip: "展开侧栏（Ctrl+B）"
                onClicked: root.toggleRequested()
            }
            IconButton {
                Layout.alignment: Qt.AlignHCenter
                iconName: "download"
                toolTip: "拉取代码…"
                enabled: root.workspace && !root.workspace.busy
                onClicked: root.syncRequested()
            }
            IconButton {
                Layout.alignment: Qt.AlignHCenter
                iconName: "refresh"
                toolTip: "刷新全部仓库状态（Ctrl+R）"
                enabled: root.workspace && !root.workspace.busy
                onClicked: root.workspace.refreshAll()
            }
        }

        ProjectSwitcher {
            visible: !root.collapsed
            Layout.fillWidth: true
            workspace: root.workspace
            onManageRequested: tab => root.openProjectManager(tab)
        }

        RepositoryList {
            visible: !root.collapsed
            Layout.fillWidth: true
            Layout.fillHeight: true
            workspace: root.workspace
            onSyncRequested: root.syncRequested()
            onBatchPushRequested: root.batchPushRequested()
        }

        BranchList {
            visible: !root.collapsed && root.workspace && root.workspace.selectedName.length > 0
            Layout.fillWidth: true
            workspace: root.workspace
        }

        Item { visible: root.collapsed; Layout.fillHeight: true }

        Spinner {
            visible: root.collapsed && root.workspace && root.workspace.busy
            running: visible
            Layout.alignment: Qt.AlignHCenter
        }

        WorkspaceStatusCard {
            visible: !root.collapsed
            Layout.fillWidth: true
            workspace: root.workspace
        }
    }

    ProjectManagerDialog {
        id: projectManagerDialog
        workspace: root.workspace
        repoManager: (typeof repoProject !== "undefined" ? repoProject : null)
    }
}
