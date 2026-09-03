import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root

    default property alias content: row.data
    implicitWidth: row.implicitWidth + 4
    implicitHeight: row.implicitHeight + 4
    radius: KodosiTheme.radiusMedium
    color: KodosiTheme.surfaceRaised

    RowLayout {
        id: row
        anchors.fill: parent
        anchors.margins: 2
        spacing: 2
    }
}
