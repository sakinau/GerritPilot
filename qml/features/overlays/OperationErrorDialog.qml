import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GerritPilot
import "../../components"

Item {
    id: root

    property var workspace
    signal upstreamRequested()
    signal conflictRequested()

    function canApply(error) {
        return workspace && !workspace.busy
            && error.workspacePath === workspace.workspacePath
            && error.repositoryPath === workspace.selectedPath
            && error.generation === workspace.operationGeneration
    }

    AppDialog {
        id: dialog
        objectName: "operationErrorDialog"
        title: "操作失败"
        standardButtons: Dialog.Ok
        property string errorMessage: ""
        property var failure: ({actions: []})
        readonly property var actions: failure.actions || []
        contentItem: ColumnLayout {
            spacing: 10
            ScrollView {
                id: errorScroll
                Layout.fillWidth: true
                Layout.preferredHeight: 240
                contentWidth: availableWidth
                clip: true
                ScrollBar.vertical: RightScrollBar { }
                TextArea {
                    id: errorText
                    width: errorScroll.availableWidth
                    readOnly: true
                    text: dialog.errorMessage
                    color: Theme.tones.danger.foreground
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Theme.fontSecondary
                    wrapMode: TextEdit.WrapAnywhere
                    padding: 12
                    background: Rectangle {
                        color: Theme.tones.danger.background
                        border.color: Theme.tones.danger.border
                        radius: Theme.radiusMedium
                    }
                }
            }
            PrimaryButton {
                objectName: "errorUpstreamButton"
                text: "为当前仓库设置拉取上游"
                secondary: true
                visible: dialog.actions.indexOf("upstream") !== -1
                enabled: root.canApply(dialog.failure)
                onClicked: {
                    if (!root.canApply(dialog.failure)) return
                    dialog.close()
                    root.upstreamRequested()
                }
            }
            PrimaryButton {
                objectName: "errorConflictButton"
                text: "查看冲突文件"
                secondary: true
                visible: dialog.actions.indexOf("conflicts") !== -1
                enabled: root.canApply(dialog.failure)
                onClicked: {
                    if (!root.canApply(dialog.failure)) return
                    dialog.close()
                    root.conflictRequested()
                }
            }
            PrimaryButton {
                objectName: "errorHookButton"
                text: dialog.actions.indexOf("resumeCommit") !== -1 ? "安装 Hook 并继续提交" : "安装 Change-Id Hook"
                secondary: true
                visible: dialog.actions.indexOf("installHook") !== -1
                enabled: root.canApply(dialog.failure)
                onClicked: {
                    if (!root.canApply(dialog.failure)) return
                    const resume = dialog.actions.indexOf("resumeCommit") !== -1
                    dialog.close()
                    root.workspace.installCommitHook(resume)
                }
            }
            PrimaryButton {
                objectName: "errorRepairButton"
                text: "修订最近提交的 Change-Id"
                secondary: true
                visible: dialog.actions.indexOf("repairChangeId") !== -1
                enabled: root.canApply(dialog.failure)
                onClicked: {
                    if (!root.canApply(dialog.failure)) return
                    repairConfirm.failure = dialog.failure
                    dialog.close()
                    repairConfirm.open()
                }
            }
        }
    }

    AppDialog {
        id: repairConfirm
        property var failure: ({})
        preferredWidth: 460
        contentHeight: repairText.contentHeight
        implicitHeight: contentHeight + topPadding + bottomPadding
            + header.implicitHeight + footer.implicitHeight + spacing * 2
        title: "确认修订最近提交"
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: { if (root.canApply(failure)) root.workspace.repairLastCommitChangeId() }
        contentItem: Text {
            id: repairText
            width: Math.max(0, repairConfirm.width - repairConfirm.leftPadding - repairConfirm.rightPadding)
            text: "将以原提交说明执行 Amend，由 Hook 补上 Change-Id。提交 SHA 会改变；程序会先确认工作区干净且 HEAD 未变化，不会自动推送。"
            wrapMode: Text.Wrap
            color: Theme.text
        }
    }

    Connections {
        target: root.workspace
        function onSelectedRepositoryChanged() {
            dialog.close()
            repairConfirm.close()
        }
        function onOperationError(error) {
            // AI generation errors are shown in the AI panel itself.
            if (error.type === "ai") return
            repairConfirm.close()
            dialog.failure = error
            dialog.title = error.title
            dialog.errorMessage = error.message
            dialog.open()
        }
    }
}
