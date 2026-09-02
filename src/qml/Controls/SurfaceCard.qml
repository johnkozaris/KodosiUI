import QtQuick
import QtQuick.Controls

Control {
    id: root

    property alias title: titleLabel.text
    property alias detail: detailLabel.text

    Accessible.name: title

    contentItem: Column {
        spacing: KodosiTheme.spacing2

        Label {
            id: titleLabel
            color: KodosiTheme.textPrimary
            font.pixelSize: 16
            font.weight: Font.DemiBold
            wrapMode: Text.Wrap
        }

        Label {
            id: detailLabel
            color: KodosiTheme.textSecondary
            font.pixelSize: 13
            wrapMode: Text.Wrap
        }
    }

    background: Rectangle {
        color: KodosiTheme.surface
        radius: KodosiTheme.radiusMedium
        border.color: KodosiTheme.seam
        border.width: 1
    }

    padding: KodosiTheme.spacing4
}
