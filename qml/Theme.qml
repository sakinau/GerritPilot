pragma Singleton
import QtQuick
import QtCore

Item {
    property alias codeFontPointSize: typographySettings.codeFontPointSize
    property alias uiScalePercent: typographySettings.uiScalePercent
    Settings {
        id: typographySettings
        category: "Appearance"
        property int codeFontPointSize: 11
        property int uiScalePercent: 100
    }
    readonly property color canvas: "#F5F5F7"
    readonly property color sidebar: "#F0F1F4"
    readonly property color surface: "#FFFFFF"
    readonly property color surfaceStrong: "#FFFFFF"
    readonly property color surfaceMuted: "#F2F2F7"
    readonly property color separator: "#D8DAE0"
    readonly property color separatorSoft: "#E8E9ED"
    readonly property color scrollThumb: "#A3A6AF"
    readonly property color scrollThumbActive: "#737782"
    readonly property int controlHeight: 34
    readonly property int controlRadius: 8
    readonly property int panePadding: 12
    readonly property color text: "#1C1C1E"
    readonly property color secondaryText: "#636366"
    readonly property color tertiaryText: "#8E8E93"
    readonly property color placeholder: "#8E8E93"
    readonly property color accent: "#0A84FF"
    readonly property color accentSoft: "#E3F1FF"
    readonly property color green: "#30B36B"
    readonly property color greenSoft: "#E5F7ED"
    readonly property color orange: "#F59E0B"
    readonly property color orangeSoft: "#FFF3D6"
    readonly property color red: "#FF453A"
    readonly property color redSoft: "#FFE8E6"
    readonly property color purple: "#7957D5"
    readonly property color purpleSoft: "#EEE9FB"
    readonly property color shadow: "#16000000"

    readonly property int radiusSmall: 8
    readonly property int radiusMedium: 12
    readonly property int radiusLarge: 14
    readonly property int radiusXLarge: 18

    // UI text uses pixels consistently. Code zoom and icon glyphs are separate.
    readonly property string monoFontFamily: "DejaVu Sans Mono"
    readonly property int fontCaption: 12
    readonly property int fontSecondary: 12
    readonly property int fontBody: 13
    readonly property int fontSubheading: 14
    readonly property int fontHeading: 16
    readonly property int fontTitle: 18
}
