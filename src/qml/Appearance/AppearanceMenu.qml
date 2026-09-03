pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kodosi.Models 1.0 as Models

KPopover {
    id: root
    objectName: "panel.utility"

    signal openSettingsRequested()
    signal signInRequested()
    signal signOutRequested()

    property Item anchorItem
    property bool signedIn: false
    property bool authBusy: false

    parent: Overlay.overlay
    width: Math.min(320, parent ? parent.width - 24 : 320)
    height: contentColumn.implicitHeight
    modal: true
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
        closeButton.forceActiveFocus(Qt.PopupFocusReason)
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

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 58
            color: KodosiTheme.surfaceElevated
            topLeftRadius: KodosiTheme.radiusLarge
            topRightRadius: KodosiTheme.radiusLarge

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: KodosiTheme.spacing5
                anchors.rightMargin: KodosiTheme.spacing3
                spacing: KodosiTheme.spacing3

                KIcon {
                    Layout.preferredWidth: 22
                    Layout.preferredHeight: 22
                    name: "account"
                    color: KodosiTheme.accent
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1

                    PlainLabel {
                        text: root.signedIn
                            ? qsTr("Your Kodosi")
                            : qsTr("Kodosi controls")
                        color: KodosiTheme.textPrimary
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }

                    PlainLabel {
                        text: root.signedIn
                            ? qsTr("Account, appearance, and app controls")
                            : qsTr("Appearance and app controls")
                        color: KodosiTheme.textSecondary
                        font.pixelSize: 9
                        elide: Text.ElideRight
                    }
                }

                KIconButton {
                    id: closeButton
                    objectName: "panel.utility.close"
                    Accessible.id: objectName
                    glyph: "close"
                    size: 28
                    Accessible.name: qsTr("Close app controls")
                    onClicked: root.close()
                }
            }
        }

        KButton {
            objectName: "panel.utility.settings"
            Accessible.id: objectName
            Layout.fillWidth: true
            Layout.leftMargin: KodosiTheme.spacing2
            Layout.rightMargin: KodosiTheme.spacing2
            Layout.topMargin: KodosiTheme.spacing2
            variant: "quiet"
            iconName: "settings"
            text: qsTr("Settings")
            secondaryText: qsTr("Preferences, trust, and supervision")
            secondaryMaximumWidth: 208
            contentLeftAligned: true
            Accessible.name: qsTr("Open settings")
            onClicked: {
                root.close()
                root.openSettingsRequested()
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

            PlainLabel {
                text: qsTr("Appearance")
                color: KodosiTheme.textSecondary
                font.pixelSize: 10
                font.weight: Font.DemiBold
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
                text: Models.Appearance.preference
                    === Models.Appearance.System
                    ? qsTr("Follows the desktop appearance.")
                    : qsTr("Overrides the desktop appearance.")
                color: KodosiTheme.textTertiary
                font.pixelSize: 9
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
            secondaryText: root.signedIn
                ? qsTr("Local agents keep running")
                : qsTr("Connect your Kodosi account")
            secondaryMaximumWidth: 208
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
