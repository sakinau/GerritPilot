import QtQuick
import QtQuick.Controls.Basic
import GerritPilot
import "pages"

ApplicationWindow {
    id: applicationWindow
    property bool hasBeenActive: Application.state === Qt.ApplicationActive
    property bool wasBackgrounded: false
    width: 1440
    height: 900
    minimumWidth: 1120
    minimumHeight: 720
    visible: true
    title: {
        const api = workspacePage ? workspacePage.workspaceApi : null
        const project = api && api.projectManager ? api.projectManager.currentProjectName : ""
        const repository = api ? api.selectedName : ""
        if (project.length && repository.length)
            return project + " · " + repository
        return project || repository || "未选择项目"
    }
    color: Theme.canvas
    font.pixelSize: Theme.fontBody

    onActiveChanged: {
        if (active) {
            if (applicationWindow.hasBeenActive && applicationWindow.wasBackgrounded) {
                if (workspacePage && workspacePage.workspaceApi)
                    workspacePage.workspaceApi.requestActiveRefresh()
            }
            applicationWindow.hasBeenActive = true
            applicationWindow.wasBackgrounded = false
        } else if (applicationWindow.hasBeenActive) {
            applicationWindow.wasBackgrounded = true
        }
    }

    Connections {
        target: Application
        function onStateChanged() {
            if (Application.state === Qt.ApplicationActive) {
                if (applicationWindow.hasBeenActive && applicationWindow.wasBackgrounded)
                    workspacePage.workspaceApi.requestActiveRefresh()
                applicationWindow.hasBeenActive = true
                applicationWindow.wasBackgrounded = false
            } else if (applicationWindow.hasBeenActive) {
                applicationWindow.wasBackgrounded = true
            }
        }
    }

    WorkspacePage {
        id: workspacePage
        anchors.fill: parent
    }
}
