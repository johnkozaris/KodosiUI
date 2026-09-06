pragma Singleton
import QtQuick

QtObject {
    readonly property int fontBody: 13
    readonly property int fontCaption: 11
    readonly property int controlHeight: 34
    readonly property int compactControlHeight: 30
    readonly property int spacing2: 6
    readonly property int radiusSmall: 8
    readonly property int motionFast: 0
    readonly property color accent: "#db8a62"
    readonly property color accentHover: "#e69a72"
    readonly property color accentPressed: "#c97752"
    readonly property color accentForeground: "#160e0a"
    readonly property color danger: "#e47773"
    readonly property color dangerHover: danger
    readonly property color dangerPressed: danger
    readonly property color dangerForeground: accentForeground
    readonly property color textPrimary: "#f1e9e3"
    readonly property color textSecondary: "#ad9a8e"
    readonly property color textTertiary: textSecondary
    readonly property color disabled: "#5f5149"
    readonly property color placeholderText: textSecondary
    readonly property color input: "#181210"
    readonly property color surface: "#1d1714"
    readonly property color surfaceRaised: "#241c18"
    readonly property color surfaceElevated: "#2b211c"
    readonly property color surfaceSelected: "#3a281f"
    readonly property color focusRing: "#ef9d75"
}
