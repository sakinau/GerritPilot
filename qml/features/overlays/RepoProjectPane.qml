import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GerritPilot
import "../../components"

ColumnLayout {
    id: root
    required property var manager
    required property var workspace
    property bool active: true
    readonly property bool idle: manager && !manager.busy && !workspace.busy
    property bool showProjectLog: true
    property string selectedPlatform: "T1EJFL"
    property bool showXmlEditor: false
    property string rawXmlContent: ""

    readonly property var platformList: [
        { key: "T1EJFL", label: "T1EJFL", defaultHmi: "TDA4_T1EJFL", tip: "T1EJFL (mv_hmi: TDA4_T1EJFL / Chery Public)" },
        { key: "T13T_BEV", label: "T13T BEV", defaultHmi: "TDA4_T13T_BEV", tip: "T13T BEV (mv_hmi: TDA4_T13T_BEV / Chery Public)" },
        { key: "T1TP", label: "T1TP", defaultHmi: "TDA4_T1TP_FX", tip: "T1TP (mv_hmi: TDA4_T1TP_FX / Chery Public)" },
        { key: "T13C_BEV", label: "T13C BEV", defaultHmi: "TDA4_T13C_BEV", tip: "T13C BEV (mv_hmi: TDA4_T13C_BEV)" },
        { key: "T13C_HEV", label: "T13C HEV", defaultHmi: "TDA4_T1TP", tip: "T13C HEV (mv_hmi: TDA4_T1TP / T13C_HEV)" }
    ]

    function reloadXmlContent() {
        if (root.manager) {
            rawXmlContent = root.manager.readCustomerManifest("")
        }
    }

    readonly property string projectName: (workspace && workspace.projectManager && workspace.projectManager.currentProjectName)
        ? workspace.projectManager.currentProjectName : "当前工作区"
    readonly property string projectPath: (workspace && workspace.workspacePath.length > 0)
        ? workspace.workspacePath : (manager ? manager.path : "")
    readonly property bool hasCustomerXml: manager && manager.currentCustomerXml.length > 0

    spacing: 12

    Connections {
        target: root.manager
        function onLogMessage() { root.showProjectLog = true }
        function onChanged() {
            if (root.showXmlEditor) root.reloadXmlContent()
        }
    }

    ScrollView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        contentWidth: availableWidth
        ScrollBar.vertical: RightScrollBar { }

        ColumnLayout {
            width: parent.width
            spacing: 14

            // Card 1: 当前工作区项目信息
            Rectangle {
                Layout.fillWidth: true
                radius: 10
                color: Theme.surfaceMuted
                border.color: Theme.separatorSoft
                border.width: 1
                implicitHeight: projectInfoCol.implicitHeight + 24

                ColumnLayout {
                    id: projectInfoCol
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text {
                            text: "📦"
                            font.pixelSize: Theme.fontSubheading
                        }

                        Text {
                            text: root.projectName
                            font.pixelSize: Theme.fontSubheading
                            font.weight: Font.DemiBold
                            color: Theme.text
                        }

                        Rectangle {
                            radius: 4
                            color: root.hasCustomerXml ? "#E6F4EA" : "#F1F3F4"
                            border.color: root.hasCustomerXml ? "#CEEAD6" : "#DADCE0"
                            border.width: 1
                            implicitWidth: badgeText.implicitWidth + 12
                            implicitHeight: 22

                            Text {
                                id: badgeText
                                anchors.centerIn: parent
                                text: root.hasCustomerXml ? "Repo 项目" : "本地工作区"
                                font.pixelSize: Theme.fontCaption
                                font.weight: Font.Medium
                                color: root.hasCustomerXml ? "#137333" : Theme.secondaryText
                            }
                        }

                        Item { Layout.fillWidth: true }

                        PrimaryButton {
                            text: "打开所在目录"
                            secondary: true
                            enabled: root.projectPath.length > 0
                            onClicked: Qt.openUrlExternally("file://" + root.projectPath)
                        }
                    }

                    Text {
                        text: "路径: " + (root.projectPath.length > 0 ? root.projectPath : "未设置")
                        font.pixelSize: Theme.fontSecondary
                        font.family: Theme.monoFontFamily
                        color: Theme.secondaryText
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }

                    Text {
                        text: "💡 提示：如需新建项目或拉取完整 Repo 代码仓库，请使用侧边栏底部的「项目管理与创建」。"
                        font.pixelSize: Theme.fontCaption
                        color: Theme.tertiaryText
                        Layout.fillWidth: true
                    }
                }
            }

            // Card 2: Manifest 客户链与模块索引 (customer.xml)
            Rectangle {
                Layout.fillWidth: true
                radius: 10
                color: "#F6F9FE"
                border.color: "#D0E2FB"
                border.width: 1
                implicitHeight: manifestCardCol.implicitHeight + 24

                ColumnLayout {
                    id: manifestCardCol
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Text {
                            text: "🎯 Manifest 客户链与模块索引 (customer.xml)"
                            font.pixelSize: Theme.fontSubheading
                            font.weight: Font.DemiBold
                            color: Theme.text
                            Layout.fillWidth: true
                        }
                    }

                    Text {
                        text: root.hasCustomerXml
                            ? ("清单文件: " + root.manager.currentCustomerXml)
                            : "未在当前工作区 .repo/manifests 中检测到 customer.xml 清单文件。"
                        font.pixelSize: Theme.fontCaption
                        font.family: Theme.monoFontFamily
                        color: root.hasCustomerXml ? Theme.secondaryText : Theme.orange
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }

                    // Platform Selection Chips
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Text {
                            text: "选择车型平台（自动对齐 ci_scripts / platform-customer / 标定仿真 / HMI 客户链）："
                            font.pixelSize: Theme.fontCaption
                            font.weight: Font.Medium
                            color: Theme.text
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Repeater {
                                model: root.platformList
                                delegate: Rectangle {
                                    id: platChip
                                    required property var modelData
                                    Layout.preferredHeight: 28
                                    Layout.preferredWidth: platChipLabel.implicitWidth + 20
                                    radius: 14
                                    readonly property bool isSelected: root.selectedPlatform.toLowerCase() === modelData.key.toLowerCase()
                                    color: isSelected ? Theme.accentSoft : (platMouse.containsMouse ? "#E8F0FE" : Theme.surface)
                                    border.color: isSelected ? Theme.accent : "#C2D7EF"
                                    border.width: isSelected ? 1.5 : 1

                                    Text {
                                        id: platChipLabel
                                        anchors.centerIn: parent
                                        text: platChip.modelData.label
                                        font.pixelSize: Theme.fontCaption
                                        font.weight: platChip.isSelected ? Font.DemiBold : Font.Normal
                                        color: platChip.isSelected ? Theme.accent : Theme.text
                                    }

                                    MouseArea {
                                        id: platMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            root.selectedPlatform = platChip.modelData.key
                                            editHmiBranch.text = platChip.modelData.defaultHmi
                                            if (root.showXmlEditor && root.manager) {
                                                xmlEditorArea.text = root.manager.getCustomerTemplate(platChip.modelData.key, platChip.modelData.defaultHmi)
                                            }
                                        }
                                    }
                                }
                            }

                            Item { Layout.fillWidth: true }
                        }
                    }

                    // High-level Actions Bar
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        PrimaryButton {
                            text: (root.manager && root.manager.busy) ? "⏳ 同步中…" : "🚀 一键修复客户链并同步代码 (repo sync)"
                            enabled: root.idle
                            onClicked: {
                                if (root.manager) {
                                    root.manager.repairAndSync(root.selectedPlatform, editHmiBranch.text.trim(), false)
                                }
                            }
                        }

                        PrimaryButton {
                            text: "⚡ 仅修复并同步 HMI"
                            secondary: true
                            enabled: root.idle
                            onClicked: {
                                if (root.manager) {
                                    root.manager.repairAndSync(root.selectedPlatform, editHmiBranch.text.trim(), true)
                                }
                            }
                        }

                        PrimaryButton {
                            text: "📝 仅应用平台定义到 XML"
                            secondary: true
                            enabled: root.idle
                            onClicked: {
                                if (root.manager) {
                                    root.manager.repairCustomerManifest("", root.selectedPlatform, editHmiBranch.text.trim())
                                    root.reloadXmlContent()
                                }
                            }
                        }

                        Item { Layout.fillWidth: true }
                    }

                    // HMI branch custom edit section
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        visible: root.hasCustomerXml

                        Text {
                            text: "目标 HMI 分支 (mv_hmi)："
                            font.pixelSize: Theme.fontBody
                            font.weight: Font.Medium
                            color: Theme.text
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            TextField {
                                id: editHmiBranch
                                Layout.fillWidth: true
                                font.pixelSize: Theme.fontBody
                                text: root.manager ? root.manager.currentHmiRevision : ""
                                placeholderText: "例如 TDA4_T1TP_FX 或 TDA4_T13T_BEV"
                                enabled: root.idle
                            }

                            PrimaryButton {
                                text: "仅更新 HMI 分支"
                                secondary: true
                                enabled: root.idle && editHmiBranch.text.trim().length > 0
                                onClicked: {
                                    if (root.manager) {
                                        root.manager.setHmiBranchForProject("", editHmiBranch.text.trim())
                                        root.reloadXmlContent()
                                    }
                                }
                            }
                        }
                    }

                    // Collapsible Full XML Editor / Viewer
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        IconButton {
                            glyph: root.showXmlEditor ? "⌃" : "⌄"
                            Layout.preferredWidth: 20
                            Layout.preferredHeight: 20
                            onClicked: {
                                root.showXmlEditor = !root.showXmlEditor
                                if (root.showXmlEditor) root.reloadXmlContent()
                            }
                        }

                        Text {
                            text: "查看 / 手动编辑 customer.xml 客户链源码 (7 个子模块完整结构)"
                            font.pixelSize: Theme.fontSecondary
                            font.weight: Font.Medium
                            color: Theme.accent
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    root.showXmlEditor = !root.showXmlEditor
                                    if (root.showXmlEditor) root.reloadXmlContent()
                                }
                            }
                        }

                        Item { Layout.fillWidth: true }
                    }

                    ColumnLayout {
                        visible: root.showXmlEditor
                        Layout.fillWidth: true
                        spacing: 6

                        ScrollView {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 180
                            clip: true
                            ScrollBar.vertical: RightScrollBar { }

                            TextArea {
                                id: xmlEditorArea
                                selectByMouse: true
                                wrapMode: TextEdit.NoWrap
                                font.family: Theme.monoFontFamily
                                font.pixelSize: Theme.fontCaption
                                text: root.rawXmlContent
                                color: Theme.text
                                background: Rectangle {
                                    color: Theme.surface
                                    border.color: "#C2D7EF"
                                    border.width: 1
                                    radius: 6
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            PrimaryButton {
                                text: "🔄 从磁盘重新载入"
                                secondary: true
                                enabled: root.idle
                                onClicked: root.reloadXmlContent()
                            }

                            PrimaryButton {
                                text: "📑 载入所选平台标准模板"
                                secondary: true
                                enabled: root.idle
                                onClicked: {
                                    if (root.manager) {
                                        xmlEditorArea.text = root.manager.getCustomerTemplate(root.selectedPlatform, editHmiBranch.text.trim())
                                    }
                                }
                            }

                            PrimaryButton {
                                text: "💾 保存并写入 XML"
                                enabled: root.idle && xmlEditorArea.text.trim().length > 0
                                onClicked: {
                                    if (root.manager) {
                                        root.manager.writeRawCustomerManifest("", xmlEditorArea.text)
                                        root.reloadXmlContent()
                                    }
                                }
                            }

                            Item { Layout.fillWidth: true }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: "#E2EDF9"
                        Layout.topMargin: 4
                        Layout.bottomMargin: 4
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        PrimaryButton {
                            text: "在系统编辑器中打开 customer.xml"
                            secondary: true
                            enabled: root.idle && root.hasCustomerXml
                            onClicked: if (root.manager) root.manager.openCustomerManifest("")
                        }

                        PrimaryButton {
                            text: "打开 manifest.xml"
                            secondary: true
                            enabled: root.idle
                            onClicked: if (root.manager) root.manager.openManifest()
                        }

                        Item { Layout.fillWidth: true }
                    }
                }
            }

            // Card 3: Repo 工作区操作
            Rectangle {
                Layout.fillWidth: true
                radius: 10
                color: Theme.surfaceMuted
                border.color: Theme.separatorSoft
                border.width: 1
                implicitHeight: repoOpsCol.implicitHeight + 24

                ColumnLayout {
                    id: repoOpsCol
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 10

                    Text {
                        text: "⚡ Repo 项目操作"
                        font.pixelSize: Theme.fontSubheading
                        font.weight: Font.DemiBold
                        color: Theme.text
                    }

                    Text {
                        text: "支持对当前项目各子仓库执行同步与状态检查。同步使用 -j8 并保持 customer.xml 分支定义。"
                        font.pixelSize: Theme.fontCaption
                        color: Theme.tertiaryText
                        Layout.fillWidth: true
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        PrimaryButton {
                            text: "同步全部代码 (repo sync)"
                            enabled: root.idle
                            onClicked: {
                                if (root.manager) {
                                    root.manager.synchronize(true, 8, true)
                                }
                            }
                        }

                        PrimaryButton {
                            text: "检查仓库状态 (repo status)"
                            secondary: true
                            enabled: root.idle
                            onClicked: {
                                if (root.manager) {
                                    root.manager.checkStatus()
                                }
                            }
                        }

                        PrimaryButton {
                            text: "重新扫描子仓库"
                            secondary: true
                            enabled: root.idle && root.workspace && root.workspace.workspacePath.length > 0
                            onClicked: {
                                if (root.workspace) {
                                    root.workspace.scanWorkspace()
                                }
                            }
                        }

                        Item { Layout.fillWidth: true }
                    }
                }
            }

            // Card 4: 操作日志与控制
            Rectangle {
                Layout.fillWidth: true
                radius: 10
                color: Theme.surfaceMuted
                border.color: Theme.separatorSoft
                border.width: 1
                implicitHeight: logCol.implicitHeight + 24

                ColumnLayout {
                    id: logCol
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text {
                            text: "📜 项目操作日志"
                            font.pixelSize: Theme.fontSecondary
                            font.weight: Font.DemiBold
                            color: Theme.text
                        }

                        Rectangle {
                            visible: root.manager && root.manager.busy
                            radius: 4
                            color: "#E8F0FE"
                            implicitWidth: runningText.implicitWidth + 12
                            implicitHeight: 20

                            Text {
                                id: runningText
                                anchors.centerIn: parent
                                text: root.manager ? (root.manager.activeTask.length > 0 ? root.manager.activeTask : "执行中…") : ""
                                font.pixelSize: Theme.fontCaption
                                color: Theme.accent
                            }
                        }

                        Item { Layout.fillWidth: true }

                        PrimaryButton {
                            text: "取消操作"
                            danger: true
                            visible: root.manager && root.manager.busy
                            onClicked: if (root.manager) root.manager.cancel()
                        }

                        Button {
                            id: clearLogBtn
                            text: "清空"
                            implicitHeight: 24
                            implicitWidth: clearLogText.implicitWidth + 14
                            padding: 0
                            topInset: 0
                            bottomInset: 0
                            leftInset: 0
                            rightInset: 0
                            hoverEnabled: true
                            enabled: root.manager && root.manager.log.length > 0
                            background: Rectangle {
                                radius: 4
                                color: clearLogBtn.down ? "#E2E3E9" : clearLogBtn.hovered ? "#F0F1F5" : "transparent"
                                border.color: Theme.separatorSoft
                                border.width: 1
                            }
                            contentItem: Text {
                                id: clearLogText
                                text: clearLogBtn.text
                                font.pixelSize: Theme.fontCaption
                                color: Theme.secondaryText
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            onClicked: if (root.manager) root.manager.clearLog()
                        }

                        IconButton {
                            glyph: root.showProjectLog ? "⌃" : "⌄"
                            toolTip: root.showProjectLog ? "收起日志" : "展开日志"
                            onClicked: root.showProjectLog = !root.showProjectLog
                        }
                    }

                    // Real-time Progress Bar Card
                    Rectangle {
                        visible: root.manager && root.manager.busy
                        Layout.fillWidth: true
                        radius: 8
                        color: "#F0F6FF"
                        border.color: "#C2D7EF"
                        border.width: 1
                        implicitHeight: repoProgressCol.implicitHeight + 16

                        ColumnLayout {
                            id: repoProgressCol
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 6

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                Text {
                                    text: (root.manager && root.manager.progressStage.length > 0)
                                        ? root.manager.progressStage
                                        : (root.manager ? root.manager.activeTask : "执行中…")
                                    font.pixelSize: Theme.fontBody
                                    font.weight: Font.DemiBold
                                    color: Theme.accent
                                }

                                Item { Layout.fillWidth: true }

                                Text {
                                    visible: root.manager && root.manager.progressPercent >= 0
                                    text: root.manager ? (root.manager.progressPercent + "%") : ""
                                    font.pixelSize: Theme.fontSubheading
                                    font.weight: Font.Bold
                                    font.family: Theme.monoFontFamily
                                    color: Theme.accent
                                }
                            }

                            ProgressBar {
                                Layout.fillWidth: true
                                from: 0
                                to: 100
                                value: (root.manager && root.manager.progressPercent >= 0) ? root.manager.progressPercent : 0
                                indeterminate: !root.manager || root.manager.progressPercent < 0
                            }

                            Text {
                                visible: root.manager && root.manager.progressDetail.length > 0
                                text: root.manager ? root.manager.progressDetail : ""
                                font.pixelSize: Theme.fontCaption
                                font.family: Theme.monoFontFamily
                                color: Theme.secondaryText
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }
                        }
                    }

                    ScrollView {
                        visible: root.showProjectLog
                        Layout.fillWidth: true
                        Layout.preferredHeight: 150
                        clip: true

                        TextArea {
                            id: projectLogArea
                            text: {
                                const base = (root.manager && root.manager.log.length > 0) ? root.manager.log : ""
                                if (root.manager && root.manager.busy && root.manager.progressText.length > 0) {
                                    return base + (base.endsWith("\n") || base.length === 0 ? "" : "\n") + "▶ " + root.manager.progressText + "\n"
                                }
                                return base.length > 0 ? base : "（暂无操作日志）"
                            }
                            readOnly: true
                            selectByMouse: true
                            wrapMode: TextEdit.WrapAnywhere
                            font.family: Theme.monoFontFamily
                            font.pixelSize: Theme.fontSecondary
                            color: (root.manager && root.manager.log.length > 0) ? Theme.text : Theme.tertiaryText
                            background: Rectangle {
                                radius: 6
                                color: Theme.surfaceStrong
                                border.color: Theme.separatorSoft
                            }
                        }
                    }
                }
            }
        }
    }
}
