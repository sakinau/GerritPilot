import QtQuick
import QtQuick.Controls.Basic

PrimaryButton {
    id: root
    property var workspace
    property var confirmation: ({})
    objectName: "skipCherryPickButton"
    text: "跳过当前提交"
    kind: "gray"
    visible: workspace ? Boolean(workspace.canSkipCherryPick) : false
    enabled: workspace && workspace.canSkipCherryPick && !workspace.busy
    toolTip: "仅跳过没有剩余改动的当前拣选提交"
    function stillCurrent() {
        return workspace && !workspace.busy && workspace.canSkipCherryPick
            && confirmation.workspace === workspace.workspacePath
            && confirmation.repository === workspace.selectedPath
            && confirmation.generation === workspace.operationGeneration
            && confirmation.revision === workspace.pendingCherryPickRevision
    }
    onClicked: {
        confirmation = {workspace: workspace.workspacePath, repository: workspace.selectedPath,
            generation: workspace.operationGeneration, revision: workspace.pendingCherryPickRevision}
        dialog.open()
    }
    AppDialog {
        id: dialog
        objectName: "skipCherryPickConfirmation"
        title: "跳过空拣选提交"
        contentHeight: explanation.contentHeight
        implicitHeight: contentHeight + topPadding + bottomPadding
            + header.implicitHeight + footer.implicitHeight + spacing * 2
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: { if (root.stillCurrent()) root.workspace.skipCherryPick(root.confirmation.revision) }
        contentItem: Text {
            id: explanation
            width: Math.max(0, dialog.width - dialog.leftPadding - dialog.rightPadding)
            font: dialog.font
            text: "跳过当前没有剩余改动的提交，不创建空提交。若还有后续拣选提交，Git 会继续执行；不会自动推送。"
            wrapMode: Text.Wrap
        }
    }
    Connections {
        target: root.workspace
        function onSelectedRepositoryChanged() { dialog.close() }
    }
}
