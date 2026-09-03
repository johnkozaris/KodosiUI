pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Kodosi.Models 1.0 as Models

Item {
    id: root

    property string accessibleId: "auth.gate"
    property string title: qsTr("Sign in to Kodosi")
    property string detail: qsTr(
        "Sign in to create Missions, invite people, link devices, and share supervision.")
    signal signInRequested()

    objectName: accessibleId
    Accessible.id: objectName
    Accessible.role: Accessible.Pane
    Accessible.name: title

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(460, parent.width - 48)
        spacing: KodosiTheme.spacing5

        KIcon {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 34
            Layout.preferredHeight: 34
            name: "account"
            color: KodosiTheme.accent
            strokeWidth: 1.9
        }

        PlainLabel {
            Layout.fillWidth: true
            text: root.title
            color: KodosiTheme.textPrimary
            font.pixelSize: 20
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
        }

        PlainLabel {
            Layout.fillWidth: true
            text: root.detail
            color: KodosiTheme.textSecondary
            font.pixelSize: 12
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
        }

        KButton {
            objectName: root.accessibleId + ".signIn"
            Accessible.id: objectName
            Layout.alignment: Qt.AlignHCenter
            variant: "directional"
            iconName: "chevron-right"
            text: Models.AuthActions.busy
                ? qsTr("Signing in")
                : qsTr("Sign in to Kodosi")
            enabled: !Models.AuthActions.busy
            Accessible.name: text
            onClicked: root.signInRequested()
        }

        PlainLabel {
            visible: Models.AuthActions.lastError.length > 0
            Layout.fillWidth: true
            text: Models.AuthActions.lastError
            color: KodosiTheme.danger
            font.pixelSize: 10
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            Accessible.name: text
        }
    }
}
