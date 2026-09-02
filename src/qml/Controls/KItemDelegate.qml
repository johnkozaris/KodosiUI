import QtQuick
import QtQuick.Controls

ItemDelegate {
    id: root

    hoverEnabled: true
    leftPadding: 10
    rightPadding: 10

    background: Rectangle {
        color: root.down
            ? KodosiTheme.surfaceSelected
            : root.highlighted || root.checked
              ? KodosiTheme.surfaceSelected
              : root.hovered
                ? KodosiTheme.surfaceElevated
                : "transparent"
        radius: KodosiTheme.radiusSmall
        border.width: root.activeFocus ? 1 : 0
        border.color: KodosiTheme.focusRing
    }
}
