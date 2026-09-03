import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Control {
    id: root

    required property string text
    property color tone: KodosiTheme.textSecondary

    Accessible.name: text

    implicitWidth: contentRow.implicitWidth + 14
    implicitHeight: 24

    contentItem: RowLayout {
        id: contentRow
        anchors.centerIn: parent
        spacing: 6

        Rectangle {
            Layout.preferredWidth: 6
            Layout.preferredHeight: 6
            radius: 3
            color: root.tone
        }

        Label {
            text: root.text
            color: KodosiTheme.textSecondary
            font.pixelSize: 10
            font.weight: Font.DemiBold
        }
    }

    background: Rectangle {
        radius: KodosiTheme.radiusSmall
        color: KodosiTheme.surfaceElevated
    }
}
