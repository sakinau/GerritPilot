import QtQuick
import QtQuick.Layouts
import GerritPilot
import "../../components"

// Merge / rebase / cherry-pick / revert in progress, with conflict guidance.
Banner {
    id: root
    property var workspace
    property var selectedConflictPaths: []
    signal abortRequested()

    readonly property bool hasConflicts: workspace ? Boolean(workspace.hasConflicts) : false
    readonly property bool hasPending: workspace ? Boolean(workspace.hasPendingOperation) : false
    readonly property string opName: workspace ? workspace.pendingOperation : ""
    readonly property string operationLabel: opName === "merge" ? "合并"
        : opName === "rebase" ? "变基" : opName === "cherry-pick" ? "拣选提交"
        : opName === "revert" ? "撤销提交" : "操作"
    readonly property int conflictCount: workspace ? workspace.groupedChanges.filter(function(file) { return file.conflict }).length : 0

    visible: hasConflicts || hasPending
    tone: hasConflicts ? "warning" : "info"
    title: hasConflicts
        ? (operationLabel + "存在 " + conflictCount + " 个冲突文件")
        : workspace && workspace.canSkipCherryPick
            ? "当前拣选没有剩余改动，可跳过当前提交或终止"
            : (operationLabel + "已无未解决冲突，可以继续或终止")
    message: hasConflicts ? "在左侧点击 ↗ 打开冲突文件编辑并保存，再点击 ✓ 标记为已解决。"
        : opName === "rebase" ? "如需修改停留的提交：改好文件并暂存后，在提交面板勾选“修订”提交，再点击继续。" : ""

    // Flow is right-to-left: the first child is the rightmost action.
    PrimaryButton {
        visible: root.hasPending
        text: "终止" + root.operationLabel
        kind: "dangerTinted"
        compact: true
        enabled: root.workspace && !root.workspace.busy
        toolTip: "终止当前操作并恢复到操作前状态"
        onClicked: root.abortRequested()
    }
    PrimaryButton {
        visible: root.hasPending
        text: "继续" + root.operationLabel
        compact: true
        enabled: root.workspace && !root.workspace.busy && !root.hasConflicts
        toolTip: root.hasConflicts ? "需先解决并标记所有冲突文件" : "继续执行中断的 Git 操作"
        onClicked: root.workspace.continueOperation()
    }
    PrimaryButton {
        visible: root.hasConflicts
        text: "标记选中已解决"
        kind: "tinted"
        compact: true
        iconName: "check"
        toolTip: "只标记左侧选中的冲突文件；请先完成编辑"
        enabled: root.workspace && !root.workspace.busy && root.selectedConflictPaths.length > 0
        onClicked: root.workspace.markResolved(root.selectedConflictPaths)
    }
    CherryPickSkipAction {
        workspace: root.workspace
        compact: true
    }
}
