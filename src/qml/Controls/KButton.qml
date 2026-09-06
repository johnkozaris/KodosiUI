import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Button {
    id: root

    property string variant: "secondary"
    property string iconName: ""
    property bool iconTrailing: variant === "directional"
    property bool uppercase: false
    property bool compact: false
    property bool contentLeftAligned: false
    property bool showLeadingDot: false
    property color leadingDotColor: KodosiTheme.textTertiary
    property bool tonalSelection: false
    property color iconColor: label.color
    property string secondaryText: ""
    property real secondaryMaximumWidth: 160
    Accessible.description: secondaryText
    activeFocusOnTab: true

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
            ? 7
            : 0

        Item {
            visible: !root.contentLeftAligned
            Layout.fillWidth: true
        }

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
            Layout.fillWidth: root.contentLeftAligned
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
            font.pixelSize: KodosiTheme.fontBody
            font.weight: Font.Medium
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
            Layout.leftMargin: KodosiTheme.spacing2
            text: root.secondaryText
            color: root.enabled
                ? KodosiTheme.textSecondary
                : KodosiTheme.disabled
            font.pixelSize: KodosiTheme.fontCaption
            elide: Text.ElideRight
        }

        KIcon {
            visible: root.iconName.length > 0 && root.iconTrailing
            Layout.preferredWidth: 14
            Layout.preferredHeight: 14
            name: root.iconName
            color: root.iconColor
        }

        Item {
            visible: !root.contentLeftAligned
            Layout.fillWidth: true
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
                ? KodosiTheme.surface
                : KodosiTheme.surfaceRaised
        }

        Behavior on color {
            ColorAnimation { duration: KodosiTheme.motionFast }
        }
    }

    KFocusIndicator {
        objectName: "control.keyboardFocus"
        Accessible.id: objectName
        active: root.activeFocus && root.enabled
        color: root.filled ? KodosiTheme.accentForeground : KodosiTheme.focusRing
    }

    transform: Translate { y: root.pressed && root.enabled ? 1 : 0 }
}
