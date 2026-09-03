import QtQuick
import QtQuick.Controls

MenuItem {
    id: root

    implicitHeight: 36
    leftPadding: 12
    rightPadding: 12

    contentItem: PlainLabel {
        text: root.text
        color: !root.enabled
            ? KodosiTheme.disabled
            : root.highlighted
              ? KodosiTheme.textPrimary
              : KodosiTheme.textSecondary
        font.pixelSize: 11
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        color: root.highlighted
            ? KodosiTheme.surfaceSelected
            : KodosiTheme.surface
        radius: KodosiTheme.radiusSmall
    }
}
