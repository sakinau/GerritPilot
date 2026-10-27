import QtQuick
import QtQuick.Controls.Basic
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
    function openChangeDownload() { changeDialog.open() }

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
        readonly property bool initializedOnly: repoAvailable && (totalCount === 0
            || (totalCount === 1 && root.workspace.selectedPath === ".repo/manifests"))
        readonly property string previewCommand: {
            if (!root.workspace) return "未选择任何仓库"
            if (syncMethod === 1 && typeof root.workspace.syncPreview === "function")
                return root.workspace.syncPreview(syncScope, 1)
            const pullCommand = root.workspace.pullRebase ? "pull --rebase" : "pull --ff-only"
            if (syncScope === 0) {
                if (!hasActiveRepo) return "未选择任何仓库"
                const repoPath = root.workspace.selectedPath
                const repoName = root.workspace.selectedName || repoPath
                return "git -C " + repoPath + " " + pullCommand
                    + (repoName ? " (" + repoName + ")" : "")
            } else if (syncScope === 1) {
                if (checkedCount <= 0) return "未勾选任何仓库"
                return "git " + pullCommand + "（已勾选 " + checkedCount + " 个仓库）"
            } else if (syncScope === 2) {
                if (totalCount <= 0) return "无可用仓库"
                return "git " + pullCommand + "（全部 " + totalCount + " 个仓库）"
            }
            return "未选择任何仓库"
        }
        readonly property bool canSync: {
            if (!root.workspace || root.workspace.busy) return false
            if (syncMethod === 1 && !repoAvailable) return false
            if (syncScope === 0) return hasActiveRepo
            if (syncScope === 1) return checkedCount > 0
            if (syncScope === 2) return syncMethod === 1 ? repoAvailable : totalCount > 0
            return false
        }

        function updateOkButton() {
            const okBtn = syncDialog.standardButton(Dialog.Ok)
            if (okBtn) okBtn.enabled = canSync
        }

        onOpened: {
            if (initializedOnly) {
                syncScope = 2
            } else if (checkedCount > 0) {
                syncScope = 1
            } else if (hasActiveRepo) {
                syncScope = 0
            } else {
                syncScope = 2
            }
            syncMethod = initializedOnly ? 1 : (hasActiveRepo || checkedCount > 0 ? 0 : (repoAvailable ? 1 : 0))
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
                    ? "按当前本地 Manifest 同步代码，不需要 Git 上游分支。"
                    : root.workspace && root.workspace.pullRebase
                        ? "拉取所选仓库的 Git 上游分支，并把本地未合入的提交变基到最新代码之后（保留 Change-Id）。"
                        : "通过快进（fast-forward）拉取所选仓库的 Git 上游分支。"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.text
                font.pixelSize: Theme.fontSecondary
                font.weight: Font.Medium
            }

            RowLayout {
                spacing: 14
                AppRadioButton {
                    text: "Git 拉取上游"
                    checked: syncDialog.syncMethod === 0
                    onClicked: syncDialog.syncMethod = 0
                }
                AppRadioButton {
                    text: "Repo 按清单同步"
                    checked: syncDialog.syncMethod === 1
                    enabled: syncDialog.repoAvailable
                    onClicked: syncDialog.syncMethod = 1
                }
            }

            RowLayout {
                visible: syncDialog.syncMethod === 0
                spacing: 14
                AppRadioButton {
                    text: "仅快进"
                    checked: root.workspace && !root.workspace.pullRebase
                    onClicked: root.workspace.pullRebase = false
                }
                AppRadioButton {
                    text: "变基（Rebase，Gerrit 推荐）"
                    checked: root.workspace && root.workspace.pullRebase
                    onClicked: root.workspace.pullRebase = true
                }
            }

            Text {
                visible: syncDialog.syncMethod === 0 && root.workspace && root.workspace.pullRebase
                text: "需要干净的工作区；如发生冲突，变基会暂停，可在改动列表解决后继续或终止。"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.secondaryText
                font.pixelSize: Theme.fontSecondary
            }

            Text {
                visible: syncDialog.syncMethod === 1
                text: syncDialog.syncScope === 2
                    ? "将同步整个项目，可能下载大量代码。使用本地 Manifest，不更新清单检出、不强制覆盖改动；失败保留现场。确认范围后再执行。"
                    : "仅同步所选业务仓库；使用本地 Manifest，不更新清单检出、不强制覆盖改动。Manifest 仓库本身请使用 Git 拉取。"
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
                AppRadioButton {
                    id: scopeCurrentRadio
                    text: "当前仓库" + (syncDialog.hasActiveRepo ? (" (" + root.workspace.selectedName + ")") : "")
                    checked: syncDialog.syncScope === 0
                    enabled: syncDialog.hasActiveRepo
                    onClicked: syncDialog.syncScope = 0
                }
                AppRadioButton {
                    id: scopeCheckedRadio
                    text: "已勾选 (" + syncDialog.checkedCount + ")"
                    checked: syncDialog.syncScope === 1
                    enabled: syncDialog.checkedCount > 0
                    onClicked: syncDialog.syncScope = 1
                }
                AppRadioButton {
                    id: scopeAllRadio
                    text: syncDialog.syncMethod === 1 ? "整个项目" : "全部仓库 (" + syncDialog.totalCount + ")"
                    checked: syncDialog.syncScope === 2
                    enabled: syncDialog.syncMethod === 1 ? syncDialog.repoAvailable : syncDialog.totalCount > 0
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
                color: Theme.surfaceGrouped
                radius: Theme.radiusMedium
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
            AppTextField {
                id: upstreamRemote
                objectName: "upstreamRemote"
                Layout.fillWidth: true
                placeholderText: "Remote 名称，例如 origin"
                font.pixelSize: Theme.fontSecondary
            }
            AppTextField {
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
        property string currentReviewers: ""
        property string currentCc: ""
        property string currentHashtags: ""
        property int workState: 0 // 0 keep, 1 WIP, 2 ready
        property bool privateChange: false
        property string patchsetMessage: ""
        property bool showAdvanced: false
        readonly property var pushOptions: ({
            topic: currentTopic.trim(),
            reviewers: currentReviewers,
            cc: currentCc,
            hashtags: currentHashtags,
            workState: workState,
            private: privateChange,
            message: patchsetMessage
        })
        readonly property string optionsError: root.workspace && typeof root.workspace.pushOptionsError === "function"
            ? root.workspace.pushOptionsError(pushOptions) : ""
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

        readonly property string previewCommand: root.workspace
            ? root.workspace.pushCommandPreview(currentRemote, selectedTargetBranch.trim(), pushOptions)
            : "未指定目标分支"

        readonly property bool branchExists: root.workspace && root.workspace.remoteBranchesLoaded
            && root.workspace.remoteBranchesRemote === currentRemote.trim()
            && root.workspace.remoteBranches.indexOf(selectedTargetBranch.trim()) !== -1
        readonly property bool canPush: branchExists
                                        && optionsError.length === 0
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
            // Reviewers and CC persist per repository; the remaining options apply to one push.
            currentReviewers = root.workspace ? (root.workspace.reviewers || "") : ""
            currentCc = root.workspace ? (root.workspace.cc || "") : ""
            currentHashtags = ""
            workState = 0
            privateChange = false
            patchsetMessage = ""
            showAdvanced = false

            branchSearchInput.text = ""
            topicInput.text = currentTopic
            reviewersInput.text = currentReviewers
            ccInput.text = currentCc
            hashtagsInput.text = ""
            messageInput.text = ""
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
                root.workspace.reviewers = pushDialog.currentReviewers
                root.workspace.cc = pushDialog.currentCc
                root.workspace.pushActive({
                    hashtags: pushDialog.currentHashtags,
                    workState: pushDialog.workState,
                    private: pushDialog.privateChange,
                    message: pushDialog.patchsetMessage
                })
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
                Layout.preferredHeight: Math.max(56, pushPreviewText.implicitHeight + 20)
                color: Theme.accentSoft
                radius: Theme.radiusMedium
                border.color: Theme.tones.info.border
                Text {
                    id: pushPreviewText
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
                kind: "gray"
                compact: true
                iconName: "refresh"
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
                AppTextField {
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
                    AppTextField {
                        id: branchSearchInput
                        objectName: "branchSearchInput"
                        Layout.fillWidth: true
                        placeholderText: "搜索远端目标分支"
                        font.pixelSize: Theme.fontSecondary
                        onTextChanged: {
                            pushDialog.selectedTargetBranch = ""
                            pushDialog.updateOkButton()
                        }
                    }
                    AppComboBox {
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
                AppTextField {
                    id: topicInput
                    objectName: "topicInput"
                    Layout.fillWidth: true
                    font.pixelSize: Theme.fontSecondary
                    placeholderText: "Gerrit Topic，例如 bugfix-102"
                    onTextChanged: pushDialog.currentTopic = text.trim()
                }

                Text {
                    text: "评审人（可选）："
                    color: Theme.tertiaryText
                    font.pixelSize: Theme.fontSecondary
                    Layout.alignment: Qt.AlignVCenter
                }
                AppTextField {
                    id: reviewersInput
                    objectName: "reviewersInput"
                    Layout.fillWidth: true
                    font.pixelSize: Theme.fontSecondary
                    placeholderText: "用户名或邮箱，多个用逗号分隔"
                    onTextChanged: pushDialog.currentReviewers = text
                }

                Text {
                    text: "抄送（可选）："
                    color: Theme.tertiaryText
                    font.pixelSize: Theme.fontSecondary
                    Layout.alignment: Qt.AlignVCenter
                }
                AppTextField {
                    id: ccInput
                    objectName: "ccInput"
                    Layout.fillWidth: true
                    font.pixelSize: Theme.fontSecondary
                    placeholderText: "用户名或邮箱，多个用逗号分隔"
                    onTextChanged: pushDialog.currentCc = text
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

            PrimaryButton {
                text: pushDialog.showAdvanced ? "收起更多选项" : "更多选项（WIP / 私有 / Hashtag / 补丁集说明）"
                kind: "plain"
                compact: true
                iconName: pushDialog.showAdvanced ? "chevron-up" : "chevron-down"
                onClicked: pushDialog.showAdvanced = !pushDialog.showAdvanced
            }

            GridLayout {
                visible: pushDialog.showAdvanced
                Layout.fillWidth: true
                columns: 2
                rowSpacing: 10
                columnSpacing: 12

                Text {
                    text: "评审状态："
                    color: Theme.tertiaryText
                    font.pixelSize: Theme.fontSecondary
                    Layout.alignment: Qt.AlignVCenter
                }
                SegmentedControl {
                    compact: true
                    model: ["保持不变", "标记 WIP", "标记可评审"]
                    currentIndex: pushDialog.workState
                    onActivated: index => pushDialog.workState = index
                }

                Text {
                    text: "可见性："
                    color: Theme.tertiaryText
                    font.pixelSize: Theme.fontSecondary
                    Layout.alignment: Qt.AlignVCenter
                }
                FlatCheckBox {
                    text: "设为私有 Change（仅自己和评审人可见）"
                    controlled: true
                    checked: pushDialog.privateChange
                    onToggled: checked => pushDialog.privateChange = checked
                }

                Text {
                    text: "Hashtag："
                    color: Theme.tertiaryText
                    font.pixelSize: Theme.fontSecondary
                    Layout.alignment: Qt.AlignVCenter
                }
                AppTextField {
                    id: hashtagsInput
                    objectName: "hashtagsInput"
                    Layout.fillWidth: true
                    font.pixelSize: Theme.fontSecondary
                    placeholderText: "多个用逗号分隔，例如 hotfix, v2.1"
                    onTextChanged: pushDialog.currentHashtags = text
                }

                Text {
                    text: "补丁集说明："
                    color: Theme.tertiaryText
                    font.pixelSize: Theme.fontSecondary
                    Layout.alignment: Qt.AlignVCenter
                }
                AppTextField {
                    id: messageInput
                    objectName: "patchsetMessageInput"
                    Layout.fillWidth: true
                    font.pixelSize: Theme.fontSecondary
                    placeholderText: "显示在 Gerrit 评审记录中，例如“按评审意见修改”"
                    onTextChanged: pushDialog.patchsetMessage = text
                }
            }

            Text {
                visible: pushDialog.optionsError.length > 0
                text: pushDialog.optionsError
                color: Theme.red
                font.pixelSize: Theme.fontSecondary
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            Text {
                visible: !pushDialog.canPush && pushDialog.optionsError.length === 0
                text: pushDialog.selectedTargetBranch.trim().length === 0
                    ? "请输入关键词并从远端目标分支中选择。"
                    : "请选择远端存在的完整分支名，并确认推送远端。"
                color: Theme.red
                font.pixelSize: Theme.fontSecondary
                font.bold: true
            }

            Text {
                text: "上传后会在 Gerrit 中创建或更新 Change；目标分支、Topic、评审人与抄送会自动保存供下次使用。"
                color: Theme.tertiaryText
                font.pixelSize: Theme.fontCaption
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }
    }

    AppDialog {
        id: changeDialog
        objectName: "changeDialog"
        preferredWidth: 520
        title: "下载 Gerrit Change"
        standardButtons: Dialog.Cancel | Dialog.Ok
        property int mode: 0 // 0 new branch, 1 cherry-pick, 2 detached checkout
        readonly property bool canFetch: root.workspace && !root.workspace.busy
            && root.workspace.selectedPath.length > 0 && changeInput.text.trim().length > 0

        function updateOkButton() {
            const okBtn = changeDialog.standardButton(Dialog.Ok)
            if (okBtn) {
                okBtn.enabled = canFetch
                okBtn.text = "下载"
            }
        }

        onOpened: {
            changeInput.text = ""
            mode = 0
            updateOkButton()
            changeInput.forceActiveFocus()
        }
        onCanFetchChanged: updateOkButton()
        onAccepted: {
            if (canFetch)
                root.workspace.fetchChange(changeInput.text.trim(), mode)
        }

        contentItem: ColumnLayout {
            spacing: 10
            Text {
                Layout.fillWidth: true
                text: root.workspace ? ("当前仓库：" + root.workspace.selectedName + " · 远端：" + (root.workspace.remote || "origin")) : ""
                color: Theme.text
                font.pixelSize: Theme.fontSecondary
                wrapMode: Text.Wrap
            }
            AppTextField {
                id: changeInput
                objectName: "changeInput"
                Layout.fillWidth: true
                font.pixelSize: Theme.fontSecondary
                placeholderText: "Change 号（12345 或 12345/3）、评审链接或 refs/changes/..."
                onAccepted: if (changeDialog.canFetch) changeDialog.accept()
            }
            Text {
                Layout.fillWidth: true
                text: "未指定补丁集时自动下载最新补丁集。Change 必须属于当前仓库。"
                color: Theme.tertiaryText
                font.pixelSize: Theme.fontCaption
                wrapMode: Text.Wrap
            }
            ColumnLayout {
                spacing: 6
                AppRadioButton {
                    text: "检出到新的本地分支 review/<Change>-<补丁集>"
                    checked: changeDialog.mode === 0
                    onClicked: changeDialog.mode = 0
                }
                AppRadioButton {
                    text: "摘取（Cherry-pick）到当前分支"
                    checked: changeDialog.mode === 1
                    onClicked: changeDialog.mode = 1
                }
                AppRadioButton {
                    text: "仅检出查看（detached HEAD）"
                    checked: changeDialog.mode === 2
                    onClicked: changeDialog.mode = 2
                }
            }
            Text {
                Layout.fillWidth: true
                text: changeDialog.mode === 0
                    ? "适合接手或基于该 Change 继续修改；在新分支上修订后推送会更新原 Change。"
                    : changeDialog.mode === 1
                        ? "摘取的提交保留原 Change-Id：推送到同一目标分支会更新原 Change，推送到其他分支会创建新的 Change。"
                        : "适合评审时运行或编译验证；完成后可在分支列表切回原分支。"
                color: Theme.secondaryText
                font.pixelSize: Theme.fontCaption
                wrapMode: Text.Wrap
            }
        }
    }
}
