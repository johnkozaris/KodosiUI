import QtQuick
import QtQuick.Controls

Menu {
    id: root

    margins: 8
    padding: 6

    background: Rectangle {
        implicitWidth: 190
        color: KodosiTheme.surfaceRaised
        radius: KodosiTheme.radiusLarge
        border.width: 1
        border.color: KodosiTheme.seamStrong

        Rectangle {
            anchors.fill: parent
            anchors.topMargin: 5
            anchors.leftMargin: 4
            z: -1
            radius: parent.radius
            color: KodosiTheme.shadow
            opacity: 0.35
        }
    }
}
