import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GerritPilot
import "../../components"

// Footer status: ready state or the running task with a stop action.
Rectangle {
    id: root
    property var workspace
    readonly property bool busy: workspace ? workspace.busy : false

    implicitHeight: content.implicitHeight + 20
    radius: Theme.radiusMedium
    color: Theme.sidebarCard
    border.width: 1
    border.color: Theme.separatorSoft

    RowLayout {
        id: content
        x: 10
        y: 10
        width: parent.width - 20
        spacing: 10

        Item {
            Layout.preferredWidth: 18
            Layout.preferredHeight: 18
            Layout.alignment: Qt.AlignVCenter
            Spinner {
                anchors.fill: parent
                running: root.busy
                color: Theme.accent
            }
            Rectangle {
                visible: !root.busy
                anchors.centerIn: parent
                width: 10
                height: 10
                radius: 5
                color: Theme.green
                Rectangle {
                    anchors.centerIn: parent
                    width: 18
                    height: 18
                    radius: 9
                    color: "transparent"
                    border.width: 3
                    border.color: "#3334C759"
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1
            Text {
                text: root.busy
                    ? (root.workspace.activeTask.length ? root.workspace.activeTask : "正在执行命令…")
                    : "工作区已就绪"
                color: Theme.text
                font.pixelSize: Theme.fontSecondary
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Text {
                text: root.busy ? "可随时停止当前任务" : ((root.workspace ? root.workspace.repositoryCount : 0) + " 个仓库已载入")
                color: Theme.tertiaryText
                font.pixelSize: Theme.fontCaption
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }

        PrimaryButton {
            visible: root.busy
            kind: "dangerTinted"
            compact: true
            iconName: "stop"
            text: "停止"
            Layout.alignment: Qt.AlignVCenter
            onClicked: root.workspace.stop()
        }
    }
}
