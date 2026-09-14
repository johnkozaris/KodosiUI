pragma ComponentBehavior: Bound
import Kodosi 1.0
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kodosi.Models 1.0 as Models

Item {
    id: root

    property bool detailVisible: Models.Workspace.selectedMissionId.length > 0
    property int sessionRevision: 0

    Accessible.id: objectName
    objectName: "panel.missions"

    Connections {
        function onModelReset() {
            root.sessionRevision += 1;
        }

        target: Models.Sessions
    }
    Connections {
        function onMissionChanged() {
            if (!Models.Workspace.selectedMissionId.length)
                root.detailVisible = false;
        }

        target: Models.Workspace
    }
    KScrollView {
        anchors.fill: parent
        anchors.margins: 24

        ColumnLayout {
            spacing: 14
            width: Math.min(720, parent.width)

            RowLayout {
                Layout.fillWidth: true

                KButton {
                    Accessible.id: objectName
                    iconName: "chevron-left"
                    objectName: "panel.missionsView.missions"
                    text: qsTr("Missions")
                    variant: "quiet"
                    visible: root.detailVisible

                    onClicked: {
                        root.detailVisible = false;
                    }
                }
                PlainLabel {
                    Layout.fillWidth: true
                    color: KodosiTheme.textPrimary
                    font.pixelSize: 22
                    text: root.detailVisible ? (Models.Workspace.mission.name || qsTr("Loading Mission…")) : qsTr("Missions")
                }
                KButton {
                    Accessible.id: objectName
                    enabled: Models.Workspace.signedIn
                    objectName: "panel.missionsView.new-mission"
                    text: qsTr("New Mission")
                    visible: !root.detailVisible

                    onClicked: create.open()
                }
            }
            PlainLabel {
                color: KodosiTheme.textSecondary
                text: qsTr("Sign in to organize shared projects.")
                visible: !Models.Workspace.signedIn
            }
            PlainLabel {
                Accessible.id: objectName
                Layout.fillWidth: true
                color: KodosiTheme.textSecondary
                objectName: "panel.missions.truncated"
                text: qsTr("Some Missions or invitations are hidden. Leave Missions or decline invitations to reveal more.")
                visible: !root.detailVisible && Models.Workspace.missionCatalogTruncated
                wrapMode: Text.WordWrap
            }
            Repeater {
                model: root.detailVisible ? [] : Models.Workspace.invitations

                delegate: RowLayout {
                    id: entry0

                    required property var modelData

                    Layout.fillWidth: true

                    PlainLabel {
                        Layout.fillWidth: true
                        color: KodosiTheme.textPrimary
                        text: qsTr("Invitation to %1").arg(entry0.modelData.roomName)
                    }
                    KButton {
                        Accessible.id: objectName
                        objectName: "panel.missionsView.join" + "." + entry0.modelData.id
                        text: qsTr("Join")

                        onClicked: Models.Workspace.acceptInvitation(entry0.modelData.id)
                    }
                    KButton {
                        Accessible.id: objectName
                        objectName: "panel.missionsView.decline" + "." + entry0.modelData.id
                        text: qsTr("Decline")
                        variant: "quiet"

                        onClicked: Models.Workspace.rejectInvitation(entry0.modelData.id)
                    }
                }
            }
            Repeater {
                model: root.detailVisible ? [] : Models.Workspace.missions

                delegate: KItemDelegate {
                    id: missionRow

                    required property var modelData

                    Accessible.id: objectName
                    Layout.fillWidth: true
                    objectName: "panel.missions." + missionRow.modelData.id
                    text: missionRow.modelData.name

                    onClicked: {
                        Models.Workspace.openMission(missionRow.modelData.id);
                        root.detailVisible = true;
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                visible: root.detailVisible && Models.Workspace.mission.ownerUserId === Models.Workspace.userId

                KTextField {
                    id: rename

                    Accessible.id: objectName
                    Accessible.name: qsTr("Mission name")
                    Layout.fillWidth: true
                    objectName: "panel.missions.rename.name"
                    text: Models.Workspace.mission.name || ""
                }
                KButton {
                    Accessible.id: objectName
                    objectName: "panel.missionsView.rename"
                    text: qsTr("Rename")

                    onClicked: Models.Workspace.renameMission(rename.text)
                }
            }
            PlainLabel {
                color: KodosiTheme.textPrimary
                font.weight: Font.DemiBold
                text: qsTr("People")
                visible: root.detailVisible
            }
            Repeater {
                model: root.detailVisible ? Models.Workspace.missionMembers : []

                delegate: RowLayout {
                    id: entry2

                    required property var modelData

                    Layout.fillWidth: true

                    PlainLabel {
                        Layout.fillWidth: true
                        color: KodosiTheme.textPrimary
                        text: entry2.modelData.displayName || entry2.modelData.handle
                    }
                    KButton {
                        Accessible.id: objectName
                        objectName: "panel.missionsView.remove" + "." + entry2.modelData.userId
                        text: qsTr("Remove")
                        variant: "quiet"
                        visible: Models.Workspace.mission.ownerUserId === Models.Workspace.userId && !entry2.modelData.isOwner

                        onClicked: Models.Workspace.removeMember(entry2.modelData.userId)
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                visible: root.detailVisible && Models.Workspace.mission.ownerUserId === Models.Workspace.userId

                KComboBox {
                    id: friend

                    Accessible.id: objectName
                    Accessible.name: qsTr("Invite friend")
                    Layout.fillWidth: true
                    model: Models.Workspace.friends
                    objectName: "panel.missionsView.friend"
                    textRole: "displayName"
                    valueRole: "userId"
                }
                KButton {
                    Accessible.id: objectName
                    enabled: friend.currentIndex >= 0
                    objectName: "panel.missionsView.invite"
                    text: qsTr("Invite")

                    onClicked: Models.Workspace.inviteToMission(friend.currentValue)
                }
            }
            PlainLabel {
                color: KodosiTheme.textPrimary
                font.weight: Font.DemiBold
                text: qsTr("Terminals")
                visible: root.detailVisible
            }
            Repeater {
                model: root.detailVisible ? Models.Workspace.missionSessionIds : []

                delegate: RowLayout {
                    id: entry3

                    required property string modelData
                    readonly property var session: root.sessionRevision >= 0 ? Models.Sessions.presentationForSession(entry3.modelData) : ({})

                    Layout.fillWidth: true

                    PlainLabel {
                        Layout.fillWidth: true
                        color: KodosiTheme.textPrimary
                        text: entry3.session.name || qsTr("Terminal not shared with you")
                    }
                    KButton {
                        Accessible.id: objectName
                        enabled: !!entry3.session.sessionId
                        objectName: "panel.missionsView.open" + "." + entry3.modelData
                        text: qsTr("Open")

                        onClicked: Models.Workspace.activateSession(entry3.modelData)
                    }
                }
            }
            PlainLabel {
                color: KodosiTheme.textSecondary
                text: qsTr("Attach a terminal from its session details.")
                visible: root.detailVisible && Models.Workspace.missionSessionIds.length === 0
            }
            RowLayout {
                visible: root.detailVisible && !!Models.Workspace.mission.id

                KButton {
                    Accessible.id: objectName
                    objectName: "panel.missionsView.leave-mission"
                    text: qsTr("Leave Mission")
                    variant: "quiet"
                    visible: Models.Workspace.mission.ownerUserId !== Models.Workspace.userId

                    onClicked: {
                        Models.Workspace.leaveMission();
                        root.detailVisible = false;
                    }
                }
                KButton {
                    Accessible.id: objectName
                    objectName: "panel.missionsView.delete-mission"
                    text: qsTr("Delete Mission…")
                    variant: "quiet"
                    visible: Models.Workspace.mission.ownerUserId === Models.Workspace.userId

                    onClicked: remove.open()
                }
            }
        }
    }
    KDialog {
        id: create

        objectName: "panel.missions.create"
        standardButtons: Dialog.Ok | Dialog.Cancel
        title: qsTr("New Mission")

        onAccepted: Models.Workspace.createMission(missionName.text)

        KTextField {
            id: missionName

            Accessible.id: objectName
            Accessible.name: qsTr("Mission name")
            objectName: "panel.missions.create.name"
            placeholderText: qsTr("Project name")
        }
    }
    KDialog {
        id: remove

        standardButtons: Dialog.Ok | Dialog.Cancel
        title: qsTr("Delete this Mission?")

        onAccepted: {
            Models.Workspace.deleteMission();
            root.detailVisible = false;
        }

        PlainLabel {
            color: KodosiTheme.textPrimary
            text: qsTr("Attached terminals will keep running.")
        }
    }
}
