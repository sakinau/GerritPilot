import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GerritPilot
import "../../components"

ColumnLayout {
    id: root
    property var workspace
    signal syncRequested()

    spacing: 6

    function scrollToActive() {
        if (!root.workspace || !root.visible) return
        const proxyIdx = (typeof root.workspace.activeProxyIndex === "function") ? root.workspace.activeProxyIndex() : -1
        if (proxyIdx >= 0 && repositoryList.count > 0) {
            repositoryList.currentIndex = proxyIdx
            if (proxyIdx === 0)
                repositoryList.positionViewAtBeginning()
            else
                repositoryList.positionViewAtIndex(proxyIdx, ListView.Contain)
        }
    }

    SectionHeader {
        Layout.fillWidth: true
        Layout.leftMargin: 4
        Layout.topMargin: 4
        text: "仓库"
        detail: root.workspace
            ? (root.workspace.selectedCount > 0
               ? ("已选 " + root.workspace.selectedCount + " / " + root.workspace.repositoryCount)
               : String(root.workspace.repositoryCount))
            : ""
        IconButton {
            visible: root.workspace && root.workspace.selectedCount > 0
            iconName: "x"
            toolTip: "取消全选"
            implicitWidth: 24
            implicitHeight: 24
            onClicked: root.workspace.selectAll(false)
        }
        IconButton {
            iconName: "download"
            toolTip: (root.workspace && root.workspace.selectedCount > 0)
                ? ("拉取代码（已勾选 " + root.workspace.selectedCount + " 个仓库）…")
                : (root.workspace && root.workspace.isRepoWorkspace ? "同步代码 (repo sync)…" : "拉取代码…")
            implicitWidth: 24
            implicitHeight: 24
            enabled: root.workspace && !root.workspace.busy
            onClicked: root.syncRequested()
        }
        IconButton {
            iconName: "refresh"
            toolTip: "刷新全部仓库状态（Ctrl+R）"
            implicitWidth: 24
            implicitHeight: 24
            enabled: root.workspace && !root.workspace.busy
            onClicked: root.workspace.refreshAll()
        }
    }

    SearchField {
        id: repositorySearch
        Layout.fillWidth: true
        Layout.rightMargin: 8
        placeholderText: "筛选仓库"
        onTextChanged: root.workspace.repositoryModel.filterText = text
    }

    ListView {
        id: repositoryList
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.topMargin: 2
        clip: true
        spacing: 2
        rightMargin: 8
        model: root.workspace ? root.workspace.repositoryModel : null
        ScrollBar.vertical: RightScrollBar { }
        boundsBehavior: Flickable.StopAtBounds
        Connections {
            target: root.workspace
            function onSelectedRepositoryChanged() { Qt.callLater(root.scrollToActive) }
        }
        delegate: RepositoryRow {
            width: repositoryList.width - repositoryList.rightMargin
            workspace: root.workspace
        }
        Column {
            anchors.centerIn: parent
            visible: repositoryList.count === 0
            spacing: 6
            Icon { anchors.horizontalCenter: parent.horizontalCenter; name: "folder"; size: 28; strokeWidth: 1.4; color: Theme.quaternaryText }
            Text {
                text: repositorySearch.text.length ? "没有匹配的仓库" : "没有仓库"
                color: Theme.tertiaryText
                font.pixelSize: Theme.fontSecondary
            }
        }
    }
}
