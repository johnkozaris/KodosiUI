pragma ComponentBehavior: Bound
import Kodosi 1.0
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kodosi.Models 1.0 as Models

KPopover {
    id: root

    function openModal() {
        Models.ProviderTools.select("claude", "");
        open();
        Models.ProviderTools.discover();
    }

    focus: true
    height: Math.min(640, parent ? parent.height - 32 : 640)
    modal: true
    objectName: "panel.history"
    padding: 18
    parent: Overlay.overlay
    width: Math.min(900, parent ? parent.width - 32 : 900)
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? (parent.height - height) / 2 : 0

    onClosed: {
        Models.DesktopFiles.cancelDirectory();
        Models.ProviderTools.reset();
    }

    Connections {
        function onDirectoryPicked(purpose, path) {
            if (purpose !== "history" || !root.opened)
                return;
            Models.ProviderTools.select(Models.ProviderTools.provider, path);
            Models.ProviderTools.discover();
        }

        target: Models.DesktopFiles
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        RowLayout {
            PlainLabel {
                Layout.fillWidth: true
                color: KodosiTheme.textPrimary
                font.pixelSize: 18
                text: qsTr("History")
            }
            KIconButton {
                Accessible.id: objectName
                Accessible.name: qsTr("Close conversation preview")
                glyph: "close"
                objectName: "panel.history.close"

                onClicked: root.close()
            }
        }
        RowLayout {
            KComboBox {
                Accessible.id: objectName
                Accessible.name: qsTr("Provider")
                currentIndex: Models.ProviderTools.provider === "copilot" ? 1 : 0
                model: ["Claude", "Copilot"]
                objectName: "panel.history.provider"

                onActivated: {
                    Models.ProviderTools.select(currentIndex === 0 ? "claude" : "copilot", Models.ProviderTools.directory);
                    Models.ProviderTools.discover();
                }
            }
            PlainLabel {
                Layout.fillWidth: true
                color: KodosiTheme.textSecondary
                elide: Text.ElideMiddle
                text: Models.ProviderTools.directory || qsTr("All projects")
            }
            KButton {
                Accessible.id: objectName
                objectName: "panel.history.all-projects"
                text: qsTr("All projects")
                visible: Models.ProviderTools.directory.length > 0
                onClicked: { Models.ProviderTools.select(Models.ProviderTools.provider, ""); Models.ProviderTools.discover(); }
            }
            KButton {
                Accessible.id: objectName
                objectName: "panel.history.folder"
                text: qsTr("Choose folder…")

                onClicked: Models.DesktopFiles.requestDirectory("history", Models.ProviderTools.directory)
            }
        }
        RowLayout {
            Layout.fillHeight: true
            Layout.fillWidth: true
            spacing: 14

            ListView {
                id: conversations

                Layout.fillHeight: true
                Layout.preferredWidth: Math.min(260, root.width * 0.36)
                clip: true
                model: Models.ProviderTools.conversations

                ScrollBar.vertical: KScrollBar {
                }
                delegate: KItemDelegate {
                    id: conversationRow

                    required property var modelData

                    Accessible.id: objectName
                    objectName: "panel.history.conversation." + conversationRow.modelData.nativeConversationId
                    text: conversationRow.modelData.title || conversationRow.modelData.nativeConversationId
                    width: conversations.width

                    onClicked: Models.ProviderTools.preview(conversationRow.modelData.nativeConversationId)
                }
                footer: KButton {
                    Accessible.id: objectName
                    enabled: !Models.ProviderTools.busy
                    objectName: "panel.history.more-conversations"
                    text: qsTr("More conversations")
                    visible: Models.ProviderTools.hasMore

                    onClicked: Models.ProviderTools.loadMore()
                }
            }
            KScrollView {
                Layout.fillHeight: true
                Layout.fillWidth: true

                ColumnLayout {
                    spacing: 12
                    width: parent.width

                    RowLayout {
                        KButton {
                            Accessible.id: objectName
                            enabled: !Models.ProviderTools.busy && Models.ProviderTools.hasOlder
                            objectName: "panel.history.earlier-messages"
                            text: qsTr("Earlier")
                            onClicked: Models.ProviderTools.loadOlder()
                        }
                        KButton {
                            Accessible.id: objectName
                            enabled: !Models.ProviderTools.busy && Models.ProviderTools.hasNewer
                            objectName: "panel.history.later-messages"
                            text: qsTr("Later")
                            onClicked: Models.ProviderTools.loadNewer()
                        }
                        KButton {
                            Accessible.id: objectName
                            enabled: !Models.ProviderTools.busy && Models.ProviderTools.hasNewer
                            objectName: "panel.history.latest-messages"
                            text: qsTr("Latest")
                            onClicked: Models.ProviderTools.loadLatest()
                        }
                    }
                    Repeater {
                        model: Models.ProviderTools.entries

                        delegate: ColumnLayout {
                            id: entry1

                            required property var modelData
                            required property int index
                            property bool expanded: false
                            readonly property string content: modelData.content || ""

                            Layout.fillWidth: true
                            spacing: 4

                            PlainLabel {
                                color: KodosiTheme.textSecondary
                                font.pixelSize: 10
                                text: entry1.modelData.role || ""
                            }
                            KReadOnlyText {
                                Layout.fillWidth: true
                                text: entry1.expanded || entry1.content.length <= 2000 ? entry1.content : entry1.content.slice(0, 2000) + "…"
                                visible: entry1.modelData.role !== "tool" || entry1.expanded
                                wrapMode: TextEdit.Wrap
                            }
                            KButton {
                                Accessible.id: objectName
                                objectName: "panel.history.message." + entry1.index + ".expand"
                                text: entry1.expanded ? qsTr("Show less") : entry1.modelData.role === "tool" ? qsTr("Show tool output") : qsTr("Show more")
                                visible: entry1.content.length > 2000 || entry1.modelData.role === "tool"
                                variant: "quiet"
                                onClicked: entry1.expanded = !entry1.expanded
                            }
                        }
                    }
                    PlainLabel {
                        color: KodosiTheme.textSecondary
                        text: qsTr("Select a saved conversation to preview it.")
                        visible: Models.ProviderTools.entries.length === 0 && !Models.ProviderTools.busy
                    }
                }
            }
        }
        PlainLabel {
            Layout.fillWidth: true
            color: KodosiTheme.danger
            text: Models.ProviderTools.error
            visible: Models.ProviderTools.error.length > 0
            wrapMode: Text.WordWrap
        }
        RowLayout {
            KBusyIndicator {
                running: Models.ProviderTools.busy
                visible: running
            }
            Item {
                Layout.fillWidth: true
            }
            KButton {
                Accessible.id: objectName
                enabled: !Models.ProviderTools.busy && !!Models.ProviderTools.selectedConversation.nativeConversationId
                objectName: "panel.history.start"
                text: qsTr("Resume in new terminal")

                onClicked: {
                    const selected = Models.ProviderTools.selectedConversation;
                    if (Models.Workspace.createSession("", selected.workingDirectory, Models.ProviderTools.provider, selected.nativeConversationId)) {
                        Models.DesktopSettings.setWorkingDirectory(selected.workingDirectory);
                        root.close();
                    }
                }
            }
        }
    }
}
