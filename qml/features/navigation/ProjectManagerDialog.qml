pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import GerritPilot
import "../../components"

AppDialog {
    id: root
    property var workspace
    property var repoManager
    property int currentTab: 0
    readonly property bool idle: workspace && repoManager && !workspace.busy && !repoManager.busy
    preferredWidth: 700
    title: "项目管理"
    standardButtons: Dialog.Close
    FolderDialog {
        id: picker
        property bool importing: true
        title: importing ? "选择已有项目根目录" : "选择新项目空目录"
        onAccepted: {
            const path = decodeURIComponent(selectedFolder.toString().replace(/^file:\/\//, ""))
            if (importing) importPath.text = path
            else createPath.text = path
        }
    }
    Connections { target: root.repoManager; function onLogMessage() { logToggle.checked = true } }
    contentItem: ColumnLayout {
        implicitHeight: 560
        spacing: 12
        SegmentedControl {
            Layout.alignment: Qt.AlignHCenter
            model: ["添加已有项目", "初始化新项目", "已保存项目"]
            currentIndex: root.currentTab
            onActivated: index => root.currentTab = index
        }
        ScrollView {
            id: form
            Layout.fillWidth: true; Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            ScrollBar.vertical: RightScrollBar { }
            ColumnLayout {
                width: form.availableWidth
                spacing: 12
                ColumnLayout {
                    visible: root.currentTab === 0
                    Layout.fillWidth: true
                    Text { text: "选择包含 .repo 或 .git 的项目根目录；添加不会联网。"; color: Theme.secondaryText; font.pixelSize: Theme.fontSecondary; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    RowLayout {
                        Layout.fillWidth: true
                        AppTextField { id: importPath; Layout.fillWidth: true; placeholderText: "项目绝对路径"; font.pixelSize: Theme.fontBody }
                        PrimaryButton { text: "选择目录"; kind: "gray"; iconName: "folder"; enabled: root.idle; onClicked: { picker.importing = true; picker.open() } }
                    }
                    PrimaryButton { text: "添加项目"; iconName: "plus"; enabled: root.idle && importPath.text.trim().length > 0; onClicked: root.repoManager.importLocal(importPath.text.trim()) }
                }
                ColumnLayout {
                    visible: root.currentTab === 1
                    Layout.fillWidth: true
                    spacing: 8
                    Text { text: "仅执行 repo init，不自动修改 Manifest 或同步业务代码。"; color: Theme.secondaryText; font.pixelSize: Theme.fontSecondary; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    Label { text: "新项目目录"; font.pixelSize: Theme.fontSecondary; color: Theme.text }
                    RowLayout {
                        Layout.fillWidth: true
                        AppTextField { id: createPath; Layout.fillWidth: true; placeholderText: "新目录或空目录的绝对路径"; font.pixelSize: Theme.fontBody }
                        PrimaryButton { text: "选择目录"; kind: "gray"; iconName: "folder"; enabled: root.idle; onClicked: { picker.importing = false; picker.open() } }
                    }
                    Label { text: "Manifest 仓库地址"; font.pixelSize: Theme.fontSecondary; color: Theme.text }
                    AppTextField { id: manifestUrl; Layout.fillWidth: true; placeholderText: "ssh://服务器/清单仓库"; text: root.repoManager ? root.repoManager.defaultManifestUrl : ""; font.pixelSize: Theme.fontBody }
                    Label { text: "Manifest 分支"; font.pixelSize: Theme.fontSecondary; color: Theme.text }
                    AppTextField { id: manifestBranch; Layout.fillWidth: true; placeholderText: "清单仓库的目标分支"; font.pixelSize: Theme.fontBody }
                    Label { text: "清单文件"; font.pixelSize: Theme.fontSecondary; color: Theme.text }
                    AppTextField { id: manifestFile; Layout.fillWidth: true; text: "default.xml"; placeholderText: "清单仓库内的相对 XML 路径"; font.pixelSize: Theme.fontBody }
                    Label { text: "Repo 工具仓库地址（可选）"; font.pixelSize: Theme.fontSecondary; color: Theme.text }
                    AppTextField { id: repoUrl; Layout.fillWidth: true; text: root.repoManager ? root.repoManager.defaultRepoUrl : ""; placeholderText: "对应 --repo-url；留空使用 repo 默认地址"; font.pixelSize: Theme.fontBody }
                    PrimaryButton {
                        text: "初始化项目"
                        enabled: root.idle && createPath.text.trim().length > 0 && manifestUrl.text.trim().length > 0 && manifestBranch.text.trim().length > 0 && manifestFile.text.trim().length > 0
                        onClicked: root.repoManager.initialize(createPath.text.trim(), manifestUrl.text.trim(), manifestBranch.text.trim(), manifestFile.text.trim(), repoUrl.text.trim())
                    }
                    Text { text: "初始化后可手动编辑 .repo/manifests 中的清单；在“拉取代码”中选择 Repo 同步整个项目。"; color: Theme.tertiaryText; font.pixelSize: Theme.fontCaption; wrapMode: Text.Wrap; Layout.fillWidth: true }
                }
                ColumnLayout {
                    visible: root.currentTab === 2
                    Layout.fillWidth: true
                    PrimaryButton { text: "打开当前项目 Manifest 目录"; kind: "gray"; visible: root.workspace && root.workspace.isRepoWorkspace; enabled: root.idle; onClicked: root.repoManager.openManifest() }
                    Repeater {
                        model: root.workspace && root.workspace.projectManager ? root.workspace.projectManager.projectList : []
                        delegate: Rectangle {
                            id: projectRow
                            required property var modelData
                            required property int index
                            readonly property bool current: root.workspace && root.workspace.projectManager
                                && root.workspace.projectManager.currentIndex === index
                            Layout.fillWidth: true
                            implicitHeight: 54
                            radius: Theme.radiusMedium
                            color: current ? Theme.accentTint : Theme.surfaceMuted
                            RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 8
                            spacing: 10
                            Rectangle {
                                Layout.preferredWidth: 32
                                Layout.preferredHeight: 32
                                radius: 8
                                color: Theme.avatarColor(projectRow.modelData.name)
                                Icon { anchors.centerIn: parent; name: "folder-git"; size: 17; strokeWidth: 1.8; color: Theme.textOnAccent }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                Text { text: projectRow.modelData.name; color: Theme.text; font.pixelSize: Theme.fontBody; font.weight: Font.DemiBold }
                                Text { text: projectRow.modelData.path; color: Theme.secondaryText; font.pixelSize: Theme.fontCaption; elide: Text.ElideMiddle; Layout.fillWidth: true }
                            }
                            Badge { visible: projectRow.current; text: "当前"; tone: "accent" }
                            PrimaryButton { text: "打开"; kind: "gray"; compact: true; enabled: root.idle && !projectRow.current; onClicked: root.workspace.projectManager.switchProject(projectRow.index) }
                            IconButton { iconName: "trash"; destructive: true; toolTip: "从列表移除，不删除文件"; enabled: root.idle; onClicked: root.workspace.projectManager.removeProject(projectRow.index) }
                            }
                        }
                    }
                    Text { visible: root.workspace && root.workspace.projectManager && root.workspace.projectManager.projectList.length === 0; text: "尚未添加项目"; color: Theme.secondaryText; font.pixelSize: Theme.fontSecondary }
                }
            }
        }
        RowLayout {
            visible: root.repoManager && root.repoManager.busy
            Layout.fillWidth: true
            Spinner { running: parent.visible; Layout.preferredWidth: 24; Layout.preferredHeight: 24 }
            Text { text: root.repoManager ? root.repoManager.activeTask : ""; color: Theme.text; font.pixelSize: Theme.fontSecondary; Layout.fillWidth: true; elide: Text.ElideRight }
            PrimaryButton { text: "取消操作"; kind: "gray"; onClicked: root.repoManager.cancel() }
        }
        FlatCheckBox { id: logToggle; text: "执行日志"; visible: root.repoManager && root.repoManager.log.length > 0 }
        ScrollView {
            visible: logToggle.checked && logToggle.visible
            Layout.fillWidth: true; Layout.preferredHeight: 140
            clip: true
            ScrollBar.vertical: RightScrollBar { }
            TextArea { text: root.repoManager ? root.repoManager.log : ""; readOnly: true; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere; font.family: Theme.monoFontFamily; font.pixelSize: Theme.fontCaption; color: Theme.text; onTextChanged: cursorPosition = length }
        }
    }
}
