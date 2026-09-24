import QtQuick
import QtQuick.Controls
import QtTest
import GerritPilot
import "../../qml/components"
import "../../qml/features/repositories"
import "../../qml/features/overlays"
import "../../qml/components/DiffParser.js" as DiffParser

TestCase {
    id: test
    name: "VisualControls"
    width: 1120
    height: 720
    visible: true
    when: windowShown

    QtObject {
        id: mock
        property var groupedChanges: [{path: "example.cpp", staged: false, conflict: false, status: " M", group: "改动文件"}]
        property bool busy: false
        property bool detailsCached: false
        property string selectedPath: "sample"
        property string workspacePath: "/visual-fixture"
        property var remoteBranches: []
        property var remoteTrackingBranches: ["origin/master", "origin/T1TP_FX", "review/topic"]
        property bool remoteBranchesLoaded: false
        property string remoteBranchesRemote: ""
        property string remote: "origin"
        property string pushRemoteUrl: "ssh://example.invalid/repository"
        property string targetBranch: ""
        property string topic: ""
        property string selectedName: "sample"
        property string selectedBranch: "local"
        property string gerritHost: "example.invalid"
        property string gerritUser: "tester"
        property int gerritPort: 29418
        property int pushState: 0
        property string pushStatusText: ""
        property string pushRepositoryKey: ""
        property var generatedOptions: null
        property var configuredUpstream: null
        property string lastGerritReviewUrl: ""
        property string selectedFile: "example.cpp"
        property bool selectedFileStaged: false
        property string diffText: diff.diffText
        property int hunkCount: 2
        signal selectedRepositoryChanged()
        signal detailsTextChanged()
        signal operationFailed(string title, string message)
        signal lastCommitMessageReady(string workspacePath, string repositoryPath, string message)
        signal commitSucceeded(string workspacePath, string repositoryPath, string submittedMessage)
        signal operationFinished(string title)
        function loadCommitDraft() { return "" }
        function saveCommitDraft() { }
        function generateCommitMessage(message, scope, commitType, issueId) {
            generatedOptions = {message: message, scope: scope, commitType: commitType, issueId: issueId}
        }
        function configurePullUpstream(remoteName, branchName) {
            configuredUpstream = {remote: remoteName, branch: branchName}
        }
        function requestRemoteBranches(remoteName) {
            remoteBranchesRemote = remoteName
            remoteBranches = ["T1TP_FX", "T1TP", "T13C_BEV"]
            remoteBranchesLoaded = true
        }
    }
    CommitEditor {
        id: commit
        x: 16; y: 16
        width: 340
        height: Math.max(350, implicitHeight)
        workspace: mock
    }
    ChangesFileList { id: changeList; visible: false; workspace: mock }
    ActionDialogs { id: actionDialogs; workspace: mock }
    AppDialog {
        id: dialog
        title: "确认操作"
        standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: Label { text: "视觉验证，不执行任何 Git 命令。" }
    }
    SignalSpy { id: acceptedSpy; target: dialog; signalName: "accepted" }
    SignalSpy { id: rejectedSpy; target: dialog; signalName: "rejected" }
    RightScrollBar { id: bar; parent: test; size: 0.2; active: false }
    PrimaryButton { id: typographyButton; x: 16; y: 620; width: 150; text: "提交 Commit"; glyph: "↑" }
    function test_typography() {
        compare(typographyButton.font.pixelSize, Theme.fontBody)
        const label = findChild(typographyButton, "buttonLabel")
        verify(label !== null)
        const p = label.mapToItem(typographyButton, 0, 0)
        verify(Math.abs(p.y + label.height / 2 - typographyButton.height / 2) <= 1)
        verify(p.x >= 0 && p.x + label.width <= typographyButton.width)
        compare(findChild(commit, "commitMessage").font.pixelSize, Theme.fontBody)
        typographyButton.text = "很长的提交按钮文字需要保持在按钮边界内"
        wait(20)
        const longPos = label.mapToItem(typographyButton, 0, 0)
        verify(longPos.x >= 0 && longPos.x + label.width <= typographyButton.width)
        typographyButton.text = "提交 Commit"
    }
    DiffView { id: diff; x: 380; width: 700; height: 600; workspace: mock }

    function test_narrowDiffInteraction() {
        diff.width = 280
        diff.sideBySide = true
        diff.diffText = "diff --git a/a b/a\n--- a/a\n+++ b/a\n@@ -1 +1 @@\n-old\n+" + "long code ".repeat(100) + "\n"
        wait(80)
        const toolbar = findChild(diff, "toolbarFlickable")
        const stage = findChild(diff, "stageWholeFileButton")
        compare(findChild(diff, "fontPlusBtn"), null)
        compare(findChild(diff, "fontMinusBtn"), null)
        compare(stage.width, Theme.controlHeight)
        compare(stage.height, Theme.controlHeight)
        compare(stage.toolTip, "暂存整个文件")
        for (const button of [stage]) {
            verify(button.visible, button.objectName + " worktree=" + diff.isWorktreeDiff + " hunks=" + diff.hunkTotal)
            const p = button.mapToItem(toolbar, 0, 0)
            verify(p.x >= 0 && p.x + button.width <= toolbar.width + 1)
            verify(p.y >= 0 && p.y + button.height <= toolbar.height + 1)
        }
        const scroll = findChild(diff, "diffScroll")
        const bar = findChild(diff, "diffHorizontalBar")
        verify(bar.visible && bar.contentItem.opacity > 0)
        const p = bar.mapToItem(diff, 0, 0)
        verify(p.x >= 0 && p.x + bar.width <= diff.width + 1)
        verify(p.y >= 0 && p.y + bar.height <= diff.height + 1)
        mouseDrag(bar.contentItem, bar.contentItem.width / 2, bar.contentItem.height / 2, 100, 0, Qt.LeftButton)
        wait(50)
        verify(scroll.contentItem.contentX > 0, "horizontal thumb drag moves actual code")
        scroll.contentItem.contentX = 0
        mouseDrag(scroll, 160, 80, -80, 0, Qt.MiddleButton)
        wait(30)
        verify(scroll.contentItem.contentX > 0, "middle mouse drag pans code without selecting it")
        scroll.contentItem.contentX = 0
        mouseWheel(scroll, 160, 80, 0, -120, Qt.NoButton, Qt.ShiftModifier)
        wait(30)
        verify(scroll.contentItem.contentX > 0, "Shift wheel pans horizontally")
        compare(diff.codeFontSize, Math.max(8, Math.min(22, Theme.codeFontPointSize)))
        diff.width = 700
        diff.diffText = ""
    }

    function test_diffOverflow() {
        const scroll = findChild(diff, "diffScroll")
        for (const split of [false, true]) {
            diff.sideBySide = split
            diff.diffText = "diff --git a/a b/a\n--- a/a\n+++ b/a\n@@ -1 +1 @@\n-old\n+new\n"
            wait(80)
            verify(scroll.contentWidth <= scroll.availableWidth + 1, "short diff fits horizontally")
            verify(scroll.contentHeight <= scroll.availableHeight + 1, "short diff fits vertically")
            diff.diffText += "+" + "long line ".repeat(200) + "\n"
            wait(80)
            verify(scroll.contentWidth > scroll.availableWidth, "long line remains horizontally accessible")
            diff.diffText = "diff --git a/a b/a\n--- a/a\n+++ b/a\n@@ -1 +1,200 @@\n-old\n" + "+line\n".repeat(200)
            wait(80)
            verify(scroll.contentHeight > scroll.availableHeight, "long diff remains vertically accessible")
        }
        diff.diffText = ""
    }
    function test_inactiveDiffReleasesView() {
        diff.sideBySide = false
        diff.diffText = "diff --git a/a b/a\n@@ -1 +1 @@\n-old\n+new\n"
        wait(30)
        verify(findChild(diff, "unifiedDiffText") !== null)
        diff.renderingEnabled = false
        wait(30)
        compare(diff.totalLineCount, 0)
        compare(findChild(diff, "unifiedDiffText"), null)
        diff.renderingEnabled = true
        wait(30)
        verify(findChild(diff, "unifiedDiffText") !== null)
        diff.diffText = ""
    }
    function test_diffParserLineBoundaries() {
        compare(DiffParser.parse("").length, 0)
        compare(DiffParser.parse("\n").length, 1)
        const rows = DiffParser.parse("@@ -1 +1 @@\n-old\n+new\n")
        compare(rows.length, 3)
        compare(rows[0].kind, "hunk")
        compare(rows[1].kind, "remove")
        compare(rows[2].kind, "add")
        compare(DiffParser.parse("meta without trailing newline").length, 1)
    }
    function test_singleCommitEntry() {
        compare(findChild(commit, "commitAndPushButton"), null)
        compare(findChild(commit, "commitAllButton"), null)
        compare(findChild(commit, "targetBranchCombo"), null)
        compare(findChild(commit, "pushGerritBtn"), null)
        compare(findChild(commit, "commitButton").text, "提交")
    }

    function test_pushBranchSearch() {
        actionDialogs.openPush()
        const push = findChild(actionDialogs, "pushDialog")
        const branch = findChild(actionDialogs, "branchCombo")
        const search = findChild(actionDialogs, "branchSearchInput")
        verify(push !== null && branch !== null && search !== null)
        tryCompare(push, "opened", true)
        search.text = "T1TP"
        tryCompare(branch, "count", 2)
        compare(push.canPush, false)
        push.chooseBranch(0)
        compare(push.canPush, true)
        compare(push.selectedTargetBranch, "T1TP_FX")
        search.text = "T13"
        compare(push.canPush, false)
        compare(branch.count, 1)
        push.close()
    }

    function test_pushProgressFeedback() {
        mock.pushRepositoryKey = mock.workspacePath + "/" + mock.selectedPath
        mock.pushStatusText = "正在推送…"
        mock.pushState = 1
        const row = findChild(commit, "pushStatusRow")
        const indicator = findChild(commit, "pushBusyIndicator")
        verify(row !== null && indicator !== null)
        compare(row.visible, true)
        compare(indicator.running, true)
        mock.pushState = 3
        mock.pushStatusText = "推送未确认"
        compare(indicator.running, false)
        compare(row.visible, true)
        mock.pushState = 0
    }

    function test_upstreamSelection() {
        actionDialogs.openUpstream()
        const panel = findChild(actionDialogs, "upstreamDialog")
        const remote = findChild(actionDialogs, "upstreamRemote")
        const branch = findChild(actionDialogs, "upstreamBranch")
        verify(panel !== null && remote !== null && branch !== null)
        tryCompare(panel, "opened", true)
        compare(remote.text, "origin")
        compare(panel.suggestions.length, 2)
        compare(panel.canConfigure, false)
        branch.text = "master"
        compare(panel.canConfigure, true)
        mouseClick(panel.standardButton(Dialog.Ok))
        compare(mock.configuredUpstream.remote, "origin")
        compare(mock.configuredUpstream.branch, "master")
    }

    function test_aiCommitPanelOptions() {
        mouseClick(findChild(commit, "aiCommitButton"))
        const panel = findChild(commit, "aiCommitDialog")
        const scope = findChild(commit, "aiScopeCombo")
        const type = findChild(commit, "aiTypeCombo")
        const issue = findChild(commit, "aiIssueField")
        const generate = findChild(commit, "aiGenerateButton")
        verify(panel !== null && scope !== null && type !== null && issue !== null && generate !== null)
        tryCompare(panel, "opened", true)
        compare(mock.generatedOptions, null)
        compare(scope.currentIndex, 0)
        scope.currentIndex = 1
        type.currentIndex = 0
        issue.text = "m-12345"
        mouseClick(generate)
        compare(mock.generatedOptions.scope, 1)
        compare(mock.generatedOptions.commitType, "fix")
        compare(mock.generatedOptions.issueId, "m-12345")
        panel.generating = false
        type.currentIndex = 1
        compare(issue.visible, false)
        mouseClick(generate)
        compare(mock.generatedOptions.commitType, "feat")
        compare(mock.generatedOptions.issueId, "")
        panel.generating = false
        panel.close()
    }

    function test_conflictExcludedFromBulkStage() {
        mock.groupedChanges = [
            {path: "conflict.cpp", staged: false, conflict: true, status: "UU", group: "冲突文件"},
            {path: "normal.cpp", staged: false, conflict: false, status: " M", group: "改动文件"}
        ]
        changeList.toggleFileCheck("conflict.cpp", false, true)
        changeList.toggleFileCheck("normal.cpp", false, false)
        compare(changeList.getCheckedPaths().length, 2)
        compare(changeList.getCheckedUnstaged().length, 1)
        compare(changeList.getCheckedUnstaged()[0], "normal.cpp")
        changeList.clearCheckedFiles()
        mock.groupedChanges = [{path: "example.cpp", staged: false, conflict: false, status: " M", group: "改动文件"}]
    }

    function test_commitBounds() {
        const editor = findChild(commit, "commitMessage")
        const button = findChild(commit, "commitButton")
        verify(editor !== null && button !== null)
        wait(30)
        const oldHeight = commit.height
        editor.text = "A long commit message\n".repeat(200)
        wait(30)
        compare(commit.height, oldHeight)
        verify(commit.height < 550, "commit pane leaves room for file list at minimum window size")
        const pos = button.mapToItem(commit, 0, 0)
        verify(pos.y >= 0 && pos.y + button.height <= commit.height)
        editor.text = ""
    }
    function test_dialogButtons() {
        dialog.open()
        tryCompare(dialog, "opened", true)
        verify(dialog.x >= 0 && dialog.y >= 0)
        verify(dialog.x + dialog.width <= width)
        mouseClick(dialog.standardButton(Dialog.Ok))
        compare(acceptedSpy.count, 1)
        dialog.open()
        tryCompare(dialog, "opened", true)
        mouseClick(dialog.standardButton(Dialog.Cancel))
        compare(rejectedSpy.count, 1)
    }
    function test_scrollIndicator() {
        bar.active = false
        compare(bar.contentItem.opacity, 0)
        bar.active = true
        verify(bar.contentItem.opacity > 0)
        bar.size = 1
        compare(bar.contentItem.opacity, 0)
    }
}
