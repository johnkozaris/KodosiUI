import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kodosi.Models 1.0 as Models

KPopover {
    id: root

    signal accountRecoveryRequested()

    property bool accountRecoverySurfaceOpen: false
    property bool suppressed: false

    readonly property bool hasCode:
        Models.AuthState.userCode.length > 0
    readonly property bool identityReset:
        Models.AuthActions.failedOperation === "identity.reset"
    readonly property bool failed:
        Models.AuthState.phase === Models.AuthState.Error
        || Models.AuthActions.lastError.length > 0
    readonly property string transitionTitle:
        identityReset && failed
        ? qsTr("Identity reset needs attention")
        : identityReset
          ? qsTr("Resetting identity")
          : failed
            ? qsTr("Sign-in needs attention")
            : hasCode
              ? qsTr("Approve in your browser")
              : qsTr("Preparing secure sign-in")
    readonly property string transitionDetail:
        identityReset && failed
        ? Models.AuthActions.lastError
        : identityReset
          ? qsTr(
                "Clearing the local keypair, linked devices, and friend trust pins.")
          : failed
            ? (Models.AuthActions.lastError.length > 0
               ? Models.AuthActions.lastError
               : Models.AuthState.notice)
            : hasCode
              ? qsTr(
                    "Choose the account you want to use. This window updates automatically.")
              : qsTr(
                    "Kodosi is requesting a trusted browser code from the local runtime.")

    objectName: "auth.transition"
    parent: Overlay.overlay
    x: 0
    y: 0
    width: parent ? parent.width : 0
    height: parent ? parent.height : 0
    visible: !root.suppressed
        && !Models.AuthState.signedIn
        && (Models.AuthActions.busy || hasCode || failed)
        && !accountRecoverySurfaceOpen
    modal: true
    focus: true
    closePolicy: Popup.NoAutoClose
    padding: 0

    Overlay.modal: Rectangle {
        color: KodosiTheme.overlayDim
    }

    background: Rectangle {
        color: "transparent"
    }

    contentItem: FocusScope {
        id: transitionScope
        objectName: "auth.transition"
        Accessible.id: objectName
        focus: true
        Accessible.role: Accessible.Dialog
        Accessible.name: root.transitionTitle

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(560, parent.width - 48)
            height: content.implicitHeight + 48
            radius: KodosiTheme.radiusMedium
            color: KodosiTheme.surface
            border.width: 1
            border.color: root.failed ? KodosiTheme.danger : KodosiTheme.seam

            ColumnLayout {
                id: content
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 24
                spacing: KodosiTheme.spacing5

                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 42
                    Layout.preferredHeight: 42
                    radius: KodosiTheme.radiusSmall
                    color: KodosiTheme.canvas
                    border.width: 1
                    border.color: KodosiTheme.seam

                    KIcon {
                        anchors.centerIn: parent
                        width: 22
                        height: 22
                        name: root.failed ? "warning" : "command"
                        color: root.failed ? KodosiTheme.danger : KodosiTheme.accent
                        strokeWidth: 2
                    }
                }

                PlainLabel {
                    Layout.fillWidth: true
                    text: root.transitionTitle
                    color: KodosiTheme.textPrimary
                    font.pixelSize: 19
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                }

                PlainLabel {
                    Layout.fillWidth: true
                    text: root.transitionDetail
                    color: root.failed
                        ? KodosiTheme.danger
                        : KodosiTheme.textSecondary
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                }

                KBusyIndicator {
                    objectName: "auth.identity-reset.pending"
                    Accessible.id: objectName
                    visible: root.identityReset && !root.failed
                    Layout.alignment: Qt.AlignHCenter
                    Accessible.name: qsTr("Resetting identity")
                }

                Rectangle {
                    visible: root.hasCode
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: codeLabel.implicitWidth + 32
                    Layout.preferredHeight: 44
                    radius: KodosiTheme.radiusSmall
                    color: KodosiTheme.canvas
                    border.width: 1
                    border.color: KodosiTheme.seam

                    PlainLabel {
                        id: codeLabel
                        anchors.centerIn: parent
                        text: Models.AuthState.userCode
                        color: KodosiTheme.textPrimary
                        font.family: "monospace"
                        font.pixelSize: 18
                        font.weight: Font.Bold
                        font.letterSpacing: 1.5
                    }
                }

                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: KodosiTheme.spacing3

                    KButton {
                        id: browserButton
                        visible: root.hasCode
                        objectName: "auth.open-browser"
                        Accessible.id: objectName
                        text: qsTr("Open browser")
                        variant: "directional"
                        iconName: "chevron-right"
                        Accessible.name: text
                        onClicked:
                            Models.AuthActions.openVerificationUrl(
                                Models.AuthState.verificationUrl)
                    }

                    KButton {
                        visible: root.hasCode
                        objectName: "auth.copy-code"
                        Accessible.id: objectName
                        text: qsTr("Copy code")
                        variant: "secondary"
                        Accessible.name: text
                        onClicked:
                            Models.AuthActions.copyCode(Models.AuthState.userCode)
                    }

                    KButton {
                        id: retryButton
                        visible: root.failed && !root.identityReset
                        objectName: "auth.retry"
                        Accessible.id: objectName
                        text: qsTr("Try again")
                        variant: "directional"
                        iconName: "refresh"
                        Accessible.name: text
                        onClicked: Models.AuthActions.retry()
                    }

                    KButton {
                        id: recoveryButton
                        visible: root.failed && root.identityReset
                        objectName: "auth.identity-reset.return"
                        Accessible.id: objectName
                        text: qsTr("Return to Account settings")
                        variant: "directional"
                        iconName: "chevron-right"
                        Accessible.name: text
                        onClicked:
                            root.accountRecoveryRequested()
                    }

                    KButton {
                        id: cancelButton
                        visible: !root.identityReset
                        objectName: "auth.cancel"
                        Accessible.id: objectName
                        text: qsTr("Cancel")
                        variant: "quiet"
                        Accessible.name: text
                        onClicked: Models.AuthActions.signOut()
                    }
                }
            }
        }
    }

    onOpened: {
        if (root.identityReset && !root.failed)
            transitionScope.forceActiveFocus(Qt.PopupFocusReason)
        else if (root.failed && root.identityReset)
            recoveryButton.forceActiveFocus()
        else if (root.failed)
            retryButton.forceActiveFocus()
        else if (root.hasCode)
            browserButton.forceActiveFocus()
        else
            cancelButton.forceActiveFocus()
    }
}
