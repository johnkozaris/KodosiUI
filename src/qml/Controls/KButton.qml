import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Button {
    id: root

    property string variant: "secondary"
    property string iconName: ""
    property bool iconTrailing: variant === "directional"
    property bool uppercase: variant === "primary" || variant === "directional"
    property bool compact: false
    property bool contentLeftAligned: false
    property bool showLeadingDot: false
    property color leadingDotColor: KodosiTheme.textTertiary
    property bool tonalSelection: false
    property color iconColor: label.color
    property string secondaryText: ""
    property real secondaryMaximumWidth: 160

    implicitHeight: compact
        ? KodosiTheme.compactControlHeight
        : KodosiTheme.controlHeight
    implicitWidth: Math.max(
        compact ? 34 : 54,
        contentRow.implicitWidth + leftPadding + rightPadding)
    leftPadding: compact ? 9 : 12
    rightPadding: compact ? 9 : 12
    topPadding: 6
    bottomPadding: 6
    hoverEnabled: true

    readonly property color intent: variant === "danger"
        ? KodosiTheme.danger
        : KodosiTheme.accent
    readonly property bool filled: variant === "primary"
        || variant === "directional"
        || variant === "danger"
    readonly property bool selected: checkable && checked

    contentItem: RowLayout {
        id: contentRow
        spacing: root.iconName.length > 0
            || root.showLeadingDot
            || root.secondaryText.length > 0
            ? 7
            : 0

        KIcon {
            visible: root.iconName.length > 0 && !root.iconTrailing
            Layout.preferredWidth: 15
            Layout.preferredHeight: 15
            name: root.iconName
            color: root.iconColor
        }

        Rectangle {
            Accessible.ignored: true
            visible: root.showLeadingDot
            Layout.preferredWidth: 7
            Layout.preferredHeight: 7
            radius: 4
            color: root.leadingDotColor
        }

        PlainLabel {
            id: label
            Layout.fillWidth: true
            text: root.uppercase ? root.text.toUpperCase() : root.text
            color: !root.enabled
                ? KodosiTheme.disabled
                : root.selected
                  ? (root.tonalSelection
                     ? KodosiTheme.textPrimary
                     : KodosiTheme.accent)
                  : root.filled
                  ? (root.variant === "danger"
                     ? KodosiTheme.dangerForeground
                     : KodosiTheme.accentForeground)
                  : root.variant === "quiet"
                    ? (root.hovered
                       ? KodosiTheme.textPrimary
                       : KodosiTheme.textSecondary)
                    : root.variant === "dangerQuiet"
                      ? KodosiTheme.danger
                      : KodosiTheme.textPrimary
            font.pixelSize: 11
            font.weight: Font.DemiBold
            font.letterSpacing: root.uppercase ? 1.0 : 0
            horizontalAlignment: root.contentLeftAligned
                ? Text.AlignLeft
                : Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        PlainLabel {
            visible: root.secondaryText.length > 0
            Layout.maximumWidth: root.secondaryMaximumWidth
            text: root.secondaryText
            color: root.enabled
                ? KodosiTheme.textSecondary
                : KodosiTheme.disabled
            font.pixelSize: 9
            horizontalAlignment: Text.AlignLeft
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideMiddle
        }

        KIcon {
            visible: root.iconName.length > 0 && root.iconTrailing
            Layout.preferredWidth: 14
            Layout.preferredHeight: 14
            name: root.iconName
            color: root.iconColor
        }
    }

    background: Rectangle {
        radius: KodosiTheme.radiusSmall
        color: {
            if (!root.enabled)
                return KodosiTheme.surfaceRaised
            if (root.selected)
                return root.pressed
                    ? KodosiTheme.surfaceElevated
                    : KodosiTheme.surfaceSelected
            if (root.filled) {
                if (root.pressed)
                    return root.variant === "danger"
                        ? KodosiTheme.dangerPressed
                        : KodosiTheme.accentPressed
                if (root.hovered)
                    return root.variant === "danger"
                        ? KodosiTheme.dangerHover
                        : KodosiTheme.accentHover
                return root.intent
            }
            if (root.pressed)
                return KodosiTheme.surfaceSelected
            if (root.hovered)
                return KodosiTheme.surfaceElevated
            return root.variant === "quiet" || root.variant === "dangerQuiet"
                ? "transparent"
                : KodosiTheme.surfaceRaised
        }
        border.width: root.activeFocus
            || (root.selected && !root.tonalSelection)
            || (!root.filled
            && root.variant !== "quiet" && root.variant !== "dangerQuiet") ? 1 : 0
        border.color: root.activeFocus
            ? KodosiTheme.focusRing
            : root.selected && !root.tonalSelection
              ? KodosiTheme.accentMuted
              : root.variant === "dangerQuiet"
              ? Qt.rgba(
                    KodosiTheme.danger.r,
                    KodosiTheme.danger.g,
                    KodosiTheme.danger.b,
                    0.45)
              : KodosiTheme.seam

        Behavior on color {
            ColorAnimation { duration: KodosiTheme.motionFast }
        }
    }

    transform: Translate { y: root.pressed && root.enabled ? 1 : 0 }
}
