pragma ComponentBehavior: Bound
import Kodosi 1.0
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kodosi.Models 1.0 as Models

Item {
    id: root

    property bool detailVisible: Models.Missions.selectedMissionId.length > 0
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
        function onSelectionChanged() {
            if (!Models.Missions.selectedMissionId.length)
                root.detailVisible = false;
        }

        target: Models.Missions
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
                    variant: KButton.Quiet
                    visible: root.detailVisible

                    onClicked: {
                        root.detailVisible = false;
                    }
                }
                PlainLabel {
                    Layout.fillWidth: true
                    color: KodosiTheme.textPrimary
                    font.pixelSize: 22
                    text: root.detailVisible ? (Models.Missions.selectedMission.name || qsTr("Loading Mission…")) : qsTr("Missions")
                }
                KButton {
                    Accessible.id: objectName
                    enabled: Models.Account.signedIn
                    objectName: "panel.missionsView.new-mission"
                    text: qsTr("New Mission")
                    visible: !root.detailVisible

                    onClicked: create.open()
                }
            }
            PlainLabel {
                color: KodosiTheme.textSecondary
                text: qsTr("Sign in to use Missions.")
                visible: !Models.Account.signedIn
            }
            PlainLabel {
                Accessible.id: objectName
                Layout.fillWidth: true
                color: KodosiTheme.textSecondary
                objectName: "panel.missions.truncated"
                text: qsTr("Mission list full. Leave one or decline an invite to see more.")
                visible: !root.detailVisible && Models.Missions.catalogTruncated
                wrapMode: Text.WordWrap
            }
            Repeater {
                model: root.detailVisible ? [] : Models.Missions.invitations

                delegate: RowLayout {
                    id: entry0

                    required property var modelData

                    Layout.fillWidth: true

                    PlainLabel {
                        Layout.fillWidth: true
                        color: KodosiTheme.textPrimary
                        text: qsTr("Invitation to %1").arg(entry0.modelData.missionName)
                    }
                    KButton {
                        Accessible.id: objectName
                        objectName: "panel.missionsView.join" + "." + entry0.modelData.id
                        text: qsTr("Join")

                        onClicked: Models.Missions.acceptInvitation(entry0.modelData.id)
                    }
                    KButton {
                        Accessible.id: objectName
                        objectName: "panel.missionsView.decline" + "." + entry0.modelData.id
                        text: qsTr("Decline")
                        variant: KButton.Quiet

                        onClicked: Models.Missions.declineInvitation(entry0.modelData.id)
                    }
                }
            }
            Repeater {
                model: root.detailVisible ? [] : Models.Missions.missions

                delegate: KItemDelegate {
                    id: missionRow

                    required property var modelData

                    Accessible.id: objectName
                    Layout.fillWidth: true
                    objectName: "panel.missions." + missionRow.modelData.id
                    text: missionRow.modelData.name

                    onClicked: {
                        Models.Missions.open(missionRow.modelData.id);
                        root.detailVisible = true;
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                visible: root.detailVisible && Models.Missions.selectedMission.ownerUserId === Models.Account.userId

                KTextField {
                    id: rename

                    Accessible.id: objectName
                    Accessible.name: qsTr("Mission name")
                    Layout.fillWidth: true
                    objectName: "panel.missions.rename.name"
                    text: Models.Missions.selectedMission.name || ""
                }
                KButton {
                    Accessible.id: objectName
                    objectName: "panel.missionsView.rename"
                    text: qsTr("Rename")

                    onClicked: Models.Missions.rename(rename.text)
                }
            }
            PlainLabel {
                color: KodosiTheme.textPrimary
                font.weight: Font.DemiBold
                text: qsTr("People")
                visible: root.detailVisible
            }
            Repeater {
                model: root.detailVisible ? Models.Missions.members : []

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
                        variant: KButton.Quiet
                        visible: Models.Missions.selectedMission.ownerUserId === Models.Account.userId && !entry2.modelData.isOwner

                        onClicked: Models.Missions.removeMember(entry2.modelData.userId)
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                visible: root.detailVisible && Models.Missions.selectedMission.ownerUserId === Models.Account.userId

                KComboBox {
                    id: friend

                    Accessible.id: objectName
                    Accessible.name: qsTr("Invite friend")
                    Layout.fillWidth: true
                    model: Models.People.friends
                    objectName: "panel.missionsView.friend"
                    textRole: "displayName"
                    valueRole: "userId"
                }
                KButton {
                    Accessible.id: objectName
                    enabled: friend.currentIndex >= 0
                    objectName: "panel.missionsView.invite"
                    text: qsTr("Invite")

                    onClicked: Models.Missions.invite(friend.currentValue)
                }
            }
            PlainLabel {
                color: KodosiTheme.textPrimary
                font.weight: Font.DemiBold
                text: qsTr("Terminals")
                visible: root.detailVisible
            }
            Repeater {
                model: root.detailVisible ? Models.Missions.sessionIds : []

                delegate: RowLayout {
                    id: entry3

                    required property string modelData
                    readonly property var session: root.sessionRevision >= 0 ? Models.Sessions.presentationForSession(entry3.modelData) : ({})

                    Layout.fillWidth: true

                    PlainLabel {
                        Layout.fillWidth: true
                        color: KodosiTheme.textPrimary
                        text: entry3.session.name || qsTr("Unavailable terminal")
                    }
                    KButton {
                        Accessible.id: objectName
                        enabled: !!entry3.session.sessionId
                        objectName: "panel.missionsView.open" + "." + entry3.modelData
                        text: qsTr("Open")

                        onClicked: Models.SessionActions.activate(entry3.modelData)
                    }
                }
            }
            PlainLabel {
                color: KodosiTheme.textSecondary
                text: qsTr("Attach terminals from Details.")
                visible: root.detailVisible && Models.Missions.sessionIds.length === 0
            }
            RowLayout {
                visible: root.detailVisible && !!Models.Missions.selectedMission.id

                KButton {
                    Accessible.id: objectName
                    objectName: "panel.missionsView.leave-mission"
                    text: qsTr("Leave Mission")
                    variant: KButton.Quiet
                    visible: Models.Missions.selectedMission.ownerUserId !== Models.Account.userId

                    onClicked: {
                        Models.Missions.leave();
                        root.detailVisible = false;
                    }
                }
                KButton {
                    Accessible.id: objectName
                    objectName: "panel.missionsView.delete-mission"
                    text: qsTr("Delete Mission…")
                    variant: KButton.Quiet
                    visible: Models.Missions.selectedMission.ownerUserId === Models.Account.userId

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

        onAccepted: Models.Missions.create(missionName.text)

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
            Models.Missions.remove();
            root.detailVisible = false;
        }

        PlainLabel {
            color: KodosiTheme.textPrimary
            text: qsTr("Attached terminals will keep running.")
        }
    }
}
