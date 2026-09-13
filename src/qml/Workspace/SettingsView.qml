pragma ComponentBehavior: Bound
import Kodosi 1.0
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kodosi.Models 1.0 as Models

Item {
    id: root

    Accessible.id: objectName
    objectName: "panel.settings"

    KScrollView {
        anchors.fill: parent
        anchors.margins: 24

        ColumnLayout {
            spacing: 18
            width: Math.min(720, parent.width)

            PlainLabel {
                color: KodosiTheme.textPrimary
                font.pixelSize: 22
                text: qsTr("Settings")
            }
            RowLayout {
                PlainLabel { text: qsTr("Appearance"); color: KodosiTheme.textPrimary }
                KComboBox {
                    Accessible.id: objectName
                    Accessible.name: qsTr("Appearance")
                    objectName: "settings.appearance"
                    model: [qsTr("System"), qsTr("Light"), qsTr("Dark")]
                    currentIndex: Models.Appearance.preference === Models.Appearance.Light ? 1 : Models.Appearance.preference === Models.Appearance.Dark ? 2 : 0
                    onActivated: Models.Appearance.setPreference(currentIndex === 1 ? Models.Appearance.Light : currentIndex === 2 ? Models.Appearance.Dark : Models.Appearance.System)
                }
            }
            RowLayout {
                PlainLabel {
                    Layout.fillWidth: true
                    color: KodosiTheme.textPrimary
                    text: Models.Workspace.signedIn ? qsTr("Signed in") : qsTr("Use your sessions on other devices")
                }
                KButton {
                    Accessible.id: objectName
                    enabled: !Models.Workspace.signingIn
                    objectName: "panel.settings.account"
                    text: Models.Workspace.signedIn ? qsTr("Sign out") : Models.Workspace.signingIn ? qsTr("Signing in…") : qsTr("Sign in")

                    onClicked: Models.Workspace.signedIn ? Models.Workspace.logout() : Models.Workspace.login()
                }
            }
            ColumnLayout {
                visible: Models.Workspace.userCode.length > 0

                PlainLabel {
                    color: KodosiTheme.textSecondary
                    text: qsTr("Enter this code in your browser:")
                }
                KReadOnlyText {
                    font.pixelSize: 24
                    text: Models.Workspace.userCode
                }
                KButton {
                    Accessible.id: objectName
                    objectName: "panel.settingsView.open-sign-in-page"
                    text: qsTr("Open sign-in page")

                    onClicked: Models.DesktopFiles.openWebUrl(Models.Workspace.verificationUri)
                }
            }
            PlainLabel {
                color: KodosiTheme.textPrimary
                font.weight: Font.DemiBold
                text: qsTr("Your devices")
                visible: Models.Workspace.signedIn
            }
            RowLayout {
                visible: Models.Workspace.signedIn && !Models.Workspace.localDeviceEnrolled

                PlainLabel {
                    Layout.fillWidth: true
                    color: KodosiTheme.textSecondary
                    text: Models.Workspace.selfDeviceCode || qsTr("Approve this computer from one of your devices.")
                    wrapMode: Text.WordWrap
                }
                KButton {
                    Accessible.id: objectName
                    objectName: "panel.settings.enrollment"
                    text: Models.Workspace.selfDeviceCode.length ? qsTr("Cancel") : qsTr("Request approval")

                    onClicked: Models.Workspace.selfDeviceCode.length ? Models.Workspace.cancelEnrollment() : Models.Workspace.enrollDevice()
                }
            }
            Repeater {
                model: Models.Workspace.devices

                delegate: RowLayout {
                    id: entry0

                    required property var modelData

                    Layout.fillWidth: true

                    PlainLabel {
                        Layout.fillWidth: true
                        color: KodosiTheme.textPrimary
                        text: entry0.modelData.label
                    }
                    KButton {
                        Accessible.id: objectName
                        objectName: "panel.settingsView.remove" + "." + entry0.modelData.deviceId
                        text: qsTr("Remove…")
                        visible: entry0.modelData.deviceId !== Models.Workspace.selfDeviceId
                        variant: "quiet"

                        onClicked: {
                            revoke.deviceId = entry0.modelData.deviceId;
                            revoke.label = entry0.modelData.label;
                            revoke.open();
                        }
                    }
                }
            }
            Repeater {
                model: Models.Workspace.deviceRequests

                delegate: RowLayout {
                    id: requestRow

                    required property var modelData

                    Layout.fillWidth: true

                    PlainLabel {
                        Layout.fillWidth: true
                        color: KodosiTheme.textPrimary
                        text: qsTr("%1 is requesting access").arg(requestRow.modelData.deviceLabel)
                    }
                    PlainLabel {
                        color: KodosiTheme.textSecondary
                        text: requestRow.modelData.userCode
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                visible: Models.Workspace.signedIn && Models.Workspace.localDeviceEnrolled

                KTextField {
                    id: code

                    Accessible.id: objectName
                    Accessible.name: qsTr("Device approval code")
                    Layout.fillWidth: true
                    objectName: "panel.settingsView.code"
                    placeholderText: qsTr("Code from another device")
                }
                KButton {
                    Accessible.id: objectName
                    enabled: code.text.trim().length > 0
                    objectName: "panel.settingsView.approve-device"
                    text: qsTr("Approve device")

                    onClicked: Models.Workspace.approveDevice(code.text)
                }
            }
            PlainLabel {
                color: KodosiTheme.textPrimary
                font.weight: Font.DemiBold
                text: qsTr("Terminal")
            }
            GridLayout {
                Layout.fillWidth: true
                columnSpacing: 18
                columns: 2
                rowSpacing: 10

                PlainLabel {
                    color: KodosiTheme.textSecondary
                    text: qsTr("Font")
                }
                KTextField {
                    id: family

                    Accessible.id: objectName
                    Accessible.name: qsTr("Terminal font")
                    Layout.fillWidth: true
                    objectName: "panel.settingsView.family"
                    text: Models.DesktopSettings.fontFamily
                }
                PlainLabel {
                    color: KodosiTheme.textSecondary
                    text: qsTr("Size")
                }
                KSpinBox {
                    id: size

                    Accessible.id: objectName
                    Accessible.name: qsTr("Terminal font size")
                    from: 8
                    objectName: "panel.settingsView.size"
                    to: 32
                    value: Models.DesktopSettings.fontSize
                }
                PlainLabel {
                    color: KodosiTheme.textSecondary
                    text: qsTr("Cursor")
                }
                KComboBox {
                    id: cursor

                    Accessible.id: objectName
                    Accessible.name: qsTr("Cursor style")
                    currentIndex: Models.DesktopSettings.cursorStyle
                    model: [qsTr("Block"), qsTr("Bar"), qsTr("Underline")]
                    objectName: "panel.settingsView.cursor"
                }
                PlainLabel {
                    color: KodosiTheme.textSecondary
                    text: qsTr("Scrollback lines")
                }
                KSpinBox {
                    id: scrollback

                    Accessible.id: objectName
                    Accessible.name: qsTr("Scrollback lines")
                    editable: true
                    from: 100
                    objectName: "panel.settingsView.scrollback"
                    stepSize: 100
                    to: 100000
                    value: Models.DesktopSettings.scrollbackLines
                }
                PlainLabel {
                    color: KodosiTheme.textSecondary
                    text: qsTr("Blink cursor")
                }
                KCheckBox {
                    id: blink

                    Accessible.id: objectName
                    Accessible.name: qsTr("Blink cursor")
                    checked: Models.DesktopSettings.cursorBlink
                    objectName: "panel.settingsView.blink"
                }
            }
            RowLayout {
                KButton {
                    Accessible.id: objectName
                    objectName: "panel.settingsView.apply-terminal-settings"
                    text: qsTr("Apply terminal settings")

                    onClicked: Models.DesktopSettings.apply(family.text, size.value, cursor.currentIndex, Models.DesktopSettings.lineHeight, scrollback.value, blink.checked)
                }
                KButton {
                    Accessible.id: objectName
                    objectName: "panel.settingsView.reset"
                    text: qsTr("Reset")
                    variant: "quiet"

                    onClicked: Models.DesktopSettings.resetTerminal()
                }
            }
            PlainLabel {
                color: KodosiTheme.danger
                text: Models.DesktopSettings.settingsError
                visible: Models.DesktopSettings.settingsError.length > 0
            }
            PlainLabel {
                color: KodosiTheme.textPrimary
                font.weight: Font.DemiBold
                text: qsTr("Provider configuration")
            }
            RowLayout {
                KComboBox {
                    id: provider

                    Accessible.id: objectName
                    Accessible.name: qsTr("Configuration provider")
                    model: ["Claude", "Copilot"]
                    objectName: "panel.settingsView.provider"
                }
                KButton {
                    Accessible.id: objectName
                    enabled: !Models.ProviderTools.busy
                    objectName: "panel.settingsView.show-configuration-files"
                    text: qsTr("Show configuration files")

                    onClicked: {
                        Models.ProviderTools.select(provider.currentIndex === 0 ? "claude" : "copilot", Models.DesktopSettings.effectiveWorkingDirectory);
                        Models.ProviderTools.inspect();
                    }
                }
            }
            PlainLabel {
                Layout.fillWidth: true
                color: KodosiTheme.textSecondary
                text: Models.ProviderTools.installation.executable || Models.ProviderTools.installation.message || ""
                wrapMode: Text.WordWrap
            }
            Repeater {
                model: Models.ProviderTools.installation.files || []

                delegate: RowLayout {
                    id: entry1

                    required property var modelData

                    Layout.fillWidth: true

                    ColumnLayout {
                        Layout.fillWidth: true

                        PlainLabel {
                            color: KodosiTheme.textPrimary
                            text: entry1.modelData.label
                        }
                        PlainLabel {
                            Layout.fillWidth: true
                            color: KodosiTheme.textSecondary
                            elide: Text.ElideMiddle
                            text: entry1.modelData.path
                        }
                    }
                    KButton {
                        Accessible.id: objectName
                        enabled: entry1.modelData.exists
                        objectName: "panel.settingsView.open" + "." + entry1.modelData.label
                        text: qsTr("Open")

                        onClicked: Models.DesktopFiles.openPath(entry1.modelData.path)
                    }
                }
            }
            PlainLabel {
                Layout.fillWidth: true
                color: KodosiTheme.danger
                text: Models.ProviderTools.error
                visible: Models.ProviderTools.error.length > 0
                wrapMode: Text.WordWrap
            }
            KButton {
                Accessible.id: objectName
                objectName: "panel.settingsView.quit-kodosi"
                text: qsTr("Quit Kodosi…")
                variant: "quiet"

                onClicked: quitConfirmation.open()
            }
        }
    }
    KDialog {
        id: revoke

        property string deviceId: ""
        property string label: ""

        standardButtons: Dialog.Ok | Dialog.Cancel
        title: qsTr("Remove %1?").arg(label)

        onAccepted: Models.Workspace.revokeDevice(deviceId)

        PlainLabel {
            color: KodosiTheme.textPrimary
            text: qsTr("This device will lose access to your shared terminals.")
        }
    }
    KDialog {
        id: quitConfirmation

        standardButtons: Dialog.Ok | Dialog.Cancel
        title: qsTr("Quit Kodosi?")

        onAccepted: Qt.quit()

        PlainLabel {
            color: KodosiTheme.textPrimary
            text: qsTr("Terminal processes running on this computer will stop.")
        }
    }
}
