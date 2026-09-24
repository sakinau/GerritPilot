pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GerritPilot
import "../../components"

GlassCard {
    id: root

    property var workspace
    property int currentTab: 0
    onCurrentTabChanged: {
        if (currentTab === 1 && workspace) workspace.loadHistory()
    }
    property bool sideBySideDiff: false
    readonly property bool compactActions: width < 760

    signal tabRequested(int index)
    signal branchRequested()
    signal hookRequested()
    signal settingsRequested()
    signal revisionRequested(string revision)
    signal revisionCheckoutRequested(string revision)
    signal workingTreeRequested()
    signal syncRequested()
    signal commitRequested(string message, bool amend, bool stageAll)
    signal pushRequested()

    property string pendingRevision: ""
    property string pendingCherryPickRevision: ""
    property string pendingRevertRevision: ""
    property var selectedChangePaths: []
    readonly property var selectedConflictPaths: {
        if (!root.workspace) return []
        const checked = changesFileList ? changesFileList.getCheckedPaths() : []
        const selected = checked.length > 0 ? checked : root.selectedChangePaths
        return selected.filter(function(path) {
            return root.workspace.groupedChanges.some(function(file) {
                return file.conflict && file.path === path
            })
        })
    }
    property string selectionRepository: ""

    readonly property alias checkedFilesCount: changesFileList.checkedFilesCount
    function clearCheckedFiles() { if (changesFileList) changesFileList.clearCheckedFiles() }
    function toggleFileCheck(path, staged, conflict) { if (changesFileList) changesFileList.toggleFileCheck(path, staged, conflict) }
    function getCheckedUnstaged() { return changesFileList ? changesFileList.getCheckedUnstaged() : [] }
    function getCheckedStaged() { return changesFileList ? changesFileList.getCheckedStaged() : [] }
    function getAllUnstaged() { return changesFileList ? changesFileList.getAllUnstaged() : [] }
    function getAllStaged() { return changesFileList ? changesFileList.getAllStaged() : [] }

    Connections {
        target: root.workspace
        function onSelectedRepositoryChanged() {
            if (root.currentTab === 1) Qt.callLater(function() { root.workspace.loadHistory() })
            if (root.selectionRepository !== root.workspace.selectedPath) {
                root.selectionRepository = root.workspace.selectedPath
                root.selectedChangePaths = []
            }
        }
        function onDetailsTextChanged() {
            root.selectedChangePaths = []
        }
    }

    function openCommandPanel() {
        const point = root.mapToItem(Overlay.overlay, root.width, 0)
        commandPopup.x = Math.max(18, point.x - commandPopup.width)
        commandPopup.y = point.y + 54
        commandPopup.open()
    }

    Layout.fillWidth: true
    Layout.fillHeight: true
    Layout.minimumWidth: 420

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.panePadding
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Rectangle {
                Layout.preferredWidth: 40
                Layout.preferredHeight: 40
                radius: 12
                color: Theme.accentSoft
                Text {
                    anchors.centerIn: parent
                    text: "⌘"
                    color: Theme.accent
                    font.pixelSize: 19
                    font.weight: Font.DemiBold
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                Text {
                    id: repositoryName
                    objectName: "repositoryName"
                    text: root.workspace.selectedName.length ? root.workspace.selectedName : "未选择仓库"
                    Layout.fillWidth: true
                    color: Theme.text
                    font.pixelSize: Theme.fontTitle
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    HoverHandler { id: repositoryNameHover }
                    ToolTip.visible: repositoryNameHover.hovered && root.workspace.selectedPath.length > 0
                    ToolTip.text: root.workspace.workspacePath + "/" + root.workspace.selectedPath
                    ToolTip.delay: 500
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: root.workspace.selectedPath.length > 0
                    Rectangle {
                        objectName: "headerBranchButton"
                        color: branchMouse.containsMouse ? Theme.accentSoft : "#F0F4FA"
                        border.color: branchMouse.containsMouse ? Theme.accent : Theme.separatorSoft
                        border.width: 1
                        radius: 6
                        implicitWidth: branchRow.implicitWidth + 14
                        implicitHeight: 24
                        RowLayout {
                            id: branchRow
                            anchors.centerIn: parent
                            spacing: 4
                            Text {
                                text: "⎇ " + root.workspace.selectedBranch
                                color: Theme.accent
                                font.pixelSize: Theme.fontSecondary
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                                Layout.maximumWidth: 280
                            }
                            Text {
                                text: "▾"
                                color: Theme.accent
                                font.pixelSize: Theme.fontSecondary
                            }
                        }
                        MouseArea {
                            id: branchMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            ToolTip.visible: containsMouse
                            ToolTip.text: "本地分支管理（切换 / 新建 / 删除）"
                            onClicked: root.branchRequested()
                        }
                    }
                }
                Text {
                    visible: root.workspace.selectedPath.length === 0
                    text: "从左侧选择一个仓库"
                    color: Theme.secondaryText
                    font.pixelSize: Theme.fontSecondary
                }
            }
            PrimaryButton {
                id: headerPushBtn
                objectName: "headerPushBtn"
                glyph: "↑"
                text: "推送"
                toolTip: "推送当前仓库到 Gerrit 评审 (Ctrl+Enter)"
                implicitHeight: 32
                implicitWidth: 76
                enabled: root.workspace && !root.workspace.busy && root.workspace.selectedPath.length > 0
                onClicked: root.pushRequested()
            }
            IconButton {
                glyph: "↻"
                toolTip: "刷新当前仓库状态"
                implicitHeight: 32
                implicitWidth: 32
                enabled: root.workspace && !root.workspace.busy
                onClicked: root.workspace.refreshActive()
            }
            IconButton {
                glyph: "⌘"
                toolTip: "命令日志"
                implicitHeight: 32
                implicitWidth: 32
                onClicked: root.openCommandPanel()
            }
            IconButton {
                glyph: "⚙"
                toolTip: "设置"
                implicitHeight: 32
                implicitWidth: 32
                onClicked: root.settingsRequested()
            }
            IconButton {
                visible: !root.compactActions
                glyph: "▣"
                toolTip: "贮藏管理"
                implicitHeight: 32
                implicitWidth: 32
                enabled: !root.workspace.busy && root.workspace.selectedPath.length > 0
                onClicked: {
                    root.workspace.loadStashList()
                    stashDialog.open()
                }
            }
            IconButton {
                visible: !root.compactActions
                glyph: root.workspace.selectedIgnored ? "⊕" : "⊖"
                toolTip: root.workspace.selectedIgnored ? "恢复仓库显示" : "隐藏当前仓库（可在设置中恢复）"
                implicitHeight: 32
                implicitWidth: 32
                enabled: root.workspace.selectedPath.length > 0
                onClicked: root.workspace.setActiveRepositoryIgnored(!root.workspace.selectedIgnored)
            }
            IconButton {
                visible: root.compactActions
                glyph: "⋯"
                toolTip: "仓库选项"
                implicitHeight: 32
                implicitWidth: 32
                onClicked: repositoryMenu.open()
                Menu {
                    id: repositoryMenu
                    y: parent.height
                    MenuItem {
                        text: "贮藏管理…"
                        onTriggered: {
                            if (root.workspace) root.workspace.loadStashList()
                            stashDialog.open()
                        }
                    }
                    MenuItem {
                        text: root.workspace.selectedIgnored ? "恢复显示此仓库" : "隐藏此仓库"
                        onTriggered: root.workspace.setActiveRepositoryIgnored(!root.workspace.selectedIgnored)
                    }
                }
            }
        }

        // Consolidated Secondary Toolbar (Segmented Tabs + Contextual Actions)
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            spacing: 10

            // Compact Segmented Control
            Rectangle {
                Layout.preferredWidth: 220
                Layout.fillHeight: true
                radius: 7
                color: "#EEF0F4"
                border.color: Theme.separatorSoft
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 2
                    spacing: 2

                    Repeater {
                        model: ["工作区改动", "提交历史"]
                        Button {
                            id: tabButton
                            required property int index
                            required property string modelData
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            text: modelData
                            padding: 0
                            hoverEnabled: true
                            onClicked: root.tabRequested(index)
                            background: Rectangle {
                                radius: 5
                                color: root.currentTab === tabButton.index ? Theme.surfaceStrong : (tabButton.hovered ? "#F8F9FA" : "transparent")
                                border.color: root.currentTab === tabButton.index ? Theme.separatorSoft : "transparent"
                                border.width: root.currentTab === tabButton.index ? 1 : 0
                            }
                            contentItem: Text {
                                text: tabButton.text
                                color: root.currentTab === tabButton.index ? Theme.text : Theme.secondaryText
                                font.pixelSize: Theme.fontBody
                                font.weight: root.currentTab === tabButton.index ? Font.DemiBold : Font.Medium
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }
                }
            }

            // Tab 0 Contextual Controls: Diff Toolbar
            RowLayout {
                visible: root.currentTab === 0
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 8

                Text {
                    text: root.workspace.selectedFile.length
                        ? root.workspace.selectedFile + (root.workspace.selectedFileStaged ? " · 已暂存" : " · 工作区")
                        : "代码差异"
                    elide: Text.ElideMiddle
                    color: Theme.secondaryText
                    font.pixelSize: Theme.fontSecondary
                    Layout.fillWidth: true
                }
                Button {
                    id: workingTreeButton
                    text: "工作区改动"
                    implicitWidth: 80
                    implicitHeight: 28
                    padding: 0
                    hoverEnabled: true
                    onClicked: root.workingTreeRequested()
                    background: Rectangle {
                        radius: 6
                        color: workingTreeButton.down ? "#DCEBFA" : workingTreeButton.hovered ? Theme.accentSoft : "transparent"
                        border.color: workingTreeButton.hovered ? Theme.accent : "transparent"
                        border.width: 1
                    }
                    contentItem: Text {
                        text: workingTreeButton.text
                        color: Theme.accent
                        font.pixelSize: Theme.fontCaption
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                Text {
                    objectName: "staleStatusLabel"
                    visible: root.workspace.detailsCached
                    text: "状态未验证"
                    color: Theme.orange
                    font.pixelSize: Theme.fontCaption
                }
                FileActionButton {
                    objectName: "refreshRepositoryButton"
                    text: "↻"
                    toolTip: "刷新当前仓库"
                    enabled: !root.workspace.busy && root.workspace.selectedPath.length > 0
                    onClicked: root.workspace.refreshActive()
                }
                Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 16; color: Theme.separatorSoft }
                ToolButton {
                    id: unifiedButton
                    text: "统一"
                    implicitWidth: 46
                    implicitHeight: 28
                    padding: 0
                    hoverEnabled: true
                    checkable: true
                    checked: !root.sideBySideDiff
                    onClicked: root.sideBySideDiff = false
                    background: Rectangle {
                        radius: 6
                        color: unifiedButton.checked ? Theme.accentSoft : (unifiedButton.hovered ? "#F8F9FA" : "transparent")
                        border.color: unifiedButton.checked ? Theme.accent : "transparent"
                        border.width: 1
                    }
                    contentItem: Text {
                        text: unifiedButton.text
                        color: unifiedButton.checked ? Theme.accent : Theme.secondaryText
                        font.pixelSize: Theme.fontCaption
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                ToolButton {
                    id: splitButton
                    text: "左右对比"
                    implicitWidth: 68
                    implicitHeight: 28
                    padding: 0
                    hoverEnabled: true
                    checkable: true
                    checked: root.sideBySideDiff
                    onClicked: root.sideBySideDiff = true
                    background: Rectangle {
                        radius: 6
                        color: splitButton.checked ? Theme.accentSoft : (splitButton.hovered ? "#F8F9FA" : "transparent")
                        border.color: splitButton.checked ? Theme.accent : "transparent"
                        border.width: 1
                    }
                    contentItem: Text {
                        text: splitButton.text
                        color: splitButton.checked ? Theme.accent : Theme.secondaryText
                        font.pixelSize: Theme.fontCaption
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                ToolButton {
                    id: whitespaceButton
                    text: "忽略空白"
                    implicitWidth: 68
                    implicitHeight: 28
                    padding: 0
                    hoverEnabled: true
                    checkable: true
                    checked: root.workspace ? Boolean(root.workspace.ignoreWhitespace) : false
                    onClicked: {
                        if (root.workspace)
                            root.workspace.ignoreWhitespace = !root.workspace.ignoreWhitespace
                    }
                    background: Rectangle {
                        radius: 6
                        color: whitespaceButton.checked ? Theme.accentSoft : (whitespaceButton.hovered ? "#F8F9FA" : "transparent")
                        border.color: whitespaceButton.checked ? Theme.accent : "transparent"
                        border.width: 1
                    }
                    contentItem: Text {
                        text: whitespaceButton.text
                        color: whitespaceButton.checked ? Theme.accent : Theme.secondaryText
                        font.pixelSize: Theme.fontCaption
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }

            // Tab 1 Contextual Controls: History Search Filter
            RowLayout {
                visible: root.currentTab === 1
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 8

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 30
                    radius: 6
                    color: Theme.surface
                    border.color: historyFilterInput.activeFocus ? Theme.accent : Theme.separatorSoft
                    border.width: 1

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 6
                        spacing: 4

                        Text {
                            text: "🔍"
                            font.pixelSize: Theme.fontSecondary
                            color: Theme.tertiaryText
                        }

                        TextInput {
                            id: historyFilterInput
                            Layout.fillWidth: true
                            font.pixelSize: Theme.fontSecondary
                            color: Theme.text
                            clip: true
                            selectByMouse: true
                            onTextChanged: historyPane.searchFilter = text

                            Text {
                                anchors.fill: parent
                                text: "按说明、作者或 SHA 筛选已加载历史..."
                                color: Theme.placeholder
                                font.pixelSize: Theme.fontSecondary
                                visible: !historyFilterInput.text.length && !historyFilterInput.activeFocus
                                verticalAlignment: Text.AlignVCenter
                            }
                        }

                        IconButton {
                            visible: historyFilterInput.text.length > 0
                            glyph: "✕"
                            toolTip: "清除筛选"
                            implicitWidth: 20
                            implicitHeight: 20
                            onClicked: historyFilterInput.text = ""
                        }
                    }
                }
            }
        }

        Rectangle {
            id: operationBanner
            readonly property bool hasConflicts: root.workspace ? Boolean(root.workspace.hasConflicts) : false
            readonly property bool hasPending: root.workspace ? Boolean(root.workspace.hasPendingOperation) : false
            readonly property string opName: root.workspace ? root.workspace.pendingOperation : ""
            readonly property string operationLabel: opName === "merge" ? "合并"
                : opName === "rebase" ? "变基" : opName === "cherry-pick" ? "拣选提交"
                : opName === "revert" ? "撤销提交" : "操作"
            readonly property int conflictCount: root.workspace ? root.workspace.groupedChanges.filter(function(file) { return file.conflict }).length : 0

            visible: hasConflicts || hasPending
            Layout.fillWidth: true
            Layout.preferredHeight: hasConflicts ? 104 : 68
            radius: 8
            color: hasConflicts ? "#FFF8E1" : Theme.accentSoft
            border.color: hasConflicts ? Theme.orange : Theme.accent

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 4

                Text {
                    text: operationBanner.hasConflicts
                        ? ("⚠️ " + operationBanner.operationLabel + "存在 " + operationBanner.conflictCount + " 个冲突文件")
                        : ("ℹ️ " + operationBanner.operationLabel + "已无未解决冲突，可以继续或终止")
                    color: operationBanner.hasConflicts ? "#B76E00" : Theme.accent
                    font.pixelSize: Theme.fontSecondary
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                }
                Text {
                    visible: operationBanner.hasConflicts
                    text: "左侧点 ↗ 编辑冲突标记并保存，再点 ✓ 标记解决。"
                    color: Theme.secondaryText
                    font.pixelSize: Theme.fontCaption
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Item { Layout.fillWidth: true }
                    PrimaryButton {
                        visible: operationBanner.hasConflicts
                        text: "标记选中已解决"
                        toolTip: "只标记左侧选中的冲突文件；请先完成编辑"
                        enabled: root.workspace && !root.workspace.busy && root.selectedConflictPaths.length > 0
                        onClicked: root.workspace.markResolved(root.selectedConflictPaths)
                    }
                    PrimaryButton {
                        visible: operationBanner.hasPending
                        text: "继续" + operationBanner.operationLabel
                        enabled: root.workspace && !root.workspace.busy && !operationBanner.hasConflicts
                        toolTip: operationBanner.hasConflicts ? "需先解决并标记所有冲突文件" : "继续执行中断的 Git 操作"
                        onClicked: root.workspace.continueOperation()
                    }
                    PrimaryButton {
                        visible: operationBanner.hasPending
                        text: "终止" + operationBanner.operationLabel
                        secondary: true
                        enabled: root.workspace && !root.workspace.busy
                        toolTip: "终止当前操作并恢复到操作前状态"
                        onClicked: abortConfirmDialog.open()
                    }
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.currentTab
            clip: true

            SplitView {
                objectName: "changesSplit"
                orientation: Qt.Horizontal
                clip: true
                handle: Rectangle {
                    implicitWidth: 6
                    color: "transparent"
                    Rectangle {
                        anchors.centerIn: parent
                        width: (SplitHandle.pressed || SplitHandle.hovered) ? 2 : 1
                        height: parent.height
                        color: (SplitHandle.pressed || SplitHandle.hovered) ? Theme.accent : Theme.separatorSoft
                    }
                }
                GlassCard {
                    objectName: "changesFilesPane"
                    radius: 0
                    border.width: 0
                    SplitView.preferredWidth: 360
                    SplitView.minimumWidth: 300
                    SplitView.maximumWidth: 700
                    clip: true
                    SplitView {
                        anchors.fill: parent
                        anchors.margins: 0
                        anchors.rightMargin: 10
                        orientation: Qt.Vertical
                        clip: true
                        handle: Rectangle {
                            implicitHeight: 6
                            color: "transparent"
                            Rectangle {
                                anchors.centerIn: parent
                                height: (SplitHandle.pressed || SplitHandle.hovered) ? 2 : 1
                                width: parent.width
                                color: (SplitHandle.pressed || SplitHandle.hovered) ? Theme.accent : Theme.separatorSoft
                            }
                        }
                    ChangesFileList {
                        id: changesFileList
                        SplitView.fillHeight: true
                        SplitView.minimumHeight: 120
                        workspace: root.workspace
                        onDiscardRequested: (paths) => {
                            root.selectedChangePaths = paths
                            discardDialog.open()
                        }
                    }
                        CommitEditor {
                            objectName: "commitEditorPane"
                            SplitView.preferredHeight: Math.max(250, implicitHeight)
                            SplitView.minimumHeight: implicitHeight
                            workspace: root.workspace
                            onCommitRequested: (message, amend, stageAll) => root.commitRequested(message, amend, stageAll)
                        }
                    }
                }
                DiffView {
                    objectName: "changesDiffPane"
                    SplitView.fillWidth: true
                    SplitView.minimumWidth: 240
                    clip: true
                    diffText: root.workspace ? root.workspace.diffText : ""
                    renderingEnabled: root.currentTab === 0
                    sideBySide: root.sideBySideDiff
                    workspace: root.workspace
                }
            }
            HistoryPane {
                workspace: root.workspace
                active: root.currentTab === 1
                onRevisionRequested: revision => root.revisionRequested(revision)
                onRevisionCheckoutRequested: revision => {
                    root.pendingRevision = revision
                    revisionCheckoutDialog.open()
                }
                onCherryPickRequested: revision => {
                    root.pendingCherryPickRevision = revision
                    cherryPickConfirmDialog.open()
                }
                onRevertRequested: revision => {
                    root.pendingRevertRevision = revision
                    revertConfirmDialog.open()
                }
            }
        }
    }

    AppDialog {
        id: discardDialog
        objectName: "discardDialog"
        preferredWidth: 440
        title: "丢弃文件改动"
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: root.workspace.discardFiles(root.selectedChangePaths)
        contentItem: ScrollView {
            id: discardScroll
            implicitHeight: 160
            contentWidth: availableWidth
            clip: true
            ScrollBar.vertical: RightScrollBar { }
            TextArea {
                width: discardScroll.availableWidth
                readOnly: true
                selectByMouse: true
                text: "将丢弃以下文件的未暂存修改，保留已暂存内容；未跟踪文件将被删除。此操作无法撤销。\n\n" + root.selectedChangePaths.join("\n")
                wrapMode: TextEdit.Wrap
                color: Theme.red
            }
        }
    }

    AppDialog {
        id: revisionCheckoutDialog
        preferredWidth: 470
        title: "检出历史提交"
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: root.revisionCheckoutRequested(root.pendingRevision)
        contentItem: ColumnLayout {
            spacing: 10
            Text {
                text: "将以 detached HEAD 状态检出提交 " + root.pendingRevision
                color: Theme.text
                font.pixelSize: Theme.fontBody
                font.weight: Font.Medium
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Text {
                text: "适合运行或检查历史代码。Git 不会强制覆盖本地改动；如存在冲突会拒绝切换。完成后可在左侧分支列表切回任意分支。"
                color: Theme.secondaryText
                font.pixelSize: Theme.fontSecondary
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }
    }

    AppDialog {
        id: cherryPickConfirmDialog
        preferredWidth: 470
        title: "摘取提交"
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: {
            if (root.workspace && typeof root.workspace.cherryPick === "function") {
                root.workspace.cherryPick(root.pendingCherryPickRevision)
            }
        }
        contentItem: ColumnLayout {
            spacing: 10
            Text {
                text: "将把提交 " + root.pendingCherryPickRevision.slice(0, 10) + " 的修改摘取（Cherry-pick）并应用到当前分支。"
                color: Theme.text
                font.pixelSize: Theme.fontBody
                font.weight: Font.Medium
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Text {
                text: "如果产生代码冲突，可在左侧改动列表解决并标记冲突，然后点击顶部的“继续”或“终止”。"
                color: Theme.secondaryText
                font.pixelSize: Theme.fontSecondary
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }
    }

    AppDialog {
        id: revertConfirmDialog
        preferredWidth: 440
        title: "撤销此提交"
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: {
            if (root.workspace && typeof root.workspace.revertCommit === "function")
                root.workspace.revertCommit(root.pendingRevertRevision)
        }
        contentItem: ColumnLayout {
            spacing: 10
            Text {
                text: "将在当前分支新增一条反向提交，抵消 " + root.pendingRevertRevision.slice(0, 10) + " 的改动；原提交仍保留在历史中。"
                color: Theme.text
                font.pixelSize: Theme.fontBody
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Text {
                text: "需要干净的工作区。若发生冲突，可在改动列表处理后继续或终止；不会自动推送。"
                color: Theme.secondaryText
                font.pixelSize: Theme.fontSecondary
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }
    }

    Popup {
        id: commandPopup
        parent: Overlay.overlay
        width: Math.min(620, Math.max(420, root.width - 36))
        height: 260
        padding: 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            color: Theme.surfaceStrong
            radius: Theme.radiusMedium
            border.color: Theme.separator
        }
        contentItem: ColumnLayout {
            spacing: 0
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 12
                Text { text: "命令日志"; color: Theme.text; font.pixelSize: Theme.fontSecondary; font.weight: Font.DemiBold; Layout.fillWidth: true }
                CheckBox { id: followLog; text: "跟随输出"; checked: true }
                Button {
                    id: cancelTaskBtn
                    text: "取消任务"
                    implicitHeight: 28
                    implicitWidth: cancelTaskText.implicitWidth + 16
                    padding: 0
                    topInset: 0
                    bottomInset: 0
                    leftInset: 0
                    rightInset: 0
                    hoverEnabled: true
                    enabled: root.workspace.busy
                    background: Rectangle {
                        radius: 6
                        color: cancelTaskBtn.down ? "#FDD" : cancelTaskBtn.hovered ? Theme.redSoft : "transparent"
                        border.color: Theme.separator
                    }
                    contentItem: Text {
                        id: cancelTaskText
                        text: cancelTaskBtn.text
                        color: cancelTaskBtn.enabled ? Theme.red : Theme.tertiaryText
                        font.pixelSize: Theme.fontCaption
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        acceptedButtons: Qt.NoButton
                    }
                    onClicked: root.workspace.stop()
                }
                IconButton { glyph: "×"; toolTip: "关闭"; onClicked: commandPopup.close() }
            }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.separatorSoft }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: 10
                clip: true
                ScrollBar.vertical: RightScrollBar { }
                TextArea {
                    id: commandLogText
                    objectName: "commandLogText"
                    textFormat: TextEdit.PlainText
                    selectByMouse: true
                    onTextChanged: {
                        if (followLog.checked) Qt.callLater(function() {
                            commandLogText.cursorPosition = commandLogText.length
                        })
                    }
                    readOnly: true
                    text: commandPopup.opened
                        ? (root.workspace.consoleText.length ? root.workspace.consoleText : "命令输出会显示在这里")
                        : ""
                    color: Theme.secondaryText
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Theme.fontCaption
                    wrapMode: TextEdit.WrapAnywhere
                    padding: 10
                    background: Rectangle { color: Theme.surfaceMuted; radius: 9 }
                }
            }
        }
    }

    AppDialog {
        id: abortConfirmDialog
        preferredWidth: 420
        title: "终止冲突操作"
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: {
            if (root.workspace)
                root.workspace.abortOperation()
        }
        contentItem: Text {
            text: "确定要终止当前的合并或变基操作吗？\n终止操作将放弃解决冲突过程中的修改并恢复到操作前的状态。"
            color: Theme.red
            font.pixelSize: Theme.fontSecondary
            wrapMode: Text.Wrap
        }
    }

    StashDialog {
        id: stashDialog
        workspace: root.workspace
    }
}
