import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GerritPilot
import "../../components"

// Header above the working-tree diff: file identity and view options.
Rectangle {
    id: root
    property var workspace
    property bool sideBySide: false
    signal sideBySideRequested(bool enabled)
    signal workingTreeRequested()

    readonly property string filePath: workspace ? workspace.selectedFile : ""
    readonly property string fileName: filePath.length ? filePath.slice(filePath.lastIndexOf("/") + 1) : ""
    readonly property string fileDir: filePath.indexOf("/") >= 0 ? filePath.slice(0, filePath.lastIndexOf("/")) : ""

    implicitHeight: 42
    color: Theme.surface

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.separatorSoft
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 10
        spacing: 8

        Icon {
            name: root.filePath.length ? "file-diff" : "git-commit"
            size: 15
            color: Theme.tertiaryText
            Layout.alignment: Qt.AlignVCenter
        }
        Text {
            text: root.filePath.length ? root.fileName : "全部改动"
            color: Theme.text
            font.pixelSize: Theme.fontBody
            font.weight: Font.DemiBold
            elide: Text.ElideMiddle
            // The file name keeps its width; only the directory gives way.
            Layout.preferredWidth: implicitWidth
            Layout.minimumWidth: Math.min(implicitWidth, 80)
        }
        Text {
            visible: root.fileDir.length > 0
            text: root.fileDir
            color: Theme.tertiaryText
            font.pixelSize: Theme.fontCaption
            elide: Text.ElideMiddle
            Layout.fillWidth: true
            Layout.preferredWidth: 0
            Layout.minimumWidth: 0
        }
        Badge {
            visible: root.filePath.length > 0
            text: root.workspace && root.workspace.selectedFileStaged ? "已暂存" : "工作区"
            tone: root.workspace && root.workspace.selectedFileStaged ? "green" : "orange"
        }
        Badge {
            objectName: "staleStatusLabel"
            visible: root.workspace ? Boolean(root.workspace.detailsCached) : false
            text: "状态未验证"
            iconName: "alert-triangle"
            tone: "orange"
        }
        Item { Layout.fillWidth: true; visible: root.fileDir.length === 0 }

        PrimaryButton {
            visible: root.filePath.length > 0
            kind: "plain"
            compact: true
            iconName: "chevron-left"
            text: "全部改动"
            toolTip: "返回当前仓库的完整工作区差异"
            onClicked: root.workingTreeRequested()
        }
        IconButton {
            objectName: "refreshRepositoryButton"
            iconName: "refresh"
            toolTip: "刷新当前仓库"
            implicitWidth: 28
            implicitHeight: 28
            enabled: root.workspace && !root.workspace.busy && root.workspace.selectedPath.length > 0
            onClicked: root.workspace.refreshActive()
        }
        SegmentedControl {
            compact: true
            model: ["统一", "左右对比"]
            currentIndex: root.sideBySide ? 1 : 0
            onActivated: index => root.sideBySideRequested(index === 1)
        }
        ToggleChip {
            text: "忽略空白"
            toolTip: "对比时忽略空白字符改动"
            checked: root.workspace ? Boolean(root.workspace.ignoreWhitespace) : false
            onClicked: {
                if (root.workspace)
                    root.workspace.ignoreWhitespace = !root.workspace.ignoreWhitespace
            }
        }
    }
}
