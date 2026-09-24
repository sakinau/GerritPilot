import QtQuick
import QtQuick.Controls
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
        Overlay.modal: Rectangle { color: "#33000000" }
        background: Rectangle {
            color: Theme.surfaceStrong
            radius: Theme.radiusXLarge
            border.color: Theme.separator
        }
        contentItem: ColumnLayout {
            spacing: 0
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 16
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Text { text: "本地分支管理"; color: Theme.text; font.pixelSize: Theme.fontHeading; font.weight: Font.DemiBold }
                    Text { text: root.workspace.selectedName; color: Theme.tertiaryText; font.pixelSize: Theme.fontCaption }
                }
                IconButton { glyph: "×"; Layout.preferredWidth: Theme.controlHeight; onClicked: branchPopup.close() }
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
                    enabled: root.workspace && !root.workspace.busy
                    onClicked: { branchPopup.close(); createBranchDialog.open() }
                }
                PrimaryButton {
                    id: pruneBranchButton
                    objectName: "pruneBranchButton"
                    Layout.fillWidth: true
                    text: (root.workspace && root.workspace.isRepoWorkspace) ? "清理已合入分支" : "清理失效远端分支"
                    secondary: true
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
                    height: 40
                    hoverEnabled: true
                    onClicked: {
                        if (modelData !== root.workspace.selectedBranch) {
                            root.pendingBranch = modelData
                            branchPopup.close()
                            branchConfirmDialog.open()
                        }
                    }
                    readonly property bool isCurrent: branchDelegate.modelData === root.workspace.selectedBranch
                    background: Rectangle {
                        radius: 6
                        color: branchDelegate.isCurrent ? Theme.accentSoft : branchDelegate.hovered ? "#F8F9FA" : "transparent"
                        border.color: branchDelegate.isCurrent ? Theme.accent : branchDelegate.hovered ? Theme.separatorSoft : "transparent"
                        border.width: 1
                    }
                    contentItem: RowLayout {
                        spacing: 9
                        Text {
                            text: branchDelegate.modelData === root.workspace.selectedBranch ? "✓" : ""
                            color: Theme.accent
                            font.pixelSize: Theme.fontBody
                            font.weight: Font.Bold
                            Layout.preferredWidth: 16
                        }
                        Text {
                            text: branchDelegate.modelData
                            color: Theme.text
                            font.pixelSize: Theme.fontSecondary
                            font.weight: branchDelegate.modelData === root.workspace.selectedBranch ? Font.DemiBold : Font.Normal
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }
                        IconButton {
                            visible: branchDelegate.modelData !== root.workspace.selectedBranch
                            glyph: "🗑"
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
            CheckBox {
                id: forceDeleteCheckbox
                text: "强制删除 (-D，即使包含未合并提交也强制删除)"
                checked: false
                Layout.fillWidth: true
            }
        }
    }

    AppDialog {
        id: createBranchDialog
        objectName: "createBranchDialog"
        preferredWidth: 470
        title: "新建本地分支"
        standardButtons: Dialog.Cancel | Dialog.Ok
        readonly property string trimmedName: newBranchName.text.trim()
        readonly property bool isDuplicate: root.workspace && root.workspace.availableBranches && root.workspace.availableBranches.indexOf(trimmedName) !== -1
        readonly property bool canAccept: trimmedName.length > 0 && !isDuplicate

        function updateOkButton() {
            const okBtn = createBranchDialog.standardButton(Dialog.Ok)
            if (okBtn) okBtn.enabled = canAccept
        }

        onOpened: {
            newBranchName.text = ""
            newBranchName.forceActiveFocus()
            updateOkButton()
        }
        onCanAcceptChanged: updateOkButton()
        onAccepted: {
            if (canAccept && root.workspace) {
                const useRepo = (root.workspace.isRepoWorkspace && repoStartCheck.checked)
                root.workspace.createBranch(trimmedName, useRepo)
            }
        }
        contentItem: ColumnLayout {
            width: parent.width
            spacing: 10
            Text {
                text: (root.workspace && root.workspace.isRepoWorkspace && repoStartCheck.checked)
                    ? ("将在当前项目执行: repo start " + (createBranchDialog.trimmedName || "<分支名>") + " .")
                    : ("新分支将基于 " + (root.workspace ? root.workspace.selectedBranch : "") + " 创建并立即切换。")
                color: Theme.secondaryText
                font.pixelSize: Theme.fontSecondary
            }
            TextField { font.pixelSize: Theme.fontBody;
                id: newBranchName
                objectName: "newBranchName"
                Layout.fillWidth: true
                placeholderText: "例如 feature/avm-manual"
                selectByMouse: true
                onAccepted: {
                    if (createBranchDialog.canAccept) {
                        createBranchDialog.accept()
                    }
                }
            }
            FlatCheckBox {
                id: repoStartCheck
                visible: root.workspace && root.workspace.isRepoWorkspace
                text: "使用 repo start 创建开发分支（关联 Manifest 上游）"
                checked: true
            }
            Text {
                visible: createBranchDialog.isDuplicate
                text: "⚠️ 分支 “" + createBranchDialog.trimmedName + "” 已存在，请换一个名称或直接切换。"
                color: Theme.orange
                font.pixelSize: Theme.fontSecondary
            }
        }
    }
}
