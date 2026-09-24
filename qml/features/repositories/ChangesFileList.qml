pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
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
            root.clearCheckedFiles()
        }
    }

    spacing: 4

    // Batch Action Toolbar
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 32
        radius: 6
        color: Theme.surface
        border.color: Theme.separatorSoft
        border.width: 1

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 6

            // When files are checked
            RowLayout {
                visible: root.checkedFilesCount > 0
                Layout.fillWidth: true
                spacing: 6

                Text {
                    text: "已选 " + root.checkedFilesCount + " 项"
                    color: Theme.accent
                    font.pixelSize: Theme.fontCaption
                    font.weight: Font.DemiBold
                }

                Item { Layout.fillWidth: true }

                Button {
                    id: stageCheckedBtn
                    text: "暂存已选"
                    implicitHeight: 26
                    implicitWidth: stageCheckedText.implicitWidth + 14
                    padding: 0
                    topInset: 0
                    bottomInset: 0
                    leftInset: 0
                    rightInset: 0
                    hoverEnabled: true
                    visible: root.getCheckedUnstaged().length > 0
                    enabled: root.workspace && !root.workspace.busy
                    background: Rectangle {
                        radius: 6
                        color: stageCheckedBtn.down ? "#DCEBFA" : stageCheckedBtn.hovered ? Theme.accentSoft : "transparent"
                    }
                    contentItem: Text {
                        id: stageCheckedText
                        text: stageCheckedBtn.text
                        color: Theme.accent
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
                    onClicked: {
                        root.workspace.stageFiles(root.getCheckedUnstaged())
                        root.clearCheckedFiles()
                    }
                }

                Button {
                    id: unstageCheckedBtn
                    text: "取消暂存"
                    implicitHeight: 26
                    implicitWidth: unstageCheckedText.implicitWidth + 14
                    padding: 0
                    topInset: 0
                    bottomInset: 0
                    leftInset: 0
                    rightInset: 0
                    hoverEnabled: true
                    visible: root.getCheckedStaged().length > 0
                    enabled: root.workspace && !root.workspace.busy
                    background: Rectangle {
                        radius: 6
                        color: unstageCheckedBtn.down ? "#DCEBFA" : unstageCheckedBtn.hovered ? Theme.accentSoft : "transparent"
                    }
                    contentItem: Text {
                        id: unstageCheckedText
                        text: unstageCheckedBtn.text
                        color: Theme.accent
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
                    onClicked: {
                        root.workspace.unstageFiles(root.getCheckedStaged())
                        root.clearCheckedFiles()
                    }
                }

                Button {
                    id: discardCheckedBtn
                    text: "丢弃已选"
                    implicitHeight: 26
                    implicitWidth: discardCheckedText.implicitWidth + 14
                    padding: 0
                    topInset: 0
                    bottomInset: 0
                    leftInset: 0
                    rightInset: 0
                    hoverEnabled: true
                    visible: root.getCheckedUnstaged().length > 0
                    enabled: root.workspace && !root.workspace.busy
                    background: Rectangle {
                        radius: 6
                        color: discardCheckedBtn.down ? "#FDD" : discardCheckedBtn.hovered ? Theme.redSoft : "transparent"
                    }
                    contentItem: Text {
                        id: discardCheckedText
                        text: discardCheckedBtn.text
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
                    onClicked: {
                        root.discardRequested(root.getCheckedUnstaged())
                        root.clearCheckedFiles()
                    }
                }

                IconButton {
                    glyph: "✕"
                    toolTip: "取消多选"
                    Layout.preferredWidth: 22
                    Layout.preferredHeight: 22
                    onClicked: root.clearCheckedFiles()
                }
            }

            // When no files are checked
            RowLayout {
                visible: root.checkedFilesCount === 0
                Layout.fillWidth: true
                spacing: 4

                Text {
                    text: "改动文件"
                    color: Theme.secondaryText
                    font.pixelSize: Theme.fontCaption
                    font.weight: Font.DemiBold
                }

                Item { Layout.fillWidth: true }

                Button {
                    id: stageAllBtn
                    text: "＋ 暂存全部"
                    implicitHeight: 26
                    implicitWidth: stageAllText.implicitWidth + 14
                    padding: 0
                    topInset: 0
                    bottomInset: 0
                    leftInset: 0
                    rightInset: 0
                    hoverEnabled: true
                    visible: root.getAllUnstaged().length > 0
                    enabled: root.workspace && !root.workspace.busy
                    background: Rectangle {
                        radius: 6
                        color: stageAllBtn.down ? "#DCEBFA" : stageAllBtn.hovered ? Theme.accentSoft : "transparent"
                    }
                    contentItem: Text {
                        id: stageAllText
                        text: stageAllBtn.text
                        color: Theme.accent
                        font.pixelSize: Theme.fontCaption
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        acceptedButtons: Qt.NoButton
                    }
                    onClicked: root.workspace.stageFiles(root.getAllUnstaged())
                }

                Button {
                    id: unstageAllBtn
                    text: "− 取消全部"
                    implicitHeight: 26
                    implicitWidth: unstageAllText.implicitWidth + 14
                    padding: 0
                    topInset: 0
                    bottomInset: 0
                    leftInset: 0
                    rightInset: 0
                    hoverEnabled: true
                    visible: root.getAllStaged().length > 0
                    enabled: root.workspace && !root.workspace.busy
                    background: Rectangle {
                        radius: 6
                        color: unstageAllBtn.down ? "#E4E5EB" : unstageAllBtn.hovered ? Theme.surfaceMuted : "transparent"
                    }
                    contentItem: Text {
                        id: unstageAllText
                        text: unstageAllBtn.text
                        color: Theme.secondaryText
                        font.pixelSize: Theme.fontCaption
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        acceptedButtons: Qt.NoButton
                    }
                    onClicked: root.workspace.unstageFiles(root.getAllStaged())
                }

                Button {
                    id: discardAllBtn
                    text: "↶ 丢弃全部"
                    implicitHeight: 26
                    implicitWidth: discardAllText.implicitWidth + 14
                    padding: 0
                    topInset: 0
                    bottomInset: 0
                    leftInset: 0
                    rightInset: 0
                    hoverEnabled: true
                    visible: root.getAllUnstaged().length > 0
                    enabled: root.workspace && !root.workspace.busy
                    background: Rectangle {
                        radius: 6
                        color: discardAllBtn.down ? "#FDD" : discardAllBtn.hovered ? Theme.redSoft : "transparent"
                    }
                    contentItem: Text {
                        id: discardAllText
                        text: discardAllBtn.text
                        color: Theme.red
                        font.pixelSize: Theme.fontCaption
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        acceptedButtons: Qt.NoButton
                    }
                    onClicked: root.discardRequested(root.getAllUnstaged())
                }
            }
        }
    }

    ListView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        ScrollBar.vertical: RightScrollBar { }
        model: root.displayedChanges
        clip: true

        delegate: ItemDelegate {
            id: changeRow
            required property int index
            required property var modelData
            readonly property bool groupStart: index === 0 || !!(root.displayedChanges[index - 1] && changeRow.modelData && root.displayedChanges[index - 1].group !== changeRow.modelData.group)
            width: ListView.view ? ListView.view.width : (parent ? parent.width : 0)
            height: groupStart ? 66 : 36
            topPadding: groupStart ? 30 : 0
            bottomPadding: 0
            topInset: 0
            bottomInset: 0
            readonly property bool selectedFile: !!changeRow.modelData
                && root.workspace.selectedFile === changeRow.modelData.path
                && root.workspace.selectedFileStaged === changeRow.modelData.staged
            background: Rectangle {
                y: changeRow.groupStart ? 30 : 0
                height: 36
                radius: 6
                color: changeRow.selectedFile ? Theme.accentSoft : changeRow.hovered ? "#F8F9FA" : "transparent"
                border.color: changeRow.selectedFile ? Theme.accent : changeRow.hovered ? Theme.separatorSoft : "transparent"
                border.width: 1
            }

            Text {
                visible: changeRow.groupStart
                text: changeRow.modelData ? changeRow.modelData.group : ""
                x: 8; y: 5
                font.pixelSize: Theme.fontSecondary
                font.bold: true
                color: Theme.secondaryText
            }

            onClicked: {
                if (changeRow.modelData)
                    root.workspace.showFileDiff(changeRow.modelData.path, changeRow.modelData.staged)
            }

            contentItem: RowLayout {
                spacing: 4

                FlatCheckBox {
                    id: fileCheck
                    checked: !!(changeRow.modelData && root.checkedFiles[(changeRow.modelData.staged ? "S:" : "U:") + changeRow.modelData.path])
                    Layout.preferredWidth: 18
                    Layout.preferredHeight: 18
                    Layout.alignment: Qt.AlignVCenter
                    onToggled: {
                        if (changeRow.modelData)
                            root.toggleFileCheck(changeRow.modelData.path, changeRow.modelData.staged,
                                                 changeRow.modelData.conflict)
                    }
                }

                Text {
                    text: changeRow.modelData ? changeRow.modelData.status : ""
                    color: (changeRow.modelData && changeRow.modelData.status && changeRow.modelData.status[0] !== " ") ? Theme.green : Theme.orange
                    font.family: "DejaVu Sans Mono"
                    font.pixelSize: Theme.fontCaption
                    font.weight: Font.DemiBold
                }

                Text {
                    text: changeRow.modelData ? changeRow.modelData.path : ""
                    color: Theme.text
                    font.pixelSize: Theme.fontBody
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }

                FileActionButton {
                    text: "↗"
                    toolTip: changeRow.modelData && changeRow.modelData.conflict
                        ? "打开冲突文件进行编辑" : "用默认应用打开文件"
                    enabled: !!changeRow.modelData
                    onClicked: {
                        if (changeRow.modelData)
                            root.workspace.openFile(changeRow.modelData.path)
                    }
                }

                FileActionButton {
                    text: "↶"
                    toolTip: "丢弃未暂存改动"
                    destructive: true
                    visible: !!changeRow.modelData && !changeRow.modelData.staged && !changeRow.modelData.conflict
                    enabled: !root.workspace.busy && !!changeRow.modelData
                    onClicked: {
                        if (changeRow.modelData)
                            root.discardRequested([changeRow.modelData.path])
                    }
                }

                FileActionButton {
                    text: changeRow.modelData ? (changeRow.modelData.conflict ? "✓" : changeRow.modelData.staged ? "−" : "+") : ""
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
    }
}
