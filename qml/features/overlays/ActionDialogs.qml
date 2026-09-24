import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GerritPilot
import "../../components"

Item {
    id: root

    property var workspace

    function openSync() { syncDialog.open() }
    function openUpstream() { upstreamDialog.open() }
    function commit(message, amend, stageAll) { root.workspace.commitActive(message, amend, stageAll) }
    function openPush() { pushDialog.open() }

    AppDialog {
        id: syncDialog
        objectName: "syncDialog"
        preferredWidth: 520
        title: "拉取代码"
        standardButtons: Dialog.Cancel | Dialog.Ok
        property int syncScope: 0
        property int syncMethod: 0 // 0: Git fast-forward pull; 1: selected Repo projects from local Manifest
        readonly property int checkedCount: root.workspace ? (typeof root.workspace.selectedCount !== "undefined" ? root.workspace.selectedCount : (typeof root.workspace.checkedRepositoryCount === "function" ? root.workspace.checkedRepositoryCount() : 0)) : 0
        readonly property int totalCount: root.workspace ? (typeof root.workspace.repositoryCount !== "undefined" ? root.workspace.repositoryCount : (typeof root.workspace.totalRepositoryCount === "function" ? root.workspace.totalRepositoryCount() : 0)) : 0
        readonly property bool hasActiveRepo: root.workspace && root.workspace.selectedPath && root.workspace.selectedPath.length > 0
        readonly property bool repoAvailable: root.workspace && root.workspace.isRepoWorkspace
        readonly property string previewCommand: {
            if (!root.workspace) return "未选择任何仓库"
            if (syncMethod === 1 && typeof root.workspace.syncPreview === "function")
                return root.workspace.syncPreview(syncScope, 1)
            if (syncScope === 0) {
                if (!hasActiveRepo) return "未选择任何仓库"
                const repoPath = root.workspace.selectedPath
                const repoName = root.workspace.selectedName || repoPath
                return "git -C " + repoPath + " pull --ff-only"
                    + (repoName ? " (" + repoName + ")" : "")
            } else if (syncScope === 1) {
                if (checkedCount <= 0) return "未勾选任何仓库"
                return "git pull --ff-only（已勾选 " + checkedCount + " 个仓库）"
            } else if (syncScope === 2) {
                if (totalCount <= 0) return "无可用仓库"
                return "git pull --ff-only（全部 " + totalCount + " 个仓库）"
            }
            return "未选择任何仓库"
        }
        readonly property bool canSync: {
            if (!root.workspace || root.workspace.busy) return false
            if (syncMethod === 1 && !repoAvailable) return false
            if (syncScope === 0) return hasActiveRepo
            if (syncScope === 1) return checkedCount > 0
            if (syncScope === 2) return totalCount > 0
            return false
        }

        function updateOkButton() {
            const okBtn = syncDialog.standardButton(Dialog.Ok)
            if (okBtn) okBtn.enabled = canSync
        }

        onOpened: {
            if (checkedCount > 0) {
                syncScope = 1
            } else if (repoAvailable) {
                syncScope = 2
            } else if (hasActiveRepo) {
                syncScope = 0
            } else {
                syncScope = 2
            }
            syncMethod = repoAvailable ? 1 : 0
            updateOkButton()
        }

        onSyncScopeChanged: updateOkButton()
        onSyncMethodChanged: updateOkButton()
        onCanSyncChanged: updateOkButton()

        onAccepted: {
            if (canSync) {
                if (syncMethod === 1)
                    root.workspace.syncRepoSelection(syncScope)
                else
                    root.workspace.syncSelection(syncScope)
            }
        }

        contentItem: ColumnLayout {
            spacing: 12

            Text {
                text: syncDialog.syncMethod === 1
                    ? "按当前本地 Manifest 同步所选仓库；适用于 detached 仓库，不需要 Git 上游分支。"
                    : "通过快进（fast-forward）拉取所选仓库的 Git 上游分支。"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.text
                font.pixelSize: Theme.fontSecondary
                font.weight: Font.Medium
            }

            RowLayout {
                spacing: 14
                RadioButton {
                    text: "Git 拉取上游"
                    checked: syncDialog.syncMethod === 0
                    onClicked: syncDialog.syncMethod = 0
                }
                RadioButton {
                    text: "Repo 按清单同步"
                    checked: syncDialog.syncMethod === 1
                    enabled: syncDialog.repoAvailable
                    onClicked: syncDialog.syncMethod = 1
                }
            }

            Text {
                visible: syncDialog.syncMethod === 1
                text: "仅同步下面选定的仓库；使用 --no-manifest-update，不强制覆盖本地改动。同步可能更新代码，发生冲突时会保留现场并显示命令日志。"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.secondaryText
                font.pixelSize: Theme.fontSecondary
            }

            Text {
                visible: root.workspace && root.workspace.busy
                text: "⚠️ 当前正在执行任务（" + ((root.workspace && root.workspace.activeTask) ? root.workspace.activeTask : "处理中") + "），请等待完成后再执行拉取。"
                color: Theme.orange
                font.pixelSize: Theme.fontSecondary
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }

            RowLayout {
                spacing: 14
                RadioButton {
                    id: scopeCurrentRadio
                    text: "当前仓库" + (syncDialog.hasActiveRepo ? (" (" + root.workspace.selectedName + ")") : "")
                    checked: syncDialog.syncScope === 0
                    enabled: syncDialog.hasActiveRepo
                    onClicked: syncDialog.syncScope = 0
                }
                RadioButton {
                    id: scopeCheckedRadio
                    text: "已勾选 (" + syncDialog.checkedCount + ")"
                    checked: syncDialog.syncScope === 1
                    enabled: syncDialog.checkedCount > 0
                    onClicked: syncDialog.syncScope = 1
                }
                RadioButton {
                    id: scopeAllRadio
                    text: "全部仓库 (" + syncDialog.totalCount + ")"
                    checked: syncDialog.syncScope === 2
                    enabled: syncDialog.totalCount > 0
                    onClicked: syncDialog.syncScope = 2
                }
            }

            Text {
                text: "预览执行命令："
                color: Theme.secondaryText
                font.pixelSize: Theme.fontSecondary
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 56
                color: Theme.surfaceMuted
                radius: 8
                border.color: Theme.separatorSoft
                Text {
                    anchors.fill: parent
                    anchors.margins: 10
                    text: syncDialog.previewCommand
                    color: syncDialog.canSync ? Theme.text : Theme.red
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Theme.fontSecondary
                    verticalAlignment: Text.AlignVCenter
                    wrapMode: Text.WrapAnywhere
                }
            }

            PrimaryButton {
                text: "设置当前仓库拉取上游"
                secondary: true
                visible: syncDialog.syncMethod === 0
                enabled: root.workspace && syncDialog.hasActiveRepo && !root.workspace.busy
                onClicked: {
                    syncDialog.close()
                    upstreamDialog.open()
                }
            }
        }
    }

    AppDialog {
        id: upstreamDialog
        objectName: "upstreamDialog"
        preferredWidth: 510
        title: "设置拉取上游"
        standardButtons: Dialog.Cancel | Dialog.Ok
        readonly property bool canConfigure: root.workspace && root.workspace.selectedPath.length > 0
            && !root.workspace.busy
            && /^[A-Za-z0-9][A-Za-z0-9._-]*$/.test(upstreamRemote.text.trim())
            && /^[A-Za-z0-9][A-Za-z0-9._/-]*$/.test(upstreamBranch.text.trim())
        readonly property var suggestions: {
            if (!root.workspace || !root.workspace.remoteTrackingBranches) return []
            const remote = upstreamRemote.text.trim().toLowerCase()
            const query = upstreamBranch.text.trim().toLowerCase()
            return root.workspace.remoteTrackingBranches.filter(function(ref) {
                const slash = ref.indexOf("/")
                return slash > 0 && ref.slice(0, slash).toLowerCase() === remote
                    && ref.slice(slash + 1).toLowerCase().indexOf(query) !== -1
            })
        }

        function updateOkButton() {
            const okBtn = upstreamDialog.standardButton(Dialog.Ok)
            if (okBtn) {
                okBtn.enabled = canConfigure
                okBtn.text = "验证并保存"
            }
        }

        onOpened: {
            const refs = root.workspace && root.workspace.remoteTrackingBranches
                ? root.workspace.remoteTrackingBranches : []
            const first = refs.find(function(ref) { return ref.startsWith("origin/") })
                || (refs.length ? refs[0] : "")
            upstreamRemote.text = first.indexOf("/") > 0 ? first.slice(0, first.indexOf("/")) : "origin"
            upstreamBranch.text = ""
            updateOkButton()
        }
        onCanConfigureChanged: updateOkButton()
        onAccepted: {
            if (canConfigure)
                root.workspace.configurePullUpstream(upstreamRemote.text.trim(), upstreamBranch.text.trim())
        }

        contentItem: ColumnLayout {
            spacing: 10
            Text {
                Layout.fillWidth: true
                text: root.workspace ? ("当前仓库：" + root.workspace.selectedName + " · 本地分支：" + root.workspace.selectedBranch) : ""
                color: Theme.text
                font.pixelSize: Theme.fontSecondary
                wrapMode: Text.Wrap
            }
            Text {
                Layout.fillWidth: true
                text: "明确指定本地分支日常拉取的 Remote 和远端分支；不会改变 Gerrit 推送目标。"
                color: Theme.secondaryText
                font.pixelSize: Theme.fontCaption
                wrapMode: Text.Wrap
            }
            TextField {
                id: upstreamRemote
                objectName: "upstreamRemote"
                Layout.fillWidth: true
                placeholderText: "Remote 名称，例如 origin"
                font.pixelSize: Theme.fontSecondary
            }
            TextField {
                id: upstreamBranch
                objectName: "upstreamBranch"
                Layout.fillWidth: true
                placeholderText: "远端分支，例如 master"
                font.pixelSize: Theme.fontSecondary
            }
            Text {
                Layout.fillWidth: true
                text: "本地已有的远端分支（可点选；也可手动输入）："
                color: Theme.tertiaryText
                font.pixelSize: Theme.fontCaption
            }
            ListView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(contentHeight, 128)
                clip: true
                model: upstreamDialog.suggestions
                ScrollBar.vertical: RightScrollBar { }
                delegate: ItemDelegate {
                    required property string modelData
                    width: ListView.view.width
                    height: 30
                    text: modelData
                    onClicked: upstreamBranch.text = modelData.slice(modelData.indexOf("/") + 1)
                }
            }
            Text {
                Layout.fillWidth: true
                text: "保存前会向该 Remote 核实分支存在；此操作只设置本地上游，不会自动拉取代码。"
                color: Theme.tertiaryText
                font.pixelSize: Theme.fontCaption
                wrapMode: Text.Wrap
            }
        }
    }

    AppDialog {
        id: pushDialog
        objectName: "pushDialog"
        preferredWidth: 540
        title: "推送 Gerrit 评审"
        standardButtons: Dialog.Cancel | Dialog.Ok

        property string selectedTargetBranch: ""
        property string currentTopic: ""
        property string currentRemote: ""
        readonly property var filteredBranchChoices: {
            const query = branchSearchInput.text.trim().toLowerCase()
            if (!query.length) return candidateBranches
            return candidateBranches.filter(function(branch) {
                return branch.toLowerCase().indexOf(query) !== -1
            })
        }

        readonly property var candidateBranches: {
            let list = []
            if (root.workspace) {
                if (!root.workspace.remoteBranchesLoaded
                    && root.workspace.targetBranch && root.workspace.targetBranch.length > 0)
                    list.push(root.workspace.targetBranch)
                if (root.workspace.remoteBranches && root.workspace.remoteBranchesRemote === currentRemote.trim()) {
                    for (let i = 0; i < root.workspace.remoteBranches.length; ++i) {
                        const b = root.workspace.remoteBranches[i]
                        if (b && !list.includes(b)) list.push(b)
                    }
                }
            }
            return list
        }

        readonly property string previewCommand: {
            const r = currentRemote.trim() || "origin"
            const b = selectedTargetBranch.trim()
            if (!b) return "未指定目标分支"
            let ref = "HEAD:refs/for/" + b
            if (currentTopic.trim().length > 0)
                ref += "%topic=" + currentTopic.trim()
            return "git push " + r + " " + ref
        }

        readonly property bool branchExists: root.workspace && root.workspace.remoteBranchesLoaded
            && root.workspace.remoteBranchesRemote === currentRemote.trim()
            && root.workspace.remoteBranches.indexOf(selectedTargetBranch.trim()) !== -1
        readonly property bool canPush: branchExists
                                        && currentRemote.trim().length > 0
                                        && (!root.workspace || !root.workspace.busy)

        function updateOkButton() {
            const okBtn = pushDialog.standardButton(Dialog.Ok)
            if (okBtn) {
                okBtn.enabled = canPush
                okBtn.text = "确认推送 Gerrit"
            }
            const cancelBtn = pushDialog.standardButton(Dialog.Cancel)
            if (cancelBtn) {
                cancelBtn.text = "取消"
            }
        }

        function chooseBranch(index) {
            if (index < 0 || index >= filteredBranchChoices.length) return
            selectedTargetBranch = filteredBranchChoices[index]
            updateOkButton()
        }

        onOpened: {
            selectedTargetBranch = ""
            currentRemote = (root.workspace && root.workspace.remote.length) ? root.workspace.remote : "origin"
            const initialBranch = (root.workspace && root.workspace.targetBranch.length)
                ? root.workspace.targetBranch
                : ""
            selectedTargetBranch = initialBranch
            currentTopic = root.workspace ? (root.workspace.topic || "") : ""

            branchSearchInput.text = ""
            topicInput.text = currentTopic
            remoteInput.text = currentRemote
            if (root.workspace && typeof root.workspace.requestRemoteBranches === "function")
                root.workspace.requestRemoteBranches(currentRemote)
            updateOkButton()
        }

        onCanPushChanged: updateOkButton()

        onAccepted: {
            if (canPush && root.workspace) {
                root.workspace.remote = pushDialog.currentRemote.trim()
                root.workspace.targetBranch = pushDialog.selectedTargetBranch.trim()
                root.workspace.topic = pushDialog.currentTopic.trim()
                root.workspace.pushActive()
            }
        }

        contentItem: ColumnLayout {
            spacing: 12

            Text {
                text: "请确认上传目标分支与参数："
                color: Theme.secondaryText
                font.pixelSize: Theme.fontSecondary
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 56
                color: Theme.accentSoft
                radius: 10
                border.color: "#BBD7F0"
                Text {
                    anchors.fill: parent
                    anchors.margins: 10
                    text: pushDialog.previewCommand
                    color: Theme.accent
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Theme.fontSecondary
                    verticalAlignment: Text.AlignVCenter
                    wrapMode: Text.WrapAnywhere
                }
            }

            Text {
                Layout.fillWidth: true
                wrapMode: Text.WrapAnywhere
                color: Theme.secondaryText
                text: root.workspace && root.workspace.remoteBranchesRemote === pushDialog.currentRemote
                    ? "实际推送地址：" + (root.workspace.pushRemoteUrl || "待查询") : "Remote 已变更，请查询目标分支"
            }
            PrimaryButton {
                text: "刷新远端目标分支"
                secondary: true
                enabled: root.workspace && !root.workspace.busy
                onClicked: root.workspace.requestRemoteBranches(pushDialog.currentRemote)
            }
            GridLayout {
                // Connection and branch selection belong to the same Remote.
                Layout.fillWidth: true
                columns: 2
                rowSpacing: 10
                columnSpacing: 12

                Text {
                    text: "当前仓库："
                    color: Theme.tertiaryText
                    font.pixelSize: Theme.fontSecondary
                    Layout.alignment: Qt.AlignVCenter
                }
                Text {
                    text: root.workspace ? (root.workspace.selectedName + " (" + root.workspace.selectedPath + ")") : ""
                    color: Theme.text
                    font.pixelSize: Theme.fontSecondary
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                    elide: Text.ElideMiddle
                }

                Text {
                    text: "当前本地分支："
                    color: Theme.tertiaryText
                    font.pixelSize: Theme.fontSecondary
                    Layout.alignment: Qt.AlignVCenter
                }
                Text {
                    text: root.workspace ? root.workspace.selectedBranch : ""
                    color: Theme.text
                    font.pixelSize: Theme.fontSecondary
                    Layout.fillWidth: true
                }

                Text {
                    text: "推送远端："
                    color: Theme.tertiaryText
                    font.pixelSize: Theme.fontSecondary
                    Layout.alignment: Qt.AlignVCenter
                }
                TextField {
                    id: remoteInput
                    objectName: "remoteInput"
                    Layout.fillWidth: true
                    font.pixelSize: Theme.fontSecondary
                    placeholderText: "例如 origin"
                    onEditingFinished: {
                        if (root.workspace && typeof root.workspace.requestRemoteBranches === "function")
                            root.workspace.requestRemoteBranches(text.trim())
                    }
                    onTextChanged: {
                        pushDialog.currentRemote = text.trim()
                        pushDialog.updateOkButton()
                    }
                }

                Text {
                    text: "推送目标分支："
                    color: Theme.tertiaryText
                    font.pixelSize: Theme.fontSecondary
                    Layout.alignment: Qt.AlignVCenter
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    TextField {
                        id: branchSearchInput
                        objectName: "branchSearchInput"
                        Layout.fillWidth: true
                        placeholderText: "搜索远端分支，如 T1TP"
                        font.pixelSize: Theme.fontSecondary
                        onTextChanged: {
                            pushDialog.selectedTargetBranch = ""
                            pushDialog.updateOkButton()
                        }
                    }
                    ComboBox {
                        id: branchCombo
                        objectName: "branchCombo"
                        Layout.fillWidth: true
                        model: pushDialog.filteredBranchChoices
                        displayText: pushDialog.selectedTargetBranch || "选择匹配的完整分支"
                        font.pixelSize: Theme.fontSecondary
                        onActivated: index => pushDialog.chooseBranch(index)
                    }
                }

                Text {
                    text: "Topic（可选）："
                    color: Theme.tertiaryText
                    font.pixelSize: Theme.fontSecondary
                    Layout.alignment: Qt.AlignVCenter
                }
                TextField {
                    id: topicInput
                    objectName: "topicInput"
                    Layout.fillWidth: true
                    font.pixelSize: Theme.fontSecondary
                    placeholderText: "Gerrit Topic，例如 bugfix-102"
                    onTextChanged: pushDialog.currentTopic = text.trim()
                }

                Text {
                    text: "Gerrit 服务器："
                    color: Theme.tertiaryText
                    font.pixelSize: Theme.fontSecondary
                    Layout.alignment: Qt.AlignVCenter
                }
                Text {
                    text: root.workspace && root.workspace.gerritHost.length > 0
                        ? (root.workspace.gerritUser + "@" + root.workspace.gerritHost + ":" + root.workspace.gerritPort)
                        : "（使用 Remote 默认配置）"
                    color: Theme.secondaryText
                    font.pixelSize: Theme.fontSecondary
                    Layout.fillWidth: true
                }
            }

            Text {
                visible: !pushDialog.canPush
                text: pushDialog.selectedTargetBranch.trim().length === 0
                    ? "请输入关键词并从远端目标分支中选择。"
                    : "请选择远端存在的完整分支名，并确认推送远端。"
                color: Theme.red
                font.pixelSize: Theme.fontSecondary
                font.bold: true
            }

            Text {
                text: "上传后会在 Gerrit 中创建或更新 Change；目标分支与 Topic 会自动保存供下次使用。"
                color: Theme.tertiaryText
                font.pixelSize: Theme.fontCaption
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }
    }

}
