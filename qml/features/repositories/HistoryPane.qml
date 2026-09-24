pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
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

    SplitView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        orientation: Qt.Vertical
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
        ColumnLayout {
            SplitView.preferredHeight: root.height * 0.48
            SplitView.minimumHeight: 140
            clip: true
    ListView {
        id: historyList
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        spacing: 4
        objectName: "historyList"
        model: root.filteredEntries
        ScrollBar.vertical: RightScrollBar { }
        delegate: ItemDelegate {
            id: historyDelegate
            required property var modelData
            readonly property bool isSelected: root.workspace && root.workspace.selectedRevision === historyDelegate.modelData.revision
            width: historyList.width
            height: 56
            hoverEnabled: true
            leftPadding: 12
            rightPadding: 10
            topPadding: 6
            bottomPadding: 6
            onClicked: {
                const revision = historyDelegate.modelData.revision
                if (revision.length)
                    root.revisionRequested(revision)
            }
            background: Rectangle {
                radius: 6
                color: historyDelegate.isSelected ? Theme.accentSoft : (historyDelegate.hovered ? "#F8F9FA" : "transparent")
                border.color: historyDelegate.isSelected ? Theme.accent : (historyDelegate.hovered ? Theme.separatorSoft : "transparent")
                border.width: 1

                Rectangle {
                    visible: historyDelegate.isSelected
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.margins: 4
                    width: 3
                    radius: 1.5
                    color: Theme.accent
                }
            }
            contentItem: ColumnLayout {
                spacing: 2
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: historyDelegate.modelData.shortRevision
                        color: Theme.accent
                        font.family: Theme.monoFontFamily
                        font.pixelSize: Theme.fontCaption
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: historyDelegate.modelData.author
                        color: Theme.secondaryText
                        font.pixelSize: Theme.fontCaption
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Text {
                        text: historyDelegate.modelData.date.slice(0, 10)
                        color: Theme.tertiaryText
                        font.pixelSize: Theme.fontCaption
                    }
                    PrimaryButton {
                        id: checkoutButton
                        text: "检出"
                        secondary: true
                        implicitHeight: 26
                        implicitWidth: 56
                        enabled: !root.workspace.busy
                        onClicked: {
                            root.revisionCheckoutRequested(historyDelegate.modelData.revision)
                        }
                    }
                    PrimaryButton {
                        id: cherryPickButton
                        text: "摘取"
                        secondary: true
                        toolTip: "将此提交摘取到当前分支"
                        implicitHeight: 26
                        implicitWidth: 56
                        enabled: !root.workspace.busy
                        onClicked: {
                            root.cherryPickRequested(historyDelegate.modelData.revision)
                        }
                    }
                    PrimaryButton {
                        text: "撤销"
                        secondary: true
                        toolTip: "新增反向提交，不删除原提交"
                        implicitHeight: 26
                        implicitWidth: 56
                        enabled: !root.workspace.busy
                        onClicked: root.revertRequested(historyDelegate.modelData.revision)
                    }
                }
                Text {
                    objectName: "historySubject"
                    text: historyDelegate.modelData.subject
                    color: Theme.text
                    font.pixelSize: Theme.fontBody
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }
        }
        Text {
            anchors.centerIn: parent
            visible: historyList.count === 0
            text: root.searchFilter.length ? "未找到匹配的提交（当前仅检索已加载记录）" : "暂无提交历史"
            color: Theme.tertiaryText
            font.pixelSize: Theme.fontSecondary
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Text {
            Layout.fillWidth: true
            text: root.searchFilter.trim().length > 0
                ? ("匹配 " + root.filteredEntries.length + " / 已加载 " + (root.workspace ? root.workspace.historyEntries.length : 0) + " 条（当前仅筛选已加载记录）")
                : ("已加载 " + (root.workspace ? root.workspace.historyEntries.length : 0) + " 条提交")
            color: Theme.tertiaryText
            font.pixelSize: Theme.fontSecondary
        }
        PrimaryButton {
            objectName: "loadMoreHistoryButton"
            text: "加载更多"
            secondary: true
            visible: root.workspace ? root.workspace.historyHasMore : false
            enabled: root.workspace ? !root.workspace.busy : false
            onClicked: root.workspace.loadMoreHistory()
        }
    }

        }
        ColumnLayout {
            SplitView.fillHeight: true
            SplitView.minimumHeight: 180
            clip: true
    RowLayout {
        Layout.fillWidth: true
        Text {
            Layout.fillWidth: true
            text: root.workspace && root.workspace.selectedRevision.length
                ? "提交 " + root.workspace.selectedRevision
                : "选择提交查看差异"
            color: Theme.secondaryText
        }
        FileActionButton {
            text: "⇄"
            toolTip: root.sideBySideDiff ? "切换到统一 Diff" : "切换到左右 Diff"
            onClicked: root.sideBySideDiff = !root.sideBySideDiff
        }
    }

    SplitView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        orientation: Qt.Horizontal
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
        ListView {
            id: files
            SplitView.preferredWidth: 260
            SplitView.minimumWidth: 120
            clip: true
            model: root.workspace ? root.workspace.revisionFiles : []
            ScrollBar.vertical: RightScrollBar { }
            delegate: ItemDelegate {
                id: fileRow
                required property string modelData
                width: files.width
                height: 34
                text: modelData
                highlighted: modelData === (root.workspace ? root.workspace.revisionFile : "")
                onClicked: root.workspace.showRevisionFileDiff(modelData)
                contentItem: Text {
                    text: fileRow.text
                    color: Theme.text
                    font.pixelSize: Theme.fontSecondary
                    elide: Text.ElideMiddle
                    verticalAlignment: Text.AlignVCenter
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
