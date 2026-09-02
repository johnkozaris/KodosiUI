pragma Singleton

import QtQuick

QtObject {
    readonly property color canvas: "#100d0b"
    readonly property color surface: "#1d1714"
    readonly property color surfaceRaised: "#241c18"
    readonly property color surfaceElevated: "#2b211c"
    readonly property color surfaceSelected: "#3a281f"
    readonly property color terminal: "#0a0807"
    readonly property color seam: "#40332c"
    readonly property color seamStrong: "#57443a"
    readonly property color textPrimary: "#f1e9e3"
    readonly property color textSecondary: "#ad9a8e"
    readonly property color textTertiary: "#806e64"
    readonly property color accent: "#db8a62"
    readonly property color accentHover: "#e69a72"
    readonly property color accentPressed: "#c97752"
    readonly property color accentMuted: "#5a3527"
    readonly property color accentForeground: "#160e0a"
    readonly property color success: "#72a687"
    readonly property color warning: "#d2aa60"
    readonly property color danger: "#df6c68"
    readonly property color dangerHover: "#e87773"
    readonly property color dangerPressed: "#c25e5a"
    readonly property color dangerForeground: "#160e0a"
    readonly property color reconnecting: "#629bb8"
    readonly property color focusRing: "#ef9d75"
    readonly property color overlayDim: "#b3100d0b"
    readonly property color shadow: "#b8000000"
    readonly property color input: "#17120f"
    readonly property color placeholderText: "#948178"
    readonly property color disabled: "#5f5149"

    readonly property int spacing1: 3
    readonly property int spacing2: 6
    readonly property int spacing3: 8
    readonly property int spacing4: 10
    readonly property int spacing5: 12
    readonly property int spacing6: 16
    readonly property int spacing7: 24

    readonly property real radiusSmall: 8
    readonly property real radiusMedium: 10
    readonly property real radiusLarge: 12
    readonly property real radiusModal: 16
    readonly property real headerHeight: 56
    readonly property real sidebarWidth: 272
    readonly property real controlHeight: 34
    readonly property real compactControlHeight: 30
    readonly property real iconButtonSize: 32
    readonly property int motionFast: 150
    readonly property int motionNormal: 200
}
