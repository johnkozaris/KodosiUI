import Kodosi 1.0
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kodosi.Models 1.0 as Models

ColumnLayout {
    spacing: 12

    RowLayout {
        Layout.fillWidth: true

        PlainLabel {
            Layout.fillWidth: true
            color: KodosiTheme.textPrimary
            text: qsTr("Appearance")
        }
        KComboBox {
            Accessible.id: objectName
            Accessible.name: qsTr("Appearance")
            currentIndex: Models.Appearance.preference === Models.Appearance.Light ? 1 : Models.Appearance.preference === Models.Appearance.Dark ? 2 : 0
            model: [qsTr("System"), qsTr("Light"), qsTr("Dark")]
            objectName: "settings.appearance"

            onActivated: Models.Appearance.setPreference(currentIndex === 1 ? Models.Appearance.Light : currentIndex === 2 ? Models.Appearance.Dark : Models.Appearance.System)
        }
    }
    RowLayout {
        Layout.fillWidth: true

        PlainLabel {
            Layout.fillWidth: true
            color: KodosiTheme.textPrimary
            text: qsTr("Account")
        }
        KButton {
            Accessible.id: objectName
            enabled: !Models.Account.signingIn
            objectName: "panel.settings.account"
            text: Models.Account.signedIn ? qsTr("Sign out") : Models.Account.signingIn ? qsTr("Signing in…") : qsTr("Sign in")

            onClicked: Models.Account.signedIn ? Models.Account.logout() : Models.Account.login()
        }
    }
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 8
        visible: Models.Account.userCode.length > 0

        PlainLabel {
            color: KodosiTheme.textSecondary
            text: qsTr("Enter this code in your browser")
        }
        KReadOnlyText {
            font.pixelSize: 24
            text: Models.Account.userCode
        }
        KButton {
            Accessible.id: objectName
            objectName: "panel.settingsView.open-sign-in-page"
            text: qsTr("Open sign-in page")

            onClicked: Models.DesktopFiles.openWebUrl(Models.Account.verificationUri)
        }
    }
    KButton {
        Accessible.id: objectName
        objectName: "panel.settingsView.quit-kodosi"
        text: qsTr("Quit Kodosi…")
        variant: KButton.Quiet

        onClicked: quitConfirmation.open()
    }
    KDialog {
        id: quitConfirmation

        standardButtons: Dialog.Ok | Dialog.Cancel
        title: qsTr("Quit Kodosi?")

        onAccepted: Qt.quit()

        PlainLabel {
            color: KodosiTheme.textPrimary
            text: qsTr("Local terminal processes will stop.")
        }
    }
}
