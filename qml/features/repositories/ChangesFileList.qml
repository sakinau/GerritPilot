pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GerritPilot
import "../../components"

ColumnLayout {
    id: root

    property var workspace
    property var checkedFiles: ({})
    property int checkedFilesCount: 0
    // groupedChanges is synthesized by C++; keep one snapshot per details update
    // instead of rebuilding the whole list for every visible delegate and toolbar binding.
    readonly property var displayedChanges: workspace ? workspace.groupedChanges : []

    signal discardRequested(var paths)

    function clearCheckedFiles() {
        checkedFiles = {}
        checkedFilesCount = 0
    }

    function reconcileCheckedFiles() {
        const available = {}
        for (const file of displayedChanges || [])
            available[(file.staged ? "S:" : "U:") + file.path] = true
        const kept = {}
        for (const key in checkedFiles) {
            if (available[key]) kept[key] = checkedFiles[key]
        }
        checkedFiles = kept
        checkedFilesCount = Object.keys(kept).length
    }

    function toggleFileCheck(path, staged, conflict) {
        const key = (staged ? "S:" : "U:") + path
        const map = Object.assign({}, checkedFiles)
        if (map[key]) {
            delete map[key]
        } else {
            map[key] = { path: path, staged: staged, conflict: !!conflict }
        }
        checkedFiles = map
        checkedFilesCount = Object.keys(map).length
    }

    function getCheckedUnstaged() {
        const res = []
        for (const k in checkedFiles) {
            if (!checkedFiles[k].staged && !checkedFiles[k].conflict) res.push(checkedFiles[k].path)
        }
        return res
    }

    function getCheckedStaged() {
        const res = []
        for (const k in checkedFiles) {
            if (checkedFiles[k].staged) res.push(checkedFiles[k].path)
        }
        return res
    }

    function getCheckedPaths() {
        const res = []
        for (const k in checkedFiles) {
            res.push(checkedFiles[k].path)
        }
        return res
    }

    function getAllUnstaged() {
        if (!root.displayedChanges) return []
        const res = []
        for (let i = 0; i < root.displayedChanges.length; ++i) {
            const item = root.displayedChanges[i]
            if (!item.staged && !item.conflict) res.push(item.path)
        }
        return res
    }

    function getAllStaged() {
        if (!root.displayedChanges) return []
        const res = []
        for (let i = 0; i < root.displayedChanges.length; ++i) {
            const item = root.displayedChanges[i]
            if (item.staged) res.push(item.path)
        }
        return res
    }

    Connections {
        target: root.workspace
        function onSelectedRepositoryChanged() {
            root.clearCheckedFiles()
        }
        function onDetailsTextChanged() {
            Qt.callLater(root.reconcileCheckedFiles)
        }
    }

    spacing: 6

    // Toolbar: batch actions for checked files, or bulk actions for everything.
    RowLayout {
        Layout.fillWidth: true
        Layout.preferredHeight: 28
        spacing: 2

        Text {
            text: root.checkedFilesCount > 0 ? ("已选 " + root.checkedFilesCount + " 项") : "改动"
            color: root.checkedFilesCount > 0 ? Theme.accent : Theme.text
            font.pixelSize: Theme.fontSubheading
            font.weight: Font.DemiBold
            Layout.alignment: Qt.AlignVCenter
        }
        Badge {
            visible: root.checkedFilesCount === 0 && root.displayedChanges.length > 0
            text: root.displayedChanges.length
            Layout.leftMargin: 4
            Layout.alignment: Qt.AlignVCenter
        }

        Item { Layout.fillWidth: true }

        // When files are checked
        PrimaryButton {
            objectName: "stageSelectedFilesButton"
            visible: root.checkedFilesCount > 0 && root.getCheckedUnstaged().length > 0
            kind: "tinted"
            compact: true
            iconName: "plus"
            text: "暂存"
            toolTip: "暂存选中的文件"
            enabled: root.workspace && !root.workspace.busy
            onClicked: {
                root.workspace.stageFiles(root.getCheckedUnstaged())
                root.clearCheckedFiles()
            }
        }
        PrimaryButton {
            visible: root.checkedFilesCount > 0 && root.getCheckedStaged().length > 0
            kind: "plain"
            compact: true
            iconName: "minus"
            text: "取消暂存"
            enabled: root.workspace && !root.workspace.busy
            onClicked: {
                root.workspace.unstageFiles(root.getCheckedStaged())
                root.clearCheckedFiles()
            }
        }
        PrimaryButton {
            visible: root.checkedFilesCount > 0 && root.getCheckedUnstaged().length > 0
            kind: "dangerTinted"
            compact: true
            iconName: "undo"
            text: "丢弃"
            toolTip: "丢弃选中文件的未暂存改动"
            enabled: root.workspace && !root.workspace.busy
            onClicked: {
                root.discardRequested(root.getCheckedUnstaged())
                root.clearCheckedFiles()
            }
        }
        IconButton {
            visible: root.checkedFilesCount > 0
            iconName: "x"
            toolTip: "取消多选"
            implicitWidth: 26
            implicitHeight: 26
            onClicked: root.clearCheckedFiles()
        }

        // When no files are checked
        PrimaryButton {
            visible: root.checkedFilesCount === 0 && root.getAllUnstaged().length > 0
            kind: "tinted"
            compact: true
            iconName: "plus"
            text: "暂存全部"
            enabled: root.workspace && !root.workspace.busy
            onClicked: root.workspace.stageFiles(root.getAllUnstaged())
        }
        PrimaryButton {
            visible: root.checkedFilesCount === 0 && root.getAllStaged().length > 0
            kind: "plain"
            compact: true
            iconName: "minus"
            text: "取消全部"
            enabled: root.workspace && !root.workspace.busy
            onClicked: root.workspace.unstageFiles(root.getAllStaged())
        }
        IconButton {
            visible: root.checkedFilesCount === 0 && root.getAllUnstaged().length > 0
            iconName: "undo"
            destructive: true
            toolTip: "丢弃全部未暂存改动"
            implicitWidth: 26
            implicitHeight: 26
            enabled: root.workspace && !root.workspace.busy
            onClicked: root.discardRequested(root.getAllUnstaged())
        }
    }

    ListView {
        id: changesList
        Layout.fillWidth: true
        Layout.fillHeight: true
        ScrollBar.vertical: RightScrollBar { }
        model: root.displayedChanges
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        delegate: ItemDelegate {
            id: changeRow
            required property int index
            required property var modelData
            readonly property bool groupStart: index === 0 || !!(root.displayedChanges[index - 1] && changeRow.modelData && root.displayedChanges[index - 1].group !== changeRow.modelData.group)
            readonly property string path: modelData ? modelData.path : ""
            readonly property string fileName: path.slice(path.lastIndexOf("/") + 1)
            readonly property string fileDir: path.indexOf("/") >= 0 ? path.slice(0, path.lastIndexOf("/")) : ""
            readonly property string status: modelData && modelData.status ? modelData.status : ""
            readonly property string statusCode: {
                // VS Code convention: U = untracked, ! = conflict.
                if (modelData && modelData.conflict) return "!"
                const code = (status[0] && status[0] !== " ") ? status[0] : (status[1] || "")
                return code === "?" ? "U" : code.trim()
            }
            readonly property color statusColor: modelData && modelData.conflict ? Theme.red
                : statusCode === "A" || statusCode === "U" ? Theme.green
                : statusCode === "D" ? Theme.red
                : statusCode === "R" || statusCode === "C" ? Theme.purple
                : Theme.orange
            readonly property bool showActions: hovered || selectedFile
            width: ListView.view ? ListView.view.width - 8 : (parent ? parent.width : 0)
            height: groupStart ? 62 : 34
            topPadding: groupStart ? 28 : 0
            bottomPadding: 0
            leftPadding: 6
            rightPadding: 4
            topInset: 0
            bottomInset: 0
            hoverEnabled: true
            readonly property bool selectedFile: !!changeRow.modelData
                && root.workspace.selectedFile === changeRow.modelData.path
                && root.workspace.selectedFileStaged === changeRow.modelData.staged
            background: Rectangle {
                y: changeRow.groupStart ? 28 : 0
                height: 34
                radius: Theme.radiusSmall + 1
                color: changeRow.selectedFile ? Theme.accentTint : changeRow.hovered ? Theme.hoverFill : "transparent"
            }

            Text {
                visible: changeRow.groupStart
                text: changeRow.modelData ? changeRow.modelData.group : ""
                x: 6; y: 8
                font.pixelSize: Theme.fontCaption
                font.weight: Font.DemiBold
                font.letterSpacing: 0.4
                color: Theme.tertiaryText
            }

            onClicked: {
                if (changeRow.modelData)
                    root.workspace.showFileDiff(changeRow.modelData.path, changeRow.modelData.staged)
            }

            contentItem: RowLayout {
                spacing: 7

                FlatCheckBox {
                    id: fileCheck
                    controlled: true
                    checked: !!(changeRow.modelData && root.checkedFiles[(changeRow.modelData.staged ? "S:" : "U:") + changeRow.modelData.path])
                    Layout.preferredWidth: 16
                    Layout.alignment: Qt.AlignVCenter
                    onToggled: {
                        if (changeRow.modelData)
                            root.toggleFileCheck(changeRow.modelData.path, changeRow.modelData.staged,
                                                 changeRow.modelData.conflict)
                    }
                }

                Rectangle {
                    Layout.preferredWidth: 18
                    Layout.preferredHeight: 18
                    Layout.alignment: Qt.AlignVCenter
                    radius: 5
                    color: Qt.rgba(changeRow.statusColor.r, changeRow.statusColor.g, changeRow.statusColor.b, 0.14)
                    Text {
                        anchors.centerIn: parent
                        text: changeRow.statusCode || "M"
                        color: changeRow.statusColor
                        font.family: Theme.monoFontFamily
                        font.pixelSize: Theme.fontCaption
                        font.weight: Font.Bold
                    }
                    HoverHandler { id: statusHover }
                    ToolTip.visible: statusHover.hovered
                    ToolTip.text: "Git 状态：" + changeRow.status
                    ToolTip.delay: 500
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    spacing: 6
                    Text {
                        text: changeRow.fileName
                        color: Theme.text
                        font.pixelSize: Theme.fontBody
                        font.weight: changeRow.selectedFile ? Font.DemiBold : Font.Normal
                        elide: Text.ElideMiddle
                        Layout.preferredWidth: implicitWidth
                        Layout.minimumWidth: Math.min(implicitWidth, 60)
                    }
                    Text {
                        visible: changeRow.fileDir.length > 0
                        text: changeRow.fileDir
                        color: Theme.tertiaryText
                        font.pixelSize: Theme.fontCaption
                        elide: Text.ElideLeft
                        Layout.fillWidth: true
                        Layout.preferredWidth: 0
                        Layout.minimumWidth: 0
                    }
                    Item { Layout.fillWidth: true; visible: changeRow.fileDir.length === 0 }
                }

                FileActionButton {
                    visible: changeRow.showActions
                    iconName: "arrow-up-right"
                    toolTip: changeRow.modelData && changeRow.modelData.conflict
                        ? "打开冲突文件进行编辑" : "用默认应用打开文件"
                    enabled: !!changeRow.modelData
                    onClicked: {
                        if (changeRow.modelData)
                            root.workspace.openFile(changeRow.modelData.path)
                    }
                }

                FileActionButton {
                    iconName: "undo"
                    toolTip: "丢弃未暂存改动"
                    destructive: true
                    visible: changeRow.showActions && !!changeRow.modelData && !changeRow.modelData.staged && !changeRow.modelData.conflict
                    enabled: !root.workspace.busy && !!changeRow.modelData
                    onClicked: {
                        if (changeRow.modelData)
                            root.discardRequested([changeRow.modelData.path])
                    }
                }

                FileActionButton {
                    // Conflict resolution stays visible: it is the next required step.
                    visible: changeRow.showActions || (!!changeRow.modelData && changeRow.modelData.conflict)
                    iconName: changeRow.modelData ? (changeRow.modelData.conflict ? "check" : changeRow.modelData.staged ? "minus" : "plus") : ""
                    toolTip: changeRow.modelData ? (changeRow.modelData.conflict ? "标记已解决" : changeRow.modelData.staged ? "取消暂存" : "暂存文件") : ""
                    enabled: !root.workspace.busy && !!changeRow.modelData
                    onClicked: {
                        if (!changeRow.modelData) return
                        if (changeRow.modelData.conflict) {
                            root.workspace.markResolved([changeRow.modelData.path])
                        } else if (changeRow.modelData.staged) {
                            root.workspace.unstageFiles([changeRow.modelData.path])
                        } else {
                            root.workspace.stageFiles([changeRow.modelData.path])
                        }
                    }
                }
            }
        }

        Column {
            anchors.centerIn: parent
            visible: changesList.count === 0
            spacing: 8
            readonly property bool hasRepository: root.workspace && root.workspace.selectedPath.length > 0
            Icon { anchors.horizontalCenter: parent.horizontalCenter; name: parent.hasRepository ? "check-circle" : "folder"; size: 30; strokeWidth: 1.4; color: Theme.quaternaryText }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: parent.hasRepository ? "工作区很干净" : "未选择仓库"
                color: Theme.tertiaryText
                font.pixelSize: Theme.fontSecondary
            }
        }
    }
}
