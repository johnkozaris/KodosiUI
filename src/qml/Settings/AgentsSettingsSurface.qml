pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kodosi.Models 1.0 as Models

ColumnLayout {
    id: root
    objectName: "panel.settings.agents.surface"
    Accessible.id: objectName

    signal openProjectIntel(string sourceId)
    signal ensureVisible(var item)

    property string discoveryActionStatus
    property bool discoveryActionError: false

    Layout.fillWidth: true
    spacing: 18

    function refreshAll() {
        Models.ProjectIntelligence.refreshSources(true)
        Models.AgentAutoModeRules.refresh(false)
        Models.ExternalDiscovery.refresh(true)
        Models.AgentGlobal.refresh()
    }

    function requestAutoModeReload() {
        if (Models.AgentAutoModeRules.dirty)
            autoModeReloadDialog.open()
        else
            Models.AgentAutoModeRules.refresh(true)
    }

    function integrationStatus(
        state,
        version,
        pluginCount,
        skillCount,
        agentCount,
        mcpCount) {
        if (state === Models.AgentGlobal.Loading)
            return qsTr("Refreshing…")
        if (state === Models.AgentGlobal.Degraded)
            return qsTr("Degraded")
        if (state === Models.AgentGlobal.Failed)
            return qsTr("Refresh failed")
        if (state === Models.AgentGlobal.Loaded) {
            return version.length > 0 || pluginCount > 0
                    || skillCount > 0 || agentCount > 0
                    || mcpCount > 0
                ? qsTr("Detected")
                : qsTr("Not detected")
        }
        return qsTr("Not refreshed")
    }

    function integrationTone(state, detected) {
        if (state === Models.AgentGlobal.Failed)
            return KodosiTheme.danger
        if (state === Models.AgentGlobal.Degraded
                || state === Models.AgentGlobal.Loading)
            return KodosiTheme.warning
        if (state === Models.AgentGlobal.Loaded && detected)
            return KodosiTheme.success
        return KodosiTheme.textTertiary
    }

    function kindLabel(kind) {
        if (kind === "object")
            return qsTr("Object")
        if (kind === "array")
            return qsTr("Array")
        if (kind === "string")
            return qsTr("Text")
        if (kind === "integer")
            return qsTr("Integer")
        if (kind === "number")
            return qsTr("Number")
        if (kind === "boolean")
            return qsTr("Boolean")
        return qsTr("Not set")
    }

    function containsItem(item) {
        let current = item
        while (current) {
            if (current === root)
                return true
            current = current.parent
        }
        return false
    }

    Connections {
        target: Models.ExternalDiscovery

        function onActionMessage(message, error) {
            root.discoveryActionStatus = message
            root.discoveryActionError = error
        }
    }

    Connections {
        target: root.Window.window

        function onActiveFocusItemChanged() {
            const item = root.Window.window
                ? root.Window.window.activeFocusItem
                : null
            if (!root.visible || !root.containsItem(item))
                return
            Qt.callLater(function() {
                root.ensureVisible(item)
            })
        }
    }

    RowLayout {
        Layout.fillWidth: true

        PlainLabel {
            Layout.fillWidth: true
            text: qsTr("Agent settings")
            color: KodosiTheme.textPrimary
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }

        KIconButton {
            objectName: "panel.settings.agents.refresh"
            Accessible.id: objectName
            glyph: "refresh"
            glyphColor: KodosiTheme.textSecondary
            Accessible.name:
                qsTr("Refresh agent settings and discovery")
            onClicked: root.refreshAll()
        }
    }

    KSegmentedBar {
        objectName: "panel.settings.agents.scope"
        Accessible.id: objectName
        Accessible.role: Accessible.PageTabList
        Accessible.name: qsTr("Settings scope")
        Layout.fillWidth: true
        Layout.preferredHeight: 36

        Repeater {
            id: scopeRepeater
            model: [
                {key: "managed", label: qsTr("Managed")},
                {key: "user", label: qsTr("User")},
                {key: "project", label: qsTr("Project")},
                {key: "local", label: qsTr("Local")}
            ]

            delegate: KButton {
                required property int index
                required property var modelData
                Layout.fillWidth: true
                objectName:
                    "panel.settings.agents.scope."
                    + modelData.key
                Accessible.id: objectName
                Accessible.role: Accessible.PageTab
                text: modelData.label
                variant: "quiet"
                checkable: true
                checked:
                    Models.ProjectIntelligence.settingsScope
                        === modelData.key
                tonalSelection: true
                Accessible.selected: checked
                onClicked:
                    Models.ProjectIntelligence.settingsScope =
                        modelData.key
                Accessible.onPressAction:
                    Models.ProjectIntelligence.settingsScope =
                        modelData.key
                Keys.onLeftPressed: event => {
                    const next = Math.max(0, index - 1)
                    const item = scopeRepeater.itemAt(next)
                    if (item) {
                        Models.ProjectIntelligence
                            .settingsScope =
                            root.scopeKey(next)
                        item.forceActiveFocus(
                            Qt.TabFocusReason)
                    }
                    event.accepted = true
                }
                Keys.onRightPressed: event => {
                    const next = Math.min(3, index + 1)
                    const item = scopeRepeater.itemAt(next)
                    if (item) {
                        Models.ProjectIntelligence
                            .settingsScope =
                            root.scopeKey(next)
                        item.forceActiveFocus(
                            Qt.TabFocusReason)
                    }
                    event.accepted = true
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            KSegmentedBar {
                objectName: "panel.settings.agents.agent"
                Accessible.id: objectName
                Accessible.role: Accessible.PageTabList
                Accessible.name: qsTr("Agent")
                Layout.preferredWidth: 220
                Layout.preferredHeight: 36

                KButton {
                    id: claudeAgentButton
                    objectName:
                        "panel.settings.agents.agent.claude"
                    Accessible.id: objectName
                    Accessible.role: Accessible.PageTab
                    Layout.fillWidth: true
                    text: qsTr("Claude")
                    variant: "quiet"
                    checkable: true
                    checked:
                        Models.ProjectIntelligence.settingsAgent
                            === "claude"
                    tonalSelection: true
                    Accessible.selected: checked
                    onClicked:
                        Models.ProjectIntelligence.settingsAgent =
                            "claude"
                    Accessible.onPressAction:
                        Models.ProjectIntelligence.settingsAgent =
                            "claude"
                    Keys.onRightPressed: event => {
                        Models.ProjectIntelligence.settingsAgent =
                            "copilot"
                        copilotAgentButton.forceActiveFocus(
                            Qt.TabFocusReason)
                        event.accepted = true
                    }
                }

                KButton {
                    id: copilotAgentButton
                    objectName:
                        "panel.settings.agents.agent.copilot"
                    Accessible.id: objectName
                    Accessible.role: Accessible.PageTab
                    Layout.fillWidth: true
                    text: qsTr("Copilot")
                    variant: "quiet"
                    checkable: true
                    checked:
                        Models.ProjectIntelligence.settingsAgent
                            === "copilot"
                    tonalSelection: true
                    Accessible.selected: checked
                    onClicked:
                        Models.ProjectIntelligence.settingsAgent =
                            "copilot"
                    Accessible.onPressAction:
                        Models.ProjectIntelligence.settingsAgent =
                            "copilot"
                    Keys.onLeftPressed: event => {
                        Models.ProjectIntelligence.settingsAgent =
                            "claude"
                        claudeAgentButton.forceActiveFocus(
                            Qt.TabFocusReason)
                        event.accepted = true
                    }
                }
            }

            Item { Layout.fillWidth: true }

            KComboBox {
                id: workspaceSource
                objectName: "panel.settings.agents.workspace"
                Accessible.id: objectName
                Layout.preferredWidth: 240
                model: Models.ProjectIntelligence.sources
                textRole: "title"
                valueRole: "itemId"
                Accessible.name: qsTr("Agent settings workspace")
                onCurrentValueChanged: {
                    if (currentValue)
                        Models.ProjectIntelligence.selectSource(
                            currentValue)
                }
            }
        }

        ColumnLayout {
            objectName: "panel.settings.agents.integration"
            Accessible.id: objectName
            Layout.fillWidth: true
            spacing: 7

            PlainLabel {
                text: qsTr("Detected integration")
                color: KodosiTheme.textPrimary
                font.weight: Font.DemiBold
            }

            PlainLabel {
                objectName:
                    "panel.settings.agents.integration.empty"
                Accessible.id: objectName
                visible: Models.AgentGlobal.count === 0
                text: qsTr("Not refreshed")
                color: KodosiTheme.textSecondary
            }

            ListView {
                objectName:
                    "panel.settings.agents.integration.statuses"
                Accessible.id: objectName
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Agent integration status")
                Layout.fillWidth: true
                Layout.preferredHeight: contentHeight
                interactive: false
                spacing: 3
                model: Models.AgentGlobal

                delegate: Rectangle {
                    id: integrationRow

                    required property string vendor
                    required property string version
                    required property int refreshState
                    required property string error
                    required property int pluginCount
                    required property int skillCount
                    required property int agentCount
                    required property int mcpCount
                    required property var notices
                    readonly property bool detected:
                        version.length > 0 || pluginCount > 0
                        || skillCount > 0 || agentCount > 0
                        || mcpCount > 0
                    readonly property string statusText:
                        root.integrationStatus(
                            refreshState,
                            version,
                            pluginCount,
                            skillCount,
                            agentCount,
                            mcpCount)

                    objectName:
                        "panel.settings.agents.integration."
                        + vendor
                    Accessible.id: objectName
                    Accessible.role: Accessible.ListItem
                    Accessible.name: vendor === "claude"
                        ? qsTr("Claude Code")
                        : qsTr("GitHub Copilot CLI")
                    Accessible.description: statusText
                    width: ListView.view.width
                    implicitHeight:
                        integrationContent.implicitHeight + 18
                    radius: KodosiTheme.radiusSmall
                    color: KodosiTheme.surface

                    RowLayout {
                        id: integrationContent
                        anchors.fill: parent
                        anchors.margins: 9
                        spacing: 9

                        Rectangle {
                            Accessible.ignored: true
                            Layout.preferredWidth: 7
                            Layout.preferredHeight: 7
                            radius: 4
                            color: root.integrationTone(
                                integrationRow.refreshState,
                                integrationRow.detected)
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            RowLayout {
                                Layout.fillWidth: true

                                PlainLabel {
                                    Layout.fillWidth: true
                                    text:
                                        integrationRow.vendor
                                            === "claude"
                                        ? qsTr("Claude Code")
                                        : qsTr(
                                              "GitHub Copilot CLI")
                                    color:
                                        KodosiTheme.textPrimary
                                    font.weight: Font.DemiBold
                                }

                                PlainLabel {
                                    text:
                                        integrationRow.statusText
                                    color:
                                        KodosiTheme.textSecondary
                                }

                                PlainLabel {
                                    visible:
                                        integrationRow.version.length
                                            > 0
                                    text: qsTr("Version %1").arg(
                                        integrationRow.version)
                                    color:
                                        KodosiTheme.textPrimary
                                    font.family: "monospace"
                                    font.pixelSize: 10
                                }
                            }

                            PlainLabel {
                                Layout.fillWidth: true
                                text: qsTr(
                                    "Plugins %1 · Skills %2 · Agents %3 · MCP %4")
                                    .arg(
                                        integrationRow
                                            .pluginCount)
                                    .arg(
                                        integrationRow
                                            .skillCount)
                                    .arg(
                                        integrationRow
                                            .agentCount)
                                    .arg(
                                        integrationRow.mcpCount)
                                color:
                                    KodosiTheme.textSecondary
                                font.pixelSize: 10
                                wrapMode: Text.Wrap
                            }

                            PlainLabel {
                                Layout.fillWidth: true
                                visible:
                                    integrationRow.error.length > 0
                                text: integrationRow.error
                                color: KodosiTheme.danger
                                wrapMode: Text.Wrap
                            }

                            PlainLabel {
                                Layout.fillWidth: true
                                visible:
                                    integrationRow.notices
                                    && integrationRow.notices
                                        .length > 0
                                text: visible
                                    ? integrationRow.notices.join(
                                          "\n")
                                    : ""
                                color: KodosiTheme.warning
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }
            }

            PlainLabel {
                visible: Models.AgentGlobal.mcpServers.count > 0
                text: qsTr("MCP provenance and health")
                color: KodosiTheme.textSecondary
                font.weight: Font.DemiBold
            }

            ListView {
                objectName:
                    "panel.settings.agents.integration.mcp"
                Accessible.id: objectName
                Accessible.role: Accessible.List
                Accessible.name:
                    qsTr("MCP provenance and health")
                Layout.fillWidth: true
                Layout.preferredHeight: contentHeight
                visible: Models.AgentGlobal.mcpServers.count > 0
                interactive: false
                spacing: 1
                model: Models.AgentGlobal.mcpServers

                delegate: Rectangle {
                    id: mcpHealthRow

                    required property int index
                    required property string vendor
                    required property string scope
                    required property string name
                    required property string healthKind
                    required property string healthReason

                    objectName:
                        "panel.settings.agents.integration.mcp."
                        + mcpHealthRow.index
                    Accessible.id: objectName
                    Accessible.role: Accessible.ListItem
                    Accessible.name: mcpHealthRow.name
                    Accessible.description: [
                        mcpHealthRow.vendor,
                        mcpHealthRow.scope,
                        mcpHealthRow.healthKind,
                        mcpHealthRow.healthReason
                    ].filter(function(value) {
                        return value && value.length > 0
                    }).join(". ")
                    width: ListView.view.width
                    height: 38
                    color: KodosiTheme.surface

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 8

                        PlainLabel {
                            Layout.preferredWidth: 40
                            text: qsTr("MCP")
                            color: KodosiTheme.textSecondary
                            font.weight: Font.DemiBold
                        }

                        PlainLabel {
                            Layout.fillWidth: true
                            text: mcpHealthRow.name
                            color: KodosiTheme.textPrimary
                            font.family: "monospace"
                            elide: Text.ElideRight
                        }

                        PlainLabel {
                            text: mcpHealthRow.vendor + " · "
                                + mcpHealthRow.scope
                            color: KodosiTheme.textSecondary
                            elide: Text.ElideRight
                        }

                        PlainLabel {
                            text: mcpHealthRow.healthKind
                            color: mcpHealthRow.healthKind
                                    === "healthy"
                                ? KodosiTheme.success
                                : mcpHealthRow.healthKind
                                      === "unreachable"
                                      || mcpHealthRow.healthKind
                                          === "misconfigured"
                                  ? KodosiTheme.danger
                                  : KodosiTheme.textSecondary
                        }
                    }
                }
            }
        }

        PlainLabel {
            objectName:
                "panel.settings.agents.scope.unavailable"
            Accessible.id: objectName
            Layout.fillWidth: true
            visible:
                Models.ProjectIntelligence
                    .settingsAvailabilityMessage.length > 0
            text:
                Models.ProjectIntelligence
                    .settingsAvailabilityMessage
            color: KodosiTheme.textSecondary
            wrapMode: Text.Wrap
        }

        ListView {
            objectName: "panel.settings.agents.tree"
            Accessible.id: objectName
            Accessible.role: Accessible.Tree
            Accessible.name: qsTr("Agent settings values")
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(
                320,
                Math.max(72, contentHeight))
            visible:
                Models.ProjectIntelligence
                    .settingsAvailabilityMessage.length === 0
            clip: true
            spacing: 1
            model: Models.ProjectIntelligence.settingsTree
            boundsBehavior: Flickable.StopAtBounds

            delegate: Rectangle {
                id: settingRow

                required property int index
                required property string itemId
                required property string title
                required property string subtitle
                required property string kind
                required property int numberA
                required property int numberB

                objectName:
                    "panel.settings.agents.tree."
                    + settingRow.itemId
                Accessible.id: objectName
                Accessible.role: Accessible.TreeItem
                Accessible.name: settingRow.title
                Accessible.description:
                    settingRow.numberB > 0
                    ? root.kindLabel(settingRow.kind)
                        + qsTr(", %1 children").arg(
                              settingRow.numberB)
                    : root.kindLabel(settingRow.kind) + ". "
                        + settingRow.subtitle
                width: ListView.view.width
                height: 38
                color: settingRow.index % 2 === 0
                    ? KodosiTheme.surface
                    : KodosiTheme.surface

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin:
                        8 + settingRow.numberA * 14
                    anchors.rightMargin: 8
                    spacing: 8

                    KIcon {
                        Layout.preferredWidth: 13
                        Layout.preferredHeight: 13
                        name: settingRow.numberB > 0
                            ? "chevron-down"
                            : settingRow.kind === "boolean"
                              ? "check"
                              : "intel"
                        color: KodosiTheme.textTertiary
                    }

                    PlainLabel {
                        Layout.preferredWidth: 170
                        text: settingRow.title
                        color: settingRow.numberB > 0
                            ? KodosiTheme.textPrimary
                            : KodosiTheme.textSecondary
                        font.weight: settingRow.numberB > 0
                            ? Font.DemiBold : Font.Normal
                        elide: Text.ElideRight
                    }

                    PlainLabel {
                        Layout.preferredWidth: 58
                        text: root.kindLabel(settingRow.kind)
                        color: KodosiTheme.textTertiary
                        font.pixelSize: 9
                        elide: Text.ElideRight
                    }

                    PlainLabel {
                        Layout.fillWidth: true
                        text: settingRow.numberB > 0
                            ? qsTr("%1 children").arg(
                                  settingRow.numberB)
                            : settingRow.subtitle
                        color: settingRow.kind === "boolean"
                                && settingRow.subtitle === qsTr("On")
                            ? KodosiTheme.success
                            : KodosiTheme.textPrimary
                        font.family: "monospace"
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 1
        color: KodosiTheme.seam
    }

    RowLayout {
        Layout.fillWidth: true

        PlainLabel {
            Layout.fillWidth: true
            text: qsTr("Project and archive browsers")
            color: KodosiTheme.textPrimary
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }

        KButton {
            objectName:
                "panel.settings.agents.openProjectBrowser"
            Accessible.id: objectName
            text: qsTr("Open Project Intelligence")
            variant: "secondary"
            onClicked: root.openProjectIntel("")
        }
    }

    ListView {
        objectName: "panel.settings.agents.sources"
        Accessible.id: objectName
        Accessible.role: Accessible.List
        Accessible.name: qsTr("Project and archive sources")
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(
            320,
            Math.max(72, contentHeight))
        clip: true
        spacing: 2
        model: Models.ProjectIntelligence.sources
        boundsBehavior: Flickable.StopAtBounds

        delegate: KButton {
            id: browserSource

            required property string itemId
            required property string title
            required property string subtitle
            required property string kind

            width: ListView.view.width
            implicitHeight: 52
            variant: "quiet"
            contentLeftAligned: true
            objectName:
                "panel.settings.agents.source."
                + browserSource.itemId
            Accessible.id: objectName
            Accessible.role: Accessible.ListItem
            Accessible.name: browserSource.title
            Accessible.description: browserSource.subtitle
            onClicked:
                root.openProjectIntel(browserSource.itemId)
            Accessible.onPressAction:
                root.openProjectIntel(browserSource.itemId)

            contentItem: RowLayout {
                spacing: 10

                KIcon {
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                    name: browserSource.kind === "active"
                        ? "folder" : "archive"
                    color: KodosiTheme.textSecondary
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1

                    PlainLabel {
                        Layout.fillWidth: true
                        text: browserSource.title
                        color: KodosiTheme.textPrimary
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }

                    PlainLabel {
                        Layout.fillWidth: true
                        text: browserSource.subtitle
                        color: KodosiTheme.textSecondary
                        font.pixelSize: 9
                        elide: Text.ElideRight
                    }
                }

                KIcon {
                    Layout.preferredWidth: 12
                    Layout.preferredHeight: 12
                    name: "chevron-right"
                    color: KodosiTheme.textSecondary
                }
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 1
        color: KodosiTheme.seam
    }

    PlainLabel {
        text: qsTr("Claude Auto Mode rules")
        color: KodosiTheme.textPrimary
        font.pixelSize: 15
        font.weight: Font.DemiBold
    }

    Repeater {
        model: [
            {
                key: "environment",
                label: qsTr("Environment"),
                hint: qsTr("Tools that can run without approval.")
            },
            {
                key: "allow",
                label: qsTr("Allow"),
                hint: qsTr("Patterns that Auto Mode allows.")
            },
            {
                key: "softDeny",
                label: qsTr("Soft deny"),
                hint: qsTr("Patterns that require a warning prompt.")
            },
            {
                key: "hardDeny",
                label: qsTr("Hard deny"),
                hint: qsTr("Patterns that Auto Mode always refuses.")
            }
        ]

        delegate: ColumnLayout {
            id: autoModeRule

            required property var modelData
            Layout.fillWidth: true
            spacing: 4

            PlainLabel {
                text: autoModeRule.modelData.label
                color: KodosiTheme.textPrimary
                font.weight: Font.DemiBold
            }

            PlainLabel {
                Layout.fillWidth: true
                text: autoModeRule.modelData.hint
                color: KodosiTheme.textSecondary
                wrapMode: Text.Wrap
            }

            KTextArea {
                id: autoModeEditor

                Layout.fillWidth: true
                Layout.preferredHeight: 84
                objectName:
                    "panel.settings.autoMode."
                    + autoModeRule.modelData.key
                Accessible.id: objectName
                Accessible.name: autoModeRule.modelData.label
                text: autoModeRule.modelData.key === "environment"
                    ? Models.AgentAutoModeRules.environmentText
                    : autoModeRule.modelData.key === "allow"
                      ? Models.AgentAutoModeRules.allowText
                      : autoModeRule.modelData.key === "softDeny"
                        ? Models.AgentAutoModeRules.softDenyText
                        : Models.AgentAutoModeRules.hardDenyText
                onTextChanged: {
                    if (!autoModeEditor.activeFocus)
                        return
                    if (autoModeRule.modelData.key === "environment")
                        Models.AgentAutoModeRules.environmentText =
                            autoModeEditor.text
                    else if (autoModeRule.modelData.key === "allow")
                        Models.AgentAutoModeRules.allowText =
                            autoModeEditor.text
                    else if (autoModeRule.modelData.key === "softDeny")
                        Models.AgentAutoModeRules.softDenyText =
                            autoModeEditor.text
                    else
                        Models.AgentAutoModeRules.hardDenyText =
                            autoModeEditor.text
                }
            }
        }
    }

    PlainLabel {
        objectName: "panel.settings.autoMode.status"
        Accessible.id: objectName
        Layout.fillWidth: true
        visible: Models.AgentAutoModeRules.error.length > 0
            || Models.AgentAutoModeRules.statusMessage.length > 0
        text: Models.AgentAutoModeRules.error.length > 0
            ? Models.AgentAutoModeRules.error
            : Models.AgentAutoModeRules.statusMessage
        color: Models.AgentAutoModeRules.error.length > 0
            ? KodosiTheme.danger
            : KodosiTheme.success
        wrapMode: Text.Wrap
    }

    RowLayout {
        Layout.fillWidth: true

        Item { Layout.fillWidth: true }

        KButton {
            id: autoModeReloadButton
            objectName: "panel.settings.autoMode.reload"
            Accessible.id: objectName
            text: qsTr("Reload")
            variant: "quiet"
            enabled: !Models.AgentAutoModeRules.saving
            onClicked: root.requestAutoModeReload()
        }

        KButton {
            objectName: "panel.settings.autoMode.save"
            Accessible.id: objectName
            text: Models.AgentAutoModeRules.saving
                ? qsTr("Saving") : qsTr("Save rules")
            enabled: Models.AgentAutoModeRules.dirty
                && !Models.AgentAutoModeRules.saving
            onClicked: Models.AgentAutoModeRules.save()
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 1
        color: KodosiTheme.seam
    }

    RowLayout {
        Layout.fillWidth: true

        PlainLabel {
            Layout.fillWidth: true
            text: qsTr("External discovery")
            color: KodosiTheme.textPrimary
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }

        KButton {
            objectName:
                "panel.settings.externalDiscovery.refresh"
            Accessible.id: objectName
            text: qsTr("Refresh")
            variant: "quiet"
            enabled: !Models.ExternalDiscovery.loading
            onClicked: Models.ExternalDiscovery.refresh(true)
        }
    }

    Rectangle {
        objectName:
            "panel.settings.externalDiscovery.status"
        Accessible.id: objectName
        Accessible.role: Accessible.StaticText
        Accessible.name: root.discoveryActionStatus.length > 0
            ? root.discoveryActionStatus
            : Models.ExternalDiscovery.error
        Layout.fillWidth: true
        implicitHeight:
            discoveryStatusRow.implicitHeight + 16
        visible: root.discoveryActionStatus.length > 0
            || Models.ExternalDiscovery.error.length > 0
        color: root.discoveryActionError
                || Models.ExternalDiscovery.error.length > 0
            ? KodosiTheme.surfaceRaised
            : KodosiTheme.surfaceRaised

        RowLayout {
            id: discoveryStatusRow
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 6
            spacing: 8

            PlainLabel {
                Layout.fillWidth: true
                text: root.discoveryActionStatus.length > 0
                    ? root.discoveryActionStatus
                    : Models.ExternalDiscovery.error
                color: root.discoveryActionError
                        || Models.ExternalDiscovery.error.length > 0
                    ? KodosiTheme.danger
                    : KodosiTheme.textSecondary
                wrapMode: Text.Wrap
            }

            KIconButton {
                objectName:
                    "panel.settings.externalDiscovery.status.dismiss"
                Accessible.id: objectName
                glyph: "close"
                glyphColor: KodosiTheme.textSecondary
                Accessible.name:
                    qsTr("Dismiss external discovery status")
                onClicked: {
                    root.discoveryActionStatus = ""
                    root.discoveryActionError = false
                }
            }
        }
    }

    ExternalDiscoverySection {
        sectionKey: "mcp"
        sectionTitle: qsTr("External MCP servers")
        sectionDetail: qsTr(
            "Configs found in Claude Desktop, Cursor, and Windsurf.")
        emptyText: qsTr("No external MCP configs found.")
        rows: Models.ExternalDiscovery.servers
    }

    ExternalDiscoverySection {
        sectionKey: "sessions"
        sectionTitle: qsTr("External sessions")
        sectionDetail: qsTr(
            "Claude or Copilot sessions launched outside Kodosi.")
        emptyText: qsTr("No external sessions found.")
        rows: Models.ExternalDiscovery.sessions
    }

    component ExternalDiscoverySection: ColumnLayout {
        id: section

        property string sectionKey
        property string sectionTitle
        property string sectionDetail
        property string emptyText
        property var rows

        Layout.fillWidth: true
        spacing: 6

            PlainLabel {
                objectName:
                    section.sectionKey === "mcp"
                    ? "panel.settings.externalDiscovery.mcp.title"
                    : "panel.settings.externalDiscovery.sessions.title"
                Accessible.id: objectName
                text: section.sectionTitle
                color: KodosiTheme.textPrimary
                font.weight: Font.DemiBold
            }

            PlainLabel {
                objectName:
                    section.sectionKey === "mcp"
                    ? "panel.settings.externalDiscovery.mcp.detail"
                    : "panel.settings.externalDiscovery.sessions.detail"
                Accessible.id: objectName
                Layout.fillWidth: true
                text: section.sectionDetail
                color: KodosiTheme.textSecondary
                wrapMode: Text.Wrap
            }

            KBusyIndicator {
                objectName:
                    section.sectionKey === "mcp"
                    ? "panel.settings.externalDiscovery.mcp.loading"
                    : "panel.settings.externalDiscovery.sessions.loading"
                Accessible.id: objectName
                Layout.alignment: Qt.AlignHCenter
                visible: Models.ExternalDiscovery.loading
                    && section.rows.count === 0
                running: visible
            }

            PlainLabel {
                objectName:
                    section.sectionKey === "mcp"
                    ? "panel.settings.externalDiscovery.mcp.empty"
                    : "panel.settings.externalDiscovery.sessions.empty"
                Accessible.id: objectName
                Layout.fillWidth: true
                visible: !Models.ExternalDiscovery.loading
                    && section.rows.count === 0
                    && Models.ExternalDiscovery.error.length === 0
                text: section.emptyText
                color: KodosiTheme.textSecondary
            }

            ListView {
                objectName:
                    section.sectionKey === "mcp"
                    ? "panel.settings.externalDiscovery.mcp.list"
                    : "panel.settings.externalDiscovery.sessions.list"
                Accessible.id: objectName
                Accessible.role: Accessible.List
                Accessible.name: section.sectionTitle
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(
                    320,
                    Math.max(0, contentHeight))
                visible: section.rows.count > 0
                clip: true
                spacing: 1
                model: section.rows
                boundsBehavior: Flickable.StopAtBounds

                delegate: Rectangle {
                    id: externalRow

                    required property string itemId
                    required property string title
                    required property string subtitle
                    required property string agent
                    required property string status
                    required property string metadata
                    readonly property real capabilityRevision:
                        Models.ExternalDiscovery.capabilityRevision
                    readonly property bool canOpen:
                        externalRow.capabilityRevision >= 0
                        && Models.ExternalDiscovery.canOpenSource(
                            externalRow.itemId)
                    readonly property bool canCopy:
                        externalRow.capabilityRevision >= 0
                        && Models.ExternalDiscovery
                            .canCopySourcePath(
                                externalRow.itemId)
                    readonly property bool canReveal:
                        externalRow.capabilityRevision >= 0
                        && Models.ExternalDiscovery
                            .canRevealSource(
                                externalRow.itemId)

                    objectName:
                        "panel.settings.externalDiscovery.source."
                        + externalRow.itemId
                    Accessible.id: objectName
                    Accessible.role: Accessible.ListItem
                    Accessible.name: externalRow.title
                    Accessible.description: [
                        externalRow.subtitle,
                        externalRow.agent,
                        externalRow.status,
                        externalRow.metadata
                    ].filter(function(value) {
                        return value && value.length > 0
                    }).join(". ")
                    width: ListView.view.width
                    implicitHeight:
                        externalContent.implicitHeight + 16
                    color: KodosiTheme.surface

                    ColumnLayout {
                        id: externalContent
                        anchors.fill: parent
                        anchors.leftMargin: 9
                        anchors.rightMargin: 7
                        anchors.topMargin: 8
                        anchors.bottomMargin: 8
                        spacing: 5

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            KIcon {
                                Layout.preferredWidth: 15
                                Layout.preferredHeight: 15
                                name: section.sectionKey === "mcp"
                                    ? "server" : "sessions"
                                color:
                                    KodosiTheme.textSecondary
                            }

                            PlainLabel {
                                Layout.fillWidth: true
                                text: externalRow.title
                                color: KodosiTheme.textPrimary
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }

                            PlainLabel {
                                visible:
                                    externalRow.status.length > 0
                                text: externalRow.status
                                color:
                                    externalRow.status === "alive"
                                    ? KodosiTheme.success
                                    : KodosiTheme.textSecondary
                                font.pixelSize: 10
                            }
                        }

                        PlainLabel {
                            Layout.fillWidth: true
                            text: [
                                externalRow.subtitle,
                                externalRow.agent,
                                externalRow.metadata
                            ].filter(function(value) {
                                return value
                                    && value.length > 0
                            }).join(" · ")
                            color: KodosiTheme.textSecondary
                            font.pixelSize: 10
                            elide: Text.ElideRight
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            Item { Layout.fillWidth: true }

                            KButton {
                                objectName:
                                    "panel.settings.external.open."
                                    + externalRow.itemId
                                Accessible.id: objectName
                                visible: externalRow.canOpen
                                text: qsTr("Open")
                                variant: "quiet"
                                onClicked:
                                    Models.ExternalDiscovery
                                        .openSource(
                                            externalRow.itemId)
                            }

                            KButton {
                                objectName:
                                    "panel.settings.external.copy."
                                    + externalRow.itemId
                                Accessible.id: objectName
                                visible: externalRow.canCopy
                                text: qsTr("Copy Source Path")
                                variant: "quiet"
                                onClicked:
                                    Models.ExternalDiscovery
                                        .copySourcePath(
                                            externalRow.itemId)
                            }

                            KButton {
                                objectName:
                                    "panel.settings.external.reveal."
                                    + externalRow.itemId
                                Accessible.id: objectName
                                visible: externalRow.canReveal
                                text: Qt.platform.os === "linux"
                                    ? qsTr(
                                          "Reveal in File Manager")
                                    : qsTr("Reveal")
                                variant: "quiet"
                                onClicked:
                                    Models.ExternalDiscovery
                                        .revealSource(
                                            externalRow.itemId)
                            }
                        }
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: KodosiTheme.seam
                    }
                }
        }
    }

    Item {
        Layout.preferredWidth: 0
        Layout.preferredHeight: 0

        KDialog {
            id: autoModeReloadDialog
            objectName: "panel.settings.autoMode.reload.confirm"
            parent: Overlay.overlay
            width: Math.min(
                440,
                parent ? parent.width - 32 : 440)
            x: parent
                ? Math.round((parent.width - width) / 2)
                : 0
            y: parent
                ? Math.round((parent.height - height) / 2)
                : 0
            title: qsTr("Reload Auto Mode rules?")
            focus: true
            dim: true
            closePolicy: Popup.CloseOnEscape

            onOpened:
                autoModeReloadCancel.forceActiveFocus(
                    Qt.PopupFocusReason)
            onClosed: {
                if (root.visible)
                    autoModeReloadButton.forceActiveFocus(
                        Qt.PopupFocusReason)
            }

            contentItem: PlainLabel {
                width: parent ? parent.width : implicitWidth
                text: qsTr(
                    "Unsaved Auto Mode rule changes will be replaced by the latest saved rules.")
                color: KodosiTheme.textSecondary
                wrapMode: Text.Wrap
            }

            footer: Rectangle {
                implicitHeight: 58
                color: KodosiTheme.surface

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 18
                    anchors.rightMargin: 18
                    spacing: 8

                    Item { Layout.fillWidth: true }

                    KButton {
                        id: autoModeReloadCancel
                        objectName:
                            "panel.settings.autoMode.reload.cancel"
                        Accessible.id: objectName
                        text: qsTr("Cancel")
                        variant: "quiet"
                        onClicked: autoModeReloadDialog.close()
                    }

                    KButton {
                        objectName:
                            "panel.settings.autoMode.reload.replace"
                        Accessible.id: objectName
                        text: qsTr("Reload")
                        onClicked: {
                            Models.AgentAutoModeRules.refresh(true)
                            autoModeReloadDialog.close()
                        }
                    }
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    height: 1
                    color: KodosiTheme.seam
                }
            }
        }
    }

    function scopeKey(index) {
        if (index === 0)
            return "managed"
        if (index === 1)
            return "user"
        if (index === 2)
            return "project"
        return "local"
    }
}
