pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Kodosi.Models 1.0 as Models

Item {
    id: root
    property bool embedded: false

    objectName: embedded
        ? "panel.settings.account.devices"
        : "surface.devices"
    Accessible.id: objectName
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Account and Devices")

    signal signInRequested()
    implicitHeight: embedded
        ? (Models.AuthState.signedIn
            ? deviceContent.implicitHeight
            : 260)
        : 0

    function formattedExpiry(value) {
        const expiry = new Date(value)
        if (isNaN(expiry.getTime()))
            return qsTr("Expiry unavailable")
        return qsTr("Expires %1").arg(
            Qt.formatDateTime(expiry, "d MMM, HH:mm"))
    }

    function formattedIssued(value) {
        const issued = new Date(value)
        if (isNaN(issued.getTime()))
            return ""
        return qsTr("Issued %1").arg(
            Qt.formatDateTime(issued, "d MMM yyyy, HH:mm"))
    }

    function retryDeviceError() {
        if (Models.DeviceActions.lastOperation === "refresh"
                || Models.DeviceActions.lastOperation
                    === "discovery.invalidated") {
            Models.DeviceActions.refresh()
        } else if (Models.DeviceActions.lastOperation === "link.approve"
                && Models.DeviceActions.lastUserCode.length > 0) {
            Models.DeviceActions.approveLink(
                Models.DeviceActions.lastUserCode)
        } else if (Models.DeviceActions.lastOperation === "link.startSelf") {
            Models.DeviceActions.startSelfLink()
        }
    }

    readonly property bool deviceErrorRetryable:
        Models.DeviceActions.lastOperation === "refresh"
        || Models.DeviceActions.lastOperation === "discovery.invalidated"
        || (Models.DeviceActions.lastOperation === "link.approve"
            && Models.DeviceActions.lastUserCode.length > 0)
        || Models.DeviceActions.lastOperation === "link.startSelf"

    Connections {
        target: Models.Devices

        function onStateChanged() {
            if (Models.Devices.lastResolvedUserCode.length > 0
                    && Models.Devices.lastResolvedUserCode
                        === Models.Devices.normalizeUserCode(
                            linkCode.text))
                linkCode.clear()
        }
    }

    Rectangle {
        anchors.fill: parent
        visible: !root.embedded
        color: KodosiTheme.canvas
    }

    AuthGate {
        anchors.fill: parent
        visible: !Models.AuthState.signedIn
        accessibleId: "auth.gate.devices"
        title: qsTr("Sign in to manage devices")
        detail: qsTr(
            "Sign in to link this device, review trusted signers, and manage account recovery. Local My Agents stays available.")
        onSignInRequested: root.signInRequested()
    }

    KScrollView {
        anchors.fill: parent
        visible: Models.AuthState.signedIn
        Accessible.ignored: !visible
        contentWidth: availableWidth

        ColumnLayout {
            id: deviceContent
            width: parent.width
            spacing: KodosiTheme.spacing5

            Item {
                Layout.preferredHeight: root.embedded
                    ? 0
                    : KodosiTheme.spacing5
            }

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
                            text: Models.Devices.selfDeviceLabel.length > 0
                                ? Models.Devices.selfDeviceLabel
                                : qsTr("This Linux device")
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
                        PlainLabel {
                            objectName: "devices.current.signer"
                            Accessible.id: objectName
                            visible:
                                Models.Devices.selfCertSignerDeviceId.length > 0
                            text: qsTr("Signed by %1").arg(
                                Models.Devices.selfCertSignerDeviceId)
                            color: KodosiTheme.textTertiary
                            font.pixelSize: KodosiTheme.fontCaption
                            elide: Text.ElideMiddle
                        }
                        PlainLabel {
                            objectName: "devices.current.issued"
                            Accessible.id: objectName
                            visible: !isNaN(
                                new Date(
                                    Models.Devices.selfCertIssuedAt).getTime())
                            text: root.formattedIssued(
                                Models.Devices.selfCertIssuedAt)
                            color: KodosiTheme.textTertiary
                            font.pixelSize: KodosiTheme.fontCaption
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

            ColumnLayout {
                objectName: "devices.incoming"
                Accessible.id: objectName
                visible: Models.Devices.pendingLinkPresentations.length > 0
                Layout.fillWidth: true
                Layout.leftMargin: KodosiTheme.spacing7
                Layout.rightMargin: KodosiTheme.spacing7
                spacing: KodosiTheme.spacing3

                PlainLabel {
                    text: qsTr("Incoming link requests")
                    color: KodosiTheme.textPrimary
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }

                Repeater {
                    objectName: "devices.incoming.repeater"
                    model: Models.Devices.pendingLinkPresentations

                    delegate: Rectangle {
                        id: incoming
                        required property var modelData
                        readonly property var request: modelData
                        readonly property string userCode:
                            request.userCode || ""
                        readonly property string deviceLabel:
                            request.deviceLabel || ""
                        readonly property var expiresAt:
                            request.expiresAt

                        objectName: "devices.incoming." + userCode
                        Accessible.id: objectName
                        Accessible.role: Accessible.Pane
                        Accessible.name: qsTr(
                            "Link request from %1").arg(
                                deviceLabel.length > 0
                                    ? deviceLabel
                                    : qsTr("another device"))
                        Layout.fillWidth: true
                        implicitHeight: incomingContent.implicitHeight + 24
                        radius: KodosiTheme.radiusSmall
                        color: KodosiTheme.surfaceElevated

                        ColumnLayout {
                            id: incomingContent
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 5

                            PlainLabel {
                                Layout.fillWidth: true
                                text: qsTr("%1 wants to join your account.")
                                    .arg(
                                        incoming.deviceLabel.length > 0
                                            ? incoming.deviceLabel
                                            : qsTr("Another device"))
                                color: KodosiTheme.textPrimary
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                                wrapMode: Text.Wrap
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: KodosiTheme.spacing3

                                PlainLabel {
                                    text: incoming.userCode
                                    color: KodosiTheme.warning
                                    font.family: "monospace"
                                    font.pixelSize: 15
                                    font.weight: Font.Bold
                                    font.letterSpacing: 1.2
                                }

                                PlainLabel {
                                    Layout.fillWidth: true
                                    text: root.formattedExpiry(
                                        incoming.expiresAt)
                                    color: KodosiTheme.textSecondary
                                    font.pixelSize: 10
                                }

                                KButton {
                                    objectName:
                                        "devices.incoming."
                                        + incoming.userCode + ".approve"
                                    Accessible.id: objectName
                                    text:
                                        Models.DeviceActions.approvalPending(
                                            incoming.userCode)
                                        ? qsTr("Approving")
                                        :
                                        Models.DeviceActions.lastOperation
                                            === "link.approve"
                                        && Models.DeviceActions.lastUserCode
                                            === incoming.userCode
                                        && Models.DeviceActions.lastError
                                            .length > 0
                                        ? qsTr("Retry approval")
                                        : qsTr("Approve")
                                    enabled:
                                        Models.Devices.inventoryState
                                            === Models.Devices.Fresh
                                        && Models.Devices
                                            .localDeviceEnrolled
                                        && !Models.DeviceActions
                                            .approvalPending(
                                                incoming.userCode)
                                    Accessible.name: text
                                    onClicked:
                                        Models.DeviceActions.approveLink(
                                            incoming.userCode)
                                }
                            }

                            PlainLabel {
                                text: qsTr(
                                    "Ignore this request to let it expire.")
                                color: KodosiTheme.textTertiary
                                font.pixelSize: KodosiTheme.fontCaption
                            }
                        }
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
                    PlainLabel {
                        objectName: "devices.self-link.expiry"
                        Accessible.id: objectName
                        text: root.formattedExpiry(
                            Models.Devices.selfLinkExpiresAt)
                        color: KodosiTheme.textSecondary
                        font.pixelSize: 10
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
                objectName: "devices.self-link.outcome"
                Accessible.id: objectName
                visible:
                    Models.Devices.selfLinkOutcomeMessage.length > 0
                    && !Models.Devices.hasSelfLinkPending
                Layout.fillWidth: true
                Layout.leftMargin: KodosiTheme.spacing7
                Layout.rightMargin: KodosiTheme.spacing7
                implicitHeight: selfOutcomeContent.implicitHeight + 24
                radius: KodosiTheme.radiusSmall
                color: KodosiTheme.surfaceElevated

                RowLayout {
                    id: selfOutcomeContent
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: KodosiTheme.spacing3

                    KIcon {
                        Layout.preferredWidth: 18
                        Layout.preferredHeight: 18
                        name: Models.Devices.selfLinkApproved
                            ? "check"
                            : "warning"
                        color: Models.Devices.selfLinkApproved
                            ? KodosiTheme.success
                            : KodosiTheme.warning
                    }

                    PlainLabel {
                        Layout.fillWidth: true
                        text: Models.Devices.selfLinkOutcomeMessage
                        color: KodosiTheme.textPrimary
                        font.pixelSize: 10
                        wrapMode: Text.Wrap
                    }

                    KButton {
                        objectName: "devices.self-link.regenerate"
                        Accessible.id: objectName
                        visible: !Models.Devices.selfLinkApproved
                        text: qsTr("Generate another code")
                        enabled: Models.Devices.hasEnrollmentState
                            && !Models.Devices.localDeviceEnrolled
                        Accessible.name: text
                        onClicked: Models.DeviceActions.startSelfLink()
                    }

                    KIconButton {
                        objectName: "devices.self-link.outcome.dismiss"
                        Accessible.id: objectName
                        glyph: "close"
                        size: 26
                        Accessible.name:
                            qsTr("Dismiss device link outcome")
                        onClicked:
                            Models.Devices.dismissSelfLinkOutcome()
                    }
                }
            }

            Rectangle {
                objectName: "devices.link.outcome"
                Accessible.id: objectName
                visible: Models.Devices.lastLinkOutcomeMessage.length > 0
                Layout.fillWidth: true
                Layout.leftMargin: KodosiTheme.spacing7
                Layout.rightMargin: KodosiTheme.spacing7
                implicitHeight: linkOutcomeContent.implicitHeight + 24
                radius: KodosiTheme.radiusSmall
                color: KodosiTheme.surfaceElevated

                RowLayout {
                    id: linkOutcomeContent
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: KodosiTheme.spacing3

                    KIcon {
                        Layout.preferredWidth: 18
                        Layout.preferredHeight: 18
                        name: Models.Devices.lastLinkApproved
                            ? "check"
                            : "warning"
                        color: Models.Devices.lastLinkApproved
                            ? KodosiTheme.success
                            : KodosiTheme.warning
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        PlainLabel {
                            Layout.fillWidth: true
                            text: Models.Devices.lastLinkOutcomeMessage
                            color: KodosiTheme.textPrimary
                            font.pixelSize: 10
                            wrapMode: Text.Wrap
                        }
                        PlainLabel {
                            visible:
                                Models.Devices.lastResolvedUserCode.length > 0
                            text: Models.Devices.lastResolvedUserCode
                            color: KodosiTheme.textSecondary
                            font.family: "monospace"
                            font.pixelSize: KodosiTheme.fontCaption
                        }
                    }

                    KIconButton {
                        objectName: "devices.link.outcome.dismiss"
                        Accessible.id: objectName
                        glyph: "close"
                        size: 26
                        Accessible.name:
                            qsTr("Dismiss device link outcome")
                        onClicked: Models.Devices.dismissLinkOutcome()
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
                                && !Models.DeviceActions.approvalPending(
                                    Models.Devices.normalizeUserCode(
                                        linkCode.text))
                            onClicked: {
                                Models.DeviceActions.approveLink(linkCode.text)
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

            Rectangle {
                objectName: "devices.error"
                Accessible.id: objectName
                visible: Models.DeviceActions.lastError.length > 0
                Layout.fillWidth: true
                Layout.leftMargin: KodosiTheme.spacing7
                Layout.rightMargin: KodosiTheme.spacing7
                implicitHeight: deviceErrorContent.implicitHeight + 20
                radius: KodosiTheme.radiusSmall
                color: KodosiTheme.surfaceRaised

                RowLayout {
                    id: deviceErrorContent
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: KodosiTheme.spacing2

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        PlainLabel {
                            Layout.fillWidth: true
                            text: Models.DeviceActions.lastError
                            color: KodosiTheme.danger
                            font.pixelSize: 10
                            wrapMode: Text.Wrap
                            Accessible.name: text
                        }
                        PlainLabel {
                            visible:
                                Models.DeviceActions.lastUserCode.length > 0
                            text: Models.DeviceActions.lastUserCode
                            color: KodosiTheme.textSecondary
                            font.family: "monospace"
                            font.pixelSize: KodosiTheme.fontCaption
                        }
                    }

                    KButton {
                        objectName: "devices.error.retry"
                        Accessible.id: objectName
                        visible: root.deviceErrorRetryable
                        text: Models.DeviceActions.lastOperation
                            === "link.startSelf"
                            ? qsTr("Generate another code")
                            : qsTr("Retry")
                        Accessible.name: text
                        onClicked: root.retryDeviceError()
                    }

                    KIconButton {
                        objectName: "devices.error.dismiss"
                        Accessible.id: objectName
                        glyph: "close"
                        size: 26
                        Accessible.name: qsTr("Dismiss device error")
                        onClicked: Models.DeviceActions.clearError()
                    }
                }
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
                    required property string certSignerDeviceId
                    required property var certIssuedAtMs
                    required property bool isSelf
                    property bool confirming: false

                    visible: !isSelf
                    width: ListView.view.width
                    height: visible ? 70 : 0
                    radius: KodosiTheme.radiusSmall
                    color: KodosiTheme.surface

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
                                font.pixelSize: KodosiTheme.fontCaption
                                elide: Text.ElideMiddle
                            }
                            PlainLabel {
                                visible:
                                    device.certSignerDeviceId.length > 0
                                    || Number(device.certIssuedAtMs) > 0
                                text: [
                                    device.certSignerDeviceId.length > 0
                                        ? qsTr("Signed by %1").arg(
                                            device.certSignerDeviceId)
                                        : "",
                                    Number(device.certIssuedAtMs) > 0
                                        ? root.formattedIssued(
                                            new Date(
                                                Number(
                                                    device.certIssuedAtMs)))
                                        : ""
                                ].filter(function(value) {
                                    return value.length > 0
                                }).join(" · ")
                                color: KodosiTheme.textTertiary
                                font.pixelSize: KodosiTheme.fontCaption
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
