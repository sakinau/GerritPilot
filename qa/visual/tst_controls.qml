import QtQuick
import QtQuick.Controls.Basic
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
        id: mockAi
        property bool busy: false
        property string validationError: ""
        function issueValidationError(type, issue, secondary) { return validationError }
        signal candidateReady(string candidate)
        signal errorOccurred(string message)
        function cancel() { busy = false }
    }

    QtObject {
        id: mock
        property var commitAi: mockAi
        property var groupedChanges: [{path: "example.cpp", staged: false, conflict: false, status: " M", group: "改动文件"}]
        property bool busy: false
        property bool detailsCached: false
        property bool canSkipCherryPick: false
        property string pendingCherryPickRevision: ""
        property string skippedRevision: ""
        property string selectedPath: "sample"
        property string workspacePath: "/visual-fixture"
        property string operationGeneration: "1"
        property var persistedDrafts: ({})
        property var installedWithResume: null
        property var remoteBranches: []
        property var remoteTrackingBranches: ["origin/master", "origin/release/alpha-fix", "review/topic"]
        property bool remoteBranchesLoaded: false
        property string remoteBranchesRemote: ""
        property string remote: "origin"
        property string pushRemoteUrl: "ssh://example.invalid/repository"
        property string targetBranch: ""
        property string topic: ""
        property string reviewers: ""
        property string cc: ""
        property bool pullRebase: false
        property bool isRepoWorkspace: false
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
        signal operationError(var error)
        signal lastCommitMessageReady(string workspacePath, string repositoryPath, string message)
        signal commitSucceeded(string workspacePath, string repositoryPath, string submittedMessage)
        signal operationFinished(string title)
        function loadCommitDraft(repository, workspacePath) {
            return persistedDrafts[(workspacePath || mock.workspacePath) + "\u0000" + (repository || selectedPath)] || ""
        }
        function saveCommitDraft(repository, text, workspacePath) {
            persistedDrafts[(workspacePath || mock.workspacePath) + "\u0000" + (repository || selectedPath)] = text
        }
        function installCommitHook(resume) { installedWithResume = resume }
        function skipCherryPick(revision) { skippedRevision = revision }
        function generateCommitMessage(message, scope, commitType, issueId, compiled, note, purpose, secondaryIssueId) {
            generatedOptions = {message: message, scope: scope, commitType: commitType, issueId: issueId,
                manualCompiled: compiled, compileNote: note, purpose: purpose, secondaryIssueId: secondaryIssueId}
        }
        function configurePullUpstream(remoteName, branchName) {
            configuredUpstream = {remote: remoteName, branch: branchName}
        }
        function requestRemoteBranches(remoteName) {
            remoteBranchesRemote = remoteName
            remoteBranches = ["release/alpha-fix", "release/alpha", "release/beta"]
            remoteBranchesLoaded = true
        }
        function pushCommandPreview(remoteName, branchName, options) {
            return branchName.length ? "git push " + remoteName + " HEAD:refs/for/" + branchName : "未指定目标分支"
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
    OperationErrorDialog { id: errors; workspace: mock }
    CherryPickSkipAction { id: skipAction; x: 920; y: 660; width: 180; workspace: mock }
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
    function test_emptyCherryPickSkipConfirmation() {
        const confirmation = findChild(skipAction, "skipCherryPickConfirmation")
        verify(!skipAction.visible)
        mock.pendingCherryPickRevision = "a".repeat(40)
        mock.canSkipCherryPick = true
        verify(skipAction.visible)
        mock.busy = true
        verify(!skipAction.enabled)
        mock.busy = false
        mouseClick(skipAction)
        tryCompare(confirmation, "opened", true)
        confirmation.reject()
        compare(mock.skippedRevision, "")
        mouseClick(skipAction)
        tryCompare(confirmation, "opened", true)
        mock.pendingCherryPickRevision = "b".repeat(40)
        confirmation.accept()
        compare(mock.skippedRevision, "")
        mouseClick(skipAction)
        tryCompare(confirmation, "opened", true)
        confirmation.accept()
        compare(mock.skippedRevision, "b".repeat(40))
        mock.skippedRevision = ""
        mock.pendingCherryPickRevision = ""
        mock.canSkipCherryPick = false
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
    function test_diffSearchIndex() {
        const rows = DiffParser.parse("@@ -1,2 +1,2 @@\n-Needle needle\n-old\n+new\n+needle\n")
        const index = DiffParser.searchIndex(rows, " NEEDLE ")
        compare(index.count, 3)
        compare(index.rows.length, 2)
        compare(DiffParser.matchRow(index, 0), 1)
        compare(DiffParser.matchRow(index, 1), 1)
        compare(DiffParser.matchRow(index, 2), 4)
        compare(DiffParser.matchRow(index, 3), -1)
        compare(DiffParser.matchRow(index, -1), -1)
        compare(DiffParser.searchIndex(rows, "-old").count, 0)
        compare(DiffParser.searchIndex(rows, "").count, 0)
        diff.diffText = "@@ -1,4500 +1,4500 @@\n"
            + "-old needle\n".repeat(4500) + "+new needle\n".repeat(4500)
        for (const split of [false, true]) {
            diff.sideBySide = split
            diff.searchQuery = "needle"
            compare(diff.searchMatchCount, 9000)
            diff.scrollToSearchMatch(8999)
            wait(30)
            verify(diff.windowStart > 0)
            compare(diff.windowEnd, diff.totalLineCount)
        }
        diff.renderingEnabled = false
        compare(diff.searchMatchCount, 0)
        compare(diff.searchIndex.rows.length, 0)
        diff.renderingEnabled = true
        diff.sideBySide = false
        diff.diffText = ""
    }

    function test_diffFullRenderRequiresConfirmation() {
        diff.sideBySide = false
        diff.diffText = "@@ -1,11000 +1,11000 @@\n"
            + "-old\n".repeat(11000) + "+new\n".repeat(11000)
        diff.requestFullDiff()
        const panel = findChild(diff, "fullDiffRenderConfirmation")
        tryCompare(panel, "opened", true)
        compare(diff.showAllDiff, false)
        mouseClick(panel.standardButton(Dialog.Cancel))
        tryCompare(panel, "opened", false)
        compare(diff.showAllDiff, false)
        diff.requestFullDiff()
        tryCompare(panel, "opened", true)
        // Switching the file must invalidate the previous confirmation.
        diff.diffText = "@@ -1 +1 @@\n-old\n+new\n"
        tryCompare(panel, "opened", false)
        diff.requestFullDiff()
        compare(diff.showAllDiff, true)
        diff.showAllDiff = false
        diff.diffText = ""
    }

    function test_largeDiffPagesRemainAccessible() {
        diff.width = 280
        diff.diffText = "@@ -1,4500 +1,4500 @@\n"
            + "-old line\n".repeat(4500) + "+new line\n".repeat(4500)
        for (const split of [false, true]) {
            diff.sideBySide = split
            diff.windowStart = 0
            diff.showAllDiff = false
            wait(80)
            compare(diff.totalLineCount, split ? 4501 : 9001)
            const banner = findChild(diff, "largeDiffBanner")
            for (const name of ["previousDiffPageButton", "nextDiffPageButton", "showAllDiffButton"]) {
                const button = findChild(diff, name)
                verify(button.visible)
                const p = button.mapToItem(banner, 0, 0)
                verify(p.x >= 0 && p.x + button.width <= banner.width + 1)
                verify(p.y >= 0 && p.y + button.height <= banner.height + 1)
            }
            let visited = 0
            while (true) {
                const rows = split ? diff.displayPairedRows : diff.displayParsedRows
                verify(rows.length <= diff.maxRenderedLines)
                visited += rows.length
                if (split) {
                    for (const pair of rows) {
                        if (pair.left && pair.left.kind === "remove") {
                            compare(pair.right.kind, "add")
                            compare(pair.left.oldNumber, pair.right.newNumber)
                        }
                    }
                }
                if (diff.windowEnd >= diff.totalLineCount) break
                mouseClick(findChild(diff, "nextDiffPageButton"))
                wait(20)
            }
            compare(visited, diff.totalLineCount, "all display rows reachable without omission or overlap")
            const lastRows = split ? diff.displayPairedRows : diff.displayParsedRows
            compare(split ? lastRows[lastRows.length - 1].right.newNumber
                          : lastRows[lastRows.length - 1].newNumber, 4500)
            mouseClick(findChild(diff, "showAllDiffButton"))
            wait(20)
            compare(diff.isTruncated, false)
            compare((split ? diff.displayPairedRows : diff.displayParsedRows).length, diff.totalLineCount)
        }
        diff.width = 700
        diff.sideBySide = false
        diff.diffText = ""
    }
    function test_longDiffMiddleActuallyPaints() {
        const scroll = findChild(diff, "diffScroll")
        diff.diffText = "@@ -1,4500 +1,4500 @@\n"
            + "-old_visible_code_0123456789\n".repeat(4500)
            + "+new_visible_code_0123456789\n".repeat(4500)
        for (const split of [false, true]) {
            diff.sideBySide = split
            for (const all of [false, true]) {
                diff.showAllDiff = all
                wait(80)
                const vertical = scroll.ScrollBar.vertical
                for (const fraction of [0.2, 0.5, 0.8, 0.1]) {
                    scroll.contentItem.contentY = 0
                    wait(20)
                    // Drag the actual thumb, then inspect pixels, not just contentHeight.
                    const thumb = vertical.contentItem
                    const targetY = fraction * (vertical.height - thumb.height)
                    mouseDrag(thumb, thumb.width / 2, thumb.height / 2,
                              0, targetY - thumb.y, Qt.LeftButton)
                    wait(80)
                    verify(scroll.contentItem.contentY > 0, "thumb moves code: split=" + split
                           + " all=" + all + " fraction=" + fraction)
                    const image = grabImage(test)
                    const origin = scroll.mapToItem(test, 0, 0)
                    const scale = image.width / test.width
                    let ink = 0
                    for (let y = 40; y < scroll.height - 40; y += 3) {
                        for (let x = 90; x < Math.min(300, scroll.width - 20); x += 3) {
                            const px = Math.floor((origin.x + x) * scale)
                            const py = Math.floor((origin.y + y) * scale)
                            if (image.red(px, py) < 180 || image.green(px, py) < 180
                                || image.blue(px, py) < 180) ++ink
                        }
                    }
                    verify(ink > 100, "code must paint in the middle: split=" + split
                           + " all=" + all + " fraction=" + fraction + " ink=" + ink)
                }
            }
        }
        diff.sideBySide = false
        diff.diffText = ""
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
        search.text = "release/alpha"
        tryCompare(branch, "count", 2)
        compare(push.canPush, false)
        push.chooseBranch(0)
        compare(push.canPush, true)
        compare(push.selectedTargetBranch, "release/alpha-fix")
        search.text = "beta"
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
        const secIssue = findChild(commit, "aiSecondaryIssueField")
        const generate = findChild(commit, "aiGenerateButton")
        verify(panel !== null && scope !== null && type !== null && issue !== null && secIssue !== null && generate !== null)
        tryCompare(panel, "opened", true)
        compare(mock.generatedOptions, null)
        compare(scope.currentIndex, 1) // No staged files: default to the working tree.
        scope.currentIndex = 1
        type.currentIndex = 0
        issue.text = "m-7114324437"
        secIssue.text = "r-7118000461"
        const purpose = findChild(commit, "aiCommitPurpose")
        purpose.text = "按需求更新提示文案"
        mouseClick(generate)
        compare(mock.generatedOptions.scope, 1)
        compare(mock.generatedOptions.commitType, "fix")
        compare(mock.generatedOptions.issueId, "m-7114324437")
        compare(mock.generatedOptions.secondaryIssueId, "r-7118000461")
        compare(mock.generatedOptions.purpose, "按需求更新提示文案")
        panel.generating = false
        type.currentIndex = 1
        compare(issue.visible, true)
        issue.text = "r-7118000461"
        secIssue.text = ""
        mouseClick(generate)
        compare(mock.generatedOptions.commitType, "feat")
        compare(mock.generatedOptions.issueId, "r-7118000461")
        compare(mock.generatedOptions.secondaryIssueId, "")
        purpose.text = ""
        panel.generating = false
        panel.close()
    }

    function test_aiPanelSurvivesStatusRefresh() {
        mouseClick(findChild(commit, "aiCommitButton"))
        const panel = findChild(commit, "aiCommitDialog")
        const issue = findChild(commit, "aiIssueField")
        tryCompare(panel, "opened", true)
        issue.text = "ISSUE-123"
        mock.detailsTextChanged()
        wait(20)
        compare(panel.opened, true)
        compare(issue.text, "ISSUE-123")
        mock.selectedRepositoryChanged()
        tryCompare(panel, "opened", false)
        issue.text = ""
    }

    function test_aiServiceResultsReachEditor() {
        const editor = findChild(commit, "commitMessage")
        const original = editor.text
        mouseClick(findChild(commit, "aiCommitButton"))
        const panel = findChild(commit, "aiCommitDialog")
        tryCompare(panel, "opened", true)
        panel.generating = true
        mockAi.errorOccurred("请求失败，保留原草稿")
        compare(panel.generating, false)
        compare(panel.errorText, "请求失败，保留原草稿")
        compare(editor.text, original)
        compare(panel.opened, true)
        panel.generating = true
        mockAi.candidateReady("m-7114324437 fix(sample): 更新说明")
        tryCompare(panel, "opened", false)
        compare(panel.generating, false)
        compare(editor.text, "m-7114324437 fix(sample): 更新说明")
        compare(commit.resultText, "AI 提交说明已生成")
        editor.text = original
        commit.resultText = ""
    }

    function test_aiValidationAndManualVerification() {
        mouseClick(findChild(commit, "aiCommitButton"))
        const panel = findChild(commit, "aiCommitDialog")
        const generate = findChild(commit, "aiGenerateButton")
        const compiled = findChild(commit, "aiManualCompileCheck")
        const note = findChild(commit, "aiCompileNoteField")
        tryCompare(panel, "opened", true)
        compare(compiled.checked, false)
        mockAi.validationError = "修复提交必须关联需求单"
        compare(generate.enabled, false)
        const previous = mock.generatedOptions
        mouseClick(generate)
        compare(mock.generatedOptions, previous)
        compare(panel.generating, false)
        mockAi.validationError = ""
        compare(generate.enabled, true)
        compiled.checked = true
        note.text = "Linux Debug"
        mouseClick(generate)
        compare(mock.generatedOptions.manualCompiled, true)
        compare(mock.generatedOptions.compileNote, "Linux Debug")
        panel.generating = false
        panel.close()
        panel.open()
        tryCompare(panel, "opened", true)
        compare(compiled.checked, false)
        compare(note.text, "")
        panel.close()
    }

    function test_draftStorageAndInFlightEdits() {
        const editor = findChild(commit, "commitMessage")
        editor.text = "original draft"
        mock.selectedPath = "other"
        mock.selectedRepositoryChanged()
        compare(editor.text, "")
        editor.text = "other draft"
        mock.selectedPath = "sample"
        mock.selectedRepositoryChanged()
        compare(editor.text, "original draft")
        editor.text = "newer in-flight edit"
        mock.commitSucceeded(mock.workspacePath, mock.selectedPath, "original draft")
        compare(editor.text, "newer in-flight edit")
        compare(mock.loadCommitDraft("sample", mock.workspacePath), "newer in-flight edit")
        mock.commitSucceeded(mock.workspacePath, mock.selectedPath, "newer in-flight edit")
        compare(editor.text, "")
        compare(mock.loadCommitDraft("sample", mock.workspacePath), "")
        compare(commit.drafts, undefined)
        mock.persistedDrafts = ({})
        commit.resultText = ""
    }

    function errorFixture(type, actions) {
        return {type: type, title: "Arbitrary title", message: "任意文案",
            actions: actions, workspacePath: mock.workspacePath,
            repositoryPath: mock.selectedPath, generation: mock.operationGeneration, operationId: "7"}
    }
    function test_errorActionsIgnoreWordingAndInvalidate() {
        const panel = findChild(errors, "operationErrorDialog")
        const hook = findChild(errors, "errorHookButton")
        const upstream = findChild(errors, "errorUpstreamButton")
        const conflict = findChild(errors, "errorConflictButton")
        let failure = errorFixture("general", [])
        failure.title = "AI 缺少 Change-Id Hook"
        failure.message = "上游 conflict 冲突 继续提交"
        mock.operationError(failure)
        tryCompare(panel, "opened", true)
        compare(hook.visible, false)
        compare(upstream.visible, false)
        compare(conflict.visible, false)
        panel.close()
        failure = errorFixture("hook", ["installHook", "resumeCommit"])
        mock.operationError(failure)
        tryCompare(panel, "opened", true)
        compare(hook.text, "安装 Hook 并继续提交")
        mouseClick(hook)
        compare(mock.installedWithResume, true)
        failure = errorFixture("upstream", ["upstream"])
        mock.operationError(failure)
        tryCompare(panel, "opened", true)
        compare(upstream.visible, true)
        compare(upstream.enabled, true)
        mock.operationGeneration = "2"
        compare(upstream.enabled, false)
        mock.selectedRepositoryChanged()
        tryCompare(panel, "opened", false)
        mock.operationGeneration = "1"
        mock.installedWithResume = null
    }
    function test_errorAiRoutesByType() {
        const panel = findChild(commit, "aiCommitDialog")
        const generic = findChild(errors, "operationErrorDialog")
        mouseClick(findChild(commit, "aiCommitButton"))
        tryCompare(panel, "opened", true)
        panel.generating = true
        const failure = errorFixture("ai", [])
        failure.message = "provider timed out"
        mock.operationError(failure)
        compare(panel.generating, false)
        compare(panel.errorText, "provider timed out")
        compare(generic.opened, false)
        panel.close()
        panel.errorText = ""
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
    function test_commitResizeKeepsReviewLinkVisible() {
        const scroll = findChild(commit, "commitMessageScroll")
        const editor = findChild(commit, "commitMessage")
        const card = findChild(commit, "gerritUrlCard")
        const copy = findChild(commit, "copyReviewUrlButton")
        const button = findChild(commit, "commitButton")
        editor.text = "Long AI-generated commit message\n".repeat(200)
        mock.pushRepositoryKey = commit.draftKey
        mock.pushState = 2
        mock.pushStatusText = "推送完成"
        mock.lastGerritReviewUrl = "https://example.invalid/c/project/+/12345"
        commit.resultText = "AI 提交说明已生成"
        commit.height = Math.max(320, commit.minimumPaneHeight)
        wait(40)
        verify(card.visible && copy.visible)
        for (const control of [button, card, copy]) {
            const p = control.mapToItem(commit, 0, 0)
            verify(p.y >= 0 && p.y + control.height <= commit.height + 1,
                   control.objectName + " remains in the fixed footer")
        }
        const smallHeight = scroll.height
        commit.height += 120
        wait(40)
        verify(scroll.height >= smallHeight + 119, "expanding the pane expands the message editor")
        verify(scroll.contentHeight > scroll.availableHeight, "long message scrolls internally")
        mock.pushState = 0
        mock.pushStatusText = ""
        mock.pushRepositoryKey = ""
        mock.lastGerritReviewUrl = ""
        commit.resultText = ""
        editor.text = ""
        commit.height = Qt.binding(function() { return Math.max(350, commit.implicitHeight) })
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
