import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GerritPilot
import "../../components"

// Current project card; click to switch, gear to manage.
Rectangle {
    id: root
    property var workspace
    signal manageRequested(int tab)

    readonly property var manager: workspace ? workspace.projectManager : null
    readonly property string projectName: manager && manager.currentProjectName ? manager.currentProjectName : "未选择项目"
    readonly property string projectPath: manager && manager.currentProjectPath
        ? manager.currentProjectPath : (workspace ? workspace.workspacePath : "")

    implicitHeight: 52
    radius: Theme.radiusMedium
    color: cardMouse.containsMouse || projectMenu.opened ? Theme.surface : Theme.sidebarCard
    border.width: 1
    border.color: projectMenu.opened ? Theme.focusRing : Theme.separatorSoft
    Behavior on color { ColorAnimation { duration: Theme.animationFast } }

    MouseArea {
        id: cardMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: projectMenu.opened ? projectMenu.close() : projectMenu.popup(0, root.height + 4)
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 6
        spacing: 10

        Rectangle {
            Layout.preferredWidth: 32
            Layout.preferredHeight: 32
            Layout.alignment: Qt.AlignVCenter
            radius: 8
            gradient: Gradient {
                GradientStop { position: 0; color: Qt.lighter(Theme.avatarColor(root.projectName), 1.25) }
                GradientStop { position: 1; color: Theme.avatarColor(root.projectName) }
            }
            Icon {
                anchors.centerIn: parent
                name: "folder-git"
                size: 17
                strokeWidth: 1.8
                color: Theme.textOnAccent
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: 1
            Text {
                text: root.projectName
                color: Theme.text
                font.pixelSize: Theme.fontBody
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Text {
                text: root.projectPath
                color: Theme.tertiaryText
                font.pixelSize: Theme.fontCaption
                elide: Text.ElideMiddle
                Layout.fillWidth: true
            }
        }

        Icon {
            name: "chevrons-up-down"
            size: 14
            color: Theme.tertiaryText
            Layout.alignment: Qt.AlignVCenter
        }

        IconButton {
            iconName: "settings"
            toolTip: "管理项目列表"
            implicitWidth: 26
            implicitHeight: 26
            Layout.alignment: Qt.AlignVCenter
            onClicked: root.manageRequested(2)
        }
    }

    AppMenu {
        id: projectMenu
        width: Math.max(root.width, 240)

        Instantiator {
            model: root.manager ? root.manager.projectList : []
            onObjectAdded: (index, obj) => projectMenu.insertItem(index, obj)
            onObjectRemoved: (index, obj) => projectMenu.removeItem(obj)

            delegate: AppMenuItem {
                required property int index
                required property var modelData
                text: modelData ? modelData.name : ("项目 " + (index + 1))
                iconName: "folder"
                checkable: true
                checked: root.manager ? root.manager.currentIndex === index : false
                enabled: !root.workspace || root.workspace.pushState !== 1
                onTriggered: {
                    if (root.manager) root.manager.switchProject(index)
                }
            }
        }

        MenuSeparator {
            visible: root.manager && root.manager.projectList ? root.manager.projectList.length > 0 : false
            contentItem: Rectangle { implicitHeight: 1; color: Theme.separatorSoft }
        }

        AppMenuItem {
            text: "添加项目…"
            iconName: "plus"
            onTriggered: root.manageRequested(0)
        }

        AppMenuItem {
            text: "管理项目列表…"
            iconName: "list"
            onTriggered: root.manageRequested(2)
        }
    }
}
