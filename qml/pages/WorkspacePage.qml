pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import GerritPilot
import "../components"
import "../features/navigation"

Item {
    id: page
    width: 1440
    height: 900

    property int detailTab: 0
    property bool sidebarCollapsed: false
    readonly property var workspaceApi: workspace

    Component.onCompleted: {
        if (workspace.workspacePath.length > 0)
            workspace.scanWorkspace()
        else
            Qt.callLater(function() { sidebar.openProjectManager(0) })
    }

    Shortcut { sequence: "Ctrl+R"; enabled: !repoProject.busy; onActivated: workspace.refreshAll() }
    Shortcut { sequence: "Ctrl+Enter"; enabled: !repoProject.busy; onActivated: actionDialogs.openPush() }
    Shortcut { sequence: "Ctrl+,"; onActivated: settingsPanel.open() }
    Shortcut { sequence: "Ctrl+B"; onActivated: page.sidebarCollapsed = !page.sidebarCollapsed }

    SplitView {
        enabled: !repoProject.busy
        anchors.fill: parent
        orientation: Qt.Horizontal
        handle: Rectangle {
            implicitWidth: 6
            color: "transparent"
            Rectangle {
                anchors.centerIn: parent
                width: (SplitHandle.pressed || SplitHandle.hovered) ? 2 : 1
                height: parent.height
                color: (SplitHandle.pressed || SplitHandle.hovered) ? Theme.accent : Theme.separator
            }
        }

        Sidebar {
            id: sidebar
            SplitView.preferredWidth: page.sidebarCollapsed ? 72 : 286
            SplitView.minimumWidth: page.sidebarCollapsed ? 72 : 240
            SplitView.maximumWidth: 380
            workspace: page.workspaceApi
            collapsed: page.sidebarCollapsed
            onToggleRequested: page.sidebarCollapsed = !page.sidebarCollapsed
            onSettingsRequested: settingsPanel.open()
            onSyncRequested: actionDialogs.openSync()
            onBranchRequested: branchPanel.open()
        }

        Item {
            SplitView.fillWidth: true

            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                anchors.topMargin: 12
                anchors.bottomMargin: 12
                spacing: 15

                RepositoryDetailPane {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    workspace: page.workspaceApi
                    currentTab: page.detailTab
                    onTabRequested: page.detailTab = index
                    onBranchRequested: branchPanel.open()
                    onSettingsRequested: settingsPanel.open()
                    onRevisionRequested: {
                        workspace.showRevisionDiff(revision)
                        page.detailTab = 1
                    }
                    onRevisionCheckoutRequested: workspace.checkoutRevision(revision)
                    onWorkingTreeRequested: workspace.showWorkingTreeDiff()
                    onSyncRequested: actionDialogs.openSync()
                    onCommitRequested: actionDialogs.commit(message, amend, stageAll)
                    onPushRequested: actionDialogs.openPush()
                }
            }
        }
    }

    WorkspaceFolderDialog {
        id: folderDialog
        workspace: page.workspaceApi
    }

    ActionDialogs {
        id: actionDialogs
        workspace: page.workspaceApi
    }

    BranchPanel {
        id: branchPanel
        workspace: page.workspaceApi
        hostWidth: page.width
    }

    SettingsPanel {
        id: settingsPanel
        workspace: page.workspaceApi
        projectManager: repoProject
        hostWidth: page.width
        hostHeight: page.height
    }

    OperationErrorDialog {
        workspace: page.workspaceApi
        onUpstreamRequested: actionDialogs.openUpstream()
        onConflictRequested: page.detailTab = 0
    }
}
