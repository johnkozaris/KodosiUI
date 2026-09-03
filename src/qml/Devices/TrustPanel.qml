pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Kodosi.Models 1.0 as Models

Rectangle {
    id: root

    implicitHeight: content.implicitHeight + 24
    radius: KodosiTheme.radiusSmall
    color: KodosiTheme.surface

    ColumnLayout {
        id: content
        anchors.fill: parent
        anchors.margins: 12
        spacing: KodosiTheme.spacing3

        RowLayout {
            Layout.fillWidth: true

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                PlainLabel {
                    text: qsTr("Pinned identities")
                    color: KodosiTheme.textPrimary
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }
                PlainLabel {
                    text: qsTr("Reset only when a person's identity has legitimately changed.")
                    color: KodosiTheme.textSecondary
                    font.pixelSize: 10
                }
            }

            KButton {
                objectName: "trust.refresh"
                Accessible.id: objectName
                text: Models.Trust.loading ? qsTr("Refreshing") : qsTr("Refresh")
                enabled: !Models.Trust.loading
                Accessible.name: text
                onClicked: Models.Trust.refresh()
            }
        }

        PlainLabel {
            visible: Models.Trust.lastError.length > 0
            Layout.fillWidth: true
            text: Models.Trust.lastError
            color: KodosiTheme.danger
            font.pixelSize: 10
            wrapMode: Text.Wrap
        }

        ListView {
            Layout.fillWidth: true
            Layout.preferredHeight: contentHeight
            interactive: false
            model: Models.Trust
            spacing: 3

            delegate: Rectangle {
                id: pin

                required property string userId
                required property double generation
                required property int deviceCount
                required property int resetAvailability

                width: ListView.view.width
                height: 48
                radius: KodosiTheme.radiusSmall
                color: KodosiTheme.surfaceElevated

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 9
                    spacing: KodosiTheme.spacing3

                    Rectangle {
                        Layout.preferredWidth: 7
                        Layout.preferredHeight: 7
                        radius: 4
                        color: KodosiTheme.success
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        PlainLabel {
                            text: qsTr("Trusted identity · %1")
                                .arg(pin.userId.slice(-8))
                            color: KodosiTheme.textPrimary
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                        }
                        PlainLabel {
                            text: qsTr("Generation %1 · %2 devices")
                                .arg(pin.generation)
                                .arg(pin.deviceCount)
                            color: KodosiTheme.textSecondary
                            font.pixelSize: 9
                        }
                    }

                    KButton {
                        objectName: "trust.reset." + pin.userId
                        Accessible.id: objectName
                        text: pin.resetAvailability === Models.Trust.Confirming
                            ? qsTr("Confirm reset")
                            : pin.resetAvailability === Models.Trust.Resetting
                              ? qsTr("Resetting")
                              : qsTr("Reset pin")
                        enabled: pin.resetAvailability === Models.Trust.Available
                            || pin.resetAvailability === Models.Trust.Confirming
                        Accessible.name: text
                        onClicked: {
                            if (pin.resetAvailability === Models.Trust.Confirming)
                                Models.Trust.confirmReset()
                            else
                                Models.Trust.requestResetConfirmation(pin.userId)
                        }
                    }

                    KButton {
                        visible: pin.resetAvailability === Models.Trust.Confirming
                        objectName: "trust.reset.cancel." + pin.userId
                        Accessible.id: objectName
                        text: qsTr("Cancel")
                        Accessible.name: text
                        onClicked: Models.Trust.cancelResetConfirmation()
                    }
                }
            }
        }

        PlainLabel {
            visible: Models.Trust.count === 0 && !Models.Trust.loading
            text: qsTr("No identities are pinned yet.")
            color: KodosiTheme.textSecondary
            font.pixelSize: 10
        }
    }
}
