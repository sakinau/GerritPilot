pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GerritPilot
import "../../components"

ColumnLayout {
    id: root
    property var workspace
    property bool active: true
    property bool sideBySideDiff: false
    property string searchFilter: ""

    signal revisionRequested(string revision)
    signal revisionCheckoutRequested(string revision)
    signal cherryPickRequested(string revision)
    signal revertRequested(string revision)
    signal rewriteRequested(string revision, string subject)

    readonly property var filteredEntries: {
        const entries = root.workspace ? root.workspace.historyEntries : []
        if (!entries || !entries.length) return []
        const filter = root.searchFilter.trim().toLowerCase()
        if (!filter) return entries
        return entries.filter(item => {
            const subject = (item.subject || "").toLowerCase()
            const author = (item.author || "").toLowerCase()
            const rev = (item.revision || "").toLowerCase()
            const shortRev = (item.shortRevision || "").toLowerCase()
            return subject.includes(filter) || author.includes(filter) || rev.includes(filter) || shortRev.includes(filter)
        })
    }

    spacing: 0

    SplitView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.margins: 10
        orientation: Qt.Vertical
        handle: SplitHandleBar { horizontalBar: true }

        ColumnLayout {
            SplitView.preferredHeight: Math.min(260, root.height * 0.34)
            SplitView.minimumHeight: 110
            clip: true
            spacing: 6

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                SearchField {
                    id: historyFilterInput
                    Layout.fillWidth: true
                    Layout.maximumWidth: 420
                    placeholderText: "按说明、作者或 SHA 筛选已加载历史"
                    text: root.searchFilter
                    onTextChanged: root.searchFilter = text
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: (root.workspace && root.workspace.historyCached ? "缓存预览 · " : "")
                        + (root.searchFilter.trim().length > 0
                        ? ("匹配 " + root.filteredEntries.length + " / 已加载 " + (root.workspace ? root.workspace.historyEntries.length : 0) + " 条")
                        : ("已加载 " + (root.workspace ? root.workspace.historyEntries.length : 0) + " 条提交"))
                    color: Theme.tertiaryText
                    font.pixelSize: Theme.fontCaption
                }
                PrimaryButton {
                    objectName: "loadMoreHistoryButton"
                    text: "加载更多"
                    kind: "plain"
                    compact: true
                    iconName: "chevron-down"
                    visible: root.workspace ? root.workspace.historyHasMore : false
                    enabled: root.workspace ? !root.workspace.busy : false
                    onClicked: root.workspace.loadMoreHistory()
                }
            }

            ListView {
                id: historyList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 2
                objectName: "historyList"
                model: root.filteredEntries
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: RightScrollBar { }
                delegate: ItemDelegate {
                    id: historyDelegate
                    required property var modelData
                    readonly property bool isSelected: root.workspace && root.workspace.selectedRevision === historyDelegate.modelData.revision
                    readonly property bool showActions: hovered || isSelected
                    width: historyList.width - 8
                    height: 50
                    hoverEnabled: true
                    leftPadding: 10
                    rightPadding: 8
                    topPadding: 6
                    bottomPadding: 6
                    onClicked: {
                        const revision = historyDelegate.modelData.revision
                        if (revision.length)
                            root.revisionRequested(revision)
                    }
                    background: Rectangle {
                        radius: Theme.radiusMedium - 2
                        color: historyDelegate.isSelected ? Theme.accentTint
                            : historyDelegate.hovered ? Theme.hoverFill : "transparent"
                    }
                    contentItem: RowLayout {
                        spacing: 10
                        Avatar {
                            name: historyDelegate.modelData.author
                            size: 28
                            Layout.alignment: Qt.AlignVCenter
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                            spacing: 3
                            Text {
                                objectName: "historySubject"
                                text: historyDelegate.modelData.subject
                                color: Theme.text
                                font.pixelSize: Theme.fontBody
                                font.weight: historyDelegate.isSelected ? Font.DemiBold : Font.Medium
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                Badge {
                                    text: historyDelegate.modelData.shortRevision
                                    tone: "accent"
                                    mono: true
                                }
                                Text {
                                    text: historyDelegate.modelData.author
                                    color: Theme.secondaryText
                                    font.pixelSize: Theme.fontCaption
                                    elide: Text.ElideRight
                                    Layout.maximumWidth: 180
                                }
                                Text {
                                    text: "· " + historyDelegate.modelData.date.slice(0, 10)
                                    color: Theme.tertiaryText
                                    font.pixelSize: Theme.fontCaption
                                }
                                Item { Layout.fillWidth: true }
                            }
                        }
                        RowLayout {
                            visible: historyDelegate.showActions
                            spacing: 4
                            Layout.alignment: Qt.AlignVCenter
                            PrimaryButton {
                                text: "检出"
                                kind: "gray"
                                compact: true
                                toolTip: "以 detached HEAD 检出此提交"
                                enabled: !root.workspace.busy
                                onClicked: root.revisionCheckoutRequested(historyDelegate.modelData.revision)
                            }
                            PrimaryButton {
                                text: "摘取"
                                kind: "gray"
                                compact: true
                                toolTip: "将此提交摘取到当前分支"
                                enabled: !root.workspace.busy
                                onClicked: root.cherryPickRequested(historyDelegate.modelData.revision)
                            }
                            PrimaryButton {
                                text: "改写"
                                kind: "gray"
                                compact: true
                                toolTip: "修改、并入暂存改动或改写说明（仅限未合入的提交，保留 Change-Id）"
                                enabled: !root.workspace.busy
                                onClicked: root.rewriteRequested(historyDelegate.modelData.revision,
                                                                 historyDelegate.modelData.subject)
                            }
                            PrimaryButton {
                                text: "撤销"
                                kind: "gray"
                                compact: true
                                toolTip: "新增反向提交，不删除原提交"
                                enabled: !root.workspace.busy
                                onClicked: root.revertRequested(historyDelegate.modelData.revision)
                            }
                        }
                    }
                }
                Column {
                    anchors.centerIn: parent
                    visible: historyList.count === 0
                    spacing: 8
                    Icon { anchors.horizontalCenter: parent.horizontalCenter; name: "clock"; size: 30; strokeWidth: 1.4; color: Theme.quaternaryText }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: root.searchFilter.length ? "未找到匹配的提交（当前仅检索已加载记录）" : "暂无提交历史"
                        color: Theme.tertiaryText
                        font.pixelSize: Theme.fontSecondary
                    }
                }
            }
        }

        ColumnLayout {
            SplitView.fillHeight: true
            SplitView.minimumHeight: 180
            clip: true
            spacing: 6

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4
                spacing: 8
                Icon { name: "git-commit"; size: 15; color: Theme.tertiaryText }
                Text {
                    Layout.fillWidth: true
                    text: root.workspace && root.workspace.selectedRevision.length
                        ? "提交 " + root.workspace.selectedRevision
                        : "选择提交查看差异"
                    color: Theme.text
                    font.family: root.workspace && root.workspace.selectedRevision.length ? Theme.monoFontFamily : Qt.application.font.family
                    font.pixelSize: Theme.fontSecondary
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                }
                SegmentedControl {
                    compact: true
                    model: ["统一", "左右对比"]
                    currentIndex: root.sideBySideDiff ? 1 : 0
                    onActivated: index => root.sideBySideDiff = index === 1
                }
            }

            SplitView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                orientation: Qt.Horizontal
                handle: SplitHandleBar { }
                ListView {
                    id: files
                    SplitView.preferredWidth: 260
                    SplitView.minimumWidth: 140
                    clip: true
                    spacing: 1
                    boundsBehavior: Flickable.StopAtBounds
                    model: root.workspace ? root.workspace.revisionFiles : []
                    ScrollBar.vertical: RightScrollBar { }
                    delegate: ItemDelegate {
                        id: fileRow
                        required property string modelData
                        readonly property bool current: modelData === (root.workspace ? root.workspace.revisionFile : "")
                        width: files.width - 6
                        height: 30
                        hoverEnabled: true
                        leftPadding: 8
                        rightPadding: 6
                        text: modelData
                        onClicked: root.workspace.showRevisionFileDiff(modelData)
                        background: Rectangle {
                            radius: Theme.radiusSmall
                            color: fileRow.current ? Theme.accentTint : fileRow.hovered ? Theme.hoverFill : "transparent"
                        }
                        contentItem: RowLayout {
                            spacing: 6
                            Icon { name: "file"; size: 13; color: fileRow.current ? Theme.accent : Theme.tertiaryText }
                            Text {
                                text: fileRow.text
                                color: fileRow.current ? Theme.accent : Theme.text
                                font.pixelSize: Theme.fontSecondary
                                font.weight: fileRow.current ? Font.DemiBold : Font.Normal
                                elide: Text.ElideMiddle
                                verticalAlignment: Text.AlignVCenter
                                Layout.fillWidth: true
                            }
                        }
                    }
                }
                DiffView {
                    SplitView.fillWidth: true
                    SplitView.minimumWidth: 200
                    diffText: root.workspace ? root.workspace.revisionDiff : ""
                    renderingEnabled: root.active
                    sideBySide: root.sideBySideDiff
                    workspace: root.workspace
                }
            }
        }
    }
}
