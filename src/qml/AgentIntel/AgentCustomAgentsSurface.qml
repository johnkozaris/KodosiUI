pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Kodosi.Models 1.0 as Models

Item {
    id: root
    objectName: "panel.agentIntel.agents"
    Accessible.id: objectName
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Custom Agents")

    readonly property bool wide: width >= 620

    GridLayout {
        anchors.fill: parent
        anchors.margins: KodosiTheme.spacing5
        columns: root.wide ? 2 : 1
        rows: root.wide ? 1 : 2
        columnSpacing: KodosiTheme.spacing3
        rowSpacing: KodosiTheme.spacing3

        Rectangle {
            Layout.row: 0
            Layout.column: 0
            Layout.fillWidth: !root.wide
            Layout.fillHeight: root.wide
            Layout.preferredWidth: root.wide ? 260 : -1
            Layout.preferredHeight: root.wide ? -1 : 210
            color: KodosiTheme.surfaceElevated
            radius: KodosiTheme.radiusSmall

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: KodosiTheme.spacing3
                spacing: KodosiTheme.spacing2

                RowLayout {
                    objectName: "panel.agentIntel.agents.notice"
                    Accessible.id: objectName
                    visible: Models.AgentCustomAgents.error.length > 0
                        && Models.AgentCustomAgents.count > 0
                    Layout.fillWidth: true
                    spacing: KodosiTheme.spacing2

                    PlainLabel {
                        Layout.fillWidth: true
                        text: Models.AgentCustomAgents.error
                        color: KodosiTheme.danger
                        wrapMode: Text.Wrap
                        Accessible.name: text
                    }

                    KButton {
                        objectName: "panel.agentIntel.agents.notice.retry"
                        Accessible.id: objectName
                        text: qsTr("Retry")
                        enabled: !Models.AgentCustomAgents.loading
                            && !Models.AgentCustomAgents.detailLoading
                        Accessible.name: qsTr("Retry Custom Agents inventory")
                        onClicked: Models.AgentCustomAgents.retry()
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: Models.AgentCustomAgents.count === 0

                    ColumnLayout {
                        anchors.centerIn: parent
                        width: Math.min(430, parent.width - 24)
                        spacing: KodosiTheme.spacing3

                        KBusyIndicator {
                            Layout.alignment: Qt.AlignHCenter
                            running: Models.AgentCustomAgents.loading
                            visible: running
                        }

                        PlainLabel {
                            objectName: "panel.agentIntel.agents.empty"
                            Accessible.id: objectName
                            Layout.fillWidth: true
                            text: Models.AgentCustomAgents.state
                                === Models.AgentCustomAgents.Failed
                                ? qsTr("Custom Agents are unavailable")
                                : Models.AgentCustomAgents.state
                                  === Models.AgentCustomAgents.Ineligible
                                  ? qsTr("Custom Agents are not available")
                                  : Models.AgentCustomAgents.loading
                                    ? qsTr("Preparing Custom Agents")
                                    : qsTr("No custom agents defined")
                            color: KodosiTheme.textPrimary
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                        }

                        PlainLabel {
                            objectName: "panel.agentIntel.agents.status"
                            Accessible.id: objectName
                            Layout.fillWidth: true
                            text: Models.AgentCustomAgents.error.length > 0
                                ? Models.AgentCustomAgents.error
                                : Models.AgentCustomAgents.statusMessage.length > 0
                                  ? Models.AgentCustomAgents.statusMessage
                                  : qsTr("Claude loads Markdown agents defined directly for this project.")
                            color: Models.AgentCustomAgents.error.length > 0
                                ? KodosiTheme.danger
                                : KodosiTheme.textSecondary
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            Accessible.name: text
                        }

                        KButton {
                            objectName: "panel.agentIntel.agents.retry"
                            Accessible.id: objectName
                            visible: Models.AgentCustomAgents.state
                                === Models.AgentCustomAgents.Failed
                            Layout.alignment: Qt.AlignHCenter
                            text: qsTr("Retry")
                            Accessible.name: qsTr("Retry Custom Agents")
                            onClicked: Models.AgentCustomAgents.retry()
                        }
                    }
                }

                ListView {
                    objectName: "panel.agentIntel.agents.list"
                    Accessible.id: objectName
                    visible: Models.AgentCustomAgents.count > 0
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 2
                    model: Models.AgentCustomAgents
                    Accessible.role: Accessible.List
                    Accessible.name: qsTr("Custom Agents")

                    delegate: KButton {
                        id: agentItem
                        required property int index
                        required property string itemToken
                        required property string name
                        required property string description
                        required property string model
                        required property var tools
                        required property string parseErrorSummary
                        required property string accessibleId

                        objectName: "panel.agentIntel.agents.item."
                            + agentItem.accessibleId
                        Accessible.id: objectName
                        width: ListView.view.width
                        height: 72
                        checkable: true
                        checked: Models.AgentCustomAgents.selectedItemToken
                            === agentItem.itemToken
                        enabled: !Models.AgentCustomAgents.loading
                            && !Models.AgentCustomAgents.detailLoading
                        Accessible.name: (agentItem.name.length > 0
                            ? agentItem.name : qsTr("Unnamed agent"))
                            + (agentItem.description.length > 0
                               ? ", " + agentItem.description : "")
                        onClicked: Models.AgentCustomAgents.select(
                            agentItem.itemToken)

                        background: Rectangle {
                            color: agentItem.checked
                                ? KodosiTheme.surface
                                : agentItem.hovered
                                  ? KodosiTheme.canvas
                                  : KodosiTheme.surface
                            radius: KodosiTheme.radiusSmall
                        }

                        contentItem: ColumnLayout {
                            spacing: 2

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: KodosiTheme.spacing2

                                PlainLabel {
                                    Layout.fillWidth: true
                                    text: agentItem.name.length > 0
                                        ? agentItem.name
                                        : qsTr("(unnamed agent)")
                                    color: agentItem.checked
                                        ? KodosiTheme.accent
                                        : KodosiTheme.textPrimary
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }

                                PlainLabel {
                                    visible: agentItem.model.length > 0
                                    text: agentItem.model
                                    color: KodosiTheme.success
                                    font.pixelSize: 10
                                    elide: Text.ElideRight
                                }

                                PlainLabel {
                                    visible: agentItem.parseErrorSummary.length > 0
                                    text: agentItem.parseErrorSummary
                                    color: KodosiTheme.danger
                                    font.pixelSize: 10
                                }
                            }

                            PlainLabel {
                                Layout.fillWidth: true
                                text: agentItem.description.length > 0
                                    ? agentItem.description
                                    : qsTr("No description")
                                color: KodosiTheme.textSecondary
                                font.pixelSize: 11
                                elide: Text.ElideRight
                            }

                            PlainLabel {
                                Layout.fillWidth: true
                                visible: agentItem.tools.length > 0
                                text: agentItem.tools.join(" · ")
                                color: KodosiTheme.textSecondary
                                font.pixelSize: 10
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            id: detail
            objectName: "panel.agentIntel.agents.detail"
            Accessible.id: objectName
            Layout.row: root.wide ? 0 : 1
            Layout.column: root.wide ? 1 : 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: KodosiTheme.terminal
            radius: KodosiTheme.radiusSmall

            Item {
                anchors.fill: parent
                visible: Models.AgentCustomAgents.selectedItemToken.length === 0

                ColumnLayout {
                    anchors.centerIn: parent
                    width: Math.min(430, parent.width - 32)
                    spacing: KodosiTheme.spacing3

                    PlainLabel {
                        Layout.fillWidth: true
                        text: qsTr("Select a custom agent")
                        color: KodosiTheme.textPrimary
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                    }

                    PlainLabel {
                        Layout.fillWidth: true
                        text: qsTr("Frontmatter, system prompt, and parse errors stay on this device and load only with this tab.")
                        color: KodosiTheme.textSecondary
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                    }
                }
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: KodosiTheme.spacing4
                visible: Models.AgentCustomAgents.selectedItemToken.length > 0
                spacing: KodosiTheme.spacing3

                PlainLabel {
                    objectName: "panel.agentIntel.agents.detail.title"
                    Accessible.id: objectName
                    Layout.fillWidth: true
                    text: Models.AgentCustomAgents.selectedName.length > 0
                        ? Models.AgentCustomAgents.selectedName
                        : qsTr("(unnamed agent)")
                    color: KodosiTheme.textPrimary
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    Accessible.name: text
                }

                KBusyIndicator {
                    objectName: "panel.agentIntel.agents.detail.loading"
                    Accessible.id: objectName
                    Layout.alignment: Qt.AlignHCenter
                    Layout.fillHeight: true
                    running: Models.AgentCustomAgents.detailLoading
                    visible: running
                }

                ColumnLayout {
                    objectName: "panel.agentIntel.agents.detail.error"
                    Accessible.id: objectName
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: Models.AgentCustomAgents.detailError.length > 0
                    spacing: KodosiTheme.spacing3

                    Item {
                        Layout.fillHeight: true
                    }

                    PlainLabel {
                        Layout.fillWidth: true
                        text: Models.AgentCustomAgents.detailError
                        color: KodosiTheme.danger
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        Accessible.name: text
                    }

                    KButton {
                        objectName: "panel.agentIntel.agents.detail.retry"
                        Accessible.id: objectName
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("Retry")
                        Accessible.name: qsTr("Retry selected Custom Agent")
                        onClicked: Models.AgentCustomAgents.retry()
                    }

                    Item {
                        Layout.fillHeight: true
                    }
                }

                KScrollView {
                    objectName: "panel.agentIntel.agents.detail.scroll"
                    Accessible.id: objectName
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: Models.AgentCustomAgents.detailLoaded
                    clip: true
                    contentWidth: availableWidth

                    Column {
                        width: parent.width
                        spacing: KodosiTheme.spacing4

                        PlainLabel {
                            objectName: "panel.agentIntel.agents.detail.description"
                            Accessible.id: objectName
                            width: parent.width
                            visible: Models.AgentCustomAgents.selectedDescription.length > 0
                            text: Models.AgentCustomAgents.selectedDescription
                            color: KodosiTheme.textSecondary
                            wrapMode: Text.Wrap
                        }

                        PlainLabel {
                            objectName: "panel.agentIntel.agents.detail.metadata"
                            Accessible.id: objectName
                            width: parent.width
                            visible: Models.AgentCustomAgents.selectedModel.length > 0
                                || Models.AgentCustomAgents.selectedTools.length > 0
                                || Models.AgentCustomAgents.selectedDisallowedTools.length > 0
                            text: [
                                Models.AgentCustomAgents.selectedModel.length > 0
                                    ? qsTr("Model: %1").arg(
                                        Models.AgentCustomAgents.selectedModel)
                                    : "",
                                Models.AgentCustomAgents.selectedTools.length > 0
                                    ? qsTr("Tools: %1").arg(
                                        Models.AgentCustomAgents.selectedTools.join(", "))
                                    : "",
                                Models.AgentCustomAgents.selectedDisallowedTools.length > 0
                                    ? qsTr("Disallowed: %1").arg(
                                        Models.AgentCustomAgents.selectedDisallowedTools.join(", "))
                                    : ""
                            ].filter(function(value) {
                                return value.length > 0
                            }).join("  ·  ")
                            color: KodosiTheme.textSecondary
                            font.pixelSize: 11
                            wrapMode: Text.Wrap
                        }

                        Column {
                            width: parent.width
                            spacing: KodosiTheme.spacing2
                            visible: Models.AgentCustomAgents.detailErrors.length > 0

                            PlainLabel {
                                text: qsTr("Parse errors")
                                color: KodosiTheme.danger
                                font.weight: Font.DemiBold
                            }

                            KReadOnlyText {
                                objectName: "panel.agentIntel.agents.detail.errors"
                                Accessible.id: objectName
                                width: parent.width
                                height: contentHeight
                                text: Models.AgentCustomAgents.detailErrors
                                color: KodosiTheme.danger
                                Accessible.name: text
                            }
                        }

                        Column {
                            width: parent.width
                            spacing: KodosiTheme.spacing2
                            visible: Models.AgentCustomAgents.frontmatter.length > 0

                            PlainLabel {
                                text: qsTr("Frontmatter")
                                color: KodosiTheme.textSecondary
                                font.weight: Font.DemiBold
                            }

                            KReadOnlyText {
                                objectName: "panel.agentIntel.agents.detail.frontmatter"
                                Accessible.id: objectName
                                width: parent.width
                                height: contentHeight
                                text: Models.AgentCustomAgents.frontmatter
                                color: KodosiTheme.textPrimary
                                font.family: "monospace"
                                Accessible.name: text
                            }
                        }

                        Column {
                            width: parent.width
                            spacing: KodosiTheme.spacing2
                            visible: Models.AgentCustomAgents.systemPrompt.length > 0

                            PlainLabel {
                                text: qsTr("System prompt")
                                color: KodosiTheme.textSecondary
                                font.weight: Font.DemiBold
                            }

                            KReadOnlyText {
                                objectName: "panel.agentIntel.agents.detail.prompt"
                                Accessible.id: objectName
                                width: parent.width
                                height: contentHeight
                                text: Models.AgentCustomAgents.systemPrompt
                                color: KodosiTheme.textPrimary
                                font.family: "monospace"
                                Accessible.name: text
                            }
                        }

                        PlainLabel {
                            width: parent.width
                            visible: Models.AgentCustomAgents.detailErrors.length === 0
                                && Models.AgentCustomAgents.frontmatter.length === 0
                                && Models.AgentCustomAgents.systemPrompt.length === 0
                                && Models.AgentCustomAgents.selectedDescription.length === 0
                                && Models.AgentCustomAgents.selectedModel.length === 0
                                && Models.AgentCustomAgents.selectedTools.length === 0
                                && Models.AgentCustomAgents.selectedDisallowedTools.length === 0
                            text: qsTr("This custom agent has no readable source content.")
                            color: KodosiTheme.textSecondary
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }
        }
    }
}
