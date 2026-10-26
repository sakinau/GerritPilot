import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GerritPilot
import "../../components"

Item {
    id: root

    property var workspace
    property real hostWidth: 1440
    property string pendingBranch: ""
    property string pendingDeleteBranch: ""

    function open() {
        branchPopup.open()
    }

    Popup {
        id: branchPopup
        objectName: "branchPopup"
        parent: Overlay.overlay
        x: parent ? Math.round((parent.width - width) / 2) : 0
        y: parent ? Math.round((parent.height - height) / 2) : 0
        width: Math.min(420, parent ? parent.width - 32 : 420)
        height: Math.min(510, parent ? parent.height - 32 : 510, 190 + branchList.count * 44)
        modal: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        padding: 0
        Overlay.modal: Rectangle { color: Theme.scrim }
        background: FloatingSurface {
            radius: Theme.radiusXLarge
            elevation: 10
        }
        contentItem: ColumnLayout {
            spacing: 0
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 16
                spacing: 10
                Rectangle {
                    Layout.preferredWidth: 30
                    Layout.preferredHeight: 30
                    radius: 8
                    color: Theme.accentTint
                    Icon { anchors.centerIn: parent; name: "git-branch"; size: 17; color: Theme.accent }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Text { text: "本地分支管理"; color: Theme.text; font.pixelSize: Theme.fontHeading; font.weight: Font.DemiBold }
                    Text { text: root.workspace.selectedName; color: Theme.tertiaryText; font.pixelSize: Theme.fontCaption }
                }
                IconButton { iconName: "x"; toolTip: "关闭"; onClicked: branchPopup.close() }
            }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.separatorSoft }
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.topMargin: 12
                Layout.bottomMargin: 12
                spacing: 8
                PrimaryButton {
                    id: createBranchButton
                    objectName: "createBranchButton"
                    Layout.fillWidth: true
                    text: "新建分支"
                    iconName: "plus"
                    enabled: root.workspace && !root.workspace.busy
                    onClicked: { branchPopup.close(); createBranchDialog.open() }
                }
                PrimaryButton {
                    id: pruneBranchButton
                    objectName: "pruneBranchButton"
                    Layout.fillWidth: true
                    text: (root.workspace && root.workspace.isRepoWorkspace) ? "清理已合入分支" : "清理失效远端分支"
                    kind: "gray"
                    toolTip: (root.workspace && root.workspace.isRepoWorkspace) ? "执行 repo prune" : "执行 git remote prune"
                    enabled: root.workspace && !root.workspace.busy
                    onClicked: {
                        branchPopup.close()
                        root.workspace.pruneBranches()
                    }
                }
            }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.separatorSoft }
            ListView {
                id: branchList
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: 8
                Layout.rightMargin: 8
                Layout.topMargin: 6
                Layout.bottomMargin: 6
                clip: true
                spacing: 2
                model: root.workspace.availableBranches
                ScrollBar.vertical: RightScrollBar { }
                delegate: ItemDelegate {
                    id: branchDelegate
                    required property string modelData
                    width: branchList.width
                    height: 38
                    hoverEnabled: true
                    leftPadding: 10
                    rightPadding: 6
                    onClicked: {
                        if (modelData !== root.workspace.selectedBranch) {
                            root.pendingBranch = modelData
                            branchPopup.close()
                            branchConfirmDialog.open()
                        }
                    }
                    readonly property bool isCurrent: branchDelegate.modelData === root.workspace.selectedBranch
                    background: Rectangle {
                        radius: Theme.radiusSmall + 2
                        color: branchDelegate.isCurrent ? Theme.accentTint : branchDelegate.hovered ? Theme.hoverFill : "transparent"
                    }
                    contentItem: RowLayout {
                        spacing: 9
                        Icon {
                            name: branchDelegate.isCurrent ? "check" : "git-branch"
                            size: 14
                            strokeWidth: branchDelegate.isCurrent ? 2.2 : 1.6
                            color: branchDelegate.isCurrent ? Theme.accent : Theme.tertiaryText
                        }
                        Text {
                            text: branchDelegate.modelData
                            color: branchDelegate.isCurrent ? Theme.accent : Theme.text
                            font.pixelSize: Theme.fontSecondary
                            font.weight: branchDelegate.modelData === root.workspace.selectedBranch ? Font.DemiBold : Font.Normal
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }
                        IconButton {
                            visible: branchDelegate.modelData !== root.workspace.selectedBranch
                            iconName: "trash"
                            destructive: true
                            opacity: branchDelegate.hovered ? 1 : 0
                            toolTip: "删除本地分支"
                            Layout.preferredWidth: 26
                            Layout.preferredHeight: 26
                            onClicked: {
                                root.pendingDeleteBranch = branchDelegate.modelData
                                branchPopup.close()
                                deleteBranchConfirmDialog.open()
                            }
                        }
                    }
                }
            }
        }
    }

    AppDialog {
        id: branchConfirmDialog
        preferredWidth: 450
        title: "切换分支"
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: root.workspace.checkoutBranch(root.pendingBranch)
        contentItem: ColumnLayout {
            width: parent.width
            spacing: 10
            Text { text: "从 " + root.workspace.selectedBranch + " 切换到 " + root.pendingBranch; color: Theme.text; font.pixelSize: Theme.fontBody; font.weight: Font.Medium; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Text { text: "如果本地改动与目标分支冲突，Git 会拒绝切换，不会强制覆盖文件。"; color: Theme.secondaryText; font.pixelSize: Theme.fontSecondary; wrapMode: Text.Wrap; Layout.fillWidth: true }
        }
    }

    AppDialog {
        id: deleteBranchConfirmDialog
        objectName: "deleteBranchConfirmDialog"
        preferredWidth: 450
        title: "删除本地分支"
        destructive: true
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: {
            if (root.workspace && typeof root.workspace.deleteBranch === "function") {
                root.workspace.deleteBranch(root.pendingDeleteBranch, forceDeleteCheckbox.checked)
            }
        }
        contentItem: ColumnLayout {
            width: parent.width
            spacing: 10
            Text {
                text: "确定要删除本地分支 “" + root.pendingDeleteBranch + "” 吗？"
                color: Theme.text
                font.pixelSize: Theme.fontBody
                font.weight: Font.Medium
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Text {
                text: "此操作将从本地 Git 仓库中移除该分支引用。"
                color: Theme.secondaryText
                font.pixelSize: Theme.fontSecondary
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            FlatCheckBox {
                id: forceDeleteCheckbox
                text: "强制删除 (-D，即使包含未合并提交也强制删除)"
                checked: false
            }
        }
    }

    AppDialog {
        id: createBranchDialog
        objectName: "createBranchDialog"
        preferredWidth: 470
        title: "创建本地分支"
        standardButtons: Dialog.Cancel | Dialog.Ok
        readonly property string trimmedName: newBranchName.text.trim()
        readonly property bool isDuplicate: root.workspace && root.workspace.availableBranches
            && root.workspace.availableBranches.indexOf(trimmedName) !== -1
        readonly property bool canAccept: trimmedName.length > 0 && !isDuplicate && root.workspace && !root.workspace.busy
        function updateOkButton() {
            const button = standardButton(Dialog.Ok)
            if (button) { button.enabled = canAccept; button.text = "创建并切换" }
        }
        onOpened: { newBranchName.text = ""; newBranchName.forceActiveFocus(); updateOkButton() }
        onCanAcceptChanged: updateOkButton()
        onAccepted: if (canAccept) root.workspace.createBranch(trimmedName)
        contentItem: ColumnLayout {
            spacing: 10
            Text { text: "从当前 HEAD 创建并切换到本地分支，保留工作区改动。不修改 Manifest，不创建远端分支。"; wrapMode: Text.Wrap; Layout.fillWidth: true; color: Theme.secondaryText; font.pixelSize: Theme.fontSecondary }
            AppTextField { id: newBranchName; objectName: "newBranchName"; Layout.fillWidth: true; placeholderText: "例如 feature/new-feature"; selectByMouse: true; font.pixelSize: Theme.fontBody; onAccepted: if (createBranchDialog.canAccept) createBranchDialog.accept() }
            Text { visible: createBranchDialog.isDuplicate; text: "同名本地分支已存在，请选择其他名称或切换到已有分支。"; wrapMode: Text.Wrap; Layout.fillWidth: true; color: Theme.orange; font.pixelSize: Theme.fontSecondary }
        }
    }
}
