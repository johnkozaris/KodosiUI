import QtQuick
import QtQuick.Controls

TextArea {
    id: root

    leftPadding: 11
    rightPadding: 11
    topPadding: 9
    bottomPadding: 9
    color: enabled ? KodosiTheme.textPrimary : KodosiTheme.disabled
    placeholderTextColor: KodosiTheme.placeholderText
    selectionColor: KodosiTheme.accent
    selectedTextColor: KodosiTheme.accentForeground
    font.pixelSize: 12
    selectByMouse: true

    background: Rectangle {
        color: KodosiTheme.input
        radius: KodosiTheme.radiusSmall
        border.width: 1
        border.color: root.activeFocus
            ? KodosiTheme.focusRing
            : KodosiTheme.seam
    }
}
