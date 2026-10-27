import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GerritPilot
import "../../components"
import "CommitIssues.js" as CommitIssues

AppDialog {
    id: root
    property var workspace
    property string existingMessage: ""
    property bool hasStagedFiles: false
    signal candidateAccepted(string message)
    signal generationFailed(string message)

    Connections {
        target: root.workspace
        ignoreUnknownSignals: true
        function onSelectedRepositoryChanged() {
            root.generating = false
            root.close()
        }
        function onOperationError(error) {
            if (root.opened && root.workspace && error.type === "ai"
                && error.workspacePath === root.workspace.workspacePath
                && error.repositoryPath === root.workspace.selectedPath
                && error.generation === root.workspace.operationGeneration) {
                root.generating = false
                root.errorText = error.message
            }
        }
    }
    Connections {
        target: root.workspace && root.workspace.commitAi ? root.workspace.commitAi : null
        function onCandidateReady(candidate) {
            root.generating = false
            root.close()
            if (candidate && candidate.length > 0)
                root.candidateAccepted(candidate)
        }
        function onErrorOccurred(error) {
            root.generating = false
            root.errorText = error
            root.generationFailed(error)
        }
    }

    objectName: "aiCommitDialog"
    preferredWidth: 500
    title: "AI 生成提交说明"
    standardButtons: Dialog.NoButton
    closePolicy: generating ? Popup.NoAutoClose : Popup.CloseOnEscape
    property bool generating: false
    property string errorText: ""
    readonly property string commitType: aiTypeCombo.typeKeys[aiTypeCombo.currentIndex] || "fix"
    readonly property string issueTypePrefix: commitType === "fix" ? "m-" : "r-"
    readonly property string issuesError: workspace && workspace.commitAi
        ? workspace.commitAi.issueValidationError(commitType, aiIssueField.text, aiSecondaryIssueField.text) : ""

    onOpened: {
        errorText = ""
        aiScopeCombo.currentIndex = root.hasStagedFiles ? 0 : 1
        aiManualCompileCheck.checked = false
        aiCompileNoteField.text = ""
    }

    contentItem: ColumnLayout {
        spacing: 12
        Text {
            text: "改动范围"
            color: Theme.text
            font.pixelSize: Theme.fontSecondary
            font.weight: Font.DemiBold
        }
        AppComboBox {
            id: aiScopeCombo
            objectName: "aiScopeCombo"
            Layout.fillWidth: true
            model: ["仅暂存区", "整个当前仓库"]
            enabled: !root.generating
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
        AppComboBox {
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
            enabled: !root.generating
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
            Badge {
                text: root.issueTypePrefix === "m-" ? "m- 问题单" : "r- 需求单"
                tone: "accent"
            }
        }
        AppTextField {
            id: aiIssueField
            objectName: "aiIssueField"
            maximumLength: 1000
            Layout.fillWidth: true
            enabled: !root.generating
            placeholderText: root.issueTypePrefix === "m-"
                ? "输入问题单号（例如 7064369220，多个用逗号隔开；将自动补 m-）"
                : "输入需求单号（例如 7064369220，多个用逗号隔开；将自动补 r-）"
            font.pixelSize: Theme.fontSecondary
        }
        Text {
            Layout.fillWidth: true
            readonly property string rawText: aiIssueField.text.trim()
            readonly property string formattedText: CommitIssues.formatMultipleIssues(rawText, root.issueTypePrefix)
            text: root.issuesError || ("主关联单号：" + formattedText)
            font.pixelSize: Theme.fontCaption
            color: root.issuesError.length ? Theme.orangeText : Theme.accent
            wrapMode: Text.Wrap
        }
        Text {
            text: root.issueTypePrefix === "m-" ? "关联需求单（r-，修复提交必填）" : "伴随关联问题单 (m-，可选)"
            color: Theme.text
            font.pixelSize: Theme.fontSecondary
            font.weight: Font.DemiBold
        }
        AppTextField {
            id: aiSecondaryIssueField
            objectName: "aiSecondaryIssueField"
            maximumLength: 1000
            Layout.fillWidth: true
            enabled: !root.generating
            placeholderText: root.issueTypePrefix === "m-"
                ? "输入关联需求单号（例如 7064369220，多个用逗号隔开；将自动补 r-）"
                : "输入关联问题单号（例如 7064369220，多个用逗号隔开；将自动补 m-）"
            font.pixelSize: Theme.fontSecondary
        }
        Text {
            Layout.fillWidth: true
            readonly property string rawText: aiSecondaryIssueField.text.trim()
            readonly property string targetPrefix: root.issueTypePrefix === "m-" ? "r-" : "m-"
            readonly property string formattedText: CommitIssues.formatMultipleIssues(rawText, targetPrefix)
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
        AppTextField {
            id: aiPurposeField
            objectName: "aiCommitPurpose"
            Layout.fillWidth: true
            maximumLength: 300
            enabled: !root.generating
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
            radius: Theme.radiusMedium
            color: Theme.surfaceGrouped

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
                        enabled: !root.generating
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: aiManualCompileCheck.checked ? "✓ 输出编译通过说明" : "未勾选时输出 - Not run"
                        color: aiManualCompileCheck.checked ? Theme.accent : Theme.secondaryText
                        font.pixelSize: Theme.fontCaption
                    }
                }

                AppTextField {
                    id: aiCompileNoteField
                    objectName: "aiCompileNoteField"
                    Layout.fillWidth: true
                    visible: aiManualCompileCheck.checked
                    enabled: !root.generating
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
            Spinner {
                objectName: "aiCommitBusyIndicator"
                Layout.preferredWidth: Theme.controlHeight * 0.7
                Layout.preferredHeight: Theme.controlHeight * 0.7
                visible: root.generating
                running: visible
            }
            Text {
                Layout.fillWidth: true
                text: root.errorText || (root.generating ? "正在读取改动并生成说明…" : "")
                visible: text.length > 0
                wrapMode: Text.Wrap
                color: root.errorText ? Theme.red : Theme.secondaryText
                font.pixelSize: Theme.fontSecondary
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Item { Layout.fillWidth: true }
            PrimaryButton {
                text: root.generating ? "取消生成" : "关闭"
                kind: "gray"
                onClicked: {
                    if (root.generating && root.workspace) {
                        if (root.workspace.commitAi && root.workspace.commitAi.busy)
                            root.workspace.commitAi.cancel()
                        else if (root.workspace.busy && typeof root.workspace.stop === "function")
                            root.workspace.stop()
                    }
                    root.generating = false
                    root.close()
                }
            }
            PrimaryButton {
                objectName: "aiGenerateButton"
                text: "生成说明"
                enabled: root.issuesError.length === 0 && !root.generating && root.workspace && !root.workspace.busy
                         && !(root.workspace.commitAi && root.workspace.commitAi.busy)
                onClicked: {
                    root.errorText = ""
                    root.generating = true
                    var typeKey = root.commitType
                    var issueVal = aiIssueField.visible ? aiIssueField.text.trim() : ""
                    if (issueVal.length > 0) {
                        issueVal = CommitIssues.formatMultipleIssues(issueVal, root.issueTypePrefix)
                    }
                    var secPrefix = (root.issueTypePrefix === "m-" ? "r-" : "m-")
                    var secVal = aiSecondaryIssueField.text.trim()
                    if (secVal.length > 0) {
                        secVal = CommitIssues.formatMultipleIssues(secVal, secPrefix)
                    }
                    root.workspace.generateCommitMessage(root.existingMessage, aiScopeCombo.currentIndex,
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
