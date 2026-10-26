pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GerritPilot
import "../../components"

GlassCard {
    id: root

    property var workspace
    property int currentTab: 0
    onCurrentTabChanged: {
        if (currentTab === 1 && workspace) workspace.loadHistory()
    }
    property bool sideBySideDiff: false

    signal tabRequested(int index)
    signal branchRequested()
    signal settingsRequested()
    signal revisionRequested(string revision)
    signal revisionCheckoutRequested(string revision)
    signal workingTreeRequested()
    signal syncRequested()
    signal commitRequested(string message, bool amend, bool stageAll)
    signal pushRequested()

    property var selectedChangePaths: []
    readonly property var selectedConflictPaths: {
        if (!root.workspace) return []
        const checked = changesFileList ? changesFileList.getCheckedPaths() : []
        const selected = checked.length > 0 ? checked : root.selectedChangePaths
        return selected.filter(function(path) {
            return root.workspace.groupedChanges.some(function(file) {
                return file.conflict && file.path === path
            })
        })
    }
    property string selectionRepository: ""

    readonly property alias checkedFilesCount: changesFileList.checkedFilesCount
    function clearCheckedFiles() { if (changesFileList) changesFileList.clearCheckedFiles() }
    function toggleFileCheck(path, staged, conflict) { if (changesFileList) changesFileList.toggleFileCheck(path, staged, conflict) }
    function getCheckedUnstaged() { return changesFileList ? changesFileList.getCheckedUnstaged() : [] }
    function getCheckedStaged() { return changesFileList ? changesFileList.getCheckedStaged() : [] }
    function getAllUnstaged() { return changesFileList ? changesFileList.getAllUnstaged() : [] }
    function getAllStaged() { return changesFileList ? changesFileList.getAllStaged() : [] }

    Connections {
        target: root.workspace
        function onSelectedRepositoryChanged() {
            if (root.currentTab === 1) Qt.callLater(function() { root.workspace.loadHistory() })
            if (root.selectionRepository !== root.workspace.selectedPath) {
                root.selectionRepository = root.workspace.selectedPath
                root.selectedChangePaths = []
            }
        }
        function onDetailsTextChanged() {
            root.selectedChangePaths = []
        }
    }

    function openCommandPanel() {
        const point = toolbar.mapToItem(Overlay.overlay, toolbar.width, toolbar.height)
        commandPopup.width = Math.min(640, Math.max(420, root.width - 36))
        commandPopup.x = Math.max(18, point.x - commandPopup.width - 12)
        commandPopup.y = point.y + 6
        commandPopup.open()
    }

    Layout.fillWidth: true
    Layout.fillHeight: true
    Layout.minimumWidth: 420
    elevated: true

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 1
        spacing: 0

        RepositoryToolbar {
            id: toolbar
            Layout.fillWidth: true
            workspace: root.workspace
            currentTab: root.currentTab
            onTabRequested: index => root.tabRequested(index)
            onBranchRequested: root.branchRequested()
            onSettingsRequested: root.settingsRequested()
            onPushRequested: root.pushRequested()
            onCommandLogRequested: root.openCommandPanel()
            onStashRequested: {
                root.workspace.loadStashList()
                stashDialog.open()
            }
        }

        OperationBanner {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.topMargin: 10
            workspace: root.workspace
            selectedConflictPaths: root.selectedConflictPaths
            onAbortRequested: confirmDialogs.confirmAbort()
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.currentTab
            clip: true

            SplitView {
                objectName: "changesSplit"
                orientation: Qt.Horizontal
                clip: true
                handle: SplitHandleBar { }

                Item {
                    objectName: "changesFilesPane"
                    SplitView.preferredWidth: 360
                    SplitView.minimumWidth: 300
                    SplitView.maximumWidth: 700
                    clip: true

                    SplitView {
                        objectName: "changesCommitSplit"
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 4
                        anchors.topMargin: 10
                        anchors.bottomMargin: 10
                        orientation: Qt.Vertical
                        clip: true
                        handle: SplitHandleBar { horizontalBar: true }

                        ChangesFileList {
                            id: changesFileList
                            SplitView.fillHeight: true
                            SplitView.minimumHeight: 120
                            workspace: root.workspace
                            onDiscardRequested: (paths) => {
                                root.selectedChangePaths = paths
                                confirmDialogs.confirmDiscard(paths)
                            }
                        }
                        CommitEditor {
                            objectName: "commitEditorPane"
                            SplitView.preferredHeight: 320
                            SplitView.minimumHeight: minimumPaneHeight
                            workspace: root.workspace
                            onCommitRequested: (message, amend, stageAll) => root.commitRequested(message, amend, stageAll)
                        }
                    }
                }

                ColumnLayout {
                    SplitView.fillWidth: true
                    SplitView.minimumWidth: 240
                    spacing: 0
                    clip: true

                    DiffToolbar {
                        Layout.fillWidth: true
                        workspace: root.workspace
                        sideBySide: root.sideBySideDiff
                        onSideBySideRequested: enabled => root.sideBySideDiff = enabled
                        onWorkingTreeRequested: root.workingTreeRequested()
                    }

                    DiffView {
                        id: changesDiffPane
                        objectName: "changesDiffPane"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.leftMargin: 8
                        Layout.topMargin: 8
                        clip: true
                        diffText: root.workspace ? root.workspace.diffText : ""
                        renderingEnabled: root.currentTab === 0
                        sideBySide: root.sideBySideDiff
                        workspace: root.workspace
                    }
                }
            }

            HistoryPane {
                id: historyPane
                objectName: "historyPane"
                workspace: root.workspace
                active: root.currentTab === 1
                onRevisionRequested: revision => root.revisionRequested(revision)
                onRevisionCheckoutRequested: revision => confirmDialogs.confirmCheckout(revision)
                onCherryPickRequested: revision => confirmDialogs.confirmCherryPick(revision)
                onRevertRequested: revision => confirmDialogs.confirmRevert(revision)
            }
        }
    }

    RepositoryConfirmDialogs {
        id: confirmDialogs
        workspace: root.workspace
        onRevisionCheckoutConfirmed: revision => root.revisionCheckoutRequested(revision)
    }

    CommandLogPopup {
        id: commandPopup
        workspace: root.workspace
    }

    StashDialog {
        id: stashDialog
        workspace: root.workspace
    }
}
