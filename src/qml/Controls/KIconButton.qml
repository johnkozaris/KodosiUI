import QtQuick

KButton {
    id: root

    required property string glyph
    property int size: KodosiTheme.iconButtonSize
    property color glyphColor: KodosiTheme.textSecondary

    text: ""
    iconName: glyph
    iconColor: enabled ? glyphColor : KodosiTheme.disabled
    variant: "quiet"
    compact: true
    implicitWidth: size
    implicitHeight: size
    leftPadding: 7
    rightPadding: 7
}
