import QtQuick
import QtQuick.Controls

Slider {
    id: root

    implicitHeight: 28

    background: Rectangle {
        x: root.leftPadding
        y: Math.round(root.topPadding + root.availableHeight / 2 - height / 2)
        width: root.availableWidth
        height: 4
        radius: 2
        color: KodosiTheme.controlBorder

        Rectangle {
            width: root.visualPosition * parent.width
            height: parent.height
            radius: parent.radius
            color: root.enabled
                ? KodosiTheme.accent
                : KodosiTheme.disabled
        }
    }

    handle: Rectangle {
        x: root.leftPadding
            + root.visualPosition * (root.availableWidth - width)
        y: Math.round(root.topPadding + root.availableHeight / 2 - height / 2)
        implicitWidth: 18
        implicitHeight: 18
        radius: 9
        color: root.pressed
            ? KodosiTheme.accentPressed
            : KodosiTheme.accent
    }
}
