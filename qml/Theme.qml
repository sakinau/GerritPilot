pragma Singleton
import QtQuick
import QtCore

// Design tokens. Colors follow the iOS / macOS system palette (light appearance);
// components read semantic tokens here instead of hard-coding hex values.
Item {
    property alias codeFontPointSize: typographySettings.codeFontPointSize
    property alias uiScalePercent: typographySettings.uiScalePercent
    Settings {
        id: typographySettings
        category: "Appearance"
        property int codeFontPointSize: 11
        property int uiScalePercent: 100
    }

    // Backgrounds: the window canvas doubles as the sidebar material, content
    // floats on white cards above it.
    readonly property color canvas: "#EEEEF3"
    readonly property color sidebar: "#EEEEF3"
    readonly property color surface: "#FFFFFF"
    readonly property color surfaceStrong: "#FFFFFF"
    readonly property color surfaceMuted: "#F5F5F7"
    readonly property color surfaceGrouped: "#F2F2F7"
    readonly property color toolbar: "#FBFBFD"
    readonly property color sidebarCard: "#B3FFFFFF"

    readonly property color separator: "#D1D1D6"
    readonly property color separatorSoft: "#E5E5EA"
    readonly property color scrollThumb: "#AEAEB2"
    readonly property color scrollThumbActive: "#8E8E93"

    // Translucent fills adapt to whatever surface they sit on (iOS systemFill family).
    readonly property color fillPrimary: "#33787880"
    readonly property color fillSecondary: "#29787880"
    readonly property color fillTertiary: "#1F767680"
    readonly property color fillQuaternary: "#14747480"
    readonly property color hoverFill: fillQuaternary
    readonly property color pressedFill: fillSecondary

    readonly property color text: "#1D1D1F"
    readonly property color secondaryText: "#6E6E73"
    readonly property color tertiaryText: "#8E8E93"
    readonly property color quaternaryText: "#AEAEB2"
    readonly property color placeholder: "#A1A1A6"
    readonly property color textOnAccent: "#FFFFFF"

    readonly property color accent: "#007AFF"
    readonly property color accentHover: "#1A88FF"
    readonly property color accentPressed: "#0062CC"
    readonly property color accentSoft: "#E5F0FF"
    readonly property color accentTint: "#1F007AFF"
    readonly property color focusRing: "#80007AFF"
    readonly property color green: "#34C759"
    readonly property color greenText: "#248A3D"
    readonly property color greenSoft: "#E6F7EA"
    readonly property color orange: "#FF9500"
    readonly property color orangeText: "#C93400"
    readonly property color orangeSoft: "#FFF2DE"
    readonly property color red: "#FF3B30"
    readonly property color redText: "#D70015"
    readonly property color redHover: "#FF5249"
    readonly property color redPressed: "#D9302A"
    readonly property color redSoft: "#FFE9E7"
    readonly property color purple: "#AF52DE"
    readonly property color purpleText: "#8944AB"
    readonly property color purpleSoft: "#F5E9FB"
    readonly property color indigo: "#5856D6"
    readonly property color teal: "#30B0C7"
    readonly property color shadow: "#16000000"
    readonly property color scrim: "#33000000"

    // Banner tones: background, border and foreground per semantic meaning.
    readonly property var tones: ({
        info:    { background: "#EBF4FF", border: "#C7DEFF", foreground: "#0058C7", icon: "info" },
        warning: { background: "#FFF6E6", border: "#FFDDA6", foreground: "#9A5300", icon: "alert-triangle" },
        danger:  { background: "#FFEFEE", border: "#FFCBC7", foreground: "#C1261C", icon: "alert-circle" },
        success: { background: "#EBF8EF", border: "#BFE8CB", foreground: "#1E7A37", icon: "check-circle" },
        neutral: { background: "#F5F5F7", border: "#E5E5EA", foreground: "#6E6E73", icon: "info" }
    })

    // Diff palette (opaque values: they are injected into rich text).
    readonly property string diffAddBackground: "#E8F8EC"
    readonly property string diffAddBackgroundActive: "#D0F0D9"
    readonly property string diffAddText: "#17692F"
    readonly property string diffRemoveBackground: "#FFEDEF"
    readonly property string diffRemoveBackgroundActive: "#FFD6DA"
    readonly property string diffRemoveText: "#B4232C"
    readonly property string diffHunkBackground: "#EEF4FF"
    readonly property string diffHunkText: "#0058C7"
    readonly property string diffHunkActiveBackground: "#007AFF"
    readonly property string diffHunkActiveNumber: "#CFE3FF"
    readonly property string diffContextText: "#1D1D1F"
    readonly property string diffContextActive: "#F4F6FA"
    readonly property string diffMetaText: "#6E6E73"
    readonly property string diffLineNumber: "#AEAEB2"
    readonly property string diffSearchHighlight: "#FFE38A"

    readonly property int controlHeight: 32
    readonly property int controlHeightSmall: 26
    readonly property int controlRadius: 8
    readonly property int panePadding: 12

    readonly property int radiusSmall: 6
    readonly property int radiusMedium: 10
    readonly property int radiusLarge: 12
    readonly property int radiusXLarge: 16

    readonly property int spacingXS: 4
    readonly property int spacingS: 8
    readonly property int spacingM: 12
    readonly property int spacingL: 16
    readonly property int spacingXL: 24

    readonly property int animationFast: 120
    readonly property int animationNormal: 200

    // UI text uses pixels consistently. Code zoom and icon glyphs are separate.
    readonly property string monoFontFamily: "DejaVu Sans Mono"
    readonly property int fontCaption: 11
    readonly property int fontSecondary: 12
    readonly property int fontBody: 13
    readonly property int fontSubheading: 14
    readonly property int fontHeading: 16
    readonly property int fontTitle: 18
    readonly property int fontLargeTitle: 22

    // Stable per-name avatar tint (author initials, project tiles).
    readonly property var avatarPalette: ["#007AFF", "#34C759", "#FF9500", "#AF52DE", "#FF2D55", "#5856D6", "#30B0C7", "#A2845E"]
    function avatarColor(name) {
        let hash = 0
        const value = String(name || "")
        for (let i = 0; i < value.length; ++i)
            hash = (hash * 31 + value.charCodeAt(i)) | 0
        return avatarPalette[Math.abs(hash) % avatarPalette.length]
    }
}
