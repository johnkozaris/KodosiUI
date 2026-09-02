import QtQuick

KButton {
    id: root

    required property string glyph
    property int size: KodosiTheme.iconButtonSize

    text: ""
    iconName: glyph
    variant: "quiet"
    compact: true
    implicitWidth: size
    implicitHeight: size
    leftPadding: 7
    rightPadding: 7
}
