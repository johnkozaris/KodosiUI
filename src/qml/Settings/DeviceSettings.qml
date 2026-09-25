pragma ComponentBehavior: Bound
import Kodosi 1.0
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kodosi.Models 1.0 as Models

ColumnLayout {
    spacing: 12

    PlainLabel {
        color: KodosiTheme.textSecondary
        text: qsTr("Sign in to manage devices.")
        visible: !Models.Account.signedIn
    }
    RowLayout {
        Layout.fillWidth: true
        visible: Models.Account.signedIn && !Models.Devices.localDeviceEnrolled

        PlainLabel {
            Layout.fillWidth: true
            color: KodosiTheme.textSecondary
            text: Models.Devices.approvalCode || qsTr("Approve this computer from another device.")
            wrapMode: Text.WordWrap
        }
        KButton {
            Accessible.id: objectName
            objectName: "panel.settings.enrollment"
            text: Models.Devices.approvalCode.length ? qsTr("Cancel") : qsTr("Request approval")

            onClicked: Models.Devices.approvalCode.length ? Models.Devices.cancelApproval() : Models.Devices.requestApproval()
        }
    }
    Repeater {
        model: Models.Devices.devices

        delegate: RowLayout {
            id: deviceRow

            required property var modelData

            Layout.fillWidth: true

            PlainLabel {
                Layout.fillWidth: true
                color: KodosiTheme.textPrimary
                text: deviceRow.modelData.label
            }
            KButton {
                Accessible.id: objectName
                objectName: "panel.settingsView.remove" + "." + deviceRow.modelData.deviceId
                text: qsTr("Remove…")
                visible: deviceRow.modelData.deviceId !== Models.Devices.selfDeviceId
                variant: KButton.Quiet

                onClicked: {
                    revoke.deviceId = deviceRow.modelData.deviceId;
                    revoke.label = deviceRow.modelData.label;
                    revoke.open();
                }
            }
        }
    }
    Repeater {
        model: Models.Devices.requests

        delegate: RowLayout {
            id: requestRow

            required property var modelData

            Layout.fillWidth: true

            PlainLabel {
                Layout.fillWidth: true
                color: KodosiTheme.textPrimary
                text: requestRow.modelData.deviceLabel
            }
            PlainLabel {
                color: KodosiTheme.textSecondary
                text: requestRow.modelData.userCode
            }
        }
    }
    RowLayout {
        Layout.fillWidth: true
        visible: Models.Account.signedIn && Models.Devices.localDeviceEnrolled

        KTextField {
            id: code

            Accessible.id: objectName
            Accessible.name: qsTr("Device approval code")
            Layout.fillWidth: true
            objectName: "panel.settingsView.code"
            placeholderText: qsTr("Approval code")
        }
        KButton {
            Accessible.id: objectName
            enabled: code.text.trim().length > 0
            objectName: "panel.settingsView.approve-device"
            text: qsTr("Approve")

            onClicked: Models.Devices.approve(code.text)
        }
    }
    KDialog {
        id: revoke

        property string deviceId: ""
        property string label: ""

        standardButtons: Dialog.Ok | Dialog.Cancel
        title: qsTr("Remove %1?").arg(label)

        onAccepted: Models.Devices.revoke(deviceId)

        PlainLabel {
            color: KodosiTheme.textPrimary
            text: qsTr("This device will lose access to shared terminals.")
        }
    }
}
