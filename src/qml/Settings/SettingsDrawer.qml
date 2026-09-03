pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kodosi.Models 1.0 as Models

KPopover {
    id: root
    objectName: "panel.settings"

    signal openDevicesRequested()
    signal openAgentIntelRequested()
    signal openDiagnosticsRequested()
    signal openProjectIntelRequested(string sourceId)

    property string selectedCategory: "terminal"
    property bool agentIntelAvailable: false
    property int desktopRequestSerial: 0
    property string directoryPickerRequestId
    property string openPathRequestId
    property string desktopFileError
    property bool transferringProjectIntel: false
    property bool agentsVisitActive: false
    readonly property bool compact: width < 760 || height < 580
    readonly property var categories: [
        {
            key: "terminal",
            title: qsTr("Terminal"),
            detail: qsTr("Typography and interaction"),
            icon: "terminal"
        },
        {
            key: "sessions",
            title: qsTr("Sessions"),
            detail: qsTr("Creation and history"),
            icon: "sessions"
        },
        {
            key: "supervision",
            title: qsTr("Supervision"),
            detail: qsTr("Approvals and automation"),
            icon: "shield"
        },
        {
            key: "agents",
            title: qsTr("Agents"),
            detail: qsTr("Tools and local intelligence"),
            icon: "cpu"
        },
        {
            key: "account",
            title: qsTr("Account & Devices"),
            detail: qsTr("Identity, devices, and trust"),
            icon: "account"
        }
    ]

    parent: Overlay.overlay
    width: Math.min(1040, parent ? parent.width - 40 : 1040)
    height: Math.min(720, parent ? parent.height - 40 : 720)
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    modal: true
    dim: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    function resetTerminalDraft() {
        fontFamily.text = Models.DesktopSettings.fontFamily
        fontSize.value = Models.DesktopSettings.fontSize
        cursor.currentIndex = Models.DesktopSettings.cursorStyle
        lineHeight.value = Models.DesktopSettings.lineHeight
        scrollback.value = Models.DesktopSettings.scrollbackLines
        cursorBlink.checked = Models.DesktopSettings.cursorBlink
    }

    function openAgents() {
        selectedCategory = "agents"
        resetDraft()
        open()
    }

    function acquireAgentsVisit() {
        if (!opened || selectedCategory !== "agents"
                || agentsVisitActive)
            return
        agentsVisitActive = true
        Models.ProjectIntelligence.refreshSources(false)
        Models.AgentAutoModeRules.refresh(false)
        Models.ExternalDiscovery.refresh(false)
        Models.AgentGlobal.refresh()
    }

    function releaseAgentsVisit(preserveProjectIntel) {
        if (!agentsVisitActive)
            return
        agentsVisitActive = false
        Models.AgentAutoModeRules.close()
        Models.ExternalDiscovery.close()
        if (!preserveProjectIntel)
            Models.ProjectIntelligence.closeSource()
    }

    function focusCategory(index) {
        const bounded = Math.max(
            0,
            Math.min(categories.length - 1, index))
        const button = categoryRepeater.itemAt(bounded)
        if (!button)
            return
        selectedCategory = categories[bounded].key
        button.forceActiveFocus(Qt.TabFocusReason)
    }

    function keepSettingControlVisible(item) {
        const viewport = scrollView.contentItem as Flickable
        if (!opened || !item || !viewport)
            return
        const margin = 12
        const position = item.mapToItem(scrollView, 0, 0)
        let nextY = viewport.contentY
        if (position.y < margin)
            nextY += position.y - margin
        else if (position.y + item.height
                > scrollView.availableHeight - margin)
            nextY += position.y + item.height
                - scrollView.availableHeight + margin
        const maximum = Math.max(
            0,
            viewport.contentHeight - scrollView.availableHeight)
        viewport.contentY = Math.max(
            0,
            Math.min(maximum, nextY))
    }

    function resetDraft() {
        resetTerminalDraft()
        toolApprovalAlerts.checked =
            Models.DesktopSettings.toolApprovalAlerts
        workingDirectory.text =
            Models.DesktopSettings.lastWorkingDirectory
    }

    function applyDraft() {
        Models.DesktopSettings.apply(
            fontFamily.text,
            fontSize.value,
            cursor.currentIndex,
            lineHeight.value,
            scrollback.value,
            cursorBlink.checked,
            toolApprovalAlerts.checked,
            workingDirectory.text)
    }

    function nextDesktopRequestId(suffix) {
        desktopRequestSerial += 1
        return "settings." + suffix + "." + desktopRequestSerial
    }

    function browseWorkingDirectory() {
        const requestId = nextDesktopRequestId("sessions.directory")
        directoryPickerRequestId = requestId
        desktopFileError = ""
        Models.DesktopFiles.requestDirectory(
            requestId,
            Models.DesktopFiles.SettingsWorkingDirectory,
            workingDirectory.text.length > 0
                ? workingDirectory.text
                : Models.DesktopSettings.effectiveWorkingDirectory)
    }

    function openWorkingDirectory() {
        const requestId = nextDesktopRequestId("sessions.openDirectory")
        openPathRequestId = requestId
        desktopFileError = ""
        Models.DesktopFiles.openPath(
            workingDirectory.text.length > 0
                ? workingDirectory.text
                : Models.DesktopSettings.effectiveWorkingDirectory,
            requestId,
            Models.DesktopFiles.SettingsOpenWorkingDirectory)
    }

    onOpened: {
        if (selectedCategory !== "agents")
            selectedCategory = "terminal"
        resetDraft()
        closeButton.forceActiveFocus()
        acquireAgentsVisit()
    }
    onSelectedCategoryChanged: {
        if (!opened)
            return
        if (selectedCategory === "agents")
            acquireAgentsVisit()
        else
            releaseAgentsVisit(false)
    }
    onClosed: {
        if (directoryPickerRequestId.length > 0) {
            Models.DesktopFiles.cancelDirectory(
                directoryPickerRequestId,
                Models.DesktopFiles.SettingsWorkingDirectory)
        }
        openPathRequestId = ""
        desktopFileError = ""
        releaseAgentsVisit(transferringProjectIntel)
        transferringProjectIntel = false
        resetDraft()
    }
    Component.onDestruction: {
        if (directoryPickerRequestId.length > 0) {
            Models.DesktopFiles.cancelDirectory(
                directoryPickerRequestId,
                Models.DesktopFiles.SettingsWorkingDirectory)
        }
        releaseAgentsVisit(false)
    }

    Connections {
        target: Models.DesktopFiles

        function onDirectoryPicked(requestId, purpose, canonicalDirectory) {
            if (requestId !== root.directoryPickerRequestId
                    || purpose
                        !== Models.DesktopFiles.SettingsWorkingDirectory)
                return
            root.directoryPickerRequestId = ""
            workingDirectory.text = canonicalDirectory
            root.desktopFileError = ""
        }

        function onDirectoryPickCancelled(requestId, purpose) {
            if (requestId !== root.directoryPickerRequestId
                    || purpose
                        !== Models.DesktopFiles.SettingsWorkingDirectory)
                return
            root.directoryPickerRequestId = ""
        }

        function onPathOpened(requestId, purpose) {
            if (requestId === root.openPathRequestId
                    && purpose
                        === Models.DesktopFiles
                            .SettingsOpenWorkingDirectory) {
                root.openPathRequestId = ""
                root.desktopFileError = ""
            }
        }

        function onOperationFailed(requestId, purpose, errorCode, message) {
            if (requestId === root.directoryPickerRequestId
                    && purpose
                        === Models.DesktopFiles.SettingsWorkingDirectory) {
                root.directoryPickerRequestId = ""
                root.desktopFileError = message
            } else if (requestId === root.openPathRequestId
                    && purpose
                        === Models.DesktopFiles
                            .SettingsOpenWorkingDirectory) {
                root.openPathRequestId = ""
                root.desktopFileError = message
            }
        }
    }

    background: Item {
        Rectangle {
            anchors.fill: parent
            anchors.topMargin: 12
            anchors.leftMargin: 8
            radius: KodosiTheme.radiusModal
            color: KodosiTheme.shadow
            opacity: 0.48
        }
        Rectangle {
            anchors.fill: parent
            color: KodosiTheme.canvas
            radius: KodosiTheme.radiusModal
            border.width: 1
            border.color: KodosiTheme.seamStrong
        }
    }

    contentItem: RowLayout {
        objectName: "panel.settings"
        Accessible.id: objectName
        Accessible.role: Accessible.Dialog
        Accessible.name: qsTr("Desktop settings")
        spacing: 0

        Rectangle {
            Layout.preferredWidth: root.compact ? 220 : 250
            Layout.fillHeight: true
            color: KodosiTheme.surface
            topLeftRadius: KodosiTheme.radiusModal
            bottomLeftRadius: KodosiTheme.radiusModal

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 18
                    Layout.rightMargin: 14
                    Layout.topMargin: 18
                    Layout.bottomMargin: 16
                    spacing: KodosiTheme.spacing3

                    Rectangle {
                        Layout.preferredWidth: 36
                        Layout.preferredHeight: 36
                        radius: KodosiTheme.radiusMedium
                        color: KodosiTheme.surfaceElevated
                        border.width: 1
                        border.color: KodosiTheme.seam

                        KIcon {
                            anchors.centerIn: parent
                            width: 19
                            height: 19
                            name: "command"
                            color: KodosiTheme.accent
                            strokeWidth: 2
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        PlainLabel {
                            text: qsTr("KODOSI")
                            color: KodosiTheme.accent
                            font.pixelSize: 9
                            font.weight: Font.Bold
                            font.letterSpacing: 1.2
                        }
                        PlainLabel {
                            text: qsTr("Settings")
                            color: KodosiTheme.textPrimary
                            font.pixelSize: 19
                            font.weight: Font.DemiBold
                        }
                    }

                    KIconButton {
                        id: closeButton
                        objectName: "panel.settings.close"
                        Accessible.id: objectName
                        glyph: "close"
                        Accessible.name: qsTr("Close settings")
                        onClicked: root.close()
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 10
                    Layout.rightMargin: 10
                    spacing: 4

                    Repeater {
                        id: categoryRepeater
                        model: root.categories

                        delegate: KButton {
                            id: categoryButton
                            required property int index
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: root.compact ? 48 : 58
                            objectName: "panel.settings.tab."
                                + modelData.key
                            Accessible.id: objectName
                            Accessible.name: modelData.title + ", "
                                + modelData.detail
                            Accessible.role: Accessible.ListItem
                            Accessible.selected: root.selectedCategory
                                === modelData.key
                            variant: root.selectedCategory === modelData.key
                                ? "secondary"
                                : "quiet"
                            tonalSelection: true
                            onClicked: root.selectedCategory = modelData.key
                            Accessible.onPressAction:
                                root.selectedCategory = modelData.key
                            Keys.onUpPressed: event => {
                                root.focusCategory(index - 1)
                                event.accepted = true
                            }
                            Keys.onDownPressed: event => {
                                root.focusCategory(index + 1)
                                event.accepted = true
                            }

                            contentItem: RowLayout {
                                spacing: 11
                                KIcon {
                                    Layout.preferredWidth: 18
                                    Layout.preferredHeight: 18
                                    name: categoryButton.modelData.icon
                                    color: root.selectedCategory
                                        === categoryButton.modelData.key
                                        ? KodosiTheme.accent
                                        : KodosiTheme.textSecondary
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 1
                                    PlainLabel {
                                        Layout.fillWidth: true
                                        text: categoryButton.modelData.title
                                        color: KodosiTheme.textPrimary
                                        font.pixelSize: 12
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }
                                    PlainLabel {
                                        visible: !root.compact
                                        Layout.fillWidth: true
                                        text: categoryButton.modelData.detail
                                        color: KodosiTheme.textSecondary
                                        font.pixelSize: 9
                                        elide: Text.ElideRight
                                    }
                                }
                                KIcon {
                                    Layout.preferredWidth: 12
                                    Layout.preferredHeight: 12
                                    name: "chevron-right"
                                    color: root.selectedCategory
                                        === categoryButton.modelData.key
                                        ? KodosiTheme.accent
                                        : KodosiTheme.textTertiary
                                }
                            }

                            background: Rectangle {
                                radius: KodosiTheme.radiusMedium
                                color: root.selectedCategory
                                    === categoryButton.modelData.key
                                    ? KodosiTheme.surfaceSelected
                                    : categoryButton.hovered
                                      ? KodosiTheme.surfaceElevated
                                      : "transparent"
                                border.width: categoryButton.activeFocus
                                    ? 1 : 0
                                border.color: KodosiTheme.focusRing
                            }
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: 16
                    spacing: 8
                    Rectangle {
                        Layout.preferredWidth: 7
                        Layout.preferredHeight: 7
                        radius: 4
                        color: Models.AuthState.signedIn
                            ? KodosiTheme.success
                            : KodosiTheme.warning
                    }
                    PlainLabel {
                        Layout.fillWidth: true
                        text: Models.AuthState.signedIn
                            ? qsTr("Account connected")
                            : qsTr("Local workspace")
                        color: KodosiTheme.textSecondary
                        font.pixelSize: 10
                    }
                }
            }

            Rectangle {
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.right: parent.right
                width: 1
                color: KodosiTheme.seam
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            spacing: 0

            KScrollView {
                id: scrollView
                objectName: "panel.settings.scroll"
                Accessible.id: objectName
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 0
                clip: true
                contentWidth: availableWidth

                ColumnLayout {
                    width: scrollView.availableWidth
                    spacing: 18

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: root.compact ? 18 : 28
                        Layout.rightMargin: root.compact ? 18 : 28
                        Layout.topMargin: root.compact ? 18 : 26
                        spacing: 5

                        PlainLabel {
                            text: root.categories.filter(function(item) {
                                return item.key === root.selectedCategory
                            })[0].title
                            color: KodosiTheme.textPrimary
                            font.pixelSize: 22
                            font.weight: Font.DemiBold
                        }
                        PlainLabel {
                            text: root.categories.filter(function(item) {
                                return item.key === root.selectedCategory
                            })[0].detail
                            color: KodosiTheme.textSecondary
                            font.pixelSize: 12
                        }
                    }

                    GridLayout {
                        visible: root.selectedCategory === "terminal"
                        Layout.fillWidth: true
                        Layout.leftMargin: root.compact ? 18 : 28
                        Layout.rightMargin: root.compact ? 18 : 28
                        columns: scrollView.availableWidth >= 650 ? 2 : 1
                        columnSpacing: 16
                        rowSpacing: 14

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 5
                            PlainLabel {
                                text: qsTr("FONT FAMILY")
                                color: KodosiTheme.textSecondary
                                font.pixelSize: 9
                                font.weight: Font.DemiBold
                                font.letterSpacing: 1.0
                            }
                            KTextField {
                                id: fontFamily
                                objectName:
                                    "panel.settings.terminal.fontFamily"
                                Accessible.id: objectName
                                Layout.fillWidth: true
                                Accessible.name:
                                    qsTr("Terminal font family")
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 5
                            PlainLabel {
                                text: qsTr("CURSOR")
                                color: KodosiTheme.textSecondary
                                font.pixelSize: 9
                                font.weight: Font.DemiBold
                                font.letterSpacing: 1.0
                            }
                            KComboBox {
                                id: cursor
                                objectName: "panel.settings.terminal.cursor"
                                Accessible.id: objectName
                                Layout.fillWidth: true
                                model: [
                                    qsTr("Block"),
                                    qsTr("Bar"),
                                    qsTr("Underline")
                                ]
                                Accessible.name:
                                    qsTr("Terminal cursor style")
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 5
                            PlainLabel {
                                text: qsTr("FONT SIZE · 8–32 PT")
                                color: KodosiTheme.textSecondary
                                font.pixelSize: 9
                                font.weight: Font.DemiBold
                                font.letterSpacing: 1.0
                            }
                            KSpinBox {
                                id: fontSize
                                objectName:
                                    "panel.settings.terminal.fontSize"
                                Accessible.id: objectName
                                Layout.fillWidth: true
                                from:
                                    Models.DesktopSettings.minimumFontSize
                                to: Models.DesktopSettings.maximumFontSize
                                Accessible.name: qsTr("Terminal font size")
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 5
                            RowLayout {
                                Layout.fillWidth: true
                                PlainLabel {
                                    Layout.fillWidth: true
                                    text: qsTr("LINE HEIGHT · 0.8–2.0")
                                    color: KodosiTheme.textSecondary
                                    font.pixelSize: 9
                                    font.weight: Font.DemiBold
                                    font.letterSpacing: 1.0
                                }
                                PlainLabel {
                                    text: lineHeight.value.toFixed(1)
                                    color: KodosiTheme.textPrimary
                                    font.family: "monospace"
                                    font.pixelSize: 11
                                }
                            }
                            KSlider {
                                id: lineHeight
                                objectName:
                                    "panel.settings.terminal.lineHeight"
                                Accessible.id: objectName
                                Layout.fillWidth: true
                                from:
                                    Models.DesktopSettings.minimumLineHeight
                                to:
                                    Models.DesktopSettings.maximumLineHeight
                                stepSize: 0.1
                                snapMode: Slider.SnapAlways
                                Accessible.name:
                                    qsTr("Terminal line height")
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.columnSpan:
                                scrollView.availableWidth >= 650 ? 2 : 1
                            spacing: 5
                            PlainLabel {
                                text: qsTr(
                                    "LINES OF HISTORY · 100–100,000")
                                color: KodosiTheme.textSecondary
                                font.pixelSize: 9
                                font.weight: Font.DemiBold
                                font.letterSpacing: 1.0
                            }
                            KSpinBox {
                                id: scrollback
                                objectName:
                                    "panel.settings.terminal.scrollback"
                                Accessible.id: objectName
                                Layout.fillWidth: true
                                from: Models.DesktopSettings
                                    .minimumScrollbackLines
                                to: Models.DesktopSettings
                                    .maximumScrollbackLines
                                stepSize: 100
                                Accessible.name:
                                    qsTr("Terminal scrollback lines")
                            }
                        }

                        KSwitch {
                            id: cursorBlink
                            objectName:
                                "panel.settings.terminal.cursorBlink"
                            Accessible.id: objectName
                            Layout.columnSpan:
                                scrollView.availableWidth >= 650 ? 2 : 1
                            text: qsTr("Blinking cursor")
                            Accessible.name: text
                        }
                    }

                    ColumnLayout {
                        visible: root.selectedCategory === "sessions"
                        Layout.fillWidth: true
                        Layout.leftMargin: root.compact ? 18 : 28
                        Layout.rightMargin: root.compact ? 18 : 28
                        spacing: 9

                        PlainLabel {
                            text: qsTr("DEFAULT WORKING DIRECTORY")
                            color: KodosiTheme.textSecondary
                            font.pixelSize: 9
                            font.weight: Font.DemiBold
                            font.letterSpacing: 1.0
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: KodosiTheme.spacing2

                            KTextField {
                                id: workingDirectory
                                objectName:
                                    "panel.settings.sessions.workingDirectory"
                                Accessible.id: objectName
                                Layout.fillWidth: true
                                maximumLength: 4096
                                placeholderText: qsTr(
                                    "Leave empty to use your home directory")
                                Accessible.name:
                                    qsTr("Default working directory")
                            }

                            KButton {
                                objectName:
                                    "panel.settings.sessions.browse"
                                Accessible.id: objectName
                                text: qsTr("Browse")
                                Accessible.name:
                                    qsTr("Browse for default working directory")
                                enabled: !Models.DesktopFiles.busy
                                onClicked: root.browseWorkingDirectory()
                            }

                            KButton {
                                objectName:
                                    "panel.settings.sessions.openFolder"
                                Accessible.id: objectName
                                text: qsTr("Open Folder")
                                Accessible.name:
                                    qsTr("Open default working directory")
                                onClicked: root.openWorkingDirectory()
                            }
                        }
                        PlainLabel {
                            Layout.fillWidth: true
                            text: qsTr("Kodosi uses this only while it remains a readable directory.")
                            color: KodosiTheme.textSecondary
                            font.pixelSize: 10
                            wrapMode: Text.Wrap
                        }
                    }

                    ColumnLayout {
                        visible: root.selectedCategory === "supervision"
                        Layout.fillWidth: true
                        Layout.leftMargin: root.compact ? 18 : 28
                        Layout.rightMargin: root.compact ? 18 : 28
                        spacing: 14

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: supervisionInfo.implicitHeight
                                + 24
                            radius: KodosiTheme.radiusMedium
                            color: KodosiTheme.surfaceRaised
                            border.width: 1
                            border.color: KodosiTheme.seam
                            RowLayout {
                                id: supervisionInfo
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 10
                                KIcon {
                                    Layout.preferredWidth: 20
                                    Layout.preferredHeight: 20
                                    name: "shield"
                                    color: KodosiTheme.accent
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    PlainLabel {
                                        text: qsTr(
                                            "Pending approval expiry")
                                        color: KodosiTheme.textPrimary
                                        font.pixelSize: 12
                                        font.weight: Font.DemiBold
                                    }
                                    PlainLabel {
                                        Layout.fillWidth: true
                                        text: qsTr("Kodosi denies the pending tool call when its countdown reaches zero.")
                                        color: KodosiTheme.textSecondary
                                        font.pixelSize: 10
                                        wrapMode: Text.Wrap
                                    }
                                }
                            }
                        }

                        KSwitch {
                            id: toolApprovalAlerts
                            objectName:
                                "panel.settings.notifications.toolApprovalAlerts"
                            Accessible.id: objectName
                            text: qsTr("Tool approval alerts")
                            Accessible.name: text
                        }
                    }

                    ColumnLayout {
                        visible: root.selectedCategory === "agents"
                        Layout.fillWidth: true
                        Layout.leftMargin: root.compact ? 18 : 28
                        Layout.rightMargin: root.compact ? 18 : 28
                        spacing: 12

                        AgentsSettingsSurface {
                            Layout.fillWidth: true
                            onEnsureVisible: function(item) {
                                root.keepSettingControlVisible(item)
                            }
                            onOpenProjectIntel: function(sourceId) {
                                root.transferringProjectIntel = true
                                root.openProjectIntelRequested(sourceId)
                            }
                        }

                        SurfaceCard {
                            Layout.fillWidth: true
                            implicitHeight: selectedSessionActions.implicitHeight + 24

                            RowLayout {
                                id: selectedSessionActions
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 10
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    PlainLabel {
                                        text: qsTr("Selected-session Agent Intel")
                                        color: KodosiTheme.textPrimary
                                        font.weight: Font.DemiBold
                                    }
                                    PlainLabel {
                                        Layout.fillWidth: true
                                        text: root.agentIntelAvailable
                                            ? qsTr("Keep the live session overview, conversation, memory, and Custom Agents close at hand.")
                                            : qsTr("Select a session in My Agents to open its live Agent Intel.")
                                        color: KodosiTheme.textSecondary
                                        wrapMode: Text.Wrap
                                    }
                                }
                                KButton {
                                    objectName: "panel.settings.agents.openIntel"
                                    Accessible.id: objectName
                                    text: root.agentIntelAvailable
                                        ? qsTr("Open Agent Intel")
                                        : qsTr("No session selected")
                                    enabled: root.agentIntelAvailable
                                    onClicked: root.openAgentIntelRequested()
                                }
                                KButton {
                                    objectName:
                                        "panel.settings.agents.openDiagnostics"
                                    Accessible.id: objectName
                                    text: qsTr("Diagnostics")
                                    variant: "quiet"
                                    onClicked: root.openDiagnosticsRequested()
                                }
                            }
                        }
                    }

                    ColumnLayout {
                        visible: root.selectedCategory === "account"
                        Layout.fillWidth: true
                        Layout.leftMargin: root.compact ? 18 : 28
                        Layout.rightMargin: root.compact ? 18 : 28
                        spacing: 12

                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: accountContent.implicitHeight
                                + 28
                            radius: KodosiTheme.radiusLarge
                            color: KodosiTheme.surfaceRaised
                            border.width: 1
                            border.color: KodosiTheme.seam
                            ColumnLayout {
                                id: accountContent
                                anchors.fill: parent
                                anchors.margins: 14
                                spacing: 8
                                KIcon {
                                    Layout.preferredWidth: 28
                                    Layout.preferredHeight: 28
                                    name: "devices"
                                    color: KodosiTheme.accent
                                }
                                PlainLabel {
                                    text: Models.AuthState.signedIn
                                        ? qsTr("Account connected")
                                        : qsTr("Local workspace")
                                    color: KodosiTheme.textPrimary
                                    font.pixelSize: 14
                                    font.weight: Font.DemiBold
                                }
                                PlainLabel {
                                    Layout.fillWidth: true
                                    text: qsTr("Manage device enrollment, integrations, trust pins, and account recovery on the dedicated Devices surface.")
                                    color: KodosiTheme.textSecondary
                                    font.pixelSize: 11
                                    wrapMode: Text.Wrap
                                }
                                KButton {
                                    objectName:
                                        "panel.settings.account.openDevices"
                                    Accessible.id: objectName
                                    variant: "directional"
                                    iconName: "chevron-right"
                                    text: qsTr("Open Account & Devices")
                                    Accessible.name: text
                                    onClicked:
                                        root.openDevicesRequested()
                                }
                            }
                        }
                    }

                    Rectangle {
                        visible:
                            Models.DesktopSettings.settingsError.length > 0
                            || Models.DesktopState.lastError.length > 0
                            || root.desktopFileError.length > 0
                        Layout.fillWidth: true
                        Layout.leftMargin: root.compact ? 18 : 28
                        Layout.rightMargin: root.compact ? 18 : 28
                        implicitHeight: settingsErrorLabel.implicitHeight
                            + 20
                        color: Qt.rgba(
                            KodosiTheme.danger.r,
                            KodosiTheme.danger.g,
                            KodosiTheme.danger.b,
                            0.08)
                        border.width: 1
                        border.color: KodosiTheme.danger
                        radius: KodosiTheme.radiusMedium

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: KodosiTheme.spacing2

                            PlainLabel {
                                id: settingsErrorLabel
                                Layout.fillWidth: true
                                text:
                                    [
                                        Models.DesktopSettings.settingsError,
                                        Models.DesktopState.lastError,
                                        root.desktopFileError
                                    ].filter(function(message) {
                                        return message.length > 0
                                    }).join("\n")
                                color: KodosiTheme.danger
                                font.pixelSize: 11
                                wrapMode: Text.Wrap
                                Accessible.name: text
                            }

                            KIconButton {
                                objectName:
                                    "panel.settings.desktopFile.error.dismiss"
                                Accessible.id: objectName
                                visible: root.desktopFileError.length > 0
                                glyph: "close"
                                Accessible.name:
                                    qsTr("Dismiss filesystem action error")
                                onClicked: root.desktopFileError = ""
                            }
                        }
                    }

                    Item { Layout.preferredHeight: 16 }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 66
                Layout.minimumHeight: 66
                Layout.maximumHeight: 66
                color: KodosiTheme.surfaceRaised
                bottomRightRadius: KodosiTheme.radiusModal

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: root.compact ? 18 : 28
                    anchors.rightMargin: root.compact ? 18 : 28
                    spacing: KodosiTheme.spacing3

                    KButton {
                        objectName: "panel.settings.reset"
                        Accessible.id: objectName
                        visible: root.selectedCategory === "terminal"
                        variant: "secondary"
                        iconName: "refresh"
                        text: qsTr("Restore Defaults")
                        Accessible.name:
                            qsTr("Restore terminal settings defaults")
                        onClicked: {
                            if (Models.DesktopSettings.resetTerminal())
                                root.resetTerminalDraft()
                        }
                    }

                    Item { Layout.fillWidth: true }

                    KButton {
                        objectName: "panel.settings.apply"
                        Accessible.id: objectName
                        variant: "directional"
                        iconName: "chevron-right"
                        text: qsTr("Apply & Save")
                        Accessible.name:
                            qsTr("Apply and save desktop settings")
                        onClicked: root.applyDraft()
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
}
