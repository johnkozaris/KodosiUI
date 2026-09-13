pragma ComponentBehavior: Bound
import Kodosi 1.0
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kodosi.Models 1.0 as Models

Item {
    id: root

    Accessible.id: objectName
    objectName: "panel.people"

    KScrollView {
        anchors.fill: parent
        anchors.margins: 24

        ColumnLayout {
            spacing: 16
            width: Math.min(640, parent.width)

            PlainLabel {
                color: KodosiTheme.textPrimary
                font.pixelSize: 22
                text: qsTr("People")
            }
            RowLayout {
                Layout.fillWidth: true

                KTextField {
                    id: handle

                    Accessible.id: objectName
                    Accessible.name: qsTr("Friend's username")
                    Layout.fillWidth: true
                    objectName: "panel.people.username"
                    placeholderText: qsTr("Friend's username")
                }
                KButton {
                    Accessible.id: objectName
                    enabled: Models.Workspace.signedIn && handle.text.trim().length > 0
                    objectName: "panel.peopleView.add-friend"
                    text: qsTr("Add friend")

                    onClicked: Models.Workspace.requestFriend(handle.text)
                }
            }
            PlainLabel {
                color: KodosiTheme.textSecondary
                text: qsTr("Sign in to connect with friends.")
                visible: !Models.Workspace.signedIn
            }
            Repeater {
                model: Models.Workspace.incoming

                delegate: RowLayout {
                    id: entry0

                    required property var modelData

                    Layout.fillWidth: true

                    PlainLabel {
                        Layout.fillWidth: true
                        color: KodosiTheme.textPrimary
                        text: entry0.modelData.displayName || entry0.modelData.handle
                    }
                    KButton {
                        Accessible.id: objectName
                        objectName: "panel.peopleView.accept" + "." + entry0.modelData.userId
                        text: qsTr("Accept")

                        onClicked: Models.Workspace.acceptFriend(entry0.modelData.handle)
                    }
                    KButton {
                        Accessible.id: objectName
                        objectName: "panel.peopleView.decline" + "." + entry0.modelData.userId
                        text: qsTr("Decline")
                        variant: "quiet"

                        onClicked: Models.Workspace.rejectFriend(entry0.modelData.handle)
                    }
                }
            }
            Repeater {
                model: Models.Workspace.outgoing

                delegate: RowLayout {
                    id: entry1

                    required property var modelData

                    Layout.fillWidth: true

                    PlainLabel {
                        Layout.fillWidth: true
                        color: KodosiTheme.textSecondary
                        text: qsTr("Request sent to %1").arg(entry1.modelData.displayName || entry1.modelData.handle)
                    }
                    KButton {
                        Accessible.id: objectName
                        objectName: "panel.peopleView.cancel-request" + "." + entry1.modelData.userId
                        text: qsTr("Cancel request")
                        variant: "quiet"

                        onClicked: Models.Workspace.cancelFriend(entry1.modelData.handle)
                    }
                }
            }
            Repeater {
                model: Models.Workspace.friends

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
                        objectName: "panel.people.remove." + entry2.modelData.userId
                        text: qsTr("Remove…")
                        variant: "quiet"

                        onClicked: {
                            removeFriend.username = entry2.modelData.handle;
                            removeFriend.open();
                        }
                    }
                }
            }
            PlainLabel {
                color: KodosiTheme.textSecondary
                text: qsTr("No friends yet.")
                visible: Models.Workspace.signedIn && Models.Workspace.friends.length === 0
            }
        }
    }
    KDialog {
        id: removeFriend

        property string username: ""

        standardButtons: Dialog.Ok | Dialog.Cancel
        title: qsTr("Remove %1?").arg(username)

        onAccepted: Models.Workspace.removeFriend(username)

        PlainLabel {
            color: KodosiTheme.textPrimary
            text: qsTr("They will lose access to sessions shared with them.")
        }
    }
}
