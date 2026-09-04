pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kodosi.Models 1.0 as Models

KPopover {
    id: root
    objectName: "panel.utility"

    signal openSettingsRequested()
    signal openShortcutsRequested()
    signal signInRequested()
    signal signOutRequested()

    property Item anchorItem
    property bool signedIn: false
    property bool authBusy: false

    parent: Overlay.overlay
    width: Math.min(240, parent ? parent.width - 24 : 240)
    height: contentColumn.implicitHeight
    modal: false
    dim: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    function reposition() {
        if (!parent || !anchorItem)
            return
        const anchor = anchorItem.mapToItem(
            parent,
            anchorItem.width,
            anchorItem.height)
        x = Math.max(
            12,
            Math.min(parent.width - width - 12, anchor.x - width))
        y = Math.min(parent.height - height - 12, anchor.y + 6)
    }

    function openAt(item) {
        anchorItem = item
        reposition()
        open()
    }

    onOpened: {
        reposition()
        settingsButton.forceActiveFocus(Qt.PopupFocusReason)
    }

    contentItem: ColumnLayout {
        id: contentColumn
        objectName: "panel.utility.content"
        Accessible.id: objectName
        Accessible.role: Accessible.Dialog
        Accessible.name: root.signedIn
            ? qsTr("Account, appearance, and app controls")
            : qsTr("Appearance and app controls")
        spacing: 0

        KButton {
            id: settingsButton
            objectName: "panel.utility.settings"
            Accessible.id: objectName
            Layout.fillWidth: true
            Layout.leftMargin: KodosiTheme.spacing2
            Layout.rightMargin: KodosiTheme.spacing2
            Layout.topMargin: KodosiTheme.spacing2
            variant: "quiet"
            iconName: "settings"
            text: qsTr("Settings")
            contentLeftAligned: true
            Accessible.name: qsTr("Open settings")
            onClicked: {
                root.close()
                root.openSettingsRequested()
            }
        }

        KButton {
            objectName: "panel.utility.shortcuts"
            Accessible.id: objectName
            Layout.fillWidth: true
            Layout.leftMargin: KodosiTheme.spacing2
            Layout.rightMargin: KodosiTheme.spacing2
            variant: "quiet"
            iconName: "command"
            text: qsTr("Keyboard shortcuts")
            contentLeftAligned: true
            Accessible.name: qsTr("Open keyboard shortcuts")
            onClicked: {
                root.close()
                root.openShortcutsRequested()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: KodosiTheme.spacing5
            Layout.rightMargin: KodosiTheme.spacing5
            Layout.topMargin: KodosiTheme.spacing2
            Layout.preferredHeight: 1
            color: KodosiTheme.seam
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: KodosiTheme.spacing5
            spacing: KodosiTheme.spacing2

            ButtonGroup {
                id: appearanceGroup
                exclusive: true
            }

            KSegmentedBar {
                Layout.fillWidth: true
                Layout.preferredHeight: 36

                KButton {
                    objectName: "panel.utility.appearance.light"
                    Accessible.id: objectName
                    Layout.fillWidth: true
                    variant: "quiet"
                    compact: true
                    checkable: true
                    ButtonGroup.group: appearanceGroup
                    tonalSelection: true
                    checked:
                        Models.Appearance.preference
                            === Models.Appearance.Light
                    text: qsTr("Light")
                    Accessible.name: qsTr("Use light appearance")
                    onClicked:
                        Models.Appearance.setPreference(
                            Models.Appearance.Light)
                }

                KButton {
                    objectName: "panel.utility.appearance.dark"
                    Accessible.id: objectName
                    Layout.fillWidth: true
                    variant: "quiet"
                    compact: true
                    checkable: true
                    ButtonGroup.group: appearanceGroup
                    tonalSelection: true
                    checked:
                        Models.Appearance.preference
                            === Models.Appearance.Dark
                    text: qsTr("Dark")
                    Accessible.name: qsTr("Use dark appearance")
                    onClicked:
                        Models.Appearance.setPreference(
                            Models.Appearance.Dark)
                }

                KButton {
                    objectName: "panel.utility.appearance.system"
                    Accessible.id: objectName
                    Layout.fillWidth: true
                    variant: "quiet"
                    compact: true
                    checkable: true
                    ButtonGroup.group: appearanceGroup
                    tonalSelection: true
                    checked:
                        Models.Appearance.preference
                            === Models.Appearance.System
                    text: qsTr("System")
                    Accessible.name: qsTr("Follow system appearance")
                    onClicked:
                        Models.Appearance.setPreference(
                            Models.Appearance.System)
                }
            }

            PlainLabel {
                Layout.fillWidth: true
                visible: Models.Appearance.settingsError.length > 0
                text: Models.Appearance.settingsError
                color: KodosiTheme.danger
                font.pixelSize: 9
                wrapMode: Text.Wrap
                Accessible.name: text
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: KodosiTheme.seam
        }

        KButton {
            objectName: root.signedIn
                ? "panel.utility.signOut"
                : "panel.utility.signIn"
            Accessible.id: objectName
            Layout.fillWidth: true
            Layout.margins: KodosiTheme.spacing2
            variant: root.signedIn ? "dangerQuiet" : "quiet"
            iconName: root.signedIn ? "logout" : "login"
            text: root.signedIn
                ? qsTr("Sign out")
                : root.authBusy
                  ? qsTr("Signing in")
                  : qsTr("Sign in")
            contentLeftAligned: true
            enabled: !root.authBusy
            Accessible.name: text
            onClicked: {
                root.close()
                if (root.signedIn)
                    root.signOutRequested()
                else
                    root.signInRequested()
            }
        }
    }
}
