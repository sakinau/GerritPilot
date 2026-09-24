import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GerritPilot
import "../../components"

Item {
    id: root

    property var workspace
    signal upstreamRequested()
    signal conflictRequested()

    AppDialog {
        id: dialog
        title: "操作失败"
        standardButtons: Dialog.Ok
        property string errorMessage: ""
        property bool showUpstreamAction: false
        property bool showConflictAction: false
        property bool showHookAction: false
        property bool showRepairAction: false
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
                    color: Theme.text
                    wrapMode: TextEdit.WrapAnywhere
                    background: Rectangle { color: Theme.redSoft; radius: 10 }
                }
            }
            PrimaryButton {
                text: "为当前仓库设置拉取上游"
                secondary: true
                visible: dialog.showUpstreamAction
                onClicked: {
                    dialog.close()
                    root.upstreamRequested()
                }
            }
            PrimaryButton {
                text: "查看冲突文件"
                secondary: true
                visible: dialog.showConflictAction
                onClicked: {
                    dialog.close()
                    root.conflictRequested()
                }
            }
            PrimaryButton {
                text: dialog.title === "缺少 Change-Id Hook" ? "安装 Hook 并继续提交" : "安装 Change-Id Hook"
                secondary: true
                visible: dialog.showHookAction
                enabled: root.workspace && !root.workspace.busy
                onClicked: {
                    const resume = dialog.title === "缺少 Change-Id Hook"
                        && dialog.errorMessage.indexOf("继续提交") !== -1
                    dialog.close()
                    root.workspace.installCommitHook(resume)
                }
            }
            PrimaryButton {
                text: "修订最近提交的 Change-Id"
                secondary: true
                visible: dialog.showRepairAction
                enabled: root.workspace && !root.workspace.busy
                onClicked: {
                    dialog.close()
                    repairConfirm.open()
                }
            }
        }
    }

    AppDialog {
        id: repairConfirm
        preferredWidth: 460
        title: "确认修订最近提交"
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: root.workspace.repairLastCommitChangeId()
        contentItem: Text {
            text: "将以原提交说明执行 Amend，由 Hook 补上 Change-Id。提交 SHA 会改变；程序会先确认工作区干净且 HEAD 未变化，不会自动推送。"
            wrapMode: Text.Wrap
            color: Theme.text
        }
    }

    Connections {
        target: root.workspace
        function onOperationFailed(title, message) {
            // AI generation errors are shown in the AI panel itself.
            if (title.indexOf("AI") === 0) return
            dialog.title = title
            dialog.errorMessage = message
            dialog.showUpstreamAction = message.indexOf("上游") !== -1
            const lower = message.toLowerCase()
            dialog.showConflictAction = lower.indexOf("conflict") !== -1
                || lower.indexOf("could not apply") !== -1
                || message.indexOf("冲突") !== -1
            dialog.showHookAction = title === "缺少 Change-Id Hook"
                || ((title === "最近提交缺少 Change-Id" || title === "待推送提交缺少 Change-Id")
                    && root.workspace && !root.workspace.hasCommitHook)
            dialog.showRepairAction = title === "最近提交缺少 Change-Id"
                && root.workspace && root.workspace.hasCommitHook
            dialog.open()
        }
    }
}
