pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Kodosi.Models 1.0 as Models

Item {
    id: root
    objectName: "surface.missions"
    Accessible.id: objectName
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Missions")

    property string selectedMissionId
    signal signInRequested()

    Connections {
        target: Models.MissionDetail

        function onStateChanged() {
            if (Models.MissionDetail.missionId.length === 0)
                root.selectedMissionId = ""
        }
    }

    Rectangle {
        anchors.fill: parent
        color: KodosiTheme.canvas
    }

    AuthGate {
        anchors.fill: parent
        visible: !Models.AuthState.signedIn
        accessibleId: "auth.gate.missions"
        title: qsTr("Sign in to use Missions")
        detail: qsTr(
            "Sign in to create Missions, invite people, and share supervision. Local My Agents stays available.")
        onSignInRequested: root.signInRequested()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: KodosiTheme.spacing7
        spacing: KodosiTheme.spacing5
        visible: Models.AuthState.signedIn
        Accessible.ignored: !visible

        RowLayout {
            Layout.fillWidth: true

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3

                PlainLabel {
                    text: qsTr("Missions")
                    color: KodosiTheme.textPrimary
                    font.pixelSize: 20
                    font.weight: Font.DemiBold
                }

                PlainLabel {
                    text: qsTr("Encrypted collaboration with people and agents")
                    color: KodosiTheme.textSecondary
                    font.pixelSize: 12
                }

                PlainLabel {
                    visible: Models.MissionActions.lastError.length > 0
                    Layout.fillWidth: true
                    text: Models.MissionActions.lastError
                    color: KodosiTheme.danger
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                }
            }

            KButton {
                objectName: "missions.unknown.discard"
                Accessible.id: objectName
                visible: Models.MissionActions.canDiscardUnknown
                text: qsTr("Discard unconfirmed change")
                Accessible.name: text
                onClicked: Models.MissionActions.discardUnknown()
            }

            KButton {
                objectName: "missions.unknown.retry"
                Accessible.id: objectName
                visible: Models.MissionActions.canRetryUnknown
                text: qsTr("Retry reconciliation")
                Accessible.name: text
                onClicked: Models.MissionActions.retryUnknown()
            }

            StatusPill {
                text: Models.AuthState.signedIn
                    ? qsTr("%1 Missions").arg(Models.Missions.count)
                    : qsTr("Sign in required")
                tone: Models.AuthState.signedIn
                    ? KodosiTheme.success
                    : KodosiTheme.warning
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: KodosiTheme.spacing5

            Rectangle {
                Layout.preferredWidth: 330
                Layout.fillHeight: true
                radius: KodosiTheme.radiusSmall
                color: KodosiTheme.surface
                border.width: 1
                border.color: KodosiTheme.seam

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: KodosiTheme.spacing4
                    spacing: KodosiTheme.spacing3

                    PlainLabel {
                        text: qsTr("Your Missions")
                        color: KodosiTheme.textPrimary
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }

                    ListView {
                        id: missionList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        model: Models.Missions
                        spacing: 2
                        clip: true

                        delegate: KItemDelegate {
                            id: mission

                            required property string missionId
                            required property string name
                            required property string slug

                            objectName: "missions.directory." + missionId
                            Accessible.id: objectName
                            Accessible.name: name
                            Accessible.description: slug
                            width: ListView.view.width
                            height: 50
                            onClicked: {
                                if (Models.MissionDetail.openMission(missionId))
                                    root.selectedMissionId = missionId
                            }

                            contentItem: RowLayout {
                                spacing: KodosiTheme.spacing3

                                Rectangle {
                                    Layout.preferredWidth: 26
                                    Layout.preferredHeight: 26
                                    radius: KodosiTheme.radiusSmall
                                    color: KodosiTheme.surfaceElevated
                                    border.width: 1
                                    border.color: KodosiTheme.seam

                                    KIcon {
                                        anchors.centerIn: parent
                                        width: 15
                                        height: 15
                                        name: "mission"
                                        color: KodosiTheme.success
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 1

                                    PlainLabel {
                                        text: mission.name
                                        color: KodosiTheme.textPrimary
                                        font.pixelSize: 12
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }

                                    PlainLabel {
                                        text: mission.slug
                                        color: KodosiTheme.textSecondary
                                        font.pixelSize: 10
                                    }
                                }
                            }

                            background: Rectangle {
                                radius: KodosiTheme.radiusSmall
                                color: root.selectedMissionId === mission.missionId
                                    ? KodosiTheme.surfaceElevated
                                    : mission.hovered
                                      ? Qt.lighter(KodosiTheme.surface, 1.08)
                                      : "transparent"
                                border.width: mission.activeFocus ? 1 : 0
                                border.color: KodosiTheme.focusRing
                            }
                        }

                        PlainLabel {
                            anchors.centerIn: parent
                            visible: Models.Missions.count === 0
                            text: Models.Missions.loading
                                ? qsTr("Loading Missions...")
                                : qsTr("No Missions yet")
                            color: KodosiTheme.textSecondary
                            font.pixelSize: 11
                        }
                    }
                }
            }

            Rectangle {
                visible: root.selectedMissionId.length === 0
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: KodosiTheme.radiusSmall
                color: KodosiTheme.surface
                border.width: 1
                border.color: KodosiTheme.seam

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: KodosiTheme.spacing5
                    spacing: KodosiTheme.spacing5

                    PlainLabel {
                        text: Models.Missions.invitations.incomingCount > 0
                            ? qsTr("Mission invitations")
                            : qsTr("People")
                        color: KodosiTheme.textPrimary
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                    }

                    ListView {
                        Layout.fillWidth: true
                        Layout.preferredHeight:
                            Math.min(150, contentHeight)
                        visible: Models.Missions.invitations.count > 0
                        model: Models.Missions.invitations
                        spacing: 3

                        delegate: KItemDelegate {
                            id: invitation

                            required property string invitationId
                            required property string roomId
                            required property string roomName
                            required property string counterpartyHandle
                            required property int direction

                            objectName: "missions.invitation." + invitationId
                            Accessible.id: objectName
                            Accessible.name: roomName
                            width: ListView.view.width
                            height: 52

                            contentItem: RowLayout {
                                PlainLabel {
                                    Layout.fillWidth: true
                                    text: invitation.direction === 0
                                        ? qsTr("%1 invited you to %2")
                                              .arg(invitation.counterpartyHandle)
                                              .arg(invitation.roomName)
                                        : qsTr("Invitation to %1")
                                              .arg(invitation.roomName)
                                    color: KodosiTheme.textPrimary
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                }

                                PlainLabel {
                                    text: invitation.direction === 0
                                        ? qsTr("Incoming")
                                        : qsTr("Sent")
                                    color: invitation.direction === 0
                                        ? KodosiTheme.warning
                                        : KodosiTheme.textSecondary
                                    font.pixelSize: 10
                                    font.weight: Font.DemiBold
                                }

                                KButton {
                                    objectName: "missions.invitation.accept."
                                        + invitation.invitationId
                                    Accessible.id: objectName
                                    visible: invitation.direction === 0
                                    text: qsTr("Accept")
                                    Accessible.name: qsTr(
                                        "Accept invitation to %1").arg(
                                            invitation.roomName)
                                    enabled: Models.MissionActions
                                        .pendingMissionIds.indexOf(
                                            invitation.roomId) === -1
                                    onClicked: Models.MissionActions
                                        .acceptInvitation(
                                            invitation.invitationId)
                                }

                                KButton {
                                    objectName: "missions.invitation.decline."
                                        + invitation.invitationId
                                    Accessible.id: objectName
                                    visible: invitation.direction === 0
                                    text: qsTr("Decline")
                                    Accessible.name: qsTr(
                                        "Decline invitation to %1").arg(
                                            invitation.roomName)
                                    enabled: Models.MissionActions
                                        .pendingMissionIds.indexOf(
                                            invitation.roomId) === -1
                                    onClicked: Models.MissionActions
                                        .declineInvitation(
                                            invitation.invitationId)
                                }

                                KButton {
                                    objectName: "missions.invitation.cancel."
                                        + invitation.invitationId
                                    Accessible.id: objectName
                                    visible: invitation.direction === 1
                                    text: qsTr("Cancel")
                                    Accessible.name: qsTr(
                                        "Cancel invitation to %1").arg(
                                            invitation.roomName)
                                    enabled: Models.MissionActions
                                        .pendingMissionIds.indexOf(
                                            invitation.roomId) === -1
                                    onClicked: Models.MissionActions
                                        .cancelInvitation(
                                            invitation.invitationId)
                                }
                            }

                            background: Rectangle {
                                radius: KodosiTheme.radiusSmall
                                color: KodosiTheme.surfaceElevated
                                border.width: 1
                                border.color: KodosiTheme.seam
                            }
                        }
                    }

                    RowLayout {
                        visible: Models.AuthState.signedIn
                        Layout.fillWidth: true
                        spacing: KodosiTheme.spacing3

                        KTextField {
                            id: friendHandle
                            objectName: "people.request.handle"
                            Accessible.id: objectName
                            Layout.fillWidth: true
                            placeholderText: qsTr("@username")
                            Accessible.name: qsTr("Friend username")
                            onAccepted: sendRequestButton.clicked()
                        }

                        KButton {
                            id: sendRequestButton
                            objectName: "people.request.send"
                            Accessible.id: objectName
                            text: qsTr("Add person")
                            Accessible.name: text
                            enabled: friendHandle.text.trim().length > 0
                            onClicked: {
                                if (Models.PeopleActions.sendRequest(friendHandle.text))
                                    friendHandle.clear()
                            }
                        }
                    }

                    PlainLabel {
                        visible: Models.PeopleActions.lastError.length > 0
                        Layout.fillWidth: true
                        text: Models.PeopleActions.lastError
                        color: KodosiTheme.danger
                        font.pixelSize: 10
                        wrapMode: Text.Wrap
                    }

                    ListView {
                        id: peopleList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        model: Models.People
                        spacing: 2
                        clip: true

                        delegate: KItemDelegate {
                            id: person

                            required property string userId
                            required property string handle
                            required property string displayName
                            required property int relationship
                            property bool confirmingRemoval: false

                            objectName: "missions.person." + userId
                            Accessible.id: objectName
                            Accessible.name: displayName
                            Accessible.description: "@" + handle
                            width: ListView.view.width
                            height: 54

                            contentItem: RowLayout {
                                spacing: KodosiTheme.spacing4

                                Rectangle {
                                    Layout.preferredWidth: 30
                                    Layout.preferredHeight: 30
                                    radius: 15
                                    color: person.relationship === 0
                                        ? KodosiTheme.success
                                        : KodosiTheme.warning

                                    PlainLabel {
                                        anchors.centerIn: parent
                                        text: person.displayName
                                            .substring(0, 1).toUpperCase()
                                        color: KodosiTheme.accentForeground
                                        font.pixelSize: 12
                                        font.weight: Font.Bold
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 1

                                    PlainLabel {
                                        text: person.displayName
                                        color: KodosiTheme.textPrimary
                                        font.pixelSize: 12
                                        font.weight: Font.DemiBold
                                    }

                                    PlainLabel {
                                        text: "@" + person.handle
                                        color: KodosiTheme.textSecondary
                                        font.pixelSize: 10
                                    }
                                }

                                RowLayout {
                                    spacing: 4

                                    KButton {
                                        visible: person.relationship === 1
                                        objectName: "people.accept." + person.userId
                                        Accessible.id: objectName
                                        text: qsTr("Accept")
                                        Accessible.name: text
                                        onClicked:
                                            Models.PeopleActions.accept(person.handle)
                                    }

                                    KButton {
                                        visible: person.relationship === 1
                                        objectName: "people.reject." + person.userId
                                        Accessible.id: objectName
                                        text: qsTr("Reject")
                                        Accessible.name: text
                                        onClicked:
                                            Models.PeopleActions.reject(person.handle)
                                    }

                                    KButton {
                                        visible: person.relationship === 2
                                        objectName: "people.cancel." + person.userId
                                        Accessible.id: objectName
                                        text: qsTr("Cancel")
                                        Accessible.name: text
                                        onClicked:
                                            Models.PeopleActions.cancel(person.handle)
                                    }

                                    KButton {
                                        visible: person.relationship === 0
                                        objectName: "people.remove." + person.userId
                                        Accessible.id: objectName
                                        text: person.confirmingRemoval
                                            ? qsTr("Confirm remove")
                                            : qsTr("Remove")
                                        variant: person.confirmingRemoval
                                            ? "danger"
                                            : "secondary"
                                        Accessible.name: text
                                        onClicked: {
                                            if (!person.confirmingRemoval) {
                                                person.confirmingRemoval = true
                                            } else {
                                                Models.PeopleActions.remove(person.handle)
                                                person.confirmingRemoval = false
                                            }
                                        }
                                    }
                                }
                            }

                            background: Rectangle {
                                radius: KodosiTheme.radiusSmall
                                color: person.hovered
                                    ? KodosiTheme.surfaceElevated
                                    : "transparent"
                                border.width: person.activeFocus ? 1 : 0
                                border.color: KodosiTheme.focusRing
                            }
                        }

                        PlainLabel {
                            anchors.centerIn: parent
                            visible: Models.People.count === 0
                            text: Models.AuthState.signedIn
                                ? qsTr("No people here yet")
                                : qsTr("Sign in to open Missions")
                            color: KodosiTheme.textSecondary
                            font.pixelSize: 12
                        }
                    }

                }
            }

            MissionDetailView {
                visible: root.selectedMissionId.length > 0
                Layout.fillWidth: true
                Layout.fillHeight: true
                missionId: root.selectedMissionId
                onCloseRequested: {
                    Models.MissionDetail.closeMission()
                    root.selectedMissionId = ""
                }
            }
        }
    }
}
