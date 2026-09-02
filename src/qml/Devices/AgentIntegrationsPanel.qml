pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Kodosi.Models 1.0 as Models

Rectangle {
    id: root

    implicitHeight: content.implicitHeight + 24
    radius: KodosiTheme.radiusSmall
    color: KodosiTheme.surface
    border.width: 1
    border.color: KodosiTheme.seam

    function stateLabel(state) {
        if (state === Models.AgentGlobal.Loading)
            return qsTr("Refreshing")
        if (state === Models.AgentGlobal.Loaded)
            return qsTr("Ready")
        if (state === Models.AgentGlobal.Degraded)
            return qsTr("Degraded")
        if (state === Models.AgentGlobal.Failed)
            return qsTr("Failed")
        return qsTr("Not checked")
    }

    function stateTone(state) {
        if (state === Models.AgentGlobal.Loaded)
            return KodosiTheme.success
        if (state === Models.AgentGlobal.Degraded)
            return KodosiTheme.warning
        if (state === Models.AgentGlobal.Failed)
            return KodosiTheme.danger
        return KodosiTheme.textSecondary
    }

    ColumnLayout {
        id: content
        anchors.fill: parent
        anchors.margins: 12
        spacing: KodosiTheme.spacing3

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                PlainLabel {
                    text: qsTr("Agent integrations")
                    color: KodosiTheme.textPrimary
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }
                PlainLabel {
                    text: qsTr("Detected CLIs, customizations, and MCP servers")
                    color: KodosiTheme.textSecondary
                    font.pixelSize: 10
                }
            }
            KButton {
                objectName: "integrations.refresh"
                Accessible.id: objectName
                text: qsTr("Refresh")
                Accessible.name: text
                onClicked: Models.AgentGlobal.refresh()
            }
        }

        ListView {
            Layout.fillWidth: true
            Layout.preferredHeight: contentHeight
            interactive: false
            model: Models.AgentGlobal
            spacing: 3

            delegate: Rectangle {
                id: integration

                required property string vendor
                required property string cwd
                required property string version
                required property int refreshState
                required property string error
                required property int pluginCount
                required property int skillCount
                required property int agentCount
                required property int mcpCount

                visible: cwd.length === 0
                objectName: visible
                    ? "integrations.vendor." + integration.vendor
                    : ""
                Accessible.id: objectName
                Accessible.name: integration.vendor === "claude"
                    ? qsTr("Claude Code integration")
                    : qsTr("GitHub Copilot CLI integration")
                Accessible.role: Accessible.ListItem
                width: ListView.view.width
                height: visible ? 52 : 0
                radius: KodosiTheme.radiusSmall
                color: KodosiTheme.surfaceElevated

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 9
                    spacing: KodosiTheme.spacing3

                    Rectangle {
                        Layout.preferredWidth: 7
                        Layout.preferredHeight: 7
                        radius: 4
                        color: root.stateTone(integration.refreshState)
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        PlainLabel {
                            text: integration.vendor === "claude"
                                ? qsTr("Claude Code")
                                : qsTr("GitHub Copilot CLI")
                            color: KodosiTheme.textPrimary
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                        }
                        PlainLabel {
                            text: integration.version.length > 0
                                ? qsTr("%1 · %2 plugins · %3 skills · %4 agents")
                                      .arg(integration.version)
                                      .arg(integration.pluginCount)
                                      .arg(integration.skillCount)
                                      .arg(integration.agentCount)
                                : root.stateLabel(integration.refreshState)
                            color: integration.error.length > 0
                                ? KodosiTheme.danger
                                : KodosiTheme.textSecondary
                            font.pixelSize: 9
                        }
                    }

                    StatusPill {
                        text: root.stateLabel(integration.refreshState)
                        tone: root.stateTone(integration.refreshState)
                    }
                }
            }
        }

        PlainLabel {
            visible: Models.AgentGlobal.mcpServers.count > 0
            text: qsTr("MCP servers")
            color: KodosiTheme.textSecondary
            font.pixelSize: 10
            font.weight: Font.DemiBold
        }

        Flow {
            Layout.fillWidth: true
            spacing: 6
            visible: Models.AgentGlobal.mcpServers.count > 0

            Repeater {
                model: Models.AgentGlobal.mcpServers

                delegate: Rectangle {
                    id: server
                    required property string vendor
                    required property string name
                    required property string healthKind

                    width: serverLabel.implicitWidth + 20
                    height: 25
                    radius: KodosiTheme.radiusSmall
                    color: KodosiTheme.surfaceElevated
                    border.width: 1
                    border.color: healthKind === "healthy"
                        ? KodosiTheme.success
                        : healthKind === "unreachable"
                          || healthKind === "misconfigured"
                          ? KodosiTheme.danger
                          : KodosiTheme.seam

                    PlainLabel {
                        id: serverLabel
                        anchors.centerIn: parent
                        text: server.vendor + " · " + server.name
                        color: KodosiTheme.textSecondary
                        font.pixelSize: 9
                    }
                }
            }
        }
    }
}
