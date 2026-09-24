import QtQuick.Dialogs

FolderDialog {
    id: root

    property var workspace

    title: "选择 repo 或 Git 工作区"
    onAccepted: {
        root.workspace.workspacePath = decodeURIComponent(selectedFolder.toString().replace("file://", ""))
        root.workspace.scanWorkspace()
    }
}
