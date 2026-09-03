import QtQuick
import QtQuick.Controls

TextArea {
    id: root

    property int maximumLength: -1

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

    onTextChanged: {
        if (maximumLength >= 0 && length > maximumLength)
            remove(maximumLength, length)
    }

    background: Rectangle {
        color: KodosiTheme.input
        radius: KodosiTheme.radiusSmall
    }
}
