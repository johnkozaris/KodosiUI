import QtQuick
import QtQuick.Controls

Control {
    id: root

    property bool running: true
    implicitWidth: 26
    implicitHeight: 26
    visible: running

    contentItem: Item {
        Rectangle {
            anchors.centerIn: parent
            width: 18
            height: 18
            radius: 9
            color: "transparent"
            border.width: 2
            border.color: KodosiTheme.seamStrong
        }

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 3
            width: 4
            height: 4
            radius: 2
            color: KodosiTheme.accent
        }

        RotationAnimator on rotation {
            from: 0
            to: 360
            duration: 900
            loops: Animation.Infinite
            running: root.running
        }
    }
}
