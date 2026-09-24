import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GerritPilot
import "../../components"

ColumnLayout {
    id: root
    property var workspace
    property var drafts: ({})
    property string draftKey: ""
    property string currentRepoPath: ""
    property string currentWorkspacePath: ""
    property string resultText: ""
    readonly property bool hasStagedFiles: workspace.groupedChanges.some(function(file) { return file.staged })
    readonly property bool hasConflicts: workspace.groupedChanges.some(function(file) { return file.conflict })
    signal commitRequested(string message, bool amend, bool stageAll)
    spacing: 8

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
            drafts[draftKey] = editor.text
            if (typeof workspace.saveCommitDraft === "function") {
                workspace.saveCommitDraft(currentRepoPath, editor.text, currentWorkspacePath)
            }
        }

        // 2. Load draft for the new workspace and repository
        let initialDraft = ""
        if (typeof workspace.loadCommitDraft === "function") {
            initialDraft = workspace.loadCommitDraft(nextRepo, nextWs) || ""
        }
        if (!initialDraft && drafts[nextKey]) {
            initialDraft = drafts[nextKey]
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
        function onSelectedRepositoryChanged() {
            aiDialog.generating = false
            aiDialog.close()
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
                    delete root.drafts[key]
                }
                root.resultText = amendCheck.checked ? "修改提交成功" : "提交成功"
                amendCheck.checked = false
            } else if (root.drafts[key] === submittedMessage) {
                delete root.drafts[key]
            }
        }
        function onOperationFailed(title, message) {
            if (aiDialog.opened && title.indexOf("AI") === 0) {
                aiDialog.generating = false
                aiDialog.errorText = message
            }
        }
    }

    Connections {
        target: (root.workspace && root.workspace.commitAi) ? root.workspace.commitAi : null
        function onCandidateReady(candidate) {
            aiDialog.generating = false
            aiDialog.close()
            if (candidate && candidate.length > 0) {
                editor.text = candidate
                root.drafts[root.draftKey] = candidate
                draftSaveTimer.restart()
                root.resultText = "AI 提交说明已生成"
            }
        }
        function onErrorOccurred(error) {
            aiDialog.generating = false
            aiDialog.errorText = error
            root.resultText = "AI 生成提示: " + error
        }
    }

    AppDialog {
        id: aiDialog
        objectName: "aiCommitDialog"
        preferredWidth: 500
        title: "AI 生成提交说明"
        standardButtons: Dialog.NoButton
        closePolicy: generating ? Popup.NoAutoClose : Popup.CloseOnEscape
        property bool generating: false
        property string errorText: ""

        onOpened: errorText = ""

        contentItem: ColumnLayout {
            spacing: 12
            Text {
                text: "改动范围"
                color: Theme.text
                font.pixelSize: Theme.fontSecondary
                font.weight: Font.DemiBold
            }
            ComboBox {
                id: aiScopeCombo
                objectName: "aiScopeCombo"
                Layout.fillWidth: true
                model: ["仅暂存区", "整个当前仓库"]
                enabled: !aiDialog.generating
            }
            Text {
                Layout.fillWidth: true
                text: aiScopeCombo.currentIndex === 0
                    ? "只依据已暂存的文件生成。"
                    : "包含已暂存、未暂存及可读取的未跟踪文本文件片段；提交前请核对暂存区，避免把未提交内容写进说明。"
                wrapMode: Text.Wrap
                color: Theme.secondaryText
                font.pixelSize: Theme.fontCaption
            }
            Text {
                text: "提交类型"
                color: Theme.text
                font.pixelSize: Theme.fontSecondary
                font.weight: Font.DemiBold
            }
            ComboBox {
                id: aiTypeCombo
                objectName: "aiTypeCombo"
                Layout.fillWidth: true
                readonly property var typeKeys: [
                    "fix", "feat", "chg", "refactor", "docs", "style", "test", "chore", "add", "del", "merge"
                ]
                model: [
                    "fix · 修复问题",
                    "feat · 新增功能",
                    "chg · 变更优化",
                    "refactor · 代码重构",
                    "docs · 文档修改",
                    "style · 格式规范",
                    "test · 测试用例",
                    "chore · 杂项构建",
                    "add · 新增模块/文件",
                    "del · 删除废弃文件",
                    "merge · 分支合并"
                ]
                enabled: !aiDialog.generating
            }
            Text {
                text: "需求 / 问题单号"
                color: Theme.text
                font.pixelSize: Theme.fontSecondary
                font.weight: Font.DemiBold
            }
            TextField {
                id: aiIssueField
                objectName: "aiIssueField"
                Layout.fillWidth: true
                visible: true
                enabled: !aiDialog.generating
                placeholderText: "需求/问题单号（可选，如 7063607059 或 m-xxx, r-xxx，逗号分隔多个，留空默认 m-0）"
                font.pixelSize: Theme.fontSecondary
            }
            Text {
                Layout.fillWidth: true
                visible: aiScopeCombo.currentIndex === 1
                text: "整个仓库的改动片段将发送给已配置的 AI 接口；请留意敏感文件。"
                wrapMode: Text.Wrap
                color: Theme.orange
                font.pixelSize: Theme.fontCaption
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                BusyIndicator {
                    objectName: "aiCommitBusyIndicator"
                    Layout.preferredWidth: Theme.controlHeight * 0.7
                    Layout.preferredHeight: Theme.controlHeight * 0.7
                    visible: aiDialog.generating
                    running: visible
                }
                Text {
                    Layout.fillWidth: true
                    text: aiDialog.errorText || (aiDialog.generating ? "正在读取改动并生成说明…" : "")
                    visible: text.length > 0
                    wrapMode: Text.Wrap
                    color: aiDialog.errorText ? Theme.red : Theme.secondaryText
                    font.pixelSize: Theme.fontSecondary
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Item { Layout.fillWidth: true }
                PrimaryButton {
                    text: aiDialog.generating ? "取消生成" : "关闭"
                    secondary: true
                    onClicked: {
                        if (aiDialog.generating && root.workspace) {
                            if (root.workspace.commitAi && root.workspace.commitAi.busy)
                                root.workspace.commitAi.cancel()
                            else if (root.workspace.busy && typeof root.workspace.stop === "function")
                                root.workspace.stop()
                        }
                        aiDialog.generating = false
                        aiDialog.close()
                    }
                }
                PrimaryButton {
                    objectName: "aiGenerateButton"
                    text: "生成说明"
                    enabled: !aiDialog.generating && root.workspace && !root.workspace.busy
                             && !(root.workspace.commitAi && root.workspace.commitAi.busy)
                    onClicked: {
                        aiDialog.errorText = ""
                        aiDialog.generating = true
                        var typeKey = aiTypeCombo.typeKeys[aiTypeCombo.currentIndex] || "fix"
                        root.workspace.generateCommitMessage(editor.text, aiScopeCombo.currentIndex,
                                                             typeKey,
                                                             aiIssueField.text.trim())
                    }
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Text { text: "提交说明"; color: Theme.text; font.pixelSize: Theme.fontSubheading; font.weight: Font.DemiBold; Layout.alignment: Qt.AlignVCenter }
        Item { Layout.fillWidth: true }
        IconButton {
            id: aiCommitButton
            objectName: "aiCommitButton"
            glyph: "✦"
            toolTip: "打开 AI 提交说明面板"
            Layout.preferredWidth: Theme.controlHeight
            Layout.preferredHeight: Theme.controlHeight
            enabled: root.workspace && root.workspace.selectedPath.length > 0
            onClicked: aiDialog.open()
        }
    }
    ScrollView {
        id: messageScroll
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: 90
        Layout.preferredHeight: 120
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
                drafts[draftKey] = text
                draftSaveTimer.restart()
            }
            background: Rectangle { color: Theme.surfaceMuted; radius: Theme.controlRadius; border.color: editor.activeFocus ? Theme.accent : Theme.separatorSoft }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        FlatCheckBox {
            id: amendCheck
            objectName: "amendCheck"
            text: "修改上次提交"
            enabled: !root.workspace.busy && root.workspace.selectedPath.length > 0
            onToggled: {
                if (checked && editor.text.trim().length === 0 && root.workspace && typeof root.workspace.requestLastCommitMessage === "function") {
                    root.workspace.requestLastCommitMessage()
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

        BusyIndicator {
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
            color: root.workspace && root.workspace.pushState === 3 ? Theme.red : Theme.secondaryText
            font.pixelSize: Theme.fontSecondary
        }
    }

    Text {
        Layout.fillWidth: true
        visible: root.resultText.length > 0
        text: root.resultText
        wrapMode: Text.Wrap
        color: Theme.secondaryText
        font.pixelSize: Theme.fontSecondary
    }

    // Gerrit Review URL Card
    Rectangle {
        objectName: "gerritUrlCard"
        Layout.fillWidth: true
        Layout.preferredHeight: gerritUrlRow.implicitHeight + 12
        radius: 6
        color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.08)
        border.color: Theme.accent
        visible: root.workspace && root.workspace.pushRepositoryKey === root.draftKey
                 && root.workspace.lastGerritReviewUrl && root.workspace.lastGerritReviewUrl.length > 0

        RowLayout {
            id: gerritUrlRow
            anchors.fill: parent
            anchors.margins: 6
            spacing: 6

            Text {
                text: "🔗"
                font.pixelSize: Theme.fontBody
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                Text {
                    text: "Gerrit 审查单已就绪"
                    color: Theme.text
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
                glyph: "⧉"
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
