pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

KPopover {
    id: root
    objectName: "panel.shortcuts"

    readonly property bool compact: width < 600
    readonly property var groups: [
        {
            key: "application",
            title: qsTr("Application"),
            shortcuts: [
                { keys: qsTr("Ctrl + ,"), label: qsTr("Settings") },
                {
                    keys: qsTr("Ctrl + Shift + /"),
                    label: qsTr("Keyboard shortcuts")
                },
                {
                    keys: qsTr("Ctrl + Shift + D"),
                    label: qsTr("Diagnostics")
                }
            ]
        },
        {
            key: "session",
            title: qsTr("Selected session"),
            shortcuts: [
                { keys: qsTr("Ctrl + S"), label: qsTr("New session") },
                {
                    keys: qsTr("Ctrl + I"),
                    label: qsTr("Agent Intelligence")
                },
                {
                    keys: qsTr("Ctrl + Shift + S"),
                    label: qsTr("Share selected session")
                },
                {
                    keys: qsTr("Ctrl + Shift + W"),
                    label: qsTr("Close selected session")
                }
            ]
        },
        {
            key: "workbench",
            title: qsTr("Workbench"),
            shortcuts: [
                { keys: qsTr("Ctrl + B"), label: qsTr("Toggle sidebar") },
                {
                    keys: qsTr("Ctrl + Shift + Enter"),
                    label: qsTr("Toggle focus")
                },
                {
                    keys: qsTr("Ctrl + Alt + Arrow"),
                    label: qsTr("Select adjacent session")
                },
                { keys: qsTr("Escape"), label: qsTr("Exit focus") }
            ]
        }
    ]

    parent: Overlay.overlay
    width: Math.min(620, parent ? parent.width - 32 : 620)
    height: Math.min(460, parent ? parent.height - 32 : 460)
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    modal: true
    dim: true
    focus: true
    closePolicy: Popup.CloseOnEscape

    onOpened: closeButton.forceActiveFocus(Qt.PopupFocusReason)

    background: Item {
        Rectangle {
            anchors.fill: parent
            anchors.topMargin: 10
            anchors.leftMargin: 7
            radius: KodosiTheme.radiusModal
            color: KodosiTheme.shadow
            opacity: 0.46
        }

        Rectangle {
            anchors.fill: parent
            radius: KodosiTheme.radiusModal
            color: KodosiTheme.canvas
        }
    }

    contentItem: ColumnLayout {
        objectName: "panel.shortcuts.content"
        Accessible.id: objectName
        Accessible.role: Accessible.Dialog
        Accessible.name: qsTr("Keyboard shortcuts")
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 62
            Layout.leftMargin: 20
            Layout.rightMargin: 14
            spacing: KodosiTheme.spacing3

            KIcon {
                Layout.preferredWidth: 22
                Layout.preferredHeight: 22
                name: "command"
                color: KodosiTheme.accent
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1

                PlainLabel {
                    text: qsTr("Keyboard Shortcuts")
                    color: KodosiTheme.textPrimary
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                }

                PlainLabel {
                    text: qsTr("Commands available in the current desktop shell")
                    color: KodosiTheme.textSecondary
                    font.pixelSize: 10
                }
            }

            KIconButton {
                id: closeButton
                objectName: "panel.shortcuts.close"
                Accessible.id: objectName
                glyph: "close"
                Accessible.name: qsTr("Close keyboard shortcuts")
                onClicked: root.close()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: KodosiTheme.seam
        }

        KScrollView {
            id: shortcutsScroll
            objectName: "panel.shortcuts.scroll"
            Accessible.id: objectName
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true

            GridLayout {
                width: shortcutsScroll.availableWidth
                columns: root.compact ? 1 : 2
                columnSpacing: 22
                rowSpacing: 0

                Repeater {
                    model: root.groups

                    delegate: ColumnLayout {
                        id: group
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        Layout.leftMargin: 20
                        Layout.rightMargin: 20
                        Layout.topMargin: 18
                        Layout.bottomMargin: 8
                        spacing: 5
                        objectName: "panel.shortcuts.group."
                            + modelData.key
                        Accessible.id: objectName
                        Accessible.role: Accessible.Grouping
                        Accessible.name: modelData.title

                        PlainLabel {
                            Layout.fillWidth: true
                            text: group.modelData.title.toUpperCase()
                            color: KodosiTheme.textSecondary
                            font.pixelSize: 9
                            font.weight: Font.DemiBold
                            font.letterSpacing: 1.0
                        }

                        Repeater {
                            model: group.modelData.shortcuts

                            delegate: RowLayout {
                                id: shortcutRow
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.preferredHeight: 34
                                spacing: 10

                                Rectangle {
                                    Layout.preferredWidth: Math.max(
                                        78,
                                        keyLabel.implicitWidth + 14)
                                    Layout.preferredHeight: 24
                                    radius: KodosiTheme.radiusSmall
                                    color: KodosiTheme.surfaceRaised

                                    PlainLabel {
                                        id: keyLabel
                                        anchors.centerIn: parent
                                        text: shortcutRow.modelData.keys
                                        color: KodosiTheme.textPrimary
                                        font.family: "monospace"
                                        font.pixelSize: 9
                                        font.weight: Font.DemiBold
                                    }
                                }

                                PlainLabel {
                                    Layout.fillWidth: true
                                    text: shortcutRow.modelData.label
                                    color: KodosiTheme.textSecondary
                                    font.pixelSize: 11
                                    wrapMode: Text.Wrap
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.topMargin: 6
                            Layout.preferredHeight: 1
                            color: KodosiTheme.seam
                        }
                    }
                }
            }
        }
    }

    Shortcut {
        objectName: "panel.shortcuts.escape"
        sequence: "Escape"
        context: Qt.WindowShortcut
        enabled: root.opened
        onActivated: root.close()
    }
}
