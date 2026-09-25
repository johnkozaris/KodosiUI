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
                    Accessible.name: qsTr("Username")
                    Layout.fillWidth: true
                    objectName: "panel.people.username"
                    placeholderText: qsTr("Username")
                }
                KButton {
                    Accessible.id: objectName
                    enabled: Models.Account.signedIn && handle.text.trim().length > 0
                    objectName: "panel.peopleView.add-friend"
                    text: qsTr("Add")

                    onClicked: Models.People.request(handle.text)
                }
            }
            PlainLabel {
                color: KodosiTheme.textSecondary
                text: qsTr("Sign in to add people.")
                visible: !Models.Account.signedIn
            }
            Repeater {
                model: Models.People.incoming

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

                        onClicked: Models.People.accept(entry0.modelData.handle)
                    }
                    KButton {
                        Accessible.id: objectName
                        objectName: "panel.peopleView.decline" + "." + entry0.modelData.userId
                        text: qsTr("Decline")
                        variant: KButton.Quiet

                        onClicked: Models.People.decline(entry0.modelData.handle)
                    }
                }
            }
            Repeater {
                model: Models.People.outgoing

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
                        text: qsTr("Cancel")
                        variant: KButton.Quiet

                        onClicked: Models.People.cancel(entry1.modelData.handle)
                    }
                }
            }
            Repeater {
                model: Models.People.friends

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
                        variant: KButton.Quiet

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
                visible: Models.Account.signedIn && Models.People.friends.length === 0
            }
        }
    }
    KDialog {
        id: removeFriend

        property string username: ""

        standardButtons: Dialog.Ok | Dialog.Cancel
        title: qsTr("Remove %1?").arg(username)

        onAccepted: Models.People.remove(username)

        PlainLabel {
            color: KodosiTheme.textPrimary
            text: qsTr("They will lose access to terminals shared with them.")
        }
    }
}
