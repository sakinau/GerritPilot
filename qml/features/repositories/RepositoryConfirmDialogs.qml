import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GerritPilot
import "../../components"

// Confirmation sheets for repository-level write operations.
Item {
    id: root
    property var workspace
    property var discardPaths: []
    property string revision: ""

    signal revisionCheckoutConfirmed(string revision)

    function confirmDiscard(paths) { discardPaths = paths; discardDialog.open() }
    function confirmCheckout(rev) { revision = rev; revisionCheckoutDialog.open() }
    function confirmCherryPick(rev) { revision = rev; cherryPickConfirmDialog.open() }
    function confirmRevert(rev) { revision = rev; revertConfirmDialog.open() }
    function confirmAbort() { abortConfirmDialog.open() }

    component DialogText: Text {
        property bool primary: false
        color: primary ? Theme.text : Theme.secondaryText
        font.pixelSize: primary ? Theme.fontBody : Theme.fontSecondary
        font.weight: primary ? Font.Medium : Font.Normal
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }

    AppDialog {
        id: discardDialog
        objectName: "discardDialog"
        preferredWidth: 460
        title: "丢弃文件改动"
        destructive: true
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: root.workspace.discardFiles(root.discardPaths)
        contentItem: ColumnLayout {
            spacing: 10
            Banner {
                Layout.fillWidth: true
                tone: "danger"
                message: "将丢弃以下文件的未暂存修改，保留已暂存内容；未跟踪文件将被删除。此操作无法撤销。"
            }
            ScrollView {
                id: discardScroll
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(140, discardList.implicitHeight + 4)
                contentWidth: availableWidth
                clip: true
                ScrollBar.vertical: RightScrollBar { }
                TextArea {
                    id: discardList
                    width: discardScroll.availableWidth
                    readOnly: true
                    selectByMouse: true
                    text: root.discardPaths.join("\n")
                    wrapMode: TextEdit.Wrap
                    color: Theme.text
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Theme.fontSecondary
                    background: Rectangle { color: Theme.surfaceGrouped; radius: Theme.radiusSmall }
                }
            }
        }
    }

    AppDialog {
        id: revisionCheckoutDialog
        preferredWidth: 470
        title: "检出历史提交"
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: root.revisionCheckoutConfirmed(root.revision)
        contentItem: ColumnLayout {
            spacing: 10
            DialogText { primary: true; text: "将以 detached HEAD 状态检出提交 " + root.revision }
            DialogText { text: "适合运行或检查历史代码。Git 不会强制覆盖本地改动；如存在冲突会拒绝切换。完成后可在左侧分支列表切回任意分支。" }
        }
    }

    AppDialog {
        id: cherryPickConfirmDialog
        preferredWidth: 470
        title: "摘取提交"
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: {
            if (root.workspace && typeof root.workspace.cherryPick === "function")
                root.workspace.cherryPick(root.revision)
        }
        contentItem: ColumnLayout {
            spacing: 10
            DialogText { primary: true; text: "将把提交 " + root.revision.slice(0, 10) + " 的修改摘取（Cherry-pick）并应用到当前分支。" }
            DialogText { text: "如果产生代码冲突，可在左侧改动列表解决并标记冲突，然后点击顶部的“继续”或“终止”。" }
        }
    }

    AppDialog {
        id: revertConfirmDialog
        preferredWidth: 450
        title: "撤销此提交"
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: {
            if (root.workspace && typeof root.workspace.revertCommit === "function")
                root.workspace.revertCommit(root.revision)
        }
        contentItem: ColumnLayout {
            spacing: 10
            DialogText { primary: true; text: "将在当前分支新增一条反向提交，抵消 " + root.revision.slice(0, 10) + " 的改动；原提交仍保留在历史中。" }
            DialogText { text: "需要干净的工作区。若发生冲突，可在改动列表处理后继续或终止；不会自动推送。" }
        }
    }

    AppDialog {
        id: abortConfirmDialog
        preferredWidth: 430
        title: "终止冲突操作"
        destructive: true
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: {
            if (root.workspace)
                root.workspace.abortOperation()
        }
        contentItem: ColumnLayout {
            spacing: 10
            DialogText { primary: true; text: "确定要终止当前的合并或变基操作吗？" }
            DialogText { text: "终止操作将放弃解决冲突过程中的修改，并恢复到操作前的状态。" }
        }
    }
}
