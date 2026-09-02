import QtQuick

TextEdit {
    id: root

    textFormat: Text.PlainText
    readOnly: true
    selectByMouse: true
    selectByKeyboard: true
    wrapMode: Text.Wrap
    color: KodosiTheme.textPrimary
    selectionColor: KodosiTheme.accent
    selectedTextColor: KodosiTheme.accentForeground
}
