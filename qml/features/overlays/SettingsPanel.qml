pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GerritPilot
import "../../components"

Popup {
    id: root

    property var workspace
    property var projectManager: null
    property real hostWidth: 1440
    property real hostHeight: 900

    parent: Overlay.overlay
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    width: Math.min(640, parent ? parent.width - 36 : root.hostWidth - 36)
    height: parent ? parent.height - 36 : root.hostHeight - 36
    modal: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: 0
    background: Rectangle {
        color: Theme.surfaceStrong
        radius: Theme.radiusXLarge
        border.color: Theme.separator
    }
    contentItem: ColumnLayout {
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 20
            Text { text: "设置"; color: Theme.text; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold; Layout.fillWidth: true }
            IconButton { glyph: "×"; onClicked: root.close() }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.separatorSoft }
        ScrollView {
            id: repositorySettingsScroll
            enabled: !root.workspace.busy && (!root.projectManager || !root.projectManager.busy)
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.topMargin: 12
            Layout.bottomMargin: 12
            contentWidth: availableWidth
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ScrollBar.vertical: RightScrollBar { }
            ColumnLayout {
                width: repositorySettingsScroll.availableWidth
                spacing: 15
                Text { text: "显示"; color: Theme.text; font.pixelSize: Theme.fontSubheading; font.weight: Font.DemiBold }
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: "界面缩放"; color: Theme.text; font.pixelSize: Theme.fontBody; Layout.fillWidth: true }
                    SpinBox {
                        objectName: "uiScaleSetting"
                        from: 80; to: 200; stepSize: 10
                        value: Theme.uiScalePercent
                        onValueModified: Theme.uiScalePercent = value
                    }
                    Text { text: "%"; color: Theme.secondaryText; font.pixelSize: Theme.fontSecondary }
                }
                Text { text: "重启应用后生效；桌面环境指定的缩放比例优先。"; color: Theme.tertiaryText; font.pixelSize: Theme.fontCaption; wrapMode: Text.Wrap; Layout.fillWidth: true }
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: "代码字号"; color: Theme.text; font.pixelSize: Theme.fontBody; Layout.fillWidth: true }
                    SpinBox {
                        objectName: "codeFontSizeSetting"
                        from: 8; to: 22
                        value: Theme.codeFontPointSize
                        onValueModified: Theme.codeFontPointSize = value
                    }
                    Text { text: "pt"; color: Theme.secondaryText; font.pixelSize: Theme.fontSecondary }
                }
                Text { text: "项目路径"; color: Theme.secondaryText; font.pixelSize: Theme.fontSecondary; font.weight: Font.DemiBold; Layout.topMargin: 18 }
                TextField { font.pixelSize: Theme.fontBody; Layout.fillWidth: true; text: root.workspace.workspacePath; readOnly: true; placeholderText: "请通过添加项目导入" }
                FlatCheckBox {
                    text: "当前仓库提交时检查 Gerrit Change-Id Hook"
                    checked: root.workspace ? root.workspace.requireChangeIdHook : true
                    enabled: root.workspace && root.workspace.selectedPath.length > 0
                    onToggled: root.workspace.requireChangeIdHook = checked
                }
                Text { text: "普通 Git 仓库可关闭；仅影响本地提交检查，不禁用 Git 自身的 Hook。Gerrit 推送仍会检查 Change-Id，当前应用尚不提供直接 Git 推送。"; color: Theme.tertiaryText; font.pixelSize: Theme.fontCaption; wrapMode: Text.Wrap; Layout.fillWidth: true }
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: "Gerrit SSH 配置"; color: Theme.secondaryText; font.pixelSize: Theme.fontSecondary; font.weight: Font.DemiBold; Layout.fillWidth: true }
                    PrimaryButton {
                        text: "打开 ~/.ssh/config"
                        secondary: true
                        enabled: root.projectManager && !root.projectManager.busy
                        onClicked: root.projectManager.openSshConfig()
                    }
                }
                Text {
                    text: root.projectManager && root.projectManager.sshConfigExists
                        ? "本机 SSH 配置文件已存在；请检查 Gerrit 主机、账号与密钥是否正确。"
                        : "尚无 SSH 配置文件；点击按钮后会安全创建并打开。"
                    color: Theme.tertiaryText
                    font.pixelSize: Theme.fontCaption
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                Text { text: "AI 提交说明（兼容 OpenAI 风格接口）"; color: Theme.secondaryText; font.pixelSize: Theme.fontSecondary; font.weight: Font.DemiBold; Layout.topMargin: 8 }
                TextField { id: aiEndpointField; font.pixelSize: Theme.fontBody;
                    Layout.fillWidth: true
                    text: (root.workspace && root.workspace.commitAi && root.workspace.commitAi.apiEndpoint) ? root.workspace.commitAi.apiEndpoint : ""
                    placeholderText: "Base URL 或完整 /chat/completions 地址"
                    selectByMouse: true
                    onEditingFinished: {
                        if (root.workspace && root.workspace.commitAi) {
                            root.workspace.commitAi.apiEndpoint = text.trim()
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    TextField { id: aiKeyField; font.pixelSize: Theme.fontBody;
                        Layout.fillWidth: true
                        echoMode: TextInput.Password
                        text: (root.workspace && root.workspace.commitAi && root.workspace.commitAi.apiKey) ? root.workspace.commitAi.apiKey : ""
                        placeholderText: "API Key (如 sk-...)"
                        onEditingFinished: {
                            if (root.workspace && root.workspace.commitAi) {
                                root.workspace.commitAi.apiKey = text.trim()
                            }
                        }
                    }
                    TextField { id: aiModelField; font.pixelSize: Theme.fontBody;
                        Layout.preferredWidth: 160
                        text: (root.workspace && root.workspace.commitAi && root.workspace.commitAi.modelName) ? root.workspace.commitAi.modelName : ""
                        placeholderText: "模型名称"
                        onEditingFinished: {
                            if (root.workspace && root.workspace.commitAi) {
                                root.workspace.commitAi.modelName = text.trim()
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    PrimaryButton {
                        text: root.workspace && root.workspace.commitAi && root.workspace.commitAi.testBusy
                            ? "正在测试…" : "测试连接"
                        secondary: true
                        enabled: root.workspace && root.workspace.commitAi && !root.workspace.commitAi.testBusy
                        onClicked: {
                            const ai = root.workspace.commitAi
                            ai.apiEndpoint = aiEndpointField.text.trim()
                            ai.apiKey = aiKeyField.text.trim()
                            ai.modelName = aiModelField.text.trim()
                            ai.testConnection()
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.workspace && root.workspace.commitAi ? root.workspace.commitAi.testResult : ""
                        color: text.startsWith("连接成功") ? Theme.green : Theme.secondaryText
                        font.pixelSize: Theme.fontCaption
                        wrapMode: Text.Wrap
                    }
                }
                FlatCheckBox {
                    text: "记住 API Key（明文保存在本机设置中）"
                    checked: root.workspace && root.workspace.commitAi ? root.workspace.commitAi.rememberApiKey : false
                    onToggled: if (root.workspace && root.workspace.commitAi) root.workspace.commitAi.rememberApiKey = checked
                }
                TextField {
                    Layout.fillWidth: true
                    font.pixelSize: Theme.fontBody
                    text: root.workspace && root.workspace.commitAi ? root.workspace.commitAi.issuePrefix : ""
                    placeholderText: "纯数字单号前缀（留空默认 m-；无单号使用 m-0）"
                    onEditingFinished: if (root.workspace && root.workspace.commitAi) root.workspace.commitAi.issuePrefix = text.trim()
                }
                Text {
                    Layout.fillWidth: true
                    text: "生成时会将所选范围的代码差异发送至配置的 API；请确认有权发送。未配置时不生成。测试连接不上传代码。不记住密钥时可每次输入或使用 GERRITPILOT_AI_API_KEY 环境变量。"
                    color: Theme.tertiaryText
                    font.pixelSize: Theme.fontCaption
                    wrapMode: Text.Wrap
                }
                PrimaryButton { Layout.fillWidth: true; text: "重新载入项目仓库"; enabled: root.workspace.workspacePath.length > 0; onClicked: { root.workspace.scanWorkspace(); root.close() } }
                Text {
                    visible: root.workspace.ignoredRepositories.length > 0
                    text: "已隐藏仓库"
                    color: Theme.secondaryText
                    font.pixelSize: Theme.fontSecondary
                    font.weight: Font.DemiBold
                    Layout.topMargin: 8
                }
                Repeater {
                    model: root.workspace.ignoredRepositories
                    delegate: Rectangle {
                        id: hiddenRepositoryRow
                        required property string modelData
                        readonly property string repositoryPath: modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: 38
                        radius: 9
                        color: Theme.surfaceMuted
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 9
                            Text {
                                text: hiddenRepositoryRow.repositoryPath
                                color: Theme.secondaryText
                                font.pixelSize: Theme.fontCaption
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }
                            Button {
                                id: restoreButton
                                text: "恢复"
                                implicitHeight: 24
                                implicitWidth: restoreText.implicitWidth + 16
                                padding: 0
                                topInset: 0
                                bottomInset: 0
                                leftInset: 0
                                rightInset: 0
                                hoverEnabled: true
                                background: Rectangle {
                                    radius: 5
                                    color: restoreButton.down ? "#DCEBFA" : restoreButton.hovered ? Theme.accentSoft : "transparent"
                                }
                                contentItem: Text {
                                    id: restoreText
                                    text: restoreButton.text
                                    color: Theme.accent
                                    font.pixelSize: Theme.fontCaption
                                    font.weight: Font.DemiBold
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    acceptedButtons: Qt.NoButton
                                }
                                onClicked: root.workspace.setRepositoryIgnored(hiddenRepositoryRow.repositoryPath, false)
                            }
                        }
                    }
                }
            }
        }
    }
}
