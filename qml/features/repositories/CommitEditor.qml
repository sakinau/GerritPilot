import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GerritPilot
import "../../components"
import "CommitIssues.js" as CommitIssues

Item {
    id: root
    property var workspace
    property string draftKey: ""
    property string currentRepoPath: ""
    property string currentWorkspacePath: ""
    property string resultText: ""
    readonly property bool hasStagedFiles: workspace ? workspace.groupedChanges.some(function(file) { return file.staged }) : false
    readonly property bool hasConflicts: workspace ? workspace.groupedChanges.some(function(file) { return file.conflict }) : false
    signal commitRequested(string message, bool amend, bool stageAll)

    clip: true
    readonly property real minimumPaneHeight: Math.ceil(mainContentCol.implicitHeight)
    implicitHeight: Math.max(320, minimumPaneHeight)

    Timer {
        id: draftSaveTimer
        interval: 500
        repeat: false
        onTriggered: {
            if (workspace && currentRepoPath.length && typeof workspace.saveCommitDraft === "function") {
                workspace.saveCommitDraft(currentRepoPath, editor.text, currentWorkspacePath)
            }
        }
    }

    function switchDraft() {
        if (!workspace) return
        const nextRepo = workspace.selectedPath || ""
        const nextWs = workspace.workspacePath || ""
        if (!nextRepo) {
            amendCheck.checked = false
            return
        }
        const nextKey = nextWs + "/" + nextRepo
        if (nextKey === draftKey) return
        amendCheck.checked = false

        draftSaveTimer.stop()

        // 1. Save previous draft using old workspace and old repository identity
        if (currentRepoPath.length && draftKey.length) {
            if (typeof workspace.saveCommitDraft === "function") {
                workspace.saveCommitDraft(currentRepoPath, editor.text, currentWorkspacePath)
            }
        }

        // 2. Load draft for the new workspace and repository
        let initialDraft = ""
        if (typeof workspace.loadCommitDraft === "function") {
            initialDraft = workspace.loadCommitDraft(nextRepo, nextWs) || ""
        }

        currentRepoPath = nextRepo
        currentWorkspacePath = nextWs
        draftKey = nextKey
        editor.text = initialDraft
        resultText = ""
    }
    Component.onCompleted: switchDraft()
    Connections {
        target: root.workspace
        ignoreUnknownSignals: true
        function onSelectedRepositoryChanged() {
            root.switchDraft()
        }
        function onLastCommitMessageReady(workspacePath, repositoryPath, message) {
            if (workspacePath + "/" + repositoryPath === root.draftKey
                && amendCheck.checked && editor.text.trim().length === 0)
                editor.text = message
        }
        function onCommitSucceeded(workspacePath, repositoryPath, submittedMessage) {
            const key = workspacePath + "/" + repositoryPath
            if (key === root.draftKey) {
                // Preserve text edited while Git was running.
                if (editor.text === submittedMessage) {
                    editor.text = ""
                }
                // Flush newer edits before any later switch/close can lose them.
                draftSaveTimer.stop()
                root.workspace.saveCommitDraft(root.currentRepoPath, editor.text, root.currentWorkspacePath)
                root.resultText = amendCheck.checked ? "修改提交成功" : "提交成功"
                amendCheck.checked = false
            }
        }
    }

    AiCommitDialog {
        id: aiDialog
        workspace: root.workspace
        existingMessage: editor.text
        hasStagedFiles: root.hasStagedFiles
        onCandidateAccepted: message => {
            editor.text = message
            draftSaveTimer.restart()
            root.resultText = "AI 提交说明已生成"
        }
        onGenerationFailed: message => root.resultText = "AI 生成提示: " + message
    }

    ColumnLayout {
        id: mainContentCol
        anchors.fill: parent
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            Text { text: "提交说明"; color: Theme.text; font.pixelSize: Theme.fontSubheading; font.weight: Font.DemiBold; Layout.alignment: Qt.AlignVCenter }
            Item { Layout.fillWidth: true }
            PrimaryButton {
                id: aiCommitButton
                objectName: "aiCommitButton"
                kind: "tinted"
                compact: true
                iconName: "sparkles"
                text: "AI 生成"
                toolTip: "打开 AI 提交说明面板"
                enabled: root.workspace && root.workspace.selectedPath.length > 0
                onClicked: aiDialog.open()
            }
        }
        ScrollView {
            id: messageScroll
            objectName: "commitMessageScroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 64
            implicitHeight: 64
            clip: true
            contentWidth: availableWidth
            ScrollBar.vertical: RightScrollBar { }
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            TextArea {
                id: editor
                objectName: "commitMessage"
                placeholderText: "填写本次提交说明…"
                selectByMouse: true
                wrapMode: TextEdit.Wrap
                padding: 10
                color: Theme.text
                font.pixelSize: Theme.fontBody
                onTextChanged: {
                    draftSaveTimer.restart()
                }
                placeholderTextColor: Theme.placeholder
                selectionColor: Theme.accentSoft
                selectedTextColor: Theme.text
                background: Rectangle {
                    color: Theme.surface
                    radius: Theme.radiusMedium
                    border.width: editor.activeFocus ? 2 : 1
                    border.color: editor.activeFocus ? Theme.focusRing : Theme.separator
                }
            }
        }

        readonly property string firstCommitLine: {
            var raw = editor.text.trim()
            if (!raw) return ""
            var lines = raw.split("\n")
            return lines.length > 0 ? lines[0].trim() : ""
        }
        readonly property var issueInfo: {
            var raw = editor.text.trim()
            if (!raw) return { hasIssues: false, allValid: false, detectedIds: "", rawBlock: "" }
            var lines = raw.split("\n")
            var line = lines.length > 0 ? lines[0].trim() : ""
            if (!line) return { hasIssues: false, allValid: false, detectedIds: "", rawBlock: "" }

            var match = line.match(/^([A-Za-z0-9._-]+(?:[,;，；]\s*[A-Za-z0-9._-]+)*)\s+([a-z]+)\([^)]*\):/i)
            if (!match) return { hasIssues: false, allValid: false, detectedIds: "", rawBlock: "" }
            var block = match[1].trim()
            var tokens = block.split(/[,;，；\s]+/)
            var expectedPrefix = match[2].toLowerCase() === "fix" ? "m-" : "r-"
            var allValid = tokens.every(function(token) {
                return /^(m|r)-[0-9]{10}$/.test(token)
                    && token.substring(2) !== "0000000000" && token.startsWith(expectedPrefix)
            })
            var req = raw.match(/^关联需求单:[ \t]*([^\r\n]+)$/m)
            var bug = raw.match(/^关联问题单:[ \t]*([^\r\n]+)$/m)
            var validFooter = function(value, prefix) {
                if (!value) return false
                var footerTokens = value[1].split(/[,;，；\s]+/).filter(function(token) { return token.length > 0 })
                return footerTokens.length > 0 && footerTokens.every(function(token) {
                    return new RegExp("^" + prefix + "[0-9]{10}$").test(token)
                        && token.substring(2) !== "0000000000"
                })
            }
            var hasReqTrailer = !!validFooter(req, "r-")
            var hasBugTrailer = !!validFooter(bug, "m-")
            return {
                hasIssues: true,
                allValid: allValid && (expectedPrefix !== "m-" || hasReqTrailer),
                detectedIds: tokens.join(","),
                rawBlock: block,
                hasReqTrailer: hasReqTrailer,
                hasBugTrailer: hasBugTrailer
            }
        }

        readonly property bool hasValidIssuePrefix: issueInfo.hasIssues && issueInfo.allValid
        readonly property string detectedIssuePrefixId: issueInfo.detectedIds

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            FlatCheckBox {
                id: amendCheck
                objectName: "amendCheck"
                text: "修改上次提交"
                enabled: !root.workspace.busy && root.workspace.selectedPath.length > 0
                onToggled: (checked) => {
                    if (checked && editor.text.trim().length === 0 && root.workspace && typeof root.workspace.requestLastCommitMessage === "function") {
                        root.workspace.requestLastCommitMessage()
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            visible: editor.text.trim().length > 0
            implicitHeight: issueStatusRow.implicitHeight + 8
            id: issueStrip
            readonly property var tone: Theme.tones[mainContentCol.hasValidIssuePrefix ? "success" : "warning"]
            radius: Theme.radiusSmall + 1
            color: tone.background
            border.color: tone.border
            border.width: 1

            RowLayout {
                id: issueStatusRow
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 6

                Icon {
                    name: mainContentCol.hasValidIssuePrefix ? "check-circle" : "alert-triangle"
                    size: 13
                    color: issueStrip.tone.foreground
                }

                Text {
                    Layout.fillWidth: true
                    text: {
                        if (!mainContentCol.hasValidIssuePrefix) {
                            return "请核对 10 位非零单号和类型前缀；fix 还须关联需求单 footer"
                        }
                        var desc = "已关联飞书单号: " + mainContentCol.detectedIssuePrefixId
                        if (mainContentCol.issueInfo.hasReqTrailer) {
                            desc += " · 已双关联需求单"
                        } else if (mainContentCol.issueInfo.hasBugTrailer) {
                            desc += " · 已伴随关联问题单"
                        }
                        return desc
                    }
                    color: issueStrip.tone.foreground
                    font.pixelSize: Theme.fontCaption
                    elide: Text.ElideRight
                }

                Rectangle {
                    visible: !mainContentCol.hasValidIssuePrefix
                    implicitWidth: quickPrefixBtnText.implicitWidth + 14
                    implicitHeight: 20
                    radius: 10
                    color: quickPrefixMouse.containsMouse ? "#FFB340" : Theme.orange
                    Text {
                        id: quickPrefixBtnText
                        anchors.centerIn: parent
                        text: "补全单号"
                        font.pixelSize: Theme.fontCaption
                        font.weight: Font.DemiBold
                        color: Theme.textOnAccent
                    }
                    MouseArea {
                        id: quickPrefixMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            var defaultPrefix = (editor.text.indexOf("fix") !== -1 ? "m-" : "r-")
                            if (mainContentCol.issueInfo.hasIssues && mainContentCol.issueInfo.rawBlock) {
                                var raw = mainContentCol.issueInfo.rawBlock
                                var formatted = CommitIssues.formatMultipleIssues(raw, defaultPrefix)
                                var currentText = editor.text
                                var idx = currentText.indexOf(raw)
                                if (idx === 0) {
                                    editor.text = formatted + currentText.substring(raw.length)
                                } else {
                                    editor.text = formatted + " " + currentText
                                }
                            } else {
                                editor.text = defaultPrefix + "实例ID " + editor.text
                                editor.cursorPosition = defaultPrefix.length
                                editor.select(defaultPrefix.length, defaultPrefix.length + 4)
                            }
                            editor.forceActiveFocus()
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            PrimaryButton {
                objectName: "commitButton"
                Layout.fillWidth: true
                text: amendCheck.checked ? "修改上次提交" : "提交"
                enabled: !root.workspace.busy && !root.workspace.detailsCached
                         && root.workspace.selectedPath.length > 0
                         && (root.hasStagedFiles || amendCheck.checked)
                         && !root.hasConflicts && editor.text.trim().length > 0
                onClicked: {
                    root.resultText = ""
                    root.commitRequested(editor.text, amendCheck.checked, false)
                }
            }
        }

        RowLayout {
            objectName: "pushStatusRow"
            Layout.fillWidth: true
            spacing: 6
            visible: root.workspace && root.workspace.pushState !== undefined
                     && root.workspace.pushState !== 0
                     && root.workspace.pushRepositoryKey === root.draftKey

            Spinner {
                objectName: "pushBusyIndicator"
                Layout.preferredWidth: Theme.controlHeight * 0.65
                Layout.preferredHeight: Theme.controlHeight * 0.65
                visible: root.workspace && root.workspace.pushState === 1
                running: visible
            }

            Text {
                Layout.fillWidth: true
                text: root.workspace ? root.workspace.pushStatusText : ""
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
                color: root.workspace && root.workspace.pushState === 3 ? Theme.red : Theme.secondaryText
                font.pixelSize: Theme.fontSecondary
            }
        }

        Text {
            Layout.fillWidth: true
            visible: root.resultText.length > 0
            text: root.resultText
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
            color: Theme.secondaryText
            font.pixelSize: Theme.fontSecondary
        }

        // Gerrit Review URL Card
        Rectangle {
            objectName: "gerritUrlCard"
            Layout.fillWidth: true
            Layout.preferredHeight: gerritUrlRow.implicitHeight + 12
            radius: Theme.radiusMedium
            color: Theme.tones.success.background
            border.color: Theme.tones.success.border
            visible: root.workspace && root.workspace.pushRepositoryKey === root.draftKey
                     && root.workspace.lastGerritReviewUrl && root.workspace.lastGerritReviewUrl.length > 0

            RowLayout {
                id: gerritUrlRow
                anchors.fill: parent
                anchors.margins: 6
                anchors.leftMargin: 10
                spacing: 8

                Icon {
                    name: "link"
                    size: 15
                    color: Theme.tones.success.foreground
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Text {
                        text: "Gerrit 审查单已就绪"
                        color: Theme.tones.success.foreground
                        font.pixelSize: Theme.fontSecondary
                        font.bold: true
                    }
                    Text {
                        text: root.workspace ? root.workspace.lastGerritReviewUrl : ""
                        color: Theme.accent
                        font.pixelSize: Theme.fontSecondary
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                }

                IconButton {
                    objectName: "copyReviewUrlButton"
                    iconName: "copy"
                    toolTip: "复制审查链接"
                    Layout.preferredHeight: Theme.controlHeight
                    Layout.preferredWidth: Theme.controlHeight
                    onClicked: {
                        if (root.workspace && typeof root.workspace.copyToClipboard === "function") {
                            root.workspace.copyToClipboard(root.workspace.lastGerritReviewUrl)
                            root.resultText = "审查链接已复制"
                        }
                    }
                }
            }
        }
    }
}
