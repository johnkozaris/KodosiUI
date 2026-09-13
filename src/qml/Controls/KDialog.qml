import Kodosi 1.0
import QtQuick
import QtQuick.Controls

Dialog {
    id: root

    modal: true
    padding: 18
    palette.base: KodosiTheme.input
    palette.button: KodosiTheme.surfaceRaised
    palette.buttonText: KodosiTheme.textPrimary
    palette.highlight: KodosiTheme.accent
    palette.highlightedText: KodosiTheme.accentForeground
    palette.text: KodosiTheme.textPrimary
    palette.window: KodosiTheme.surfaceRaised
    palette.windowText: KodosiTheme.textPrimary
    parent: Overlay.overlay
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? (parent.height - height) / 2 : 0

    Overlay.modal: Rectangle {
        color: KodosiTheme.overlayDim
    }
    background: Rectangle {
        color: KodosiTheme.surfaceRaised
        radius: KodosiTheme.radiusModal
    }
    header: Rectangle {
        color: KodosiTheme.surface
        implicitHeight: visible ? 54 : 0
        visible: root.title.length > 0

        PlainLabel {
            anchors.left: parent.left
            anchors.leftMargin: 18
            anchors.right: parent.right
            anchors.rightMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            color: KodosiTheme.textPrimary
            elide: Text.ElideRight
            font.pixelSize: 16
            font.weight: Font.DemiBold
            text: root.title
        }
        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            color: KodosiTheme.seam
            height: 1
        }
    }
}
