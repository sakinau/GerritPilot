import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GerritPilot
import "../../components"

Rectangle {
    id: root
    clip: true

    property var workspace
    property bool collapsed: false

    signal toggleRequested()
    signal settingsRequested()
    signal syncRequested()
    signal branchRequested()

    function openProjectManager(tabIndex) {
        if (typeof tabIndex === "number")
            projectManagerDialog.currentTab = tabIndex
        projectManagerDialog.open()
    }

    function filteredBranches() {
        const keyword = branchSearch.text.trim().toLowerCase()
        const current = root.workspace.selectedBranch
        const branches = root.workspace.availableBranches.filter(function(branch) {
            return !keyword.length || branch.toLowerCase().indexOf(keyword) !== -1
        })
        branches.sort(function(left, right) {
            if (left === current)
                return -1
            if (right === current)
                return 1
            return left.localeCompare(right)
        })
        return branches
    }

    function scrollToActive() {
        if (!root.workspace || root.collapsed) return
        const proxyIdx = (typeof root.workspace.activeProxyIndex === "function") ? root.workspace.activeProxyIndex() : -1
        if (proxyIdx >= 0 && repositoryList.count > 0) {
            repositoryList.currentIndex = proxyIdx
            if (proxyIdx === 0) {
                repositoryList.positionViewAtBeginning()
            } else {
                repositoryList.positionViewAtIndex(proxyIdx, ListView.Contain)
            }
        }
    }


    color: Theme.sidebar

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: Theme.separator
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: root.collapsed ? 12 : 16
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Layout.bottomMargin: 10
            spacing: 11
            AppIcon { }
            ColumnLayout {
                visible: !root.collapsed
                spacing: -1
                Text {
                    text: "GerritPilot"
                    color: Theme.text
                    font.pixelSize: Theme.fontSubheading
                    font.weight: Font.DemiBold
                }
                Text {
                    text: "代码评审工作区"
                    color: Theme.tertiaryText
                    font.pixelSize: Theme.fontCaption
                }
            }
            Item { Layout.fillWidth: true }
            IconButton {
                glyph: root.collapsed ? "›" : "‹"
                toolTip: root.collapsed ? "展开侧栏（Ctrl+B）" : "收起侧栏（Ctrl+B）"
                onClicked: root.toggleRequested()
            }
        }

        // Project Selector Card
        Rectangle {
            id: projectCard
            visible: !root.collapsed
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            Layout.bottomMargin: 4
            radius: 8
            color: Theme.surface
            border.color: (projectMenu.opened || projectMouseArea.containsMouse) ? Theme.accent : Theme.separatorSoft
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 8
                spacing: 8

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    MouseArea {
                        id: projectMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        acceptedButtons: Qt.LeftButton
                        onClicked: {
                            if (projectMenu.opened) {
                                projectMenu.close()
                            } else {
                                projectMenu.popup(0, projectCard.height + 2)
                            }
                        }
                    }

                    RowLayout {
                        anchors.fill: parent
                        spacing: 8

                        Rectangle {
                            Layout.preferredWidth: 26
                            Layout.preferredHeight: 26
                            radius: 6
                            color: Theme.accentSoft
                            Layout.alignment: Qt.AlignVCenter
                            Text {
                                anchors.centerIn: parent
                                text: "◫"
                                font.pixelSize: 15
                                color: Theme.accent
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                            spacing: 1

                            RowLayout {
                                spacing: 4
                                Text {
                                    text: (root.workspace && root.workspace.projectManager && root.workspace.projectManager.currentProjectName)
                                        ? root.workspace.projectManager.currentProjectName
                                        : "未选择项目"
                                    color: Theme.text
                                    font.pixelSize: Theme.fontBody
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                                Text {
                                    text: "▾"
                                    color: Theme.secondaryText
                                    font.pixelSize: Theme.fontSecondary
                                }
                            }

                            Text {
                                text: (root.workspace && root.workspace.projectManager && root.workspace.projectManager.currentProjectPath)
                                    ? root.workspace.projectManager.currentProjectPath
                                    : (root.workspace ? root.workspace.workspacePath : "")
                                color: Theme.tertiaryText
                                font.pixelSize: Theme.fontSecondary
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }
                        }
                    }
                }

                IconButton {
                    glyph: "⚙"
                    toolTip: "管理项目列表"
                    Layout.preferredWidth: 24
                    Layout.preferredHeight: 24
                    Layout.alignment: Qt.AlignVCenter
                    onClicked: projectManagerDialog.open()
                }
            }

            Menu {
                id: projectMenu
                y: projectCard.height + 2
                width: Math.max(projectCard.width, 240)

                Instantiator {
                    model: (root.workspace && root.workspace.projectManager)
                        ? root.workspace.projectManager.projectList
                        : []
                    onObjectAdded: (index, obj) => projectMenu.insertItem(index, obj)
                    onObjectRemoved: (index, obj) => projectMenu.removeItem(obj)

                    delegate: MenuItem {
                        required property int index
                        required property var modelData

                        readonly property bool isCurrent: (root.workspace && root.workspace.projectManager)
                            ? (root.workspace.projectManager.currentIndex === index)
                            : false

                        text: (isCurrent ? "✓  " : "    ") + (modelData ? modelData.name : ("项目 " + (index + 1)))
                        enabled: !root.workspace || root.workspace.pushState !== 1

                        onTriggered: {
                            if (root.workspace && root.workspace.projectManager) {
                                root.workspace.projectManager.switchProject(index)
                            }
                        }
                    }
                }

                MenuSeparator {
                    visible: (root.workspace && root.workspace.projectManager && root.workspace.projectManager.projectList)
                        ? root.workspace.projectManager.projectList.length > 0
                        : false
                }

                MenuItem {
                    text: "＋  新建 / 拉取项目…"
                    onTriggered: {
                        projectManagerDialog.currentTab = 0
                        projectManagerDialog.open()
                    }
                }

                MenuItem {
                    text: "⚙  管理项目列表…"
                    onTriggered: {
                        projectManagerDialog.currentTab = 1
                        projectManagerDialog.open()
                    }
                }
            }
        }

        RowLayout {
            visible: !root.collapsed
            Layout.fillWidth: true
            Layout.bottomMargin: 2
            Text {
                text: (root.workspace && root.workspace.selectedCount > 0)
                    ? ("仓库 (已选 " + root.workspace.selectedCount + ")")
                    : "仓库"
                color: Theme.secondaryText
                font.pixelSize: Theme.fontSecondary
                font.weight: Font.DemiBold
                font.letterSpacing: 0.5
                Layout.fillWidth: true
            }
            IconButton {
                visible: root.workspace && root.workspace.selectedCount > 0
                glyph: "✕"
                toolTip: "取消全选"
                onClicked: root.workspace.selectAll(false)
            }
            IconButton {
                glyph: "↓"
                toolTip: (root.workspace && root.workspace.selectedCount > 0)
                    ? ("拉取代码（已勾选 " + root.workspace.selectedCount + " 个仓库）...")
                    : (root.workspace && root.workspace.isRepoWorkspace ? "同步代码 (repo sync)..." : "拉取代码 (Sync)...")
                enabled: root.workspace && !root.workspace.busy
                onClicked: root.syncRequested()
            }
            IconButton {
                glyph: "↻"
                toolTip: "刷新全部仓库状态 (Ctrl+R)"
                enabled: root.workspace && !root.workspace.busy
                onClicked: root.workspace.refreshAll()
            }
        }

        TextField {
            id: repositorySearch
            visible: !root.collapsed
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            placeholderText: "筛选仓库…"
            selectByMouse: true
            font.pixelSize: Theme.fontSecondary
            rightPadding: clearSearchBtn.visible ? 24 : 8
            onTextChanged: root.workspace.repositoryModel.filterText = text
            background: Rectangle {
                radius: 6
                color: Theme.surface
                border.color: repositorySearch.activeFocus ? Theme.accent : Theme.separatorSoft
                border.width: 1
            }

            Text {
                id: clearSearchBtn
                visible: repositorySearch.text.length > 0
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: "✕"
                color: clearSearchMouse.containsMouse ? Theme.text : Theme.tertiaryText
                font.pixelSize: 11

                MouseArea {
                    id: clearSearchMouse
                    anchors.fill: parent
                    anchors.margins: -4
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        repositorySearch.text = ""
                        repositorySearch.forceActiveFocus()
                    }
                }
            }
        }

        ListView {
            id: repositoryList
            visible: !root.collapsed
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 2
            model: root.workspace.repositoryModel
            ScrollBar.vertical: RightScrollBar { }
            Connections {
                target: root.workspace
                function onSelectedRepositoryChanged() {
                    Qt.callLater(root.scrollToActive)
                }
            }
            delegate: ItemDelegate {
                id: repositoryDelegate
                required property int index
                required property string repoName
                required property string repoPath
                required property string branch
                required property int changeCount
                required property int ahead
                required property bool repoSelected
                width: repositoryList.width
                height: 52
                hoverEnabled: true
                onClicked: root.workspace.activateRepository(index)
                HoverHandler { id: repositoryHover }
                background: Rectangle {
                    radius: 8
                    readonly property bool selected: root.workspace.selectedPath === repositoryDelegate.repoPath
                    readonly property bool highlighted: repositoryHover.hovered || repositoryDelegate.hovered
                    color: selected ? (highlighted ? "#D6E9FF" : Theme.accentSoft)
                                    : highlighted ? "#DEE9F7" : "transparent"
                    border.color: selected ? Theme.accent : highlighted ? "#B8CFEA" : "transparent"
                    border.width: highlighted || selected ? 1 : 0
                }
                contentItem: RowLayout {
                    spacing: 6
                    CheckBox {
                        id: repoCheckBox
                        checked: repositoryDelegate.repoSelected
                        Layout.preferredWidth: 20
                        Layout.preferredHeight: 20
                        Layout.alignment: Qt.AlignVCenter
                        onToggled: {
                            if (root.workspace && typeof root.workspace.toggleRepository === "function") {
                                root.workspace.toggleRepository(repositoryDelegate.index)
                            }
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            text: repositoryDelegate.repoName
                            color: Theme.text
                            font.pixelSize: Theme.fontBody
                            font.weight: root.workspace.selectedPath === repositoryDelegate.repoPath ? Font.DemiBold : Font.Medium
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Text {
                                text: "⌘ " + repositoryDelegate.branch
                                color: Theme.secondaryText
                                font.pixelSize: Theme.fontCaption
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Text {
                                visible: repositoryDelegate.changeCount > 0
                                text: repositoryDelegate.changeCount
                                color: Theme.orange
                                font.pixelSize: Theme.fontCaption
                                font.weight: Font.DemiBold
                            }
                            Text {
                                visible: repositoryDelegate.ahead > 0
                                text: "↑" + repositoryDelegate.ahead
                                color: Theme.purple
                                font.pixelSize: Theme.fontCaption
                                font.weight: Font.DemiBold
                            }
                        }
                    }
                }
            }
            Text { anchors.centerIn: parent; visible: repositoryList.count === 0; text: "没有仓库"; color: Theme.tertiaryText; font.pixelSize: Theme.fontSecondary }
        }

        RowLayout {
            visible: !root.collapsed && root.workspace.selectedName.length > 0
            Layout.fillWidth: true
            Layout.topMargin: 10
            Layout.bottomMargin: 2
            Text {
                text: "分支 · " + root.workspace.selectedName
                color: Theme.secondaryText
                font.pixelSize: Theme.fontSecondary
                font.weight: Font.DemiBold
                font.letterSpacing: 0.5
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }

        TextField {
            id: branchSearch
            visible: !root.collapsed && root.workspace.selectedName.length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            placeholderText: "筛选分支…"
            selectByMouse: true
            font.pixelSize: Theme.fontSecondary
        }

        ListView {
            id: branchList
            visible: !root.collapsed && root.workspace.selectedName.length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 152)
            clip: true
            spacing: 1
            model: root.filteredBranches()
            ScrollBar.vertical: RightScrollBar { }
            delegate: ItemDelegate {
                id: branchDelegate
                required property string modelData
                width: branchList.width
                height: 32
                hoverEnabled: true
                onClicked: root.workspace.checkoutBranch(modelData)
                readonly property bool isCurrent: branchDelegate.modelData === root.workspace.selectedBranch
                background: Rectangle {
                    radius: 6
                    color: branchDelegate.isCurrent ? Theme.accentSoft : branchDelegate.hovered ? "#F8F9FA" : "transparent"
                    border.color: branchDelegate.isCurrent ? Theme.accent : branchDelegate.hovered ? Theme.separatorSoft : "transparent"
                    border.width: 1
                }
                contentItem: RowLayout {
                    spacing: 4
                    Text {
                        text: (branchDelegate.modelData === root.workspace.selectedBranch ? "✓  " : "   ") + branchDelegate.modelData
                        color: branchDelegate.modelData === root.workspace.selectedBranch ? Theme.accent : Theme.text
                        font.pixelSize: Theme.fontSecondary
                        font.weight: branchDelegate.modelData === root.workspace.selectedBranch ? Font.DemiBold : Font.Normal
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }

        GlassCard {
            visible: !root.collapsed
            Layout.fillWidth: true
            Layout.preferredHeight: root.workspace.busy ? 88 : 74
            color: "#F6F7FA"
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 5
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    BusyIndicator {
                        visible: root.workspace.busy
                        running: visible
                        Layout.preferredWidth: 18
                        Layout.preferredHeight: 18
                    }
                    Rectangle {
                        visible: !root.workspace.busy
                        Layout.preferredWidth: 9
                        Layout.preferredHeight: 9
                        radius: 5
                        color: Theme.green
                    }
                    Text {
                        text: root.workspace.busy ? root.workspace.activeTask : "工作区已就绪"
                        color: Theme.text
                        font.pixelSize: Theme.fontSecondary
                        font.weight: Font.Medium
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
                Text {
                    text: root.workspace.busy
                        ? (root.workspace.activeTask.length ? ("正在执行：" + root.workspace.activeTask) : "正在执行命令...")
                        : root.workspace.repositoryCount + " 个仓库已载入"
                    color: root.workspace.busy ? Theme.accent : Theme.secondaryText
                    font.pixelSize: Theme.fontCaption
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Button {
                    id: stopButton
                    visible: root.workspace.busy
                    text: "停止"
                    implicitHeight: 24
                    implicitWidth: stopText.implicitWidth + 16
                    padding: 0
                    topInset: 0
                    bottomInset: 0
                    leftInset: 0
                    rightInset: 0
                    hoverEnabled: true
                    background: Rectangle {
                        radius: 6
                        color: stopButton.down ? "#FDD" : stopButton.hovered ? Theme.redSoft : "transparent"
                        border.color: Theme.red
                        border.width: 1
                    }
                    contentItem: Text {
                        id: stopText
                        text: stopButton.text
                        color: Theme.red
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
            }
        }

    }

    ProjectManagerDialog {
        id: projectManagerDialog
        workspace: root.workspace
        repoManager: (typeof repoProject !== "undefined" ? repoProject : null)
    }
}
