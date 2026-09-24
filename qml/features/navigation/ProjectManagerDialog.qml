pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import GerritPilot
import "../../components"

AppDialog {
    id: root
    property var workspace
    property var repoManager
    property int currentTab: 0

    preferredWidth: 740
    title: "项目与工作区生命周期管理"
    standardButtons: Dialog.Close

    readonly property var presets: [
        {
            id: "T1EJFL",
            label: "T1EJFL",
            tip: "奇瑞 T1EJFL 平台 (TDA4_MAIN / TDA4_T1EJFL/app_dev.xml)"
        },
        {
            id: "T13T_BEV",
            label: "T13T BEV",
            tip: "T13T BEV 平台 (TDA4_MAIN / TDA4_T13T_BEV/app_dev.xml)"
        },
        {
            id: "T1TP",
            label: "T1TP",
            tip: "T1TP 平台 (TDA4_MAIN / TDA4_T1TP/app_dev.xml)"
        },
        {
            id: "T13C_BEV",
            label: "T13C BEV",
            tip: "T13C BEV 基线 (TDA4_MAIN / TDA4_T13C_BEV/app_dev.xml)"
        },
        {
            id: "T13C_HEV",
            label: "T13C HEV",
            tip: "T13C HEV 基线 (TDA4_MAIN / TDA4_T13C_HEV/app_dev.xml)"
        },
        {
            id: "CUSTOM",
            label: "✎ 自定义",
            tip: "自定义项目与分支配置"
        }
    ]

    property string selectedPresetId: "T1EJFL"
    property var currentResolution: ({})
    property bool showAdvancedUrls: false
    property bool showExtraOverrides: false
    property bool showExecutionLog: false

    // Tab 1 (Diagnostic) state
    property string diagSelectedPlat: "T1EJFL"
    property bool showDiagXmlEditor: false
    property string diagRawXml: ""

    function applyResolution(keyword) {
        if (!root.repoManager) return
        selectedPresetId = keyword
        const res = root.repoManager.autoResolvePlatform(keyword)
        currentResolution = res
        createProjName.text = res.projectName || ""
        const baseDir = (root.repoManager && root.repoManager.defaultBaseDir) ? root.repoManager.defaultBaseDir : "/home/liushuai/code"
        createProjPath.text = baseDir + "/" + (res.dirName || res.projectName)
        createManifestBranch.text = res.manifestBranch || "TDA4_MAIN"
        createManifestFile.text = res.manifestFile || "app_dev.xml"
        createHmiBranch.text = res.hmiBranch || ""
    }

    function reloadDiagXml() {
        if (root.repoManager) {
            diagRawXml = root.repoManager.readCustomerManifest("")
        }
    }

    Component.onCompleted: {
        if (root.repoManager) {
            root.repoManager.fetchRemoteHmiBranches()
        }
        applyResolution("T1EJFL")
    }

    Connections {
        target: root.repoManager
        function onRemoteHmiBranchesChanged() {
            // Updated when git ls-remote finishes
        }
        function onManifestRepaired(xmlPath, summary) {
            reloadDiagXml()
        }
        function onChanged() {
            if (root.currentTab === 1 && root.showDiagXmlEditor) {
                reloadDiagXml()
            }
        }
    }

    FolderDialog {
        id: importFolderDialog
        title: "选择已有项目目录"
        onAccepted: {
            const path = selectedFolder.toString().replace(/^file:\/\//, "")
            importProjectPath.text = path
            if (importProjectName.text.trim().length === 0) {
                const parts = path.split("/")
                importProjectName.text = parts[parts.length - 1]
            }
        }
    }

    FolderDialog {
        id: createFolderDialog
        title: "选择新项目存放目录"
        onAccepted: {
            const path = selectedFolder.toString().replace(/^file:\/\//, "")
            createProjPath.text = path
        }
    }

    contentItem: ColumnLayout {
        spacing: 10
        implicitHeight: 580

        // ================= Navigation Tabs =================
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 38
            radius: 8
            color: Theme.surfaceMuted

            RowLayout {
                anchors.fill: parent
                anchors.margins: 3
                spacing: 4

                // Tab 0 Button
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 6
                    color: root.currentTab === 0 ? Theme.surface : "transparent"
                    border.color: root.currentTab === 0 ? Theme.separatorSoft : "transparent"

                    Text {
                        anchors.centerIn: parent
                        text: "⚡ 智能拉取与新建"
                        font.pixelSize: Theme.fontBody
                        font.weight: root.currentTab === 0 ? Font.DemiBold : Font.Normal
                        color: root.currentTab === 0 ? Theme.accent : Theme.secondaryText
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.currentTab = 0
                    }
                }

                // Tab 1 Button
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 6
                    color: root.currentTab === 1 ? Theme.surface : "transparent"
                    border.color: root.currentTab === 1 ? Theme.separatorSoft : "transparent"

                    Text {
                        anchors.centerIn: parent
                        text: "🎯 当前项目客户链诊断与同步"
                        font.pixelSize: Theme.fontBody
                        font.weight: root.currentTab === 1 ? Font.DemiBold : Font.Normal
                        color: root.currentTab === 1 ? Theme.accent : Theme.secondaryText
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.currentTab = 1
                            root.reloadDiagXml()
                        }
                    }
                }

                // Tab 2 Button
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 6
                    color: root.currentTab === 2 ? Theme.surface : "transparent"
                    border.color: root.currentTab === 2 ? Theme.separatorSoft : "transparent"

                    Text {
                        anchors.centerIn: parent
                        text: "📁 已保存项目 (" + ((root.workspace && root.workspace.projectManager && root.workspace.projectManager.projectList) ? root.workspace.projectManager.projectList.length : 0) + ")"
                        font.pixelSize: Theme.fontBody
                        font.weight: root.currentTab === 2 ? Font.DemiBold : Font.Normal
                        color: root.currentTab === 2 ? Theme.accent : Theme.secondaryText
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.currentTab = 2
                    }
                }
            }
        }

        // ================= Tab 0: Smart Pull & Create =================
        ScrollView {
            visible: root.currentTab === 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ScrollBar.vertical: RightScrollBar { }

            ColumnLayout {
                width: parent.width
                spacing: 10

                // Platform Chips
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Text {
                        text: "① 目标车型平台快捷选择："
                        font.pixelSize: Theme.fontSecondary
                        font.weight: Font.DemiBold
                        color: Theme.text
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Repeater {
                            model: root.presets
                            delegate: Rectangle {
                                id: presetChip
                                required property var modelData
                                Layout.preferredHeight: 30
                                Layout.preferredWidth: chipText.implicitWidth + 24
                                radius: 15
                                readonly property bool isSelected: root.selectedPresetId.toUpperCase() === modelData.id.toUpperCase()
                                color: isSelected ? Theme.accentSoft : (chipMouse.containsMouse ? Theme.surfaceMuted : "#F4F5F8")
                                border.color: isSelected ? Theme.accent : (chipMouse.containsMouse ? Theme.separatorSoft : "transparent")
                                border.width: isSelected ? 1.5 : 1

                                Text {
                                    id: chipText
                                    anchors.centerIn: parent
                                    text: presetChip.modelData.label
                                    font.pixelSize: Theme.fontSecondary
                                    font.weight: presetChip.isSelected ? Font.DemiBold : Font.Normal
                                    color: presetChip.isSelected ? Theme.accent : Theme.text
                                }

                                MouseArea {
                                    id: chipMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.applyResolution(presetChip.modelData.id)
                                }
                            }
                        }

                        Item { Layout.fillWidth: true }
                    }
                }

                // Remote Branch Selector via Gerrit
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text {
                        text: "② 或选择 Gerrit 远端 HMI 分支："
                        font.pixelSize: Theme.fontCaption
                        color: Theme.secondaryText
                    }

                    ComboBox {
                        id: remoteBranchCombo
                        Layout.fillWidth: true
                        model: (root.repoManager && root.repoManager.remoteHmiBranches && root.repoManager.remoteHmiBranches.length > 0)
                            ? root.repoManager.remoteHmiBranches
                            : ["TDA4_T1EJFL", "TDA4_T13T_BEV", "TDA4_T1TP_FX", "TDA4_T13C_BEV", "TDA4_T1TP"]
                        onActivated: function(index) {
                            const b = model[index]
                            root.applyResolution(b)
                        }
                    }

                    PrimaryButton {
                        text: (root.repoManager && root.repoManager.indexingBranches) ? "⏳ 索引中…" : "🔄 刷新远端分支"
                        secondary: true
                        enabled: !root.repoManager || !root.repoManager.indexingBranches
                        implicitHeight: 32
                        onClicked: {
                            if (root.repoManager) root.repoManager.fetchRemoteHmiBranches()
                        }
                    }
                }

                // Intelligence Auto-Resolution Preview Card
                Rectangle {
                    Layout.fillWidth: true
                    radius: 8
                    color: "#F6F9FE"
                    border.color: "#D0E2FB"
                    border.width: 1
                    implicitHeight: autoResolveCol.implicitHeight + 20

                    ColumnLayout {
                        id: autoResolveCol
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 6

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Text {
                                text: "✨ 自动解析与客户链对齐规则"
                                font.pixelSize: Theme.fontSecondary
                                font.weight: Font.DemiBold
                                color: Theme.accent
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: "7-子模块完整链自动适配"
                                font.pixelSize: Theme.fontCaption
                                color: Theme.accent
                            }
                        }

                        Text {
                            text: root.currentResolution.summary ? root.currentResolution.summary : "选择平台以预览参数..."
                            font.pixelSize: Theme.fontCaption
                            font.family: Theme.monoFontFamily
                            color: Theme.text
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                    }
                }

                // Project Directory & Name
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    ColumnLayout {
                        Layout.preferredWidth: 160
                        spacing: 4
                        Text { text: "项目名称"; font.pixelSize: Theme.fontCaption; color: Theme.secondaryText }
                        TextField {
                            id: createProjName
                            Layout.fillWidth: true
                            font.pixelSize: Theme.fontBody
                            placeholderText: "例如 T1EJFL"
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Text { text: "项目存放目录"; font.pixelSize: Theme.fontCaption; color: Theme.secondaryText }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            TextField {
                                id: createProjPath
                                Layout.fillWidth: true
                                font.pixelSize: Theme.fontBody
                                placeholderText: "/home/user/code/project"
                            }
                            PrimaryButton {
                                text: "浏览..."
                                secondary: true
                                implicitHeight: 32
                                onClicked: createFolderDialog.open()
                            }
                        }
                    }
                }

                // Target HMI branch
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text {
                        text: "目标 HMI 分支 (mv_hmi)："
                        font.pixelSize: Theme.fontBody
                        color: Theme.text
                        Layout.preferredWidth: 160
                    }

                    TextField {
                        id: createHmiBranch
                        Layout.fillWidth: true
                        font.pixelSize: Theme.fontBody
                        placeholderText: "例如 TDA4_T1EJFL 或 TDA4_T13T_BEV"
                    }
                }

                // Collapsible Advanced URLs & Overrides
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    IconButton {
                        glyph: root.showAdvancedUrls ? "⌃" : "⌄"
                        Layout.preferredWidth: 20
                        Layout.preferredHeight: 20
                        onClicked: root.showAdvancedUrls = !root.showAdvancedUrls
                    }
                    Text {
                        text: "高级底层参数 (Manifest 分支/文件、Repo URL、额外覆盖)"
                        font.pixelSize: Theme.fontSecondary
                        color: Theme.secondaryText
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.showAdvancedUrls = !root.showAdvancedUrls
                        }
                    }
                    Item { Layout.fillWidth: true }
                }

                ColumnLayout {
                    visible: root.showAdvancedUrls
                    Layout.fillWidth: true
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Text { text: "Manifest 分支 (-b)"; font.pixelSize: Theme.fontCaption; color: Theme.secondaryText }
                            TextField {
                                id: createManifestBranch
                                Layout.fillWidth: true
                                font.pixelSize: Theme.fontSecondary
                                text: "TDA4_MAIN"
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Text { text: "清单文件 (-m)"; font.pixelSize: Theme.fontCaption; color: Theme.secondaryText }
                            TextField {
                                id: createManifestFile
                                Layout.fillWidth: true
                                font.pixelSize: Theme.fontSecondary
                            }
                        }
                    }

                    TextField {
                        id: createCustomOverrides
                        Layout.fillWidth: true
                        font.pixelSize: Theme.fontSecondary
                        placeholderText: "额外模块覆盖（例如 AVM）：mvpilot/mv_avm_graph=TDA4_T13C_HEV"
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        TextField {
                            id: createRepoUrl
                            Layout.fillWidth: true
                            font.pixelSize: Theme.fontSecondary
                            placeholderText: "repo 工具 URL (--repo-url)"
                            text: root.repoManager ? root.repoManager.defaultRepoUrl : ""
                        }

                        TextField {
                            id: createManifestUrl
                            Layout.fillWidth: true
                            font.pixelSize: Theme.fontSecondary
                            placeholderText: "Manifest 仓库 URL (-u)"
                            text: root.repoManager ? root.repoManager.defaultManifestUrl : ""
                        }
                    }
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.separatorSoft }

                // Sync Scope Option
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 16

                    CheckBox {
                        id: optSyncHmiOnly
                        text: "仅拉取 HMI 相关子仓库 (推荐：快速拉取 HMI / 组件 / 客户链，避免数小时全量拉取)"
                        checked: true
                    }
                }

                // Action Button
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    PrimaryButton {
                        text: (root.repoManager && root.repoManager.busy)
                            ? ("⏳ " + root.repoManager.activeTask)
                            : "🚀 智能一键拉取并创建项目"
                        enabled: (!root.repoManager || !root.repoManager.busy)
                            && createProjPath.text.trim().length > 0
                        implicitHeight: 36
                        Layout.fillWidth: true
                        onClicked: {
                            if (root.repoManager) {
                                root.showExecutionLog = true
                                root.repoManager.quickCreateProject(
                                    createProjName.text.trim(),
                                    createProjPath.text.trim(),
                                    createManifestUrl.text.trim(),
                                    createManifestBranch.text.trim(),
                                    createManifestFile.text.trim(),
                                    createRepoUrl.text.trim(),
                                    createHmiBranch.text.trim(),
                                    createCustomOverrides.text.trim(),
                                    true,
                                    optSyncHmiOnly.checked
                                )
                            }
                        }
                    }

                    PrimaryButton {
                        visible: root.repoManager && root.repoManager.busy
                        text: "取消"
                        secondary: true
                        implicitHeight: 36
                        onClicked: {
                            if (root.repoManager) root.repoManager.cancel()
                        }
                    }

                    IconButton {
                        visible: root.repoManager && root.repoManager.log.length > 0
                        glyph: root.showExecutionLog ? "📋" : "📄"
                        toolTip: root.showExecutionLog ? "隐藏实时日志" : "查看实时日志"
                        onClicked: root.showExecutionLog = !root.showExecutionLog
                    }
                }
            }
        }

        // ================= Tab 1: Current Project Diagnostics & Manifest Sync =================
        ScrollView {
            visible: root.currentTab === 1
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ScrollBar.vertical: RightScrollBar { }

            ColumnLayout {
                width: parent.width
                spacing: 12

                // Card 1: Current Project Info
                Rectangle {
                    Layout.fillWidth: true
                    radius: 8
                    color: Theme.surfaceMuted
                    border.color: Theme.separatorSoft
                    border.width: 1
                    implicitHeight: curProjInfoCol.implicitHeight + 20

                    ColumnLayout {
                        id: curProjInfoCol
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 6

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Text { text: "📦"; font.pixelSize: Theme.fontSubheading }

                            Text {
                                text: (root.workspace && root.workspace.projectManager && root.workspace.projectManager.currentProjectName)
                                    ? root.workspace.projectManager.currentProjectName : "当前工作区"
                                font.pixelSize: Theme.fontSubheading
                                font.weight: Font.DemiBold
                                color: Theme.text
                            }

                            Rectangle {
                                radius: 4
                                readonly property bool hasXml: root.repoManager && root.repoManager.currentCustomerXml.length > 0
                                color: hasXml ? "#E6F4EA" : "#FEF7E0"
                                border.color: hasXml ? "#CEEAD6" : "#FEEFC3"
                                border.width: 1
                                implicitWidth: badgeTxt.implicitWidth + 12
                                implicitHeight: 22

                                Text {
                                    id: badgeTxt
                                    anchors.centerIn: parent
                                    text: parent.hasXml ? "已识别 customer.xml" : "未发现 customer.xml"
                                    font.pixelSize: Theme.fontCaption
                                    font.weight: Font.Medium
                                    color: parent.hasXml ? "#137333" : "#B06000"
                                }
                            }

                            Item { Layout.fillWidth: true }

                            PrimaryButton {
                                text: "打开项目目录"
                                secondary: true
                                implicitHeight: 28
                                onClicked: {
                                    const p = (root.workspace && root.workspace.workspacePath) ? root.workspace.workspacePath : (root.repoManager ? root.repoManager.path : "")
                                    if (p.length > 0) Qt.openUrlExternally("file://" + p)
                                }
                            }
                        }

                        Text {
                            text: "路径: " + ((root.workspace && root.workspace.workspacePath) ? root.workspace.workspacePath : (root.repoManager ? root.repoManager.path : "未设置"))
                            font.pixelSize: Theme.fontCaption
                            font.family: Theme.monoFontFamily
                            color: Theme.secondaryText
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }

                        Text {
                            visible: root.repoManager && root.repoManager.currentCustomerXml.length > 0
                            text: "清单文件: " + (root.repoManager ? root.repoManager.currentCustomerXml : "") + " (当前 HMI revision: " + (root.repoManager ? root.repoManager.currentHmiRevision : "无") + ")"
                            font.pixelSize: Theme.fontCaption
                            font.family: Theme.monoFontFamily
                            color: Theme.accent
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }
                    }
                }

                // Card 2: 1-Click Smart Diagnostics & AI Repair
                Rectangle {
                    Layout.fillWidth: true
                    radius: 8
                    color: "#F6F9FE"
                    border.color: "#D0E2FB"
                    border.width: 1
                    implicitHeight: diagActionCol.implicitHeight + 20

                    ColumnLayout {
                        id: diagActionCol
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 8

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Text {
                                text: "🔍 智能诊断与一键修复"
                                font.pixelSize: Theme.fontSecondary
                                font.weight: Font.DemiBold
                                color: Theme.accent
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: "自动识别架构 · 修复依赖链 · 一键同步"
                                font.pixelSize: Theme.fontCaption
                                color: Theme.secondaryText
                            }
                        }

                        Text {
                            text: "自动从当前项目目录名或 .repo/manifest.xml 中检测目标车型平台，重建 customer.xml 的 7-子模块标准对齐链，并执行 repo sync 同步。"
                            font.pixelSize: Theme.fontCaption
                            color: Theme.secondaryText
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            PrimaryButton {
                                text: (root.repoManager && root.repoManager.busy) ? "⏳ 执行中…" : "🚀 智能诊断并修复当前项目"
                                enabled: !root.repoManager || !root.repoManager.busy
                                Layout.fillWidth: true
                                implicitHeight: 34
                                onClicked: {
                                    root.showExecutionLog = true
                                    if (root.repoManager) {
                                        root.repoManager.smartDiagnoseAndFixCurrentProject(true, true)
                                    }
                                }
                            }

                            PrimaryButton {
                                text: "🤖 AI 深度诊断与修复"
                                secondary: true
                                enabled: !root.repoManager || !root.repoManager.busy
                                implicitHeight: 34
                                onClicked: {
                                    root.showExecutionLog = true
                                    if (root.repoManager) {
                                        root.repoManager.aiDiagnoseAndRepair("")
                                    }
                                }
                            }
                        }
                    }
                }

                // Card 3: Platform Alignment & Customer Manifest Switcher
                Rectangle {
                    Layout.fillWidth: true
                    radius: 8
                    color: Theme.surfaceMuted
                    border.color: Theme.separatorSoft
                    border.width: 1
                    implicitHeight: platAlignCol.implicitHeight + 20

                    ColumnLayout {
                        id: platAlignCol
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 8

                        Text {
                            text: "🎯 快速对齐到目标平台客户链："
                            font.pixelSize: Theme.fontSecondary
                            font.weight: Font.DemiBold
                            color: Theme.text
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Repeater {
                                model: ["T1EJFL", "T13T_BEV", "T1TP", "T13C_BEV", "T13C_HEV"]
                                delegate: Rectangle {
                                    id: diagPlatChip
                                    required property string modelData
                                    Layout.preferredHeight: 28
                                    Layout.preferredWidth: diagChipText.implicitWidth + 20
                                    radius: 14
                                    readonly property bool isSelected: root.diagSelectedPlat.toUpperCase() === modelData.toUpperCase()
                                    color: isSelected ? Theme.accentSoft : (diagChipMouse.containsMouse ? "#E8F0FE" : Theme.surface)
                                    border.color: isSelected ? Theme.accent : "#C2D7EF"
                                    border.width: isSelected ? 1.5 : 1

                                    Text {
                                        id: diagChipText
                                        anchors.centerIn: parent
                                        text: diagPlatChip.modelData
                                        font.pixelSize: Theme.fontCaption
                                        font.weight: diagPlatChip.isSelected ? Font.DemiBold : Font.Normal
                                        color: diagPlatChip.isSelected ? Theme.accent : Theme.text
                                    }

                                    MouseArea {
                                        id: diagChipMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            root.diagSelectedPlat = diagPlatChip.modelData
                                            const resolved = root.repoManager ? root.repoManager.autoResolvePlatform(diagPlatChip.modelData) : ({})
                                            diagHmiBranchInput.text = resolved.hmiBranch || ""
                                        }
                                    }
                                }
                            }

                            Item { Layout.fillWidth: true }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Text {
                                text: "目标 HMI 分支："
                                font.pixelSize: Theme.fontCaption
                                color: Theme.text
                            }

                            TextField {
                                id: diagHmiBranchInput
                                Layout.fillWidth: true
                                font.pixelSize: Theme.fontBody
                                text: root.repoManager ? root.repoManager.currentHmiRevision : ""
                                placeholderText: "例如 TDA4_T1EJFL"
                            }

                            PrimaryButton {
                                text: "对齐平台并同步"
                                enabled: !root.repoManager || !root.repoManager.busy
                                implicitHeight: 30
                                onClicked: {
                                    root.showExecutionLog = true
                                    if (root.repoManager) {
                                        root.repoManager.repairAndSync(root.diagSelectedPlat, diagHmiBranchInput.text.trim(), true)
                                    }
                                }
                            }

                            PrimaryButton {
                                text: "仅写入 XML"
                                secondary: true
                                enabled: !root.repoManager || !root.repoManager.busy
                                implicitHeight: 30
                                onClicked: {
                                    if (root.repoManager) {
                                        root.repoManager.repairCustomerManifest("", root.diagSelectedPlat, diagHmiBranchInput.text.trim())
                                        root.reloadDiagXml()
                                    }
                                }
                            }
                        }
                    }
                }

                // Card 4: Standard Repo Operations
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    PrimaryButton {
                        text: "同步全部代码 (repo sync)"
                        secondary: true
                        enabled: !root.repoManager || !root.repoManager.busy
                        Layout.fillWidth: true
                        implicitHeight: 32
                        onClicked: {
                            root.showExecutionLog = true
                            if (root.repoManager) root.repoManager.synchronize(true, 8, true)
                        }
                    }

                    PrimaryButton {
                        text: "检查状态 (repo status)"
                        secondary: true
                        enabled: !root.repoManager || !root.repoManager.busy
                        Layout.fillWidth: true
                        implicitHeight: 32
                        onClicked: {
                            root.showExecutionLog = true
                            if (root.repoManager) root.repoManager.checkStatus()
                        }
                    }

                    PrimaryButton {
                        text: "重新扫描工作区仓库"
                        secondary: true
                        enabled: (!root.repoManager || !root.repoManager.busy) && root.workspace && root.workspace.workspacePath.length > 0
                        Layout.fillWidth: true
                        implicitHeight: 32
                        onClicked: {
                            if (root.workspace) root.workspace.scanWorkspace()
                        }
                    }
                }

                // Collapsible Full Raw XML Editor
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    IconButton {
                        glyph: root.showDiagXmlEditor ? "⌃" : "⌄"
                        Layout.preferredWidth: 20
                        Layout.preferredHeight: 20
                        onClicked: {
                            root.showDiagXmlEditor = !root.showDiagXmlEditor
                            if (root.showDiagXmlEditor) root.reloadDiagXml()
                        }
                    }

                    Text {
                        text: "查看 / 手动编辑 customer.xml 完整代码"
                        font.pixelSize: Theme.fontSecondary
                        color: Theme.accent
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                root.showDiagXmlEditor = !root.showDiagXmlEditor
                                if (root.showDiagXmlEditor) root.reloadDiagXml()
                            }
                        }
                    }

                    Item { Layout.fillWidth: true }
                }

                ColumnLayout {
                    visible: root.showDiagXmlEditor
                    Layout.fillWidth: true
                    spacing: 6

                    ScrollView {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 140
                        clip: true
                        ScrollBar.vertical: RightScrollBar { }

                        TextArea {
                            id: diagXmlEditorArea
                            selectByMouse: true
                            wrapMode: TextEdit.NoWrap
                            font.family: Theme.monoFontFamily
                            font.pixelSize: Theme.fontCaption
                            text: root.diagRawXml
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
                            text: "从磁盘重载"
                            secondary: true
                            implicitHeight: 28
                            onClicked: root.reloadDiagXml()
                        }

                        PrimaryButton {
                            text: "保存并写入 XML"
                            implicitHeight: 28
                            onClicked: {
                                if (root.repoManager) {
                                    root.repoManager.writeRawCustomerManifest("", diagXmlEditorArea.text)
                                    root.reloadDiagXml()
                                }
                            }
                        }

                        PrimaryButton {
                            text: "系统编辑器打开"
                            secondary: true
                            implicitHeight: 28
                            onClicked: if (root.repoManager) root.repoManager.openCustomerManifest("")
                        }

                        PrimaryButton {
                            text: "打开 manifest.xml"
                            secondary: true
                            implicitHeight: 28
                            onClicked: if (root.repoManager) root.repoManager.openManifest()
                        }

                        Item { Layout.fillWidth: true }
                    }
                }
            }
        }

        // ================= Tab 2: Saved Projects =================
        ColumnLayout {
            visible: root.currentTab === 2
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            Text {
                text: "导入本地已有项目"
                font.pixelSize: Theme.fontSecondary
                font.weight: Font.DemiBold
                color: Theme.text
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Rectangle {
                    Layout.preferredWidth: 140
                    Layout.preferredHeight: 32
                    radius: 7
                    color: Theme.surfaceMuted
                    border.color: importProjectName.activeFocus ? Theme.accent : "transparent"

                    TextInput {
                        id: importProjectName
                        anchors.fill: parent
                        anchors.margins: 6
                        font.pixelSize: Theme.fontSecondary
                        color: Theme.text
                        clip: true
                        selectByMouse: true

                        Text {
                            anchors.fill: parent
                            text: "项目名称"
                            color: Theme.placeholder
                            font.pixelSize: Theme.fontSecondary
                            visible: !importProjectName.text.length && !importProjectName.activeFocus
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    radius: 7
                    color: Theme.surfaceMuted
                    border.color: importProjectPath.activeFocus ? Theme.accent : "transparent"

                    TextInput {
                        id: importProjectPath
                        anchors.fill: parent
                        anchors.margins: 6
                        font.pixelSize: Theme.fontSecondary
                        color: Theme.text
                        clip: true
                        selectByMouse: true

                        Text {
                            anchors.fill: parent
                            text: "项目路径，例如 /home/user/code/project"
                            color: Theme.placeholder
                            font.pixelSize: Theme.fontSecondary
                            visible: !importProjectPath.text.length && !importProjectPath.activeFocus
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }

                PrimaryButton {
                    text: "浏览..."
                    secondary: true
                    implicitHeight: 32
                    onClicked: importFolderDialog.open()
                }

                PrimaryButton {
                    text: "导入项目"
                    implicitHeight: 32
                    enabled: importProjectPath.text.trim().length > 0 && root.workspace && root.workspace.projectManager
                    onClicked: {
                        if (root.workspace && root.workspace.projectManager) {
                            root.workspace.projectManager.addProject(importProjectName.text, importProjectPath.text)
                            importProjectName.text = ""
                            importProjectPath.text = ""
                        }
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.separatorSoft }

            Text {
                text: "已保存的项目列表 (" + ((root.workspace && root.workspace.projectManager && root.workspace.projectManager.projectList) ? root.workspace.projectManager.projectList.length : 0) + ")"
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
                    id: projListView
                    width: parent.width
                    spacing: 6
                    model: (root.workspace && root.workspace.projectManager && root.workspace.projectManager.projectList) ? root.workspace.projectManager.projectList : []

                    delegate: Rectangle {
                        id: projItemRow
                        required property int index
                        required property var modelData
                        width: projListView.width
                        height: 52
                        radius: 8
                        readonly property bool isActive: (root.workspace && root.workspace.projectManager)
                            ? root.workspace.projectManager.currentIndex === projItemRow.index
                            : false
                        color: isActive ? Theme.accentSoft : (rowMouseArea.containsMouse ? Theme.surfaceMuted : "transparent")
                        border.color: isActive ? Theme.accent : (rowMouseArea.containsMouse ? Theme.separatorSoft : "transparent")

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 8

                            Item {
                                Layout.fillWidth: true
                                Layout.fillHeight: true

                                MouseArea {
                                    id: rowMouseArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (root.workspace && root.workspace.projectManager) {
                                            root.workspace.projectManager.switchProject(projItemRow.index)
                                            root.close()
                                        }
                                    }
                                }

                                RowLayout {
                                    anchors.fill: parent
                                    spacing: 8

                                    Text {
                                        text: projItemRow.isActive ? "✓" : " "
                                        color: Theme.accent
                                        font.bold: true
                                        font.pixelSize: Theme.fontBody
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2

                                        Text {
                                            text: projItemRow.modelData ? projItemRow.modelData.name : ""
                                            color: Theme.text
                                            font.pixelSize: Theme.fontSecondary
                                            font.bold: true
                                            elide: Text.ElideRight
                                        }

                                        Text {
                                            text: projItemRow.modelData ? projItemRow.modelData.path : ""
                                            color: Theme.secondaryText
                                            font.pixelSize: Theme.fontSecondary
                                            elide: Text.ElideMiddle
                                            Layout.fillWidth: true
                                        }
                                    }
                                }
                            }

                            PrimaryButton {
                                visible: !projItemRow.isActive
                                text: "切换"
                                secondary: true
                                implicitHeight: 26
                                onClicked: {
                                    if (root.workspace && root.workspace.projectManager) {
                                        root.workspace.projectManager.switchProject(projItemRow.index)
                                        root.close()
                                    }
                                }
                            }

                            IconButton {
                                glyph: "✕"
                                toolTip: "从列表中移除此项目"
                                Layout.preferredWidth: 24
                                Layout.preferredHeight: 24
                                onClicked: {
                                    if (root.workspace && root.workspace.projectManager) {
                                        root.workspace.projectManager.removeProject(projItemRow.index)
                                    }
                                }
                            }
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: projListView.count === 0
                        text: "暂无保存的项目"
                        color: Theme.tertiaryText
                        font.pixelSize: Theme.fontSecondary
                    }
                }
            }
        }

        // ================= Common Bottom Area: Progress & Logs =================

        // Real-time Progress Bar Card (visible when busy)
        Rectangle {
            visible: root.repoManager && root.repoManager.busy
            Layout.fillWidth: true
            radius: 8
            color: "#F0F6FF"
            border.color: "#C2D7EF"
            border.width: 1
            implicitHeight: progressCardCol.implicitHeight + 16

            ColumnLayout {
                id: progressCardCol
                anchors.fill: parent
                anchors.margins: 8
                spacing: 4

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text {
                        text: (root.repoManager && root.repoManager.progressStage.length > 0)
                            ? root.repoManager.progressStage
                            : (root.repoManager ? root.repoManager.activeTask : "执行中…")
                        font.pixelSize: Theme.fontSecondary
                        font.weight: Font.DemiBold
                        color: Theme.accent
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        visible: root.repoManager && root.repoManager.progressPercent >= 0
                        text: root.repoManager ? (root.repoManager.progressPercent + "%") : ""
                        font.pixelSize: Theme.fontBody
                        font.weight: Font.Bold
                        font.family: Theme.monoFontFamily
                        color: Theme.accent
                    }
                }

                ProgressBar {
                    Layout.fillWidth: true
                    from: 0
                    to: 100
                    value: (root.repoManager && root.repoManager.progressPercent >= 0) ? root.repoManager.progressPercent : 0
                    indeterminate: !root.repoManager || root.repoManager.progressPercent < 0
                }

                Text {
                    visible: root.repoManager && root.repoManager.progressDetail.length > 0
                    text: root.repoManager ? root.repoManager.progressDetail : ""
                    font.pixelSize: Theme.fontCaption
                    font.family: Theme.monoFontFamily
                    color: Theme.secondaryText
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }
            }
        }

        // Real-time Log Output Area (Collapsible)
        ColumnLayout {
            visible: root.showExecutionLog
            Layout.fillWidth: true
            spacing: 4

            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                Text {
                    text: "实时执行日志："
                    font.pixelSize: Theme.fontCaption
                    color: Theme.secondaryText
                }

                Item { Layout.fillWidth: true }

                IconButton {
                    glyph: "✕"
                    toolTip: "关闭日志窗口"
                    Layout.preferredWidth: 20
                    Layout.preferredHeight: 20
                    onClicked: root.showExecutionLog = false
                }
            }

            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: 110
                clip: true
                ScrollBar.vertical: RightScrollBar { }

                TextArea {
                    id: execLogText
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.WrapAnywhere
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Theme.fontCaption
                    text: {
                        const base = root.repoManager ? root.repoManager.log : ""
                        if (root.repoManager && root.repoManager.busy && root.repoManager.progressText.length > 0) {
                            return base + (base.endsWith("\n") || base.length === 0 ? "" : "\n") + "▶ " + root.repoManager.progressText + "\n"
                        }
                        return base
                    }
                    color: Theme.text
                    background: Rectangle {
                        color: Theme.surfaceMuted
                        radius: 6
                    }
                    onTextChanged: {
                        cursorPosition = length
                    }
                }
            }
        }
    }
}
