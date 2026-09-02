import QtQuick
import QtQuick.Layouts
import Kodosi.Models 1.0 as Models

Rectangle {
    id: root

    objectName: "banner.desktopFile.error"
    Accessible.id: objectName
    visible: Models.DesktopFiles.errorCode !== Models.DesktopFiles.None
        && Models.DesktopFiles.errorPurpose
            === Models.DesktopFiles.SessionProject
    implicitHeight: visible ? Math.max(44, row.implicitHeight + 14) : 0
    color: KodosiTheme.surfaceElevated
    Accessible.name: qsTr("Open Project error")
    Accessible.description: Models.DesktopFiles.errorMessage

    RowLayout {
        id: row
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: KodosiTheme.spacing5
        anchors.rightMargin: KodosiTheme.spacing5
        spacing: KodosiTheme.spacing3

        KIcon {
            Layout.preferredWidth: 15
            Layout.preferredHeight: 15
            name: "warning"
            color: KodosiTheme.danger
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1

            PlainLabel {
                Layout.fillWidth: true
                text: qsTr("Kodosi could not open this session project.")
                color: KodosiTheme.textPrimary
                font.pixelSize: 11
                wrapMode: Text.Wrap
            }

            PlainLabel {
                Layout.fillWidth: true
                text: Models.DesktopFiles.errorMessage
                color: KodosiTheme.textSecondary
                font.pixelSize: 10
                elide: Text.ElideRight
            }
        }

        KIconButton {
            objectName: "banner.desktopFile.error.dismiss"
            Accessible.id: objectName
            glyph: "close"
            Accessible.name: qsTr("Dismiss Open Project error")
            onClicked: Models.DesktopFiles.clearError()
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: KodosiTheme.danger
        opacity: 0.5
    }
}
