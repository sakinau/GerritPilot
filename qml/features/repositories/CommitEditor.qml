import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GerritPilot
import "../../components"

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

    function formatMultipleIssues(rawText, defaultPrefix) {
        if (!rawText || !rawText.trim()) return ""
        var p = (defaultPrefix && defaultPrefix.trim().length > 0) ? defaultPrefix.trim() : "m-"
        if (!p.endsWith("-")) p += "-"
        var tokens = rawText.trim().split(/[,;，；\s]+/)
        var list = []
        for (var i = 0; i < tokens.length; ++i) {
            var tok = tokens[i].trim()
            if (!tok) continue
            if (/^m-/i.test(tok)) {
                tok = "m-" + tok.substring(2)
            } else if (/^r-/i.test(tok)) {
                tok = "r-" + tok.substring(2)
            } else {
                tok = p + tok
            }
            if (list.indexOf(tok) === -1) {
                list.push(tok)
            }
        }
        return list.join(",")
    }

    function switchIssuePrefix(rawText, targetPrefix) {
        if (!rawText || !rawText.trim()) return ""
        var target = targetPrefix.endsWith("-") ? targetPrefix : (targetPrefix + "-")
        var tokens = rawText.trim().split(/[,;，；\s]+/)
        var list = []
        for (var i = 0; i < tokens.length; ++i) {
            var tok = tokens[i].trim()
            if (!tok) continue
            if (/^(m|r)-/i.test(tok)) {
                tok = target + tok.substring(2)
            } else {
                tok = target + tok
            }
            if (list.indexOf(tok) === -1) {
                list.push(tok)
            }
        }
        return list.join(",")
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
                }
                // Flush newer edits before any later switch/close can lose them.
                draftSaveTimer.stop()
                root.workspace.saveCommitDraft(root.currentRepoPath, editor.text, root.currentWorkspacePath)
                root.resultText = amendCheck.checked ? "修改提交成功" : "提交成功"
                amendCheck.checked = false
            }
        }
        function onOperationError(error) {
            if (aiDialog.opened && error.type === "ai"
                && error.workspacePath === root.currentWorkspacePath
                && error.repositoryPath === root.currentRepoPath
                && error.generation === root.workspace.operationGeneration) {
                aiDialog.generating = false
                aiDialog.errorText = error.message
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
        property string issueTypePrefix: "m-"

        onOpened: {
            errorText = ""
            issueTypePrefix = (aiTypeCombo.currentIndex === 0 ? "m-" : "r-")
            aiScopeCombo.currentIndex = root.hasStagedFiles ? 0 : 1
        }

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
                onCurrentIndexChanged: aiDialog.issueTypePrefix = (currentIndex === 0 ? "m-" : "r-")
            }
            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: "关联单号（飞书项目强制绑定）"
                    color: Theme.text
                    font.pixelSize: Theme.fontSecondary
                    font.weight: Font.DemiBold
                }
                Item { Layout.fillWidth: true }
                Rectangle {
                    implicitWidth: bugPillText.implicitWidth + 12
                    implicitHeight: 22
                    radius: 11
                    color: aiDialog.issueTypePrefix === "m-" ? Theme.accent : Theme.surfaceMuted
                    border.color: aiDialog.issueTypePrefix === "m-" ? Theme.accent : Theme.separatorSoft
                    border.width: 1
                    Text {
                        id: bugPillText
                        anchors.centerIn: parent
                        text: "m- 问题单"
                        font.pixelSize: Theme.fontCaption
                        color: aiDialog.issueTypePrefix === "m-" ? "white" : Theme.secondaryText
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            aiDialog.issueTypePrefix = "m-"
                            var t = aiIssueField.text.trim()
                            if (t.length > 0 && /(?:^|[,;，；\s])r-/i.test(t)) {
                                aiIssueField.text = switchIssuePrefix(t, "m-")
                            }
                        }
                    }
                }
                Rectangle {
                    implicitWidth: reqPillText.implicitWidth + 12
                    implicitHeight: 22
                    radius: 11
                    color: aiDialog.issueTypePrefix === "r-" ? Theme.accent : Theme.surfaceMuted
                    border.color: aiDialog.issueTypePrefix === "r-" ? Theme.accent : Theme.separatorSoft
                    border.width: 1
                    Text {
                        id: reqPillText
                        anchors.centerIn: parent
                        text: "r- 需求单"
                        font.pixelSize: Theme.fontCaption
                        color: aiDialog.issueTypePrefix === "r-" ? "white" : Theme.secondaryText
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            aiDialog.issueTypePrefix = "r-"
                            var t = aiIssueField.text.trim()
                            if (t.length > 0 && /(?:^|[,;，；\s])m-/i.test(t)) {
                                aiIssueField.text = switchIssuePrefix(t, "r-")
                            }
                        }
                    }
                }
            }
            TextField {
                id: aiIssueField
                objectName: "aiIssueField"
                Layout.fillWidth: true
                enabled: !aiDialog.generating
                placeholderText: aiDialog.issueTypePrefix === "m-"
                    ? "输入问题单号（例如 7064369220，多个用逗号隔开；将自动补 m-）"
                    : "输入需求单号（例如 7064369220，多个用逗号隔开；将自动补 r-）"
                font.pixelSize: Theme.fontSecondary
            }
            Text {
                Layout.fillWidth: true
                readonly property string rawText: aiIssueField.text.trim()
                readonly property string formattedText: formatMultipleIssues(rawText, aiDialog.issueTypePrefix)
                readonly property bool allAlreadyPrefixed: {
                    if (!rawText) return false
                    var parts = rawText.split(/[,;，；\s]+/)
                    for (var i = 0; i < parts.length; ++i) {
                        var tok = parts[i].trim()
                        if (tok && !/^(m|r)-/i.test(tok)) return false
                    }
                    return true
                }
                text: rawText.length === 0
                    ? "⚠ 飞书项目与Gerrit门禁要求：必须关联单号（问题单 m- 或 需求单 r-），否则提交将被拦截"
                    : (allAlreadyPrefixed
                       ? ("✓ 单号格式有效: " + formattedText)
                       : ("✓ 提交将自动补全前缀为 " + formattedText))
                font.pixelSize: Theme.fontCaption
                color: rawText.length === 0 ? Theme.orange : Theme.accent
                wrapMode: Text.Wrap
            }
            Text {
                text: aiDialog.issueTypePrefix === "m-" ? "双关联需求单 (r-，自主修复战役单)" : "伴随关联问题单 (m-，可选)"
                color: Theme.text
                font.pixelSize: Theme.fontSecondary
                font.weight: Font.DemiBold
            }
            TextField {
                id: aiSecondaryIssueField
                objectName: "aiSecondaryIssueField"
                Layout.fillWidth: true
                enabled: !aiDialog.generating
                placeholderText: aiDialog.issueTypePrefix === "m-"
                    ? "输入关联需求单号（例如 7064369220，多个用逗号隔开；将自动补 r-）"
                    : "输入关联问题单号（例如 7064369220，多个用逗号隔开；将自动补 m-）"
                font.pixelSize: Theme.fontSecondary
            }
            Text {
                Layout.fillWidth: true
                readonly property string rawText: aiSecondaryIssueField.text.trim()
                readonly property string targetPrefix: aiDialog.issueTypePrefix === "m-" ? "r-" : "m-"
                readonly property string formattedText: formatMultipleIssues(rawText, targetPrefix)
                visible: rawText.length > 0
                text: "✓ 补充单号将作为 footer 关联: " + formattedText
                font.pixelSize: Theme.fontCaption
                color: Theme.accent
                wrapMode: Text.Wrap
            }
            Text {
                text: "改动目的"
                color: Theme.text
                font.pixelSize: Theme.fontSecondary
                font.weight: Font.DemiBold
            }
            TextField {
                id: aiPurposeField
                objectName: "aiCommitPurpose"
                Layout.fillWidth: true
                maximumLength: 300
                enabled: !aiDialog.generating
                placeholderText: "改动目的（可选）：如按需求更新提示文案，不是修复显示异常"
                font.pixelSize: Theme.fontSecondary
            }
            Text {
                text: "编译验证"
                color: Theme.text
                font.pixelSize: Theme.fontSecondary
                font.weight: Font.DemiBold
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: compileCol.implicitHeight + 14
                radius: 6
                color: "#F8F9FA"
                border.color: Theme.separatorSoft
                border.width: 1

                ColumnLayout {
                    id: compileCol
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        FlatCheckBox {
                            id: aiManualCompileCheck
                            objectName: "aiManualCompileCheck"
                            text: "人工已验证编译通过"
                            checked: false
                            enabled: !aiDialog.generating
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            text: aiManualCompileCheck.checked ? "✓ 输出编译通过说明" : "未勾选时输出 - Not run"
                            color: aiManualCompileCheck.checked ? Theme.accent : Theme.secondaryText
                            font.pixelSize: Theme.fontCaption
                        }
                    }

                    TextField {
                        id: aiCompileNoteField
                        objectName: "aiCompileNoteField"
                        Layout.fillWidth: true
                        visible: aiManualCompileCheck.checked
                        enabled: !aiDialog.generating
                        placeholderText: "编译目标或平台（可选，留空默认：本地已验证编译通过）"
                        font.pixelSize: Theme.fontSecondary
                    }
                }
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
                        var issueVal = aiIssueField.visible ? aiIssueField.text.trim() : ""
                        if (issueVal.length > 0) {
                            issueVal = formatMultipleIssues(issueVal, aiDialog.issueTypePrefix)
                        }
                        var secPrefix = (aiDialog.issueTypePrefix === "m-" ? "r-" : "m-")
                        var secVal = aiSecondaryIssueField.text.trim()
                        if (secVal.length > 0) {
                            secVal = formatMultipleIssues(secVal, secPrefix)
                        }
                        root.workspace.generateCommitMessage(editor.text, aiScopeCombo.currentIndex,
                                                             typeKey,
                                                             issueVal,
                                                             aiManualCompileCheck.checked,
                                                             aiCompileNoteField.text.trim(),
                                                             aiPurposeField.text.trim(),
                                                             secVal)
                    }
                }
            }
        }
    }

    ColumnLayout {
        id: mainContentCol
        anchors.fill: parent
        spacing: 6

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
                background: Rectangle { color: Theme.surfaceMuted; radius: Theme.controlRadius; border.color: editor.activeFocus ? Theme.accent : Theme.separatorSoft }
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

            var match = line.match(/^([A-Za-z0-9._\-]+(?:[,;，；\s]+[A-Za-z0-9._\-]+)*)(?=\s+[a-zA-Z]+(?:\([^\)]*\))?!?:|\s+|$)/)
            if (!match) {
                match = line.match(/^([A-Za-z0-9._\-]+(?:[,;，；\s]+[A-Za-z0-9._\-]+)*)/)
            }
            if (!match) return { hasIssues: false, allValid: false, detectedIds: "", rawBlock: "" }

            var block = match[1].trim()
            var tokens = block.split(/[,;，；\s]+/)
            var isIssueToken = function(tok) {
                return /^(m|r)-[A-Za-z0-9]+/i.test(tok) || /^\d{3,}$/.test(tok)
            }
            var hasAnyIssue = false
            for (var i = 0; i < tokens.length; ++i) {
                if (isIssueToken(tokens[i])) {
                    hasAnyIssue = true
                    break
                }
            }
            if (!hasAnyIssue) return { hasIssues: false, allValid: false, detectedIds: "", rawBlock: "" }

            var allValid = true
            var validIds = []
            for (var j = 0; j < tokens.length; ++j) {
                var tok = tokens[j].trim()
                if (!tok) continue
                if (/^(m|r)-[A-Za-z0-9]+/i.test(tok)) {
                    validIds.push(tok.toLowerCase().startsWith("m-") ? ("m-" + tok.substring(2)) : ("r-" + tok.substring(2)))
                } else {
                    allValid = false
                }
            }
            var hasReqTrailer = /(?:关联需求单|Requirement|Req):\s*r-[A-Za-z0-9]+/i.test(raw)
            var hasBugTrailer = /(?:关联问题单|Bug|Issue):\s*m-[A-Za-z0-9]+/i.test(raw)
            return {
                hasIssues: true,
                allValid: allValid && (validIds.length === tokens.length),
                detectedIds: validIds.join(","),
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
            radius: 4
            color: mainContentCol.hasValidIssuePrefix ? Qt.rgba(0.1, 0.7, 0.3, 0.08) : Qt.rgba(0.95, 0.55, 0.1, 0.1)
            border.color: mainContentCol.hasValidIssuePrefix ? Qt.rgba(0.1, 0.7, 0.3, 0.25) : Qt.rgba(0.95, 0.55, 0.1, 0.35)
            border.width: 1

            RowLayout {
                id: issueStatusRow
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 6

                Text {
                    text: mainContentCol.hasValidIssuePrefix ? "✓" : "⚠"
                    color: mainContentCol.hasValidIssuePrefix ? Theme.accent : Theme.orange
                    font.pixelSize: Theme.fontCaption
                    font.weight: Font.Bold
                }

                Text {
                    Layout.fillWidth: true
                    text: {
                        if (!mainContentCol.hasValidIssuePrefix) {
                            return "缺少单号前缀（需以 m-实例ID 或 r-实例ID 开头），将被飞书项目/Gerrit门禁拦截"
                        }
                        var desc = "已关联飞书单号: " + mainContentCol.detectedIssuePrefixId
                        if (mainContentCol.issueInfo.hasReqTrailer) {
                            desc += " · 已双关联需求单"
                        } else if (mainContentCol.issueInfo.hasBugTrailer) {
                            desc += " · 已伴随关联问题单"
                        }
                        return desc
                    }
                    color: mainContentCol.hasValidIssuePrefix ? Theme.text : Theme.orange
                    font.pixelSize: Theme.fontCaption
                    elide: Text.ElideRight
                }

                Rectangle {
                    visible: !mainContentCol.hasValidIssuePrefix
                    implicitWidth: quickPrefixBtnText.implicitWidth + 12
                    implicitHeight: 20
                    radius: 3
                    color: Theme.orange
                    Text {
                        id: quickPrefixBtnText
                        anchors.centerIn: parent
                        text: "补全单号"
                        font.pixelSize: 11
                        color: "white"
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            var defaultPrefix = (editor.text.indexOf("fix") !== -1 ? "m-" : "r-")
                            if (mainContentCol.issueInfo.hasIssues && mainContentCol.issueInfo.rawBlock) {
                                var raw = mainContentCol.issueInfo.rawBlock
                                var formatted = formatMultipleIssues(raw, defaultPrefix)
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
}
