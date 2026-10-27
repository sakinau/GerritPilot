pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GerritPilot
import "../../components"

// Pushes the checked repositories to their own Gerrit target branches with shared options.
AppDialog {
    id: root
    objectName: "batchPushDialog"

    property var workspace
    property string topic: ""
    property string reviewers: ""
    property string cc: ""
    property string hashtags: ""
    property int workState: 0 // 0 keep, 1 WIP, 2 ready
    property bool privateChange: false
    property string patchsetMessage: ""
    property bool showAdvanced: false

    readonly property var rows: workspace ? workspace.batchPushRows : []
    readonly property bool running: workspace ? workspace.batchPushRunning : false
    readonly property bool finished: rows.some(row => row.state === "success" || row.state === "failed"
                                                     || row.state === "canceled")
    readonly property int readyCount: rows.filter(row => row.included && row.state === "ready").length
    readonly property var reviewUrls: rows.filter(row => row.url.length > 0).map(row => row.name + "  " + row.url)
    readonly property var pushOptions: ({
        topic: topic.trim(),
        reviewers: reviewers,
        cc: cc,
        hashtags: hashtags,
        workState: workState,
        private: privateChange,
        message: patchsetMessage
    })
    readonly property string optionsError: workspace ? workspace.pushOptionsError(pushOptions) : ""
    readonly property bool canPush: workspace && !workspace.busy && !running && !finished
        && readyCount > 0 && optionsError.length === 0

    preferredWidth: 760
    title: "批量推送 Gerrit 评审"
    standardButtons: Dialog.Close

    function stateLabel(row) {
        switch (row.state) {
        case "checking": return "检查中"
        case "ready": return "待推送 " + row.commits
        case "nothing": return "无需推送"
        case "error": return "需处理"
        case "queued": return "排队中"
        case "pushing": return "推送中"
        case "success": return "成功"
        case "failed": return "失败"
        case "canceled": return "已取消"
        }
        return row.state
    }
    function stateTone(row) {
        switch (row.state) {
        case "ready": case "pushing": return "accent"
        case "success": return "green"
        case "error": case "failed": return "red"
        case "canceled": return "orange"
        }
        return "neutral"
    }

    onOpened: {
        topic = workspace ? (workspace.topic || "") : ""
        reviewers = workspace ? (workspace.reviewers || "") : ""
        cc = workspace ? (workspace.cc || "") : ""
        hashtags = ""
        workState = 0
        privateChange = false
        patchsetMessage = ""
        showAdvanced = false
        topicInput.text = topic
        reviewersInput.text = reviewers
        ccInput.text = cc
        hashtagsInput.text = ""
        messageInput.text = ""
        if (workspace) workspace.prepareBatchPush()
    }

    component FieldLabel: Text {
        color: Theme.tertiaryText
        font.pixelSize: Theme.fontSecondary
        Layout.alignment: Qt.AlignVCenter
    }

    contentItem: ColumnLayout {
        spacing: 10

        Text {
            Layout.fillWidth: true
            text: "已勾选 " + root.rows.length + " 个仓库。每个仓库推送到自己的目标分支；目标分支优先使用已保存的值，其次取自 Manifest。"
            color: Theme.secondaryText
            font.pixelSize: Theme.fontSecondary
            wrapMode: Text.Wrap
        }

        ListView {
            id: rowList
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 300)
            clip: true
            spacing: 6
            model: root.rows
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: RightScrollBar { }
            delegate: Rectangle {
                id: rowItem
                required property var modelData
                readonly property bool editable: !root.running && !root.finished
                width: rowList.width - 8
                height: rowLayout.implicitHeight + 16
                radius: Theme.radiusMedium
                color: Theme.surfaceGrouped
                border.color: modelData.warning && modelData.state !== "success" ? Theme.orange : Theme.separatorSoft
                opacity: modelData.included ? 1 : 0.55

                RowLayout {
                    id: rowLayout
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 10

                    FlatCheckBox {
                        Layout.alignment: Qt.AlignTop
                        controlled: true
                        checked: rowItem.modelData.included
                        enabled: rowItem.editable
                        onToggled: checked => root.workspace.setBatchPushIncluded(rowItem.modelData.path, checked)
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Text {
                                text: rowItem.modelData.name
                                color: Theme.text
                                font.pixelSize: Theme.fontBody
                                font.weight: Font.DemiBold
                                elide: Text.ElideMiddle
                                Layout.maximumWidth: 260
                            }
                            Text {
                                text: "· " + rowItem.modelData.branch
                                color: Theme.tertiaryText
                                font.pixelSize: Theme.fontCaption
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Badge {
                                text: root.stateLabel(rowItem.modelData)
                                tone: root.stateTone(rowItem.modelData)
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Text {
                                text: rowItem.modelData.remote + "  →  refs/for/"
                                color: Theme.secondaryText
                                font.family: Theme.monoFontFamily
                                font.pixelSize: Theme.fontCaption
                            }
                            AppTextField {
                                Layout.fillWidth: true
                                text: rowItem.modelData.target
                                enabled: rowItem.editable
                                placeholderText: "目标分支"
                                font.family: Theme.monoFontFamily
                                font.pixelSize: Theme.fontCaption
                                onEditingFinished: root.workspace.setBatchPushTarget(rowItem.modelData.path, text)
                            }
                        }

                        Text {
                            visible: rowItem.modelData.note.length > 0 && rowItem.modelData.state !== "checking"
                                     && rowItem.modelData.state !== "success"
                            text: rowItem.modelData.note
                            color: Theme.orangeText
                            font.pixelSize: Theme.fontCaption
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                        Text {
                            visible: rowItem.modelData.message.length > 0
                            text: rowItem.modelData.message
                            color: rowItem.modelData.state === "error" || rowItem.modelData.state === "failed"
                                ? Theme.redText : Theme.secondaryText
                            font.pixelSize: Theme.fontCaption
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                        Text {
                            visible: rowItem.modelData.url.length > 0
                            text: rowItem.modelData.url
                            color: Theme.accent
                            font.pixelSize: Theme.fontCaption
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: Qt.openUrlExternally(rowItem.modelData.url)
                            }
                        }
                    }
                }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            rowSpacing: 8
            columnSpacing: 12
            enabled: !root.running && !root.finished

            FieldLabel { text: "Topic（建议）：" }
            AppTextField {
                id: topicInput
                Layout.fillWidth: true
                font.pixelSize: Theme.fontSecondary
                placeholderText: "同一需求的 Change 会在 Gerrit 中按 Topic 关联"
                onTextChanged: root.topic = text
            }
            FieldLabel { text: "评审人：" }
            AppTextField {
                id: reviewersInput
                Layout.fillWidth: true
                font.pixelSize: Theme.fontSecondary
                placeholderText: "用户名或邮箱，多个用逗号分隔"
                onTextChanged: root.reviewers = text
            }
            FieldLabel { text: "抄送：" }
            AppTextField {
                id: ccInput
                Layout.fillWidth: true
                font.pixelSize: Theme.fontSecondary
                placeholderText: "用户名或邮箱，多个用逗号分隔"
                onTextChanged: root.cc = text
            }
        }

        PrimaryButton {
            visible: !root.running && !root.finished
            text: root.showAdvanced ? "收起更多选项" : "更多选项（WIP / 私有 / Hashtag / 补丁集说明）"
            kind: "plain"
            compact: true
            iconName: root.showAdvanced ? "chevron-up" : "chevron-down"
            onClicked: root.showAdvanced = !root.showAdvanced
        }

        GridLayout {
            visible: root.showAdvanced && !root.running && !root.finished
            Layout.fillWidth: true
            columns: 2
            rowSpacing: 8
            columnSpacing: 12

            FieldLabel { text: "评审状态：" }
            SegmentedControl {
                compact: true
                model: ["保持不变", "标记 WIP", "标记可评审"]
                currentIndex: root.workState
                onActivated: index => root.workState = index
            }
            FieldLabel { text: "可见性：" }
            FlatCheckBox {
                text: "设为私有 Change"
                controlled: true
                checked: root.privateChange
                onToggled: checked => root.privateChange = checked
            }
            FieldLabel { text: "Hashtag：" }
            AppTextField {
                id: hashtagsInput
                Layout.fillWidth: true
                font.pixelSize: Theme.fontSecondary
                placeholderText: "多个用逗号分隔"
                onTextChanged: root.hashtags = text
            }
            FieldLabel { text: "补丁集说明：" }
            AppTextField {
                id: messageInput
                Layout.fillWidth: true
                font.pixelSize: Theme.fontSecondary
                placeholderText: "显示在每个 Change 的评审记录中"
                onTextChanged: root.patchsetMessage = text
            }
        }

        Text {
            visible: root.optionsError.length > 0
            text: root.optionsError
            color: Theme.red
            font.pixelSize: Theme.fontSecondary
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Text {
                Layout.fillWidth: true
                text: root.running ? "正在逐个仓库推送；单个仓库失败不影响其他仓库。"
                    : root.finished ? "推送已结束。失败的仓库可修正后单独推送；成功的仓库不要重复推送。"
                    : "将推送 " + root.readyCount + " 个仓库；无需推送或需处理的仓库会被跳过。"
                color: Theme.secondaryText
                font.pixelSize: Theme.fontSecondary
                wrapMode: Text.Wrap
            }
            PrimaryButton {
                visible: root.finished && root.reviewUrls.length > 0
                text: "复制全部审查链接"
                kind: "gray"
                iconName: "copy"
                onClicked: root.workspace.copyToClipboard(root.reviewUrls.join("\n"))
            }
            PrimaryButton {
                visible: !root.finished
                text: root.running ? "推送中…" : "推送 " + root.readyCount + " 个仓库"
                iconName: "upload"
                enabled: root.canPush
                onClicked: root.workspace.startBatchPush(root.pushOptions)
            }
        }
    }
}
