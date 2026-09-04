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
    property string selectedMissionName
    property bool createOpen: false
    property bool peopleOpen: false
    property bool addFriendOpen: false
    property string friendDraft: ""
    property string pendingFriendRemovalHandle: ""
    property string pendingFriendRemovalName: ""
    signal signInRequested()

    function submitCreate() {
        if (Models.MissionActions.createMissionCanRetry)
            Models.MissionActions.retryCreateMission()
        else
            Models.MissionActions.createMission()
    }

    function openMission(missionId, missionName) {
        if (!Models.MissionDetail.openMission(missionId))
            return
        selectedMissionId = missionId
        selectedMissionName = missionName
        createOpen = false
        peopleOpen = false
    }

    function closeMission() {
        if (selectedMissionId.length > 0)
            Models.Missions.setMissionChatPinned(
                selectedMissionId,
                false)
        Models.MissionDetail.closeMission()
        selectedMissionId = ""
        selectedMissionName = ""
    }

    function closePeople() {
        peopleOpen = false
        addFriendOpen = false
        friendDraft = ""
    }

    function submitFriendRequest() {
        const handle = friendDraft.trim()
        if (handle.length === 0)
            return
        if (Models.PeopleActions.sendRequest(handle)) {
            friendDraft = ""
            addFriendOpen = false
        }
    }

    Component.onCompleted: {
        selectedMissionId = Models.MissionDetail.missionId
        if (selectedMissionId.length > 0)
            selectedMissionName = qsTr("Mission")
    }

    Connections {
        target: Models.MissionDetail

        function onStateChanged() {
            if (Models.MissionDetail.missionId.length === 0) {
                root.selectedMissionId = ""
                root.selectedMissionName = ""
            } else if (root.selectedMissionId.length === 0) {
                root.selectedMissionId = Models.MissionDetail.missionId
                root.selectedMissionName = qsTr("Mission")
            }
        }
    }

    Connections {
        target: Models.MissionActions

        function onMissionCreated(missionId) {
            root.createOpen = false
        }
    }

    Shortcut {
        sequence: "Escape"
        context: Qt.WindowShortcut
        enabled: root.visible && root.selectedMissionId.length === 0
            && (root.addFriendOpen || root.peopleOpen || root.createOpen)
        onActivated: {
            if (root.addFriendOpen) {
                root.addFriendOpen = false
                root.friendDraft = ""
            } else if (root.peopleOpen) {
                root.closePeople()
            } else {
                root.createOpen = false
            }
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
        detail: qsTr("My Agents stays local.")
        onSignInRequested: root.signInRequested()
    }

    Item {
        anchors.fill: parent
        visible: Models.AuthState.signedIn

        ColumnLayout {
            anchors.fill: parent
            visible: root.selectedMissionId.length === 0
                && !root.peopleOpen
            Accessible.ignored: !visible
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 48
                Layout.leftMargin: KodosiTheme.spacing5
                Layout.rightMargin: KodosiTheme.spacing3
                spacing: KodosiTheme.spacing2

                PlainLabel {
                    Layout.fillWidth: true
                    text: qsTr("Missions")
                    color: KodosiTheme.textPrimary
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }

                KButton {
                    objectName: "missions.people.open"
                    Accessible.id: objectName
                    text: qsTr("People")
                    compact: true
                    variant: "quiet"
                    onClicked: {
                        root.peopleOpen = true
                        root.createOpen = false
                        Models.PeopleActions.refresh()
                    }
                }

                KIconButton {
                    objectName: "missions.create.open"
                    Accessible.id: objectName
                    glyph: "plus"
                    Accessible.name: qsTr("New Mission")
                    onClicked: root.createOpen = !root.createOpen
                }
            }

            Rectangle {
                visible: root.createOpen
                Layout.fillWidth: true
                implicitHeight: createColumn.implicitHeight + 16
                color: KodosiTheme.surfaceRaised

                ColumnLayout {
                    id: createColumn
                    anchors.fill: parent
                    anchors.margins: KodosiTheme.spacing3
                    spacing: KodosiTheme.spacing2

                    KTextField {
                        objectName: "missions.create.name"
                        Accessible.id: objectName
                        Layout.fillWidth: true
                        placeholderText: qsTr("Mission name")
                        text: Models.MissionActions.createMissionName
                        maximumLength: 128
                        onTextEdited:
                            Models.MissionActions.setCreateMissionName(text)
                    }

                    KTextField {
                        objectName: "missions.create.slug"
                        Accessible.id: objectName
                        Layout.fillWidth: true
                        placeholderText: qsTr("mission-slug")
                        text: Models.MissionActions.createMissionSlug
                        maximumLength: 64
                        onTextEdited:
                            Models.MissionActions.setCreateMissionSlug(text)
                        onAccepted: {
                            if (Models.MissionActions.createMissionCanSubmit
                                    || Models.MissionActions
                                        .createMissionCanRetry)
                                root.submitCreate()
                        }
                    }

                    RowLayout {
                        visible: !root.createOpen
                            && Models.MissionActions.hasCreateMissionDraft
                        Layout.fillWidth: true
                        Layout.leftMargin: KodosiTheme.spacing3
                        Layout.rightMargin: KodosiTheme.spacing3
                        Layout.topMargin: KodosiTheme.spacing2
                        spacing: KodosiTheme.spacing2

                        PlainLabel {
                            Layout.fillWidth: true
                            text: Models.MissionActions.createMissionError.length > 0
                                ? Models.MissionActions.createMissionError
                                : Models.MissionActions.createMissionOutcome
                                    === Models.MissionActions.Pending
                                    || Models.MissionActions.createMissionOutcome
                                        === Models.MissionActions
                                            .AcceptedAwaitingProjection
                                    ? qsTr("Mission creation is still pending.")
                                    : qsTr("Mission draft saved.")
                            color: Models.MissionActions.createMissionError.length > 0
                                ? KodosiTheme.danger
                                : KodosiTheme.textSecondary
                            wrapMode: Text.Wrap
                        }

                        KButton {
                            objectName: "missions.create.resume"
                            Accessible.id: objectName
                            text: qsTr("Resume")
                            compact: true
                            onClicked: root.createOpen = true
                        }

                        KButton {
                            objectName: "missions.create.persisted.discard"
                            Accessible.id: objectName
                            text: qsTr("Discard")
                            compact: true
                            variant: "dangerQuiet"
                            enabled:
                                Models.MissionActions.createMissionCanDiscard
                            onClicked:
                                Models.MissionActions.discardCreateMission()
                        }
                    }

                    RowLayout {
                        Layout.alignment: Qt.AlignRight

                        KButton {
                            objectName: "missions.create.cancel"
                            Accessible.id: objectName
                            text: qsTr("Close")
                            variant: "quiet"
                            compact: true
                            onClicked: root.createOpen = false
                        }

                        KButton {
                            objectName: "missions.create.discard"
                            Accessible.id: objectName
                            text: qsTr("Discard")
                            variant: "quiet"
                            compact: true
                            enabled:
                                Models.MissionActions.createMissionCanDiscard
                            onClicked:
                                Models.MissionActions.discardCreateMission()
                        }

                        KButton {
                            objectName: "missions.create.submit"
                            Accessible.id: objectName
                            text: qsTr("Create")
                            variant: "primary"
                            compact: true
                            enabled:
                                Models.MissionActions.createMissionCanSubmit
                                || Models.MissionActions
                                    .createMissionCanRetry
                            onClicked: root.submitCreate()
                        }
                    }

                    PlainLabel {
                        visible:
                            Models.MissionActions.createMissionError.length > 0
                            || Models.MissionActions.createMissionOutcome
                                === Models.MissionActions.Pending
                            || Models.MissionActions.createMissionOutcome
                                === Models.MissionActions
                                    .AcceptedAwaitingProjection
                        Layout.fillWidth: true
                        text: Models.MissionActions
                            .createMissionError.length > 0
                            ? Models.MissionActions.createMissionError
                            : qsTr("Waiting for the authoritative result…")
                        color: Models.MissionActions
                            .createMissionError.length > 0
                            ? KodosiTheme.danger
                            : KodosiTheme.textSecondary
                        wrapMode: Text.Wrap
                    }

                    KButton {
                        objectName: "missions.create.check"
                        Accessible.id: objectName
                        visible: Models.MissionActions.createMissionCanCheck
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Check outcome")
                        compact: true
                        onClicked:
                            Models.MissionActions.checkCreateMission()
                    }
                }
            }

            RowLayout {
                objectName: Models.Missions.authorityState
                    === Models.Missions.Loading
                    ? "missions.directory.loading"
                    : Models.Missions.authorityState
                        === Models.Missions.Recovering
                        ? "missions.directory.recovery"
                        : Models.Missions.authorityState
                            === Models.Missions.Failed
                            ? "missions.directory.failure"
                            : "missions.directory.state"
                Accessible.id: objectName
                visible: Models.Missions.loading
                    || Models.Missions.staleDataVisible
                    || Models.Missions.lastError.length > 0
                Layout.fillWidth: true
                Layout.margins: KodosiTheme.spacing3
                spacing: KodosiTheme.spacing2

                KBusyIndicator {
                    visible: Models.Missions.loading
                    running: visible
                    implicitWidth: 20
                    implicitHeight: 20
                }

                PlainLabel {
                    Layout.fillWidth: true
                    text: Models.Missions.lastError.length > 0
                        ? Models.Missions.lastError
                        : Models.Missions.staleDataVisible
                            ? qsTr("Refreshing; showing last known Missions")
                            : qsTr("Loading Missions")
                    color: Models.Missions.lastError.length > 0
                        ? KodosiTheme.danger
                        : KodosiTheme.textSecondary
                    wrapMode: Text.Wrap
                }

                KButton {
                    objectName: "missions.error.retry"
                    Accessible.id: objectName
                    visible: Models.Missions.canRetry
                    text: qsTr("Retry")
                    compact: true
                    onClicked: Models.Missions.refresh()
                }
            }

            PlainLabel {
                visible: Models.Missions.invitations.count > 0
                Layout.leftMargin: KodosiTheme.spacing5
                Layout.topMargin: KodosiTheme.spacing2
                text: qsTr("Invitations")
                color: KodosiTheme.textSecondary
                font.pixelSize: 10
                font.weight: Font.DemiBold
            }

            ListView {
                id: invitationList
                objectName: "missions.invitations"
                Accessible.id: objectName
                visible: count > 0
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(contentHeight, 180)
                model: Models.Missions.invitations
                clip: true
                spacing: 2
                leftMargin: KodosiTheme.spacing3
                rightMargin: KodosiTheme.spacing3

                delegate: RowLayout {
                    id: invitationDelegate

                    required property string invitationId
                    required property string roomName
                    required property string roomSlug
                    required property string directionName
                    required property string counterpartyLabel

                    width: ListView.view.width
                    height: 46
                    spacing: KodosiTheme.spacing2
                    objectName: "missions.invitation." + invitationId
                    Accessible.id: objectName

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1

                        PlainLabel {
                            Layout.fillWidth: true
                            text: invitationDelegate.directionName === "incoming"
                                ? invitationDelegate.roomName
                                : qsTr("%1 · %2")
                                    .arg(invitationDelegate.roomName)
                                    .arg(invitationDelegate.counterpartyLabel)
                            color: KodosiTheme.textPrimary
                            elide: Text.ElideRight
                        }

                        PlainLabel {
                            text: invitationDelegate.directionName === "incoming"
                                ? qsTr("Invited by %1 · %2")
                                    .arg(invitationDelegate.counterpartyLabel)
                                    .arg(invitationDelegate.roomSlug)
                                : qsTr("Awaiting reply")
                            color: KodosiTheme.textSecondary
                            font.pixelSize: 9
                        }
                    }

                    KButton {
                        visible: parent.directionName === "incoming"
                        objectName: "missions.invitation."
                            + parent.invitationId + ".accept"
                        Accessible.id: objectName
                        text: qsTr("Accept")
                        compact: true
                        variant: "primary"
                        onClicked: Models.MissionActions.acceptInvitation(
                            parent.invitationId)
                    }

                    KButton {
                        objectName: "missions.invitation."
                            + parent.invitationId
                            + (parent.directionName === "incoming"
                                ? ".decline" : ".cancel")
                        Accessible.id: objectName
                        text: parent.directionName === "incoming"
                            ? qsTr("Decline") : qsTr("Cancel")
                        compact: true
                        variant: "quiet"
                        onClicked: {
                            if (parent.directionName === "incoming")
                                Models.MissionActions.declineInvitation(
                                    parent.invitationId)
                            else
                                Models.MissionActions.cancelInvitation(
                                    parent.invitationId)
                        }
                    }
                }
            }

            PlainLabel {
                visible: Models.Missions.count > 0
                Layout.leftMargin: KodosiTheme.spacing5
                Layout.topMargin: KodosiTheme.spacing2
                text: qsTr("My Missions")
                color: KodosiTheme.textSecondary
                font.pixelSize: 10
                font.weight: Font.DemiBold
            }

            ListView {
                id: missionList
                objectName: "missions.directory"
                Accessible.id: objectName
                Accessible.name: qsTr("Missions")
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: Models.Missions
                clip: true
                spacing: 2
                topMargin: KodosiTheme.spacing2
                bottomMargin: KodosiTheme.spacing2

                delegate: KItemDelegate {
                    id: missionDelegate

                    required property string missionId
                    required property string name
                    required property string slug
                    required property int unreadCount
                    required property string latestMessageBody

                    width: ListView.view.width
                        - KodosiTheme.spacing3 * 2
                    x: KodosiTheme.spacing3
                    implicitHeight: 54
                    objectName: "missions.item." + missionId
                    Accessible.id: objectName
                    Accessible.name: unreadCount > 0
                        ? qsTr("%1, %2 unread").arg(name).arg(unreadCount)
                        : name
                    onClicked:
                        root.openMission(missionId, name)

                    contentItem: RowLayout {
                        spacing: KodosiTheme.spacing2

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1

                            PlainLabel {
                                Layout.fillWidth: true
                                text: missionDelegate.name
                                color: KodosiTheme.textPrimary
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }

                            PlainLabel {
                                Layout.fillWidth: true
                                text: missionDelegate.latestMessageBody.length > 0
                                    ? missionDelegate.latestMessageBody
                                    : missionDelegate.slug
                                color: KodosiTheme.textSecondary
                                font.pixelSize: 9
                                elide: Text.ElideRight
                            }
                        }

                        PlainLabel {
                            visible: missionDelegate.unreadCount > 0
                            text: String(missionDelegate.unreadCount)
                            color: KodosiTheme.accent
                            font.weight: Font.DemiBold
                            Accessible.name: qsTr("%1 unread messages")
                                .arg(missionDelegate.unreadCount)
                        }
                    }
                }
            }

            PlainLabel {
                objectName: "missions.directory.empty"
                Accessible.id: objectName
                visible: Models.Missions.authorityState
                    === Models.Missions.Loaded
                    && Models.Missions.count === 0
                    && Models.Missions.invitations.count === 0
                    && !root.createOpen
                Layout.alignment: Qt.AlignCenter
                text: qsTr("No Missions")
                color: KodosiTheme.textSecondary
            }
        }

        ColumnLayout {
            anchors.fill: parent
            visible: root.peopleOpen
                && root.selectedMissionId.length === 0
            Accessible.ignored: !visible
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 48
                Layout.leftMargin: KodosiTheme.spacing3
                Layout.rightMargin: KodosiTheme.spacing3

                KIconButton {
                    objectName: "missions.people.close"
                    Accessible.id: objectName
                    glyph: "chevron-left"
                    Accessible.name: qsTr("Back to Missions")
                    onClicked: root.closePeople()
                }

                PlainLabel {
                    Layout.fillWidth: true
                    text: qsTr("People")
                    color: KodosiTheme.textPrimary
                    font.weight: Font.DemiBold
                }

                KIconButton {
                    objectName: "missions.people.add"
                    Accessible.id: objectName
                    glyph: root.addFriendOpen ? "close" : "plus"
                    Accessible.name: root.addFriendOpen
                        ? qsTr("Cancel adding friend")
                        : qsTr("Add friend")
                    onClicked: {
                        root.addFriendOpen = !root.addFriendOpen
                        if (!root.addFriendOpen)
                            root.friendDraft = ""
                    }
                }
            }

            Rectangle {
                visible: root.addFriendOpen
                Layout.fillWidth: true
                implicitHeight: addFriendRow.implicitHeight + 16
                color: KodosiTheme.surfaceRaised

                RowLayout {
                    id: addFriendRow
                    anchors.fill: parent
                    anchors.margins: KodosiTheme.spacing3
                    spacing: KodosiTheme.spacing2

                    KTextField {
                        objectName: "missions.people.add.handle"
                        Accessible.id: objectName
                        Layout.fillWidth: true
                        placeholderText: qsTr("Kodosi handle")
                        text: root.friendDraft
                        onTextEdited: root.friendDraft = text
                        onAccepted: root.submitFriendRequest()
                    }

                    KButton {
                        objectName: "missions.people.add.send"
                        Accessible.id: objectName
                        text: qsTr("Send")
                        variant: "primary"
                        compact: true
                        enabled: root.friendDraft.trim().length > 0
                        onClicked: root.submitFriendRequest()
                    }
                }
            }

            RowLayout {
                visible: !Models.People.ready
                    || Models.PeopleActions.lastError.length > 0
                Layout.fillWidth: true
                Layout.margins: KodosiTheme.spacing3
                spacing: KodosiTheme.spacing2

                KBusyIndicator {
                    visible: !Models.People.ready
                        && Models.PeopleActions.lastError.length === 0
                    running: visible
                    implicitWidth: 20
                    implicitHeight: 20
                }

                PlainLabel {
                    Layout.fillWidth: true
                    text: Models.PeopleActions.lastError.length > 0
                        ? Models.PeopleActions.lastError
                        : qsTr("Loading People")
                    color: Models.PeopleActions.lastError.length > 0
                        ? KodosiTheme.danger
                        : KodosiTheme.textSecondary
                    wrapMode: Text.Wrap
                }

                KButton {
                    objectName: "missions.people.error.retry"
                    Accessible.id: objectName
                    visible: Models.PeopleActions.lastError.length > 0
                    text: qsTr("Retry")
                    compact: true
                    onClicked: Models.PeopleActions.refresh()
                }
            }

            ListView {
                objectName: "missions.people.directory"
                Accessible.id: objectName
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: Models.People
                clip: true
                spacing: 2
                leftMargin: KodosiTheme.spacing3
                rightMargin: KodosiTheme.spacing3
                topMargin: KodosiTheme.spacing2
                bottomMargin: KodosiTheme.spacing2

                delegate: RowLayout {
                    id: personDelegate

                    required property string handle
                    required property string displayName
                    required property string relationshipName

                    width: ListView.view.width
                    height: 48
                    spacing: KodosiTheme.spacing2
                    objectName: "missions.people." + relationshipName
                        + "." + handle
                    Accessible.id: objectName

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1

                        PlainLabel {
                            Layout.fillWidth: true
                            text: personDelegate.displayName
                            color: KodosiTheme.textPrimary
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }

                        PlainLabel {
                            text: personDelegate.relationshipName === "friend"
                                ? qsTr("@%1 · Friend").arg(
                                    personDelegate.handle)
                                : personDelegate.relationshipName === "incoming"
                                    ? qsTr("@%1 · Needs a reply").arg(
                                        personDelegate.handle)
                                    : qsTr("@%1 · Awaiting").arg(
                                        personDelegate.handle)
                            color: KodosiTheme.textSecondary
                            font.pixelSize: 9
                        }
                    }

                    KButton {
                        visible: parent.relationshipName === "incoming"
                        objectName: "missions.people."
                            + parent.handle + ".accept"
                        Accessible.id: objectName
                        text: qsTr("Accept")
                        compact: true
                        variant: "primary"
                        onClicked: Models.PeopleActions.accept(parent.handle)
                    }

                    KButton {
                        visible: parent.relationshipName === "incoming"
                        objectName: "missions.people."
                            + parent.handle + ".decline"
                        Accessible.id: objectName
                        text: qsTr("Decline")
                        compact: true
                        variant: "quiet"
                        onClicked: Models.PeopleActions.reject(parent.handle)
                    }

                    KButton {
                        visible: parent.relationshipName === "outgoing"
                        objectName: "missions.people."
                            + parent.handle + ".cancel"
                        Accessible.id: objectName
                        text: qsTr("Cancel")
                        compact: true
                        variant: "quiet"
                        onClicked: Models.PeopleActions.cancel(parent.handle)
                    }

                    KButton {
                        visible: parent.relationshipName === "friend"
                        objectName: "missions.people."
                            + parent.handle + ".remove"
                        Accessible.id: objectName
                        text: qsTr("Remove")
                        compact: true
                        variant: "dangerQuiet"
                        onClicked: {
                            root.pendingFriendRemovalHandle = parent.handle
                            root.pendingFriendRemovalName = parent.displayName
                            removeFriendDialog.open()
                        }
                    }
                }
            }

            PlainLabel {
                visible: Models.People.ready && Models.People.count === 0
                Layout.alignment: Qt.AlignCenter
                text: qsTr("No one on call yet")
                color: KodosiTheme.textSecondary
            }
        }

        MissionDetailView {
            visible: root.selectedMissionId.length > 0
            enabled: visible
            anchors.fill: parent
            missionId: root.selectedMissionId
            missionName: root.selectedMissionName
            onCloseRequested: root.closeMission()
        }
    }

    KDialog {
        id: removeFriendDialog
        objectName: "missions.people.remove.confirmation"
        anchors.centerIn: parent
        width: Math.min(420, root.width - 32)
        title: qsTr("Remove %1?").arg(
            root.pendingFriendRemovalName.length > 0
                ? root.pendingFriendRemovalName
                : qsTr("friend"))

        contentItem: ColumnLayout {
            spacing: KodosiTheme.spacing3

            PlainLabel {
                Layout.fillWidth: true
                text: qsTr("Existing Mission membership is unchanged.")
                color: KodosiTheme.textSecondary
                wrapMode: Text.Wrap
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight

                KButton {
                    text: qsTr("Cancel")
                    variant: "quiet"
                    onClicked: removeFriendDialog.close()
                }

                KButton {
                    objectName: "missions.people.remove.confirm"
                    Accessible.id: objectName
                    text: qsTr("Remove friend")
                    variant: "danger"
                    onClicked: {
                        Models.PeopleActions.remove(
                            root.pendingFriendRemovalHandle)
                        removeFriendDialog.close()
                    }
                }
            }
        }
    }
}
