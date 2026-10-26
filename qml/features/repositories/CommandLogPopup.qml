import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GerritPilot
import "../../components"

// Popover with live process output.
Popup {
    id: root
    property var workspace
    parent: Overlay.overlay
    width: 640
    height: 300
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: FloatingSurface { radius: Theme.radiusLarge }
    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.animationFast }
    }

    contentItem: ColumnLayout {
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 8
            Layout.topMargin: 10
            Layout.bottomMargin: 10
            spacing: 8
            Icon { name: "terminal"; size: 15; color: Theme.secondaryText }
            Text {
                text: "命令日志"
                color: Theme.text
                font.pixelSize: Theme.fontBody
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
            FlatCheckBox { id: followLog; text: "跟随输出"; checked: true }
            PrimaryButton {
                text: "取消任务"
                kind: "dangerTinted"
                compact: true
                enabled: root.workspace && root.workspace.busy
                onClicked: root.workspace.stop()
            }
            IconButton { iconName: "x"; toolTip: "关闭"; implicitWidth: 26; implicitHeight: 26; onClicked: root.close() }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.separatorSoft }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 10
            clip: true
            ScrollBar.vertical: RightScrollBar { }
            TextArea {
                id: commandLogText
                objectName: "commandLogText"
                textFormat: TextEdit.PlainText
                selectByMouse: true
                readOnly: true
                onTextChanged: {
                    if (followLog.checked) Qt.callLater(function() {
                        commandLogText.cursorPosition = commandLogText.length
                    })
                }
                text: root.opened && root.workspace
                    ? (root.workspace.consoleText.length ? root.workspace.consoleText : "命令输出会显示在这里")
                    : ""
                color: Theme.secondaryText
                font.family: Theme.monoFontFamily
                font.pixelSize: Theme.fontCaption
                wrapMode: TextEdit.WrapAnywhere
                padding: 10
                background: Rectangle { color: Theme.surfaceGrouped; radius: Theme.radiusMedium }
            }
        }
    }
}
