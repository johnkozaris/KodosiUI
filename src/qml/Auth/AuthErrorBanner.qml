import QtQuick
import QtQuick.Layouts
import Kodosi.Models 1.0 as Models

Rectangle {
    id: root

    signal openAccountRequested()

    readonly property bool identityResetError:
        Models.AuthActions.failedOperation === "identity.reset"
    readonly property bool recoveryNotice:
        Models.AuthState.recoveryRequired
        && Models.AuthState.recoveryMessage.length > 0

    objectName: "auth.error.banner"
    Accessible.id: objectName
    visible: Models.AuthState.signedIn
        && (root.recoveryNotice
            || Models.AuthActions.lastError.length > 0)
    implicitHeight: visible ? 38 : 0
    color: KodosiTheme.surfaceElevated

    Accessible.name: qsTr("Authentication error")
    Accessible.description: root.recoveryNotice
        ? Models.AuthState.recoveryMessage
        : Models.AuthActions.lastError

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: KodosiTheme.spacing5
        anchors.rightMargin: KodosiTheme.spacing5
        spacing: KodosiTheme.spacing3

        Rectangle {
            Layout.preferredWidth: 7
            Layout.preferredHeight: 7
            radius: 4
            color: KodosiTheme.danger
        }

        PlainLabel {
            Layout.fillWidth: true
            text: root.recoveryNotice
                ? Models.AuthState.recoveryMessage
                : Models.AuthActions.lastError
            color: KodosiTheme.textPrimary
            font.pixelSize: 11
            elide: Text.ElideRight
        }

        KButton {
            objectName: "auth.error.retry"
            Accessible.id: objectName
            text: root.recoveryNotice || root.identityResetError
                ? qsTr("Review recovery")
                : qsTr("Retry")
            Accessible.name: text
            onClicked: {
                if (root.recoveryNotice) {
                    Models.AuthState.clearRecoveryMessage()
                    root.openAccountRequested()
                } else if (root.identityResetError) {
                    root.openAccountRequested()
                } else {
                    Models.AuthActions.retry()
                }
            }
        }

        KButton {
            objectName: "auth.error.dismiss"
            Accessible.id: objectName
            text: qsTr("Dismiss")
            Accessible.name: text
            onClicked: {
                if (root.recoveryNotice)
                    Models.AuthState.clearRecoveryMessage()
                else
                    Models.AuthActions.clearError()
            }
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
