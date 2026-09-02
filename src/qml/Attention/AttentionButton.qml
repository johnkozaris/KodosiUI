pragma ComponentBehavior: Bound

import QtQuick

KButton {
    id: root

    required property string accessibleId
    property bool primary: false
    property color intentColor: KodosiTheme.accent
    property color foregroundColor: primary
        ? KodosiTheme.accentForeground
        : KodosiTheme.textPrimary

    objectName: accessibleId
    Accessible.id: objectName
    implicitHeight: 28
    leftPadding: KodosiTheme.spacing3
    rightPadding: KodosiTheme.spacing3
    topPadding: KodosiTheme.spacing1
    bottomPadding: KodosiTheme.spacing1

    contentItem: PlainLabel {
        text: root.text
        color: root.enabled
            ? root.foregroundColor
            : KodosiTheme.textSecondary
        font.pixelSize: 11
        font.weight: Font.DemiBold
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: KodosiTheme.radiusSmall
        color: !root.enabled
            ? KodosiTheme.surface
            : root.primary
            ? root.pressed
                ? Qt.rgba(
                    root.intentColor.r,
                    root.intentColor.g,
                    root.intentColor.b,
                    0.82)
                : root.intentColor
            : root.pressed
            ? Qt.rgba(
                root.intentColor.r,
                root.intentColor.g,
                root.intentColor.b,
                0.16)
            : root.hovered
            ? Qt.rgba(
                root.intentColor.r,
                root.intentColor.g,
                root.intentColor.b,
                0.10)
            : Qt.rgba(
                KodosiTheme.surface.r,
                KodosiTheme.surface.g,
                KodosiTheme.surface.b,
                0)
        border.width: root.activeFocus || !root.primary || !root.enabled ? 1 : 0
        border.color: root.activeFocus
            ? KodosiTheme.focusRing
            : KodosiTheme.seam

        Behavior on color {
            ColorAnimation { duration: KodosiTheme.motionFast }
        }
    }
}
