import QtQuick
import QtQuick.Controls

Switch {
    id: root

    spacing: 10
    implicitHeight: Math.max(30, indicator.implicitHeight)

    indicator: Rectangle {
        implicitWidth: 42
        implicitHeight: 24
        x: root.leftPadding
        y: Math.round((root.height - height) / 2)
        radius: 12
        color: !root.enabled
            ? KodosiTheme.surfaceRaised
            : root.checked
              ? KodosiTheme.accent
              : KodosiTheme.input
        border.width: 1
        border.color: root.activeFocus
            ? KodosiTheme.focusRing
            : root.checked
              ? KodosiTheme.accent
              : KodosiTheme.seamStrong

        Rectangle {
            x: root.checked ? parent.width - width - 3 : 3
            y: 3
            width: 18
            height: 18
            radius: 9
            color: root.checked
                ? KodosiTheme.accentForeground
                : KodosiTheme.textSecondary

            Behavior on x {
                NumberAnimation {
                    duration: KodosiTheme.motionFast
                    easing.type: Easing.OutCubic
                }
            }
        }
    }

    contentItem: PlainLabel {
        text: root.text
        color: root.enabled
            ? KodosiTheme.textPrimary
            : KodosiTheme.disabled
        font.pixelSize: 12
        verticalAlignment: Text.AlignVCenter
        leftPadding: root.indicator.width + root.spacing
    }
}
