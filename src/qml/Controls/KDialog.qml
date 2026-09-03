import QtQuick
import QtQuick.Controls

Dialog {
    id: root

    modal: true
    padding: 18
    palette.window: KodosiTheme.surfaceRaised
    palette.windowText: KodosiTheme.textPrimary
    palette.base: KodosiTheme.input
    palette.text: KodosiTheme.textPrimary
    palette.button: KodosiTheme.surfaceRaised
    palette.buttonText: KodosiTheme.textPrimary
    palette.highlight: KodosiTheme.accent
    palette.highlightedText: KodosiTheme.accentForeground

    Overlay.modal: Rectangle {
        color: KodosiTheme.overlayDim
    }

    background: Rectangle {
        color: KodosiTheme.surfaceRaised
        radius: KodosiTheme.radiusModal
    }

    header: Rectangle {
        visible: root.title.length > 0
        implicitHeight: visible ? 54 : 0
        color: KodosiTheme.surface

        PlainLabel {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 18
            anchors.rightMargin: 18
            text: root.title
            color: KodosiTheme.textPrimary
            font.pixelSize: 16
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: KodosiTheme.seam
        }
    }
}
