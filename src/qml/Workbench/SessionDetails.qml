pragma ComponentBehavior: Bound
import Kodosi 1.0
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kodosi.Models 1.0 as Models

KPopover {
    id: root

    property var selectedUsers: []
    property var originalUsers: []
    property string selectedMission: ""
    property var session: ({})
    property string sessionId: ""

    function openSession(id) {
        sessionId = id;
        refresh();
        originalUsers = session.sharedWith || [];
        selectedUsers = originalUsers.slice();
        selectedMission = session.roomId || "";
        open();
    }
    function refresh() {
        session = Models.Sessions.presentationForSession(sessionId);
        if (JSON.stringify(selectedUsers.slice().sort()) === JSON.stringify((session.sharedWith || []).slice().sort()))
            originalUsers = (session.sharedWith || []).slice();
    }

    focus: true
    height: Math.min(600, parent ? parent.height - 32 : 600)
    modal: true
    objectName: "panel.sessionDetails"
    padding: 18
    parent: Overlay.overlay
    width: Math.min(460, parent ? parent.width - 32 : 460)
    x: parent ? parent.width - width - 12 : 0
    y: parent ? (parent.height - height) / 2 : 0

    Connections {
        function onModelReset() {
            root.refresh();
            if (!root.session.sessionId)
                root.close();
        }

        target: Models.Sessions
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        RowLayout {
            PlainLabel {
                Layout.fillWidth: true
                color: KodosiTheme.textPrimary
                elide: Text.ElideRight
                font.pixelSize: 18
                text: root.session.name || qsTr("Session")
            }
            KIconButton {
                Accessible.id: objectName
                Accessible.name: qsTr("Close session details")
                glyph: "close"
                objectName: "panel.sessionDetails.close"

                onClicked: root.close()
            }
        }
        PlainLabel { text: qsTr("Workspace"); color: KodosiTheme.textPrimary; font.weight: Font.DemiBold }
        PlainLabel {
            color: KodosiTheme.textSecondary
            text: root.session.kind === "local" ? qsTr("This computer") : (root.session.hostName || root.session.ownerName || qsTr("Remote computer"))
        }
        PlainLabel { Layout.fillWidth: true; text: root.session.displayDirectory || ""; color: KodosiTheme.textSecondary; wrapMode: Text.WrapAnywhere }
        KButton {
            Accessible.id: objectName
            objectName: "panel.sessionDetails.open-project-folder"
            text: qsTr("Open project folder")
            visible: root.session.kind === "local" && !!root.session.workingDirectory

            onClicked: Models.DesktopFiles.openPath(root.session.workingDirectory)
        }
        PlainLabel { text: qsTr("Mission"); color: KodosiTheme.textPrimary; font.weight: Font.DemiBold }
        RowLayout {
            Layout.fillWidth: true
            visible: root.session.isOwner === true
            KComboBox {
                id: mission
                Accessible.id: objectName
                Accessible.name: qsTr("Mission")
                Layout.fillWidth: true
                model: [{ id: "", name: qsTr("No Mission") }].concat(Models.Workspace.missions)
                objectName: "panel.sessionDetails.mission"
                textRole: "name"
                valueRole: "id"
                currentIndex: { for (let i = 0; i < model.length; ++i) if (model[i].id === root.selectedMission) return i; return 0; }
                onActivated: root.selectedMission = currentValue
            }
            KButton {
                Accessible.id: objectName
                objectName: "panel.sessionDetails.mission.save"
                text: qsTr("Save")
                visible: root.selectedMission !== (root.session.roomId || "")
                enabled: !Models.Workspace.busy
                onClicked: Models.Workspace.attachMission(root.sessionId, root.selectedMission)
            }
        }
        PlainLabel { text: root.session.roomName || qsTr("No Mission"); color: KodosiTheme.textSecondary; visible: root.session.isOwner !== true }
        PlainLabel {
            color: KodosiTheme.textPrimary
            font.weight: Font.DemiBold
            text: qsTr("People")
            visible: root.session.isOwner === true && root.session.kind === "local" && Models.Workspace.signedIn
        }
        PlainLabel {
            Layout.fillWidth: true
            color: KodosiTheme.textSecondary
            text: qsTr("Selected friends can control this terminal. Shell access is not limited to this project.")
            visible: root.session.isOwner === true && root.session.kind === "local" && Models.Workspace.signedIn
            wrapMode: Text.WordWrap
        }
        PlainLabel {
            Layout.fillWidth: true
            text: qsTr("Connected now")
            color: KodosiTheme.textSecondary
            visible: (root.session.connectedUsers || []).filter(user => user !== Models.Workspace.userId).length > 0
        }
        Repeater {
            model: (root.session.connectedUsers || []).filter(user => user !== Models.Workspace.userId)
            delegate: PlainLabel {
                required property string modelData
                color: KodosiTheme.textPrimary
                text: { const person = Models.Workspace.friends.find(friend => friend.userId === modelData); return person ? person.displayName || person.handle : qsTr("Connected person"); }
            }
        }
        ListView {
            id: friends

            Layout.fillHeight: true
            Layout.fillWidth: true
            clip: true
            model: Models.Workspace.friends
            visible: root.session.isOwner === true && root.session.kind === "local" && Models.Workspace.signedIn

            delegate: KCheckBox {
                required property var modelData

                Accessible.id: objectName
                checked: root.selectedUsers.indexOf(modelData.userId) >= 0
                objectName: "panel.sessionDetails.friend." + modelData.userId
                text: modelData.displayName || modelData.handle
                width: friends.width

                onToggled: {
                    const users = root.selectedUsers.slice();
                    const index = users.indexOf(modelData.userId);
                    if (checked && index < 0)
                        users.push(modelData.userId);
                    if (!checked && index >= 0)
                        users.splice(index, 1);
                    root.selectedUsers = users;
                }
            }
        }
        KButton {
            Accessible.id: objectName
            enabled: !Models.Workspace.busy && JSON.stringify(root.selectedUsers.slice().sort()) !== JSON.stringify(root.originalUsers.slice().sort())
            objectName: "panel.sessionDetails.share"
            text: qsTr("Save sharing")
            visible: root.session.isOwner === true && root.session.kind === "local" && Models.Workspace.signedIn

            onClicked: Models.Workspace.shareSession(root.sessionId, root.selectedUsers, root.originalUsers)
        }
        PlainLabel {
            Layout.fillWidth: true
            color: KodosiTheme.textSecondary
            text: qsTr("Change sharing on the computer hosting this terminal.")
            visible: root.session.isOwner === true && root.session.kind === "remote"
            wrapMode: Text.WordWrap
        }
        Item {
            Layout.fillHeight: true
            visible: root.session.kind !== "local"
        }
        PlainLabel {
            Layout.fillWidth: true
            color: KodosiTheme.textSecondary
            text: root.session.message || ""
            visible: !!root.session.message
            wrapMode: Text.WordWrap
        }
        KButton {
            Accessible.id: objectName
            objectName: "panel.sessionDetails.leave-shared-session"
            text: qsTr("Leave shared session")
            variant: "quiet"
            visible: root.session.isOwner === false

            onClicked: {
                Models.Workspace.leaveSession(root.sessionId);
                root.close();
            }
        }
    }
}
