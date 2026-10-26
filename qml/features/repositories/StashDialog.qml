pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GerritPilot
import "../../components"

AppDialog {
    id: root

    property var workspace
    preferredWidth: 640
    title: "贮藏管理"
    standardButtons: Dialog.Close

    contentItem: ColumnLayout {
        spacing: 12
        implicitHeight: 340

        Text {
            text: "保存当前工作区改动"
            font.pixelSize: Theme.fontSecondary
            font.weight: Font.DemiBold
            color: Theme.text
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            AppTextField {
                id: stashMsgInput
                Layout.fillWidth: true
                placeholderText: "输入说明（可选）"
            }

            FlatCheckBox {
                id: untrackedCheck
                text: "包含未跟踪文件"
            }

            PrimaryButton {
                text: "保存贮藏"
                iconName: "archive"
                enabled: root.workspace && !root.workspace.busy
                onClicked: {
                    root.workspace.stashSave(stashMsgInput.text, untrackedCheck.checked)
                    stashMsgInput.text = ""
                }
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.separatorSoft }

        Text {
            text: "已保存的贮藏（" + ((root.workspace && root.workspace.stashList) ? root.workspace.stashList.length : 0) + "）"
            font.pixelSize: Theme.fontSecondary
            font.weight: Font.DemiBold
            color: Theme.text
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ScrollBar.vertical: RightScrollBar { }

            ListView {
                id: stashListView
                width: parent.width
                spacing: 6
                model: (root.workspace && root.workspace.stashList) ? root.workspace.stashList : []

                delegate: Rectangle {
                    id: stashItemRow
                    required property int index
                    required property string modelData
                    width: stashListView.width
                    height: 44
                    radius: Theme.radiusMedium
                    color: stashRowHover.hovered ? Theme.surfaceMuted : Theme.surface
                    border.color: Theme.separatorSoft
                    border.width: 1

                    HoverHandler {
                        id: stashRowHover
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 8
                        spacing: 8

                        Icon { name: "archive"; size: 14; color: Theme.tertiaryText }
                        Text {
                            text: stashItemRow.modelData
                            color: Theme.text
                            font.pixelSize: Theme.fontSecondary
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                        }

                        PrimaryButton {
                            text: "应用"
                            kind: "gray"
                            compact: true
                            enabled: root.workspace && !root.workspace.busy
                            onClicked: root.workspace.stashApply(stashItemRow.index, root.workspace.stashSha(stashItemRow.index))
                        }

                        PrimaryButton {
                            text: "弹出"
                            kind: "gray"
                            compact: true
                            enabled: root.workspace && !root.workspace.busy
                            onClicked: root.workspace.stashPop(stashItemRow.index, root.workspace.stashSha(stashItemRow.index))
                        }

                        PrimaryButton {
                            text: "删除"
                            kind: "dangerTinted"
                            compact: true
                            enabled: root.workspace && !root.workspace.busy
                            onClicked: {
                                stashDropConfirmDialog.dropIndex = stashItemRow.index
                                stashDropConfirmDialog.dropDescription = stashItemRow.modelData
                                stashDropConfirmDialog.dropSha = (root.workspace && typeof root.workspace.stashSha === "function")
                                    ? root.workspace.stashSha(stashItemRow.index) : ""
                                stashDropConfirmDialog.open()
                            }
                        }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: stashListView.count === 0
                    text: "当前仓库暂无贮藏记录"
                    color: Theme.tertiaryText
                    font.pixelSize: Theme.fontSecondary
                }
            }
        }
    }

    AppDialog {
        id: stashDropConfirmDialog
        preferredWidth: 440
        title: "删除贮藏记录"
        destructive: true
        standardButtons: Dialog.Cancel | Dialog.Ok
        property int dropIndex: -1
        property string dropDescription: ""
        property string dropSha: ""
        onAccepted: {
            if (root.workspace && dropIndex >= 0)
                root.workspace.stashDrop(dropIndex, dropSha)
        }
        contentItem: ColumnLayout {
            spacing: 8
            Text {
                text: "确定要永久删除以下贮藏记录吗？\n此操作将永久抹除贮藏的修改，无法撤销："
                color: Theme.text
                font.pixelSize: Theme.fontSecondary
                font.weight: Font.Medium
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 38
                radius: 6
                color: Theme.surfaceMuted
                border.color: Theme.separatorSoft
                Text {
                    anchors.fill: parent
                    anchors.margins: 8
                    text: stashDropConfirmDialog.dropDescription
                    color: Theme.text
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Theme.fontSecondary
                    elide: Text.ElideMiddle
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }
}
