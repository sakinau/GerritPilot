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
    height: Math.min(780, parent ? parent.height - 36 : root.hostHeight - 36)
    modal: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: 0
    background: FloatingSurface {
        radius: Theme.radiusXLarge
        color: Theme.surfaceGrouped
        elevation: 10
    }
    Overlay.modal: Rectangle { color: Theme.scrim }

    component SettingRow: RowLayout {
        property alias label: rowLabel.text
        Layout.fillWidth: true
        spacing: 10
        Text { id: rowLabel; color: Theme.text; font.pixelSize: Theme.fontBody; Layout.fillWidth: true }
    }
    component Hairline: Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.separatorSoft }

    contentItem: ColumnLayout {
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 14
            Layout.topMargin: 16
            Layout.bottomMargin: 12
            spacing: 10
            Rectangle {
                Layout.preferredWidth: 30
                Layout.preferredHeight: 30
                radius: 8
                color: "#8E8E93"
                Icon { anchors.centerIn: parent; name: "settings"; size: 18; strokeWidth: 1.8; color: Theme.textOnAccent }
            }
            Text { text: "设置"; color: Theme.text; font.pixelSize: Theme.fontTitle; font.weight: Font.Bold; Layout.fillWidth: true }
            IconButton { iconName: "x"; toolTip: "关闭"; onClicked: root.close() }
        }
        ScrollView {
            id: repositorySettingsScroll
            enabled: !root.workspace.busy && (!root.projectManager || !root.projectManager.busy)
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.bottomMargin: 16
            contentWidth: availableWidth
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ScrollBar.vertical: RightScrollBar { }
            ColumnLayout {
                width: repositorySettingsScroll.availableWidth
                spacing: 18

                SettingsGroup {
                    Layout.fillWidth: true
                    title: "显示"
                    footnote: "界面缩放重启应用后生效；桌面环境指定的缩放比例优先。"
                    SettingRow {
                        label: "界面缩放"
                        AppSpinBox {
                            objectName: "uiScaleSetting"
                            from: 80; to: 200; stepSize: 10
                            suffix: "%"
                            value: Theme.uiScalePercent
                            onValueModified: Theme.uiScalePercent = value
                        }
                    }
                    Hairline { }
                    SettingRow {
                        label: "代码字号"
                        AppSpinBox {
                            objectName: "codeFontSizeSetting"
                            from: 8; to: 22
                            suffix: " pt"
                            value: Theme.codeFontPointSize
                            onValueModified: Theme.codeFontPointSize = value
                        }
                    }
                }

                SettingsGroup {
                    Layout.fillWidth: true
                    title: "项目与提交"
                    footnote: "普通 Git 仓库可关闭 Hook 检查；仅影响本地提交检查，不禁用 Git 自身的 Hook。Gerrit 推送仍会检查 Change-Id。"
                    Text { text: "项目路径"; color: Theme.secondaryText; font.pixelSize: Theme.fontCaption }
                    AppTextField { font.pixelSize: Theme.fontBody; Layout.fillWidth: true; text: root.workspace.workspacePath; readOnly: true; placeholderText: "请通过添加项目导入" }
                    Hairline { }
                    FlatCheckBox {
                        text: "当前仓库提交时检查 Gerrit Change-Id Hook"
                        checked: root.workspace ? root.workspace.requireChangeIdHook : true
                        enabled: root.workspace && root.workspace.selectedPath.length > 0
                        onToggled: root.workspace.requireChangeIdHook = checked
                    }
                    Hairline { }
                    PrimaryButton {
                        Layout.fillWidth: true
                        kind: "tinted"
                        iconName: "refresh"
                        text: "重新载入项目仓库"
                        enabled: root.workspace.workspacePath.length > 0
                        onClicked: { root.workspace.scanWorkspace(); root.close() }
                    }
                }

                SettingsGroup {
                    Layout.fillWidth: true
                    title: "Gerrit SSH"
                    footnote: root.projectManager && root.projectManager.sshConfigExists
                        ? "本机 SSH 配置文件已存在；请检查 Gerrit 主机、账号与密钥是否正确。"
                        : "尚无 SSH 配置文件；点击按钮后会安全创建并打开。"
                    SettingRow {
                        label: "SSH 配置文件"
                        PrimaryButton {
                            text: "打开 ~/.ssh/config"
                            kind: "gray"
                            compact: true
                            iconName: "arrow-up-right"
                            enabled: root.projectManager && !root.projectManager.busy
                            onClicked: root.projectManager.openSshConfig()
                        }
                    }
                }

                SettingsGroup {
                    Layout.fillWidth: true
                    title: "AI 提交说明（兼容 OpenAI 风格接口）"
                    footnote: "生成时会将所选范围的代码差异发送至配置的 API；请确认有权发送。未配置时不生成。测试连接不上传代码。不记住密钥时可每次输入或使用 GERRITPILOT_AI_API_KEY 环境变量。"
                    AppTextField { id: aiEndpointField; font.pixelSize: Theme.fontBody
                        Layout.fillWidth: true
                        text: (root.workspace && root.workspace.commitAi && root.workspace.commitAi.apiEndpoint) ? root.workspace.commitAi.apiEndpoint : ""
                        placeholderText: "Base URL 或完整 /chat/completions 地址"
                        onEditingFinished: {
                            if (root.workspace && root.workspace.commitAi)
                                root.workspace.commitAi.apiEndpoint = text.trim()
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        AppTextField { id: aiKeyField; font.pixelSize: Theme.fontBody
                            Layout.fillWidth: true
                            echoMode: TextInput.Password
                            text: (root.workspace && root.workspace.commitAi && root.workspace.commitAi.apiKey) ? root.workspace.commitAi.apiKey : ""
                            placeholderText: "API Key (如 sk-...)"
                            onEditingFinished: {
                                if (root.workspace && root.workspace.commitAi)
                                    root.workspace.commitAi.apiKey = text.trim()
                            }
                        }
                        AppTextField { id: aiModelField; font.pixelSize: Theme.fontBody
                            Layout.preferredWidth: 160
                            text: (root.workspace && root.workspace.commitAi && root.workspace.commitAi.modelName) ? root.workspace.commitAi.modelName : ""
                            placeholderText: "模型名称"
                            onEditingFinished: {
                                if (root.workspace && root.workspace.commitAi)
                                    root.workspace.commitAi.modelName = text.trim()
                            }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "smart-commit v2.0：问题单 m- / 需求单 r-，单号须为 10 位且不能全为 0；修复提交必须双关联需求单。"
                        color: Theme.secondaryText
                        font.pixelSize: Theme.fontSecondary
                        wrapMode: Text.Wrap
                    }
                    FlatCheckBox {
                        text: "记住 API Key（明文保存在本机设置中）"
                        checked: root.workspace && root.workspace.commitAi ? root.workspace.commitAi.rememberApiKey : false
                        onToggled: if (root.workspace && root.workspace.commitAi) root.workspace.commitAi.rememberApiKey = checked
                    }
                    Hairline { }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        PrimaryButton {
                            text: root.workspace && root.workspace.commitAi && root.workspace.commitAi.testBusy
                                ? "正在测试…" : "测试连接"
                            kind: "gray"
                            compact: true
                            iconName: "link"
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
                            color: text.startsWith("连接成功") ? Theme.greenText : Theme.secondaryText
                            font.pixelSize: Theme.fontCaption
                            wrapMode: Text.Wrap
                        }
                    }
                }

                SettingsGroup {
                    visible: root.workspace.ignoredRepositories.length > 0
                    Layout.fillWidth: true
                    title: "已隐藏仓库"
                    Repeater {
                        model: root.workspace.ignoredRepositories
                        delegate: RowLayout {
                            id: hiddenRepositoryRow
                            required property string modelData
                            readonly property string repositoryPath: modelData
                            Layout.fillWidth: true
                            spacing: 8
                            Icon { name: "eye-off"; size: 14; color: Theme.tertiaryText }
                            Text {
                                text: hiddenRepositoryRow.repositoryPath
                                color: Theme.text
                                font.pixelSize: Theme.fontSecondary
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }
                            PrimaryButton {
                                text: "恢复"
                                kind: "plain"
                                compact: true
                                onClicked: root.workspace.setRepositoryIgnored(hiddenRepositoryRow.repositoryPath, false)
                            }
                        }
                    }
                }
            }
        }
    }
}
