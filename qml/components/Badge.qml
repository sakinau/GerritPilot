import QtQuick
import QtQuick.Layouts
import GerritPilot

// Capsule label. `inverted` renders on accent-filled rows (selected list items).
Rectangle {
    id: root
    property string text: ""
    property string iconName: ""
    property string tone: "neutral"
    property bool inverted: false
    property bool mono: false

    readonly property var palette: ({
        neutral: [Theme.fillTertiary, Theme.secondaryText],
        accent: [Theme.accentTint, Theme.accent],
        green: [Theme.greenSoft, Theme.greenText],
        orange: [Theme.orangeSoft, Theme.orangeText],
        red: [Theme.redSoft, Theme.redText],
        purple: [Theme.purpleSoft, Theme.purpleText]
    })
    readonly property var colors: palette[tone] || palette.neutral

    implicitHeight: 18
    implicitWidth: Math.max(implicitHeight, row.implicitWidth + 12)
    radius: height / 2
    color: inverted ? "#33FFFFFF" : colors[0]

    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: 3
        Icon {
            visible: root.iconName.length > 0
            name: root.iconName
            size: 10
            strokeWidth: 2
            color: label.color
        }
        Text {
            id: label
            text: root.text
            color: root.inverted ? Theme.textOnAccent : root.colors[1]
            font.pixelSize: Theme.fontCaption
            font.weight: Font.DemiBold
            font.family: root.mono ? Theme.monoFontFamily : Qt.application.font.family
        }
    }
}
