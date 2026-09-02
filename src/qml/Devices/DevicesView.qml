pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Kodosi.Models 1.0 as Models

Item {
    id: root
    objectName: "surface.devices"
    Accessible.id: objectName
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Account and Devices")

    Rectangle {
        anchors.fill: parent
        color: KodosiTheme.canvas
    }

    KScrollView {
        anchors.fill: parent
        contentWidth: availableWidth

        ColumnLayout {
            width: parent.width
            spacing: KodosiTheme.spacing5

            Item { Layout.preferredHeight: KodosiTheme.spacing5 }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: KodosiTheme.spacing7
                Layout.rightMargin: KodosiTheme.spacing7

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3
                    PlainLabel {
                        text: qsTr("Devices")
                        color: KodosiTheme.textPrimary
                        font.pixelSize: 20
                        font.weight: Font.DemiBold
                    }
                    PlainLabel {
                        text: qsTr("Account enrollment and trusted device certificates")
                        color: KodosiTheme.textSecondary
                        font.pixelSize: 12
                    }
                }

                KButton {
                    objectName: "devices.refresh"
                    Accessible.id: objectName
                    text: qsTr("Refresh")
                    Accessible.name: text
                    onClicked: Models.DeviceActions.refresh()
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: KodosiTheme.spacing7
                Layout.rightMargin: KodosiTheme.spacing7
                implicitHeight: currentDevice.implicitHeight + 24
                radius: KodosiTheme.radiusSmall
                color: KodosiTheme.surface
                border.width: 1
                border.color: KodosiTheme.seam

                RowLayout {
                    id: currentDevice
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: KodosiTheme.spacing4

                    Rectangle {
                        Layout.preferredWidth: 34
                        Layout.preferredHeight: 34
                        radius: KodosiTheme.radiusSmall
                        color: KodosiTheme.surfaceElevated
                        border.width: 1
                        border.color: KodosiTheme.seam
                        KIcon {
                            anchors.centerIn: parent
                            width: 18
                            height: 18
                            name: "devices"
                            color: KodosiTheme.accent
                            strokeWidth: 1.8
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        PlainLabel {
                            text: qsTr("This Linux device")
                            color: KodosiTheme.textPrimary
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                        }
                        PlainLabel {
                            text: Models.Devices.selfDeviceId.length > 0
                                ? Models.Devices.selfDeviceId
                                : qsTr("Waiting for device inventory")
                            color: KodosiTheme.textSecondary
                            font.pixelSize: 10
                            elide: Text.ElideMiddle
                        }
                    }

                    StatusPill {
                        text: Models.Devices.localDeviceEnrolled
                            ? qsTr("Enrolled")
                            : qsTr("Not linked")
                        tone: Models.Devices.localDeviceEnrolled
                            ? KodosiTheme.success
                            : KodosiTheme.warning
                    }
                }
            }

            Rectangle {
                visible: Models.Devices.hasSelfLinkPending
                Layout.fillWidth: true
                Layout.leftMargin: KodosiTheme.spacing7
                Layout.rightMargin: KodosiTheme.spacing7
                implicitHeight: selfLinkContent.implicitHeight + 24
                radius: KodosiTheme.radiusSmall
                color: KodosiTheme.surfaceElevated
                border.width: 1
                border.color: KodosiTheme.warning

                ColumnLayout {
                    id: selfLinkContent
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 6
                    PlainLabel {
                        text: qsTr("Enter this code on an enrolled device")
                        color: KodosiTheme.textPrimary
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                    PlainLabel {
                        text: Models.Devices.selfLinkUserCode
                        color: KodosiTheme.warning
                        font.family: "monospace"
                        font.pixelSize: 20
                        font.weight: Font.Bold
                        font.letterSpacing: 1.5
                    }
                    KButton {
                        objectName: "devices.self-link.cancel"
                        Accessible.id: objectName
                        text: qsTr("Cancel link")
                        Accessible.name: text
                        onClicked: Models.DeviceActions.cancelSelfLink()
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: KodosiTheme.spacing7
                Layout.rightMargin: KodosiTheme.spacing7
                implicitHeight: linkContent.implicitHeight + 24
                radius: KodosiTheme.radiusSmall
                color: KodosiTheme.surface
                border.width: 1
                border.color: KodosiTheme.seam

                ColumnLayout {
                    id: linkContent
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: KodosiTheme.spacing3

                    PlainLabel {
                        text: Models.Devices.localDeviceEnrolled
                            ? qsTr("Approve another device")
                            : qsTr("Add this device")
                        color: KodosiTheme.textPrimary
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }

                    RowLayout {
                        visible: Models.Devices.localDeviceEnrolled
                        Layout.fillWidth: true
                        spacing: KodosiTheme.spacing3

                        KTextField {
                            id: linkCode
                            objectName: "devices.link.code"
                            Accessible.id: objectName
                            Layout.fillWidth: true
                            placeholderText: qsTr("BCDF-2345")
                            Accessible.name: qsTr("Device link code")
                            onAccepted: approveButton.clicked()
                        }
                        KButton {
                            id: approveButton
                            objectName: "devices.link.approve"
                            Accessible.id: objectName
                            text: qsTr("Approve")
                            Accessible.name: text
                            enabled: Models.Devices.isCanonicalUserCode(
                                Models.Devices.normalizeUserCode(linkCode.text))
                                && Models.Devices.inventoryState
                                    === Models.Devices.Fresh
                            onClicked: {
                                if (Models.DeviceActions.approveLink(linkCode.text))
                                    linkCode.clear()
                            }
                        }
                    }

                    KButton {
                        visible: Models.Devices.hasEnrollmentState
                            && !Models.Devices.localDeviceEnrolled
                            && !Models.Devices.hasSelfLinkPending
                        objectName: "devices.self-link.start"
                        Accessible.id: objectName
                        text: qsTr("Generate link code")
                        Accessible.name: text
                        onClicked: Models.DeviceActions.startSelfLink()
                    }
                }
            }

            PlainLabel {
                visible: Models.DeviceActions.lastError.length > 0
                Layout.fillWidth: true
                Layout.leftMargin: KodosiTheme.spacing7
                Layout.rightMargin: KodosiTheme.spacing7
                text: Models.DeviceActions.lastError
                color: KodosiTheme.danger
                font.pixelSize: 11
                wrapMode: Text.Wrap
            }

            PlainLabel {
                Layout.leftMargin: KodosiTheme.spacing7
                text: qsTr("Linked devices")
                color: KodosiTheme.textPrimary
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }

            ListView {
                Layout.fillWidth: true
                Layout.leftMargin: KodosiTheme.spacing7
                Layout.rightMargin: KodosiTheme.spacing7
                Layout.preferredHeight: contentHeight
                interactive: false
                model: Models.Devices
                spacing: 3

                delegate: Rectangle {
                    id: device
                    required property string deviceId
                    required property string label
                    required property bool isSelf
                    property bool confirming: false

                    visible: !isSelf
                    width: ListView.view.width
                    height: visible ? 50 : 0
                    radius: KodosiTheme.radiusSmall
                    color: KodosiTheme.surface
                    border.width: 1
                    border.color: KodosiTheme.seam

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: KodosiTheme.spacing3
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            PlainLabel {
                                text: device.label.length > 0
                                    ? device.label
                                    : qsTr("Device")
                                color: KodosiTheme.textPrimary
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                            }
                            PlainLabel {
                                text: device.deviceId
                                color: KodosiTheme.textSecondary
                                font.pixelSize: 9
                                elide: Text.ElideMiddle
                            }
                        }
                        KButton {
                            objectName: "devices.revoke." + device.deviceId
                            Accessible.id: objectName
                            text: device.confirming ? qsTr("Confirm revoke") : qsTr("Revoke")
                            variant: device.confirming ? "danger" : "secondary"
                            Accessible.name: text
                            enabled: Models.Devices.inventoryState
                                === Models.Devices.Fresh
                                && Models.Devices.localDeviceEnrolled
                            onEnabledChanged: {
                                if (!enabled)
                                    device.confirming = false
                            }
                            onClicked: {
                                if (!device.confirming) {
                                    device.confirming = true
                                } else {
                                    Models.DeviceActions.revoke(device.deviceId)
                                    device.confirming = false
                                }
                            }
                        }
                    }
                }
            }

            TrustPanel {
                Layout.fillWidth: true
                Layout.leftMargin: KodosiTheme.spacing7
                Layout.rightMargin: KodosiTheme.spacing7
            }

            AgentIntegrationsPanel {
                Layout.fillWidth: true
                Layout.leftMargin: KodosiTheme.spacing7
                Layout.rightMargin: KodosiTheme.spacing7
            }

            Item { Layout.preferredHeight: KodosiTheme.spacing7 }
        }
    }
}
