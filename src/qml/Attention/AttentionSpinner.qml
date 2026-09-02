pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

Item {
    id: root

    property bool running: true

    implicitWidth: dots.implicitWidth
    implicitHeight: 6

    RowLayout {
        id: dots
        anchors.centerIn: parent
        spacing: KodosiTheme.spacing1

        Repeater {
            model: 3

            delegate: Rectangle {
                id: dot
                required property int index

                Layout.preferredWidth: 4
                Layout.preferredHeight: 4
                radius: 2
                color: KodosiTheme.accent
                opacity: 0.32

                SequentialAnimation on opacity {
                    running: root.running
                    loops: Animation.Infinite

                    PauseAnimation { duration: dot.index * 120 }
                    NumberAnimation {
                        from: 0.32
                        to: 1
                        duration: KodosiTheme.motionFast
                    }
                    NumberAnimation {
                        from: 1
                        to: 0.32
                        duration: KodosiTheme.motionFast
                    }
                    PauseAnimation { duration: (2 - dot.index) * 120 }
                }
            }
        }
    }
}
