pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kodosi.Models 1.0 as Models

KPopover {
    id: root
    objectName: "panel.projectIntel"

    property int activeTab: 0
    property string copyDestinationId
    property string pendingSourceId
    property string pendingSessionId
    property bool conversationOpen: false
    property Item selectedSourceButton
    property string actionStatus
    property bool actionStatusIsError: false
    readonly property bool compact: width < 940 || height < 650
    readonly property bool activeSource:
        Models.ProjectIntelligence.selectedSourceKind === "active"

    parent: Overlay.overlay
    width: Math.min(1240, parent ? parent.width - 24 : 1240)
    height: Math.min(800, parent ? parent.height - 24 : 800)
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    padding: 0
    modal: true
    dim: true
    focus: true
    closePolicy: Popup.CloseOnEscape

    function openBrowser() {
        activeTab = 0
        conversationOpen = false
        actionStatus = ""
        Models.ProjectIntelligence.refreshSources(false)
        open()
    }

    function openForSource(sourceId) {
        activeTab = 0
        conversationOpen = false
        actionStatus = ""
        pendingSessionId = ""
        pendingSourceId = sourceId
        if (Models.ProjectIntelligence.sources.count === 0)
            Models.ProjectIntelligence.refreshSources(false)
        Qt.callLater(resolvePendingSelection)
        open()
    }

    function openForSession(sessionId) {
        activeTab = 0
        conversationOpen = false
        actionStatus = ""
        pendingSourceId = ""
        pendingSessionId = sessionId
        if (Models.ProjectIntelligence.sources.count === 0)
            Models.ProjectIntelligence.refreshSources(false)
        Qt.callLater(resolvePendingSelection)
        open()
    }

    function resolvePendingSelection() {
        if (Models.ProjectIntelligence.loading
                || Models.ProjectIntelligence.state
                    === Models.ProjectIntelligence.Failed)
            return
        if (pendingSessionId.length > 0) {
            if (Models.ProjectIntelligence.hasMoreSources) {
                if (!Models.ProjectIntelligence.loadMoreSources()) {
                    actionStatus =
                        qsTr("The remaining project sources could not be loaded.")
                    actionStatusIsError = true
                    pendingSessionId = ""
                }
                return
            }
            const sessionId = pendingSessionId
            pendingSessionId = ""
            if (!Models.ProjectIntelligence
                    .selectSourceForSession(sessionId)) {
                actionStatus =
                    qsTr("No unique project source contains this session.")
                actionStatusIsError = true
            }
            return
        }
        if (pendingSourceId.length > 0) {
            const sourceId = pendingSourceId
            if (Models.ProjectIntelligence.selectSource(sourceId)) {
                pendingSourceId = ""
                return
            }
            if (Models.ProjectIntelligence.hasMoreSources) {
                if (!Models.ProjectIntelligence.loadMoreSources()) {
                    actionStatus =
                        qsTr("The remaining project sources could not be loaded.")
                    actionStatusIsError = true
                    pendingSourceId = ""
                }
                return
            }
            pendingSourceId = ""
            actionStatus =
                qsTr("The requested project source is no longer available.")
            actionStatusIsError = true
        }
    }

    function sourceKindLabel(kind) {
        if (kind === "active")
            return qsTr("Project intelligence")
        if (kind === "claudeArchive")
            return qsTr("Claude project archive")
        return qsTr("Copilot repository archive")
    }

    function sourceCountLabel(sessionCount, memoryCount) {
        const sessions = qsTr(
            "%n sessions",
            "",
            sessionCount)
        if (memoryCount <= 0)
            return sessions
        return sessions + " · " + qsTr(
            "%n memory files",
            "",
            memoryCount)
    }

    function tabButton(index) {
        if (index === 0)
            return sessionsTab
        if (index === 1)
            return memoryTab
        if (index === 2)
            return serversTab
        return agentsTab
    }

    function activateTab(index, focusControl) {
        activeTab = Math.max(0, Math.min(3, index))
        if (focusControl) {
            const button = tabButton(activeTab)
            if (button)
                button.forceActiveFocus(Qt.TabFocusReason)
        }
    }

    function moveTabFocus(index, delta) {
        activateTab((index + delta + 4) % 4, true)
    }

    function focusSource(index) {
        if (sourceList.count === 0)
            return
        const targetIndex = Math.max(
            0,
            Math.min(sourceList.count - 1, index))
        sourceList.currentIndex = targetIndex
        sourceList.positionViewAtIndex(
            targetIndex,
            ListView.Contain)
        Qt.callLater(function() {
            const button = sourceList.itemAtIndex(targetIndex)
            if (button)
                button.forceActiveFocus(Qt.TabFocusReason)
        })
    }

    function registerSelectedSourceButton(button, selected) {
        if (selected) {
            selectedSourceButton = button
            sourceList.currentIndex = button.index
        } else if (selectedSourceButton === button) {
            selectedSourceButton = null
        }
    }

    function focusInitialControl() {
        if (selectedSourceButton) {
            selectedSourceButton.forceActiveFocus(
                Qt.PopupFocusReason)
        } else {
            closeButton.forceActiveFocus(Qt.PopupFocusReason)
        }
    }

    onActiveTabChanged: {
        if (activeTab !== 0 && conversationOpen) {
            Models.AgentConversation.close()
            conversationOpen = false
        }
    }

    onOpened: Qt.callLater(focusInitialControl)
    onSelectedSourceButtonChanged: {
        if (opened && selectedSourceButton)
            Qt.callLater(focusInitialControl)
    }

    onClosed: {
        pendingSourceId = ""
        pendingSessionId = ""
        copyDestinationId = ""
        conversationOpen = false
        selectedSourceButton = null
        actionStatus = ""
        Models.ProjectIntelligence.closeSource()
        Models.AgentConversation.close()
    }

    Connections {
        target: Models.ProjectIntelligence

        function onStateChanged() {
            if (root.pendingSourceId.length > 0
                    || root.pendingSessionId.length > 0)
                Qt.callLater(root.resolvePendingSelection)
        }

        function onSourceChanged() {
            root.conversationOpen = false
            if (root.opened)
                Qt.callLater(root.focusInitialControl)
        }

        function onActionMessage(message, error) {
            root.actionStatus = message
            root.actionStatusIsError = error
        }
    }

    Overlay.modal: Rectangle { color: KodosiTheme.overlayDim }

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
            border.width: 1
            border.color: KodosiTheme.seamStrong
        }
    }

    contentItem: ColumnLayout {
        objectName: "panel.projectIntel"
        Accessible.id: objectName
        Accessible.role: Accessible.Dialog
        Accessible.name: qsTr("Project Intelligence")
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 66
            color: KodosiTheme.surfaceElevated
            topLeftRadius: KodosiTheme.radiusModal
            topRightRadius: KodosiTheme.radiusModal

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 14
                spacing: 12

                KIcon {
                    Layout.preferredWidth: 20
                    Layout.preferredHeight: 20
                    name: "folder"
                    color: KodosiTheme.textSecondary
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1

                    PlainLabel {
                        Layout.fillWidth: true
                        text: Models.ProjectIntelligence
                                  .selectedSourceLabel.length > 0
                            ? Models.ProjectIntelligence
                                  .selectedSourceLabel
                            : qsTr("Project Intelligence")
                        color: KodosiTheme.textPrimary
                        font.pixelSize: 18
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }

                    PlainLabel {
                        Layout.fillWidth: true
                        text: Models.ProjectIntelligence
                                  .selectedSourceKind.length > 0
                            ? root.sourceKindLabel(
                                  Models.ProjectIntelligence
                                      .selectedSourceKind)
                            : qsTr("Active projects and agent archives")
                        color: KodosiTheme.textSecondary
                        font.pixelSize: 10
                        elide: Text.ElideRight
                    }
                }

                KIconButton {
                    id: refreshButton
                    objectName: "panel.projectIntel.refresh"
                    Accessible.id: objectName
                    glyph: "refresh"
                    glyphColor: KodosiTheme.textSecondary
                    Accessible.name: qsTr("Refresh project sources")
                    enabled: !Models.ProjectIntelligence.loading
                    onClicked:
                        Models.ProjectIntelligence.refreshSources(true)
                }

                KIconButton {
                    id: closeButton
                    objectName: "panel.projectIntel.close"
                    Accessible.id: objectName
                    glyph: "close"
                    glyphColor: KodosiTheme.textSecondary
                    Accessible.name: qsTr("Close Project Intelligence")
                    onClicked: root.close()
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Rectangle {
                Layout.preferredWidth: root.compact ? 224 : 272
                Layout.fillHeight: true
                color: KodosiTheme.surface

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8

                    PlainLabel {
                        text: qsTr("PROJECT SOURCES")
                        color: KodosiTheme.textSecondary
                        font.pixelSize: 9
                        font.weight: Font.Bold
                        font.letterSpacing: 1
                    }

                    KBusyIndicator {
                        Layout.alignment: Qt.AlignHCenter
                        visible: Models.ProjectIntelligence.state
                            === Models.ProjectIntelligence.LoadingSources
                            && Models.ProjectIntelligence.sources.count
                                === 0
                        running: visible
                    }

                    ListView {
                        id: sourceList
                        objectName: "panel.projectIntel.sources"
                        Accessible.id: objectName
                        Accessible.role: Accessible.List
                        Accessible.name: qsTr("Project sources")
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 3
                        model: Models.ProjectIntelligence.sources
                        boundsBehavior: Flickable.StopAtBounds
                        currentIndex: -1
                        keyNavigationEnabled: false

                        delegate: KButton {
                            id: sourceButton

                            required property int index
                            required property string itemId
                            required property string title
                            required property string subtitle
                            required property string kind
                            required property int numberA
                            required property int numberB
                            readonly property bool sourceSelected:
                                Models.ProjectIntelligence
                                    .selectedSourceId === itemId

                            width: ListView.view.width
                            implicitHeight: root.compact ? 64 : 70
                            objectName: sourceSelected
                                ? "panel.projectIntel.source.selected"
                                : "panel.projectIntel.source." + itemId
                            Accessible.id: objectName
                            Accessible.role: Accessible.ListItem
                            Accessible.name: title
                            Accessible.description: subtitle + ". "
                                + root.sourceCountLabel(numberA, numberB)
                            Accessible.selected: sourceSelected
                            checkable: true
                            checked: sourceSelected
                            tonalSelection: true
                            variant: "quiet"
                            contentLeftAligned: true

                            function activateSource() {
                                sourceList.currentIndex = index
                                Models.ProjectIntelligence.selectSource(
                                    itemId)
                            }
                            onClicked: activateSource()
                            Accessible.onPressAction: activateSource()
                            onSourceSelectedChanged:
                                root.registerSelectedSourceButton(
                                    sourceButton,
                                    sourceSelected)
                            Component.onCompleted:
                                root.registerSelectedSourceButton(
                                    sourceButton,
                                    sourceSelected)
                            Component.onDestruction: {
                                if (root.selectedSourceButton
                                        === sourceButton)
                                    root.selectedSourceButton = null
                            }
                            Keys.onUpPressed: event => {
                                root.focusSource(index - 1)
                                event.accepted = true
                            }
                            Keys.onDownPressed: event => {
                                root.focusSource(index + 1)
                                event.accepted = true
                            }
                            contentItem: RowLayout {
                                spacing: 9

                                KIcon {
                                    Layout.preferredWidth: 16
                                    Layout.preferredHeight: 16
                                    name: kind === "active"
                                        ? "folder" : "archive"
                                    color: KodosiTheme.textSecondary
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 1

                                    PlainLabel {
                                        Layout.fillWidth: true
                                        text: title
                                        color: KodosiTheme.textPrimary
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }

                                    PlainLabel {
                                        Layout.fillWidth: true
                                        text: subtitle
                                        color:
                                            KodosiTheme.textSecondary
                                        font.pixelSize: 9
                                        wrapMode: Text.Wrap
                                        maximumLineCount: 2
                                        elide: Text.ElideRight
                                    }

                                    PlainLabel {
                                        Layout.fillWidth: true
                                        text: root.sourceCountLabel(
                                            numberA,
                                            numberB)
                                        color: KodosiTheme.textTertiary
                                        font.pixelSize: 9
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }
                    }

                    KButton {
                        objectName: "panel.projectIntel.sources.more"
                        Accessible.id: objectName
                        Layout.fillWidth: true
                        visible: Models.ProjectIntelligence.hasMoreSources
                        text: qsTr("Load more")
                        variant: "quiet"
                        enabled: !Models.ProjectIntelligence.loading
                        Accessible.name:
                            qsTr("Load more project sources")
                        onClicked:
                            Models.ProjectIntelligence.loadMoreSources()
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
                spacing: 0

                Rectangle {
                    visible:
                        Models.ProjectIntelligence.selectedSourceId.length
                            > 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: 52
                    color: KodosiTheme.surface

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12

                        KSegmentedBar {
                            objectName: "panel.projectIntel.tabs"
                            Accessible.id: objectName
                            Accessible.role: Accessible.PageTabList
                            Accessible.name:
                                qsTr("Project Intelligence sections")
                            Layout.preferredHeight: 38

                            KButton {
                                id: sessionsTab
                                objectName:
                                    "panel.projectIntel.tab.sessions"
                                Accessible.id: objectName
                                Accessible.role: Accessible.PageTab
                                text: qsTr("Sessions")
                                iconName: "sessions"
                                iconColor: KodosiTheme.textSecondary
                                variant: "quiet"
                                checkable: true
                                checked: root.activeTab === 0
                                tonalSelection: true
                                Accessible.selected: checked
                                onClicked: root.activateTab(0, false)
                                Accessible.onPressAction:
                                    root.activateTab(0, false)
                                Keys.onLeftPressed: event => {
                                    root.moveTabFocus(0, -1)
                                    event.accepted = true
                                }
                                Keys.onRightPressed: event => {
                                    root.moveTabFocus(0, 1)
                                    event.accepted = true
                                }
                            }

                            KButton {
                                id: memoryTab
                                objectName:
                                    "panel.projectIntel.tab.memory"
                                Accessible.id: objectName
                                Accessible.role: Accessible.PageTab
                                text: qsTr("Memory")
                                iconName: "memory"
                                iconColor: KodosiTheme.textSecondary
                                variant: "quiet"
                                checkable: true
                                checked: root.activeTab === 1
                                tonalSelection: true
                                Accessible.selected: checked
                                onClicked: root.activateTab(1, false)
                                Accessible.onPressAction:
                                    root.activateTab(1, false)
                                Keys.onLeftPressed: event => {
                                    root.moveTabFocus(1, -1)
                                    event.accepted = true
                                }
                                Keys.onRightPressed: event => {
                                    root.moveTabFocus(1, 1)
                                    event.accepted = true
                                }
                            }

                            KButton {
                                id: serversTab
                                objectName:
                                    "panel.projectIntel.tab.servers"
                                Accessible.id: objectName
                                Accessible.role: Accessible.PageTab
                                text: qsTr("MCP Servers")
                                iconName: "server"
                                iconColor: KodosiTheme.textSecondary
                                variant: "quiet"
                                checkable: true
                                checked: root.activeTab === 2
                                tonalSelection: true
                                Accessible.selected: checked
                                onClicked: root.activateTab(2, false)
                                Accessible.onPressAction:
                                    root.activateTab(2, false)
                                Keys.onLeftPressed: event => {
                                    root.moveTabFocus(2, -1)
                                    event.accepted = true
                                }
                                Keys.onRightPressed: event => {
                                    root.moveTabFocus(2, 1)
                                    event.accepted = true
                                }
                            }

                            KButton {
                                id: agentsTab
                                objectName:
                                    "panel.projectIntel.tab.agents"
                                Accessible.id: objectName
                                Accessible.role: Accessible.PageTab
                                text: qsTr("Agents")
                                iconName: "agents"
                                iconColor: KodosiTheme.textSecondary
                                variant: "quiet"
                                checkable: true
                                checked: root.activeTab === 3
                                tonalSelection: true
                                Accessible.selected: checked
                                onClicked: root.activateTab(3, false)
                                Accessible.onPressAction:
                                    root.activateTab(3, false)
                                Keys.onLeftPressed: event => {
                                    root.moveTabFocus(3, -1)
                                    event.accepted = true
                                }
                                Keys.onRightPressed: event => {
                                    root.moveTabFocus(3, 1)
                                    event.accepted = true
                                }
                            }
                        }

                        Item { Layout.fillWidth: true }
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: KodosiTheme.seam
                    }
                }

                Rectangle {
                    objectName: "panel.projectIntel.actionMessage"
                    Accessible.id: objectName
                    Accessible.role: Accessible.StaticText
                    Accessible.name: root.actionStatus
                    Layout.fillWidth: true
                    implicitHeight: actionMessageRow.implicitHeight + 16
                    visible: root.actionStatus.length > 0
                    color: root.actionStatusIsError
                        ? Qt.rgba(
                              KodosiTheme.danger.r,
                              KodosiTheme.danger.g,
                              KodosiTheme.danger.b,
                              0.10)
                        : Qt.rgba(
                              KodosiTheme.success.r,
                              KodosiTheme.success.g,
                              KodosiTheme.success.b,
                              0.08)

                    RowLayout {
                        id: actionMessageRow
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 8
                        spacing: 8

                        KIcon {
                            Layout.preferredWidth: 15
                            Layout.preferredHeight: 15
                            name: root.actionStatusIsError
                                ? "warning" : "check"
                            color: root.actionStatusIsError
                                ? KodosiTheme.danger
                                : KodosiTheme.success
                        }

                        PlainLabel {
                            Layout.fillWidth: true
                            text: root.actionStatus
                            color: root.actionStatusIsError
                                ? KodosiTheme.danger
                                : KodosiTheme.textSecondary
                            wrapMode: Text.Wrap
                        }

                        KIconButton {
                            objectName:
                                "panel.projectIntel.actionMessage.dismiss"
                            Accessible.id: objectName
                            glyph: "close"
                            glyphColor: KodosiTheme.textSecondary
                            Accessible.name:
                                qsTr("Dismiss project action status")
                            onClicked: root.actionStatus = ""
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    ColumnLayout {
                        objectName: "panel.projectIntel.state"
                        Accessible.id: objectName
                        anchors.centerIn: parent
                        width: Math.min(460, parent.width - 40)
                        spacing: 10
                        visible:
                            !Models.ProjectIntelligence
                                .hasSourceSnapshot
                            || Models.ProjectIntelligence.state
                                === Models.ProjectIntelligence.Failed

                        KBusyIndicator {
                            Layout.alignment: Qt.AlignHCenter
                            visible: Models.ProjectIntelligence.loading
                            running: visible
                        }

                        PlainLabel {
                            Layout.fillWidth: true
                            text: Models.ProjectIntelligence.state
                                === Models.ProjectIntelligence.Failed
                                ? qsTr(
                                      "Project Intelligence is unavailable")
                                : Models.ProjectIntelligence.loading
                                  ? qsTr(
                                        "Loading project intelligence")
                                  : qsTr("Select a project source")
                            color: KodosiTheme.textPrimary
                            font.pixelSize: 16
                            font.weight: Font.DemiBold
                            horizontalAlignment: Text.AlignHCenter
                        }

                        PlainLabel {
                            Layout.fillWidth: true
                            text: Models.ProjectIntelligence.error.length
                                > 0
                                ? Models.ProjectIntelligence.error
                                : qsTr(
                                      "Choose an active project or archive to inspect sessions, memory, servers, and agents.")
                            color: Models.ProjectIntelligence.error.length
                                > 0
                                ? KodosiTheme.danger
                                : KodosiTheme.textSecondary
                            wrapMode: Text.Wrap
                            horizontalAlignment: Text.AlignHCenter
                        }

                        KButton {
                            objectName:
                                "panel.projectIntel.state.retry"
                            Accessible.id: objectName
                            Layout.alignment: Qt.AlignHCenter
                            visible: Models.ProjectIntelligence.state
                                === Models.ProjectIntelligence.Failed
                            text: qsTr("Retry")
                            onClicked:
                                Models.ProjectIntelligence.retry()
                        }
                    }

                    StackLayout {
                        anchors.fill: parent
                        visible:
                            Models.ProjectIntelligence
                                .hasSourceSnapshot
                            && Models.ProjectIntelligence.state
                                !== Models.ProjectIntelligence.Failed
                        currentIndex: root.activeTab

                        SessionsPane {}
                        MemoryPane {}
                        McpPane {}
                        AgentsPane {}
                    }

                    KBusyIndicator {
                        objectName:
                            "panel.projectIntel.snapshot.refreshing"
                        Accessible.id: objectName
                        Accessible.name:
                            qsTr("Refreshing project intelligence")
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.margins: 10
                        width: 20
                        height: 20
                        z: 2
                        visible:
                            Models.ProjectIntelligence.loading
                            && Models.ProjectIntelligence
                                .hasSourceSnapshot
                        running: visible
                    }
                }
            }
        }
    }

    component EmptyPane: ColumnLayout {
        property string objectId
        property string title
        property string detail

        objectName: objectId
        Accessible.id: objectName
        Accessible.role: Accessible.StaticText
        Accessible.name: detail.length > 0
            ? title + ". " + detail
            : title
        anchors.centerIn: parent
        width: Math.min(440, parent.width - 40)
        spacing: 6

        KIcon {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 24
            Layout.preferredHeight: 24
            name: "intel"
            color: KodosiTheme.textTertiary
        }

        PlainLabel {
            Layout.fillWidth: true
            text: parent.title
            color: KodosiTheme.textPrimary
            font.pixelSize: 15
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }

        PlainLabel {
            Layout.fillWidth: true
            visible: parent.detail.length > 0
            text: parent.detail
            color: KodosiTheme.textSecondary
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
        }
    }

    component SessionsPane: Item {
        objectName: "panel.projectIntel.sessions"
        Accessible.id: objectName

        EmptyPane {
            objectId: "panel.projectIntel.sessions.empty"
            visible: Models.ProjectIntelligence.sessions.count === 0
                && !root.conversationOpen
            title: root.activeSource
                ? qsTr("No sessions for this project")
                : qsTr("No archived sessions for this project")
            detail: root.activeSource
                ? qsTr(
                      "Start a session against this project and it will show up here.")
                : qsTr(
                      "No archived transcripts were found for this source.")
        }

        ListView {
            objectName: "panel.projectIntel.sessions.list"
            Accessible.id: objectName
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Project sessions")
            anchors.fill: parent
            anchors.margins: 12
            visible: Models.ProjectIntelligence.sessions.count > 0
                && !root.conversationOpen
            clip: true
            spacing: 1
            model: Models.ProjectIntelligence.sessions
            boundsBehavior: Flickable.StopAtBounds

            delegate: KButton {
                required property string itemId
                required property string title
                required property string subtitle
                required property string agent
                required property string status
                required property string mode
                required property bool available

                width: ListView.view.width
                implicitHeight: 58
                variant: "quiet"
                contentLeftAligned: true
                objectName:
                    "panel.projectIntel.session." + itemId
                Accessible.id: objectName
                Accessible.role: Accessible.ListItem
                Accessible.name: title
                Accessible.description: available && root.activeSource
                    ? qsTr(
                          "Open the agent transcript. A current matching session incarnation is required.")
                    : qsTr(
                          "A current matching session incarnation is required.")

                function openTranscript() {
                    if (Models.ProjectIntelligence.openSession(itemId))
                        root.conversationOpen = true
                }
                onClicked: openTranscript()
                Accessible.onPressAction: openTranscript()

                contentItem: RowLayout {
                    spacing: 10

                    Rectangle {
                        Accessible.ignored: true
                        Layout.preferredWidth: 8
                        Layout.preferredHeight: 8
                        radius: 4
                        color: available && root.activeSource
                            ? KodosiTheme.success
                            : KodosiTheme.textTertiary
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        PlainLabel {
                            Layout.fillWidth: true
                            text: title
                            color: KodosiTheme.textPrimary
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }

                        PlainLabel {
                            Layout.fillWidth: true
                            text: [
                                agent,
                                status,
                                mode,
                                subtitle
                            ].filter(function(value) {
                                return value && value.length > 0
                            }).join(" · ")
                            color: KodosiTheme.textSecondary
                            font.pixelSize: 10
                            elide: Text.ElideRight
                        }
                    }

                    KIcon {
                        Layout.preferredWidth: 12
                        Layout.preferredHeight: 12
                        name: "chevron-right"
                        color: available && root.activeSource
                            ? KodosiTheme.textSecondary
                            : KodosiTheme.textTertiary
                    }
                }
            }
        }

        ColumnLayout {
            objectName: "panel.projectIntel.conversation"
            Accessible.id: objectName
            anchors.fill: parent
            anchors.margins: 12
            visible: root.conversationOpen
            spacing: 8

            RowLayout {
                Layout.fillWidth: true

                KButton {
                    objectName:
                        "panel.projectIntel.conversation.back"
                    Accessible.id: objectName
                    text: qsTr("Back to sessions")
                    variant: "quiet"
                    iconName: "chevron-left"
                    onClicked: {
                        Models.AgentConversation.close()
                        root.conversationOpen = false
                    }
                }

                PlainLabel {
                    Layout.fillWidth: true
                    text: qsTr("Active transcript")
                    color: KodosiTheme.textPrimary
                    font.weight: Font.DemiBold
                }

                KButton {
                    objectName:
                        "panel.projectIntel.conversation.earlier"
                    Accessible.id: objectName
                    text: qsTr("Load earlier")
                    visible: Models.AgentConversation.hasEarlier
                    enabled: !Models.AgentConversation.loading
                    onClicked:
                        Models.AgentConversation.loadEarlier()
                }
            }

            KBusyIndicator {
                Layout.alignment: Qt.AlignHCenter
                visible: Models.AgentConversation.loading
                    && Models.AgentConversation.count === 0
                running: visible
            }

            PlainLabel {
                objectName:
                    "panel.projectIntel.conversation.error"
                Accessible.id: objectName
                Layout.fillWidth: true
                visible: Models.AgentConversation.error.length > 0
                text: Models.AgentConversation.error
                color: KodosiTheme.danger
                wrapMode: Text.Wrap
            }

            ListView {
                objectName:
                    "panel.projectIntel.conversation.list"
                Accessible.id: objectName
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Agent transcript")
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 0
                model: Models.AgentConversation

                delegate: Rectangle {
                    required property string role
                    required property string content
                    required property string toolName
                    required property string timestamp

                    width: ListView.view.width
                    implicitHeight:
                        transcriptColumn.implicitHeight + 20
                    color: "transparent"
                    Accessible.role: Accessible.ListItem
                    Accessible.name: toolName.length > 0
                        ? role + ", " + toolName
                        : role
                    Accessible.description: content

                    ColumnLayout {
                        id: transcriptColumn
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 4

                        PlainLabel {
                            text: toolName.length > 0
                                ? role + " · " + toolName
                                : role
                            color: KodosiTheme.textSecondary
                            font.weight: Font.DemiBold
                        }

                        KReadOnlyText {
                            Layout.fillWidth: true
                            text: content
                            Accessible.name:
                                qsTr("Transcript entry")
                        }

                        PlainLabel {
                            visible: timestamp.length > 0
                            text: timestamp
                            color: KodosiTheme.textTertiary
                            font.pixelSize: 9
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
    }

    component MemoryPane: Item {
        objectName: "panel.projectIntel.memory"
        Accessible.id: objectName

        EmptyPane {
            objectId: "panel.projectIntel.memory.empty"
            visible: Models.ProjectIntelligence.memories.count === 0
            title: qsTr("No memory files for this project")
            detail: qsTr(
                "Add a project memory file to teach the agent.")
        }

        RowLayout {
            anchors.fill: parent
            visible: Models.ProjectIntelligence.memories.count > 0
            spacing: 0

            ListView {
                objectName: "panel.projectIntel.memory.list"
                Accessible.id: objectName
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Project memory files")
                Layout.preferredWidth: root.compact ? 184 : 230
                Layout.fillHeight: true
                clip: true
                model: Models.ProjectIntelligence.memories
                spacing: 2
                boundsBehavior: Flickable.StopAtBounds

                delegate: KButton {
                    required property string itemId
                    required property string title
                    required property string subtitle

                    width: ListView.view.width
                    implicitHeight: 50
                    checkable: true
                    checked:
                        Models.ProjectIntelligence
                            .selectedMemoryId === itemId
                    tonalSelection: true
                    variant: "quiet"
                    contentLeftAligned: true
                    objectName:
                        "panel.projectIntel.memory." + itemId
                    Accessible.id: objectName
                    Accessible.role: Accessible.ListItem
                    Accessible.name: title
                    Accessible.description: subtitle
                    Accessible.selected: checked
                    enabled: !Models.ProjectIntelligence.loading
                    onClicked:
                        Models.ProjectIntelligence.selectMemory(itemId)
                    Accessible.onPressAction:
                        Models.ProjectIntelligence.selectMemory(itemId)

                    contentItem: ColumnLayout {
                        spacing: 1

                        PlainLabel {
                            Layout.fillWidth: true
                            text: title
                            color: KodosiTheme.textPrimary
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }

                        PlainLabel {
                            Layout.fillWidth: true
                            text: subtitle
                            color: KodosiTheme.textSecondary
                            font.pixelSize: 9
                            elide: Text.ElideRight
                        }
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                color: KodosiTheme.seam
            }

            Item {
                objectName: "panel.projectIntel.memory.detail"
                Accessible.id: objectName
                Layout.fillWidth: true
                Layout.fillHeight: true

                EmptyPane {
                    objectId:
                        "panel.projectIntel.memory.detail.empty"
                    visible:
                        Models.ProjectIntelligence.selectedMemoryId
                            .length === 0
                    title: qsTr("Select a file to preview")
                    detail: ""
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 14
                    visible:
                        Models.ProjectIntelligence.selectedMemoryId
                            .length > 0
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true

                        PlainLabel {
                            Layout.fillWidth: true
                            text: Models.ProjectIntelligence
                                .selectedMemoryTitle
                            color: KodosiTheme.textPrimary
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }

                        KButton {
                            objectName:
                                "panel.projectIntel.memory.open"
                            Accessible.id: objectName
                            text: qsTr("Open")
                            variant: "quiet"
                            enabled:
                                !Models.ProjectIntelligence
                                    .memoryLoading
                            onClicked:
                                Models.ProjectIntelligence
                                    .openSelectedMemory()
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        KComboBox {
                            id: destination
                            objectName:
                                "panel.projectIntel.memory.destination"
                            Accessible.id: objectName
                            Layout.fillWidth: true
                            model: Models.ProjectIntelligence
                                .copyDestinations
                            textRole: "title"
                            valueRole: "itemId"
                            Accessible.name:
                                qsTr("Destination project")
                            onCurrentValueChanged:
                                root.copyDestinationId =
                                    currentValue || ""
                        }

                        KButton {
                            objectName:
                                "panel.projectIntel.memory.copy"
                            Accessible.id: objectName
                            text: qsTr("Copy to project…")
                            enabled:
                                !Models.ProjectIntelligence
                                    .memoryLoading
                                && Models.ProjectIntelligence
                                    .canCopySelectedMemoryTo(
                                        root.copyDestinationId)
                            onClicked: {
                                if (Models.ProjectIntelligence
                                        .copySelectedMemory(
                                            root.copyDestinationId))
                                    root.copyDestinationId = ""
                            }
                        }
                    }

                    KBusyIndicator {
                        objectName:
                            "panel.projectIntel.memory.loading"
                        Accessible.id: objectName
                        Layout.alignment: Qt.AlignHCenter
                        visible:
                            Models.ProjectIntelligence.memoryLoading
                        running: visible
                    }

                    PlainLabel {
                        objectName:
                            "panel.projectIntel.memory.error"
                        Accessible.id: objectName
                        Layout.fillWidth: true
                        visible:
                            Models.ProjectIntelligence.memoryError
                                .length > 0
                        text: Models.ProjectIntelligence.memoryError
                        color: KodosiTheme.danger
                        wrapMode: Text.Wrap
                    }

                    KButton {
                        objectName:
                            "panel.projectIntel.memory.retry"
                        Accessible.id: objectName
                        Layout.alignment: Qt.AlignHCenter
                        visible:
                            Models.ProjectIntelligence.memoryError
                                .length > 0
                            && !Models.ProjectIntelligence
                                .memoryLoading
                        text: qsTr("Retry")
                        onClicked:
                            Models.ProjectIntelligence.selectMemory(
                                Models.ProjectIntelligence
                                    .selectedMemoryId)
                    }

                    KScrollView {
                        id: memoryPreview
                        objectName:
                            "panel.projectIntel.memory.preview"
                        Accessible.id: objectName
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible:
                            !Models.ProjectIntelligence.memoryLoading
                            && Models.ProjectIntelligence.memoryError
                                .length === 0

                        KReadOnlyText {
                            width: memoryPreview.availableWidth
                            text:
                                Models.ProjectIntelligence.memoryContent
                            Accessible.name:
                                qsTr("Memory file preview")
                        }
                    }
                }
            }
        }
    }

    component McpPane: Item {
        objectName: "panel.projectIntel.servers"
        Accessible.id: objectName

        EmptyPane {
            objectId: "panel.projectIntel.servers.gate"
            visible: !root.activeSource
            title:
                qsTr("MCP configuration requires an active project")
            detail: qsTr(
                "Archived project sources do not expose project-file actions.")
        }

        EmptyPane {
            objectId: "panel.projectIntel.servers.empty"
            visible: root.activeSource
                && Models.ProjectIntelligence.customizations.count
                    === 0
            title: qsTr("No MCP servers configured")
            detail: qsTr(
                "Add MCP server entries to this project or its user configuration.")
        }

        ListView {
            objectName: "panel.projectIntel.servers.list"
            Accessible.id: objectName
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Project MCP servers")
            anchors.fill: parent
            anchors.margins: 12
            visible: root.activeSource
                && Models.ProjectIntelligence.customizations.count > 0
            clip: true
            spacing: 0
            model: Models.ProjectIntelligence.customizations
            boundsBehavior: Flickable.StopAtBounds

            delegate: Rectangle {
                required property string itemId
                required property string title
                required property string subtitle
                required property string kind
                required property string status
                required property string metadata

                objectName:
                    "panel.projectIntel.server." + itemId
                Accessible.id: objectName
                Accessible.role: Accessible.ListItem
                Accessible.name: title
                Accessible.description: [
                    subtitle,
                    status,
                    metadata
                ].filter(function(value) {
                    return value && value.length > 0
                }).join(". ")
                width: ListView.view.width
                implicitHeight: serverRow.implicitHeight + 18
                color: "transparent"

                RowLayout {
                    id: serverRow
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 10

                    KIcon {
                        Layout.preferredWidth: 16
                        Layout.preferredHeight: 16
                        name: "server"
                        color: KodosiTheme.textSecondary
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        PlainLabel {
                            Layout.fillWidth: true
                            text: title
                            color: KodosiTheme.textPrimary
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }

                        PlainLabel {
                            Layout.fillWidth: true
                            text: [
                                metadata,
                                subtitle
                            ].filter(function(value) {
                                return value
                                    && value.length > 0
                            }).join(" · ")
                            color: KodosiTheme.textSecondary
                            font.pixelSize: 10
                            wrapMode: Text.Wrap
                        }
                    }

                    PlainLabel {
                        text: status
                        color: status === "loaded"
                            ? KodosiTheme.success
                            : KodosiTheme.warning
                        font.pixelSize: 10
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

    component AgentsPane: Item {
        objectName: "panel.projectIntel.agents"
        Accessible.id: objectName

        EmptyPane {
            objectId: "panel.projectIntel.agents.gate"
            visible: !root.activeSource
            title: qsTr(
                "Agents browser is only available for active projects.")
            detail: ""
        }

        EmptyPane {
            objectId: "panel.projectIntel.agents.empty"
            visible: root.activeSource
                && Models.ProjectIntelligence.agents.count === 0
            title: qsTr("No custom agents defined")
            detail: qsTr(
                "Define a project Custom Agent to make it available here.")
        }

        RowLayout {
            anchors.fill: parent
            visible: root.activeSource
                && Models.ProjectIntelligence.agents.count > 0
            spacing: 0

            ListView {
                objectName: "panel.projectIntel.agents.list"
                Accessible.id: objectName
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Project Custom Agents")
                Layout.preferredWidth: root.compact ? 210 : 270
                Layout.fillHeight: true
                clip: true
                model: Models.ProjectIntelligence.agents
                spacing: 2
                boundsBehavior: Flickable.StopAtBounds

                delegate: KButton {
                    required property string itemId
                    required property string title
                    required property string subtitle
                    required property string agent
                    required property int numberA

                    width: ListView.view.width
                    implicitHeight: 64
                    checkable: true
                    checked:
                        Models.ProjectIntelligence.selectedAgentId
                            === itemId
                    tonalSelection: true
                    variant: "quiet"
                    contentLeftAligned: true
                    objectName:
                        "panel.projectIntel.agent." + itemId
                    Accessible.id: objectName
                    Accessible.role: Accessible.ListItem
                    Accessible.name: title
                    Accessible.description: numberA > 0
                        ? qsTr("%1 parse errors").arg(numberA)
                        : subtitle
                    Accessible.selected: checked
                    enabled: !Models.ProjectIntelligence.loading
                    onClicked:
                        Models.ProjectIntelligence.selectAgent(itemId)
                    Accessible.onPressAction:
                        Models.ProjectIntelligence.selectAgent(itemId)

                    contentItem: ColumnLayout {
                        spacing: 2

                        RowLayout {
                            Layout.fillWidth: true

                            PlainLabel {
                                Layout.fillWidth: true
                                text: title
                                color: KodosiTheme.textPrimary
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }

                            PlainLabel {
                                visible: numberA > 0
                                text: qsTr("%1 errors").arg(numberA)
                                color: KodosiTheme.danger
                                font.pixelSize: 9
                            }
                        }

                        PlainLabel {
                            Layout.fillWidth: true
                            text: subtitle
                            color: KodosiTheme.textSecondary
                            font.pixelSize: 10
                            elide: Text.ElideRight
                        }

                        PlainLabel {
                            visible: agent.length > 0
                            text: agent
                            color: KodosiTheme.textTertiary
                            font.pixelSize: 9
                            elide: Text.ElideRight
                        }
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                color: KodosiTheme.seam
            }

            Item {
                objectName: "panel.projectIntel.agents.detail"
                Accessible.id: objectName
                Layout.fillWidth: true
                Layout.fillHeight: true

                EmptyPane {
                    objectId:
                        "panel.projectIntel.agents.detail.empty"
                    visible:
                        Models.ProjectIntelligence.selectedAgentId
                            .length === 0
                    title: qsTr("Select an agent to inspect")
                    detail: ""
                }

                ColumnLayout {
                    objectName:
                        "panel.projectIntel.agents.detail.loading"
                    Accessible.id: objectName
                    anchors.centerIn: parent
                    visible:
                        Models.ProjectIntelligence.selectedAgentId
                            .length > 0
                        && Models.ProjectIntelligence
                            .agentDetailLoading
                    spacing: 8

                    KBusyIndicator {
                        Layout.alignment: Qt.AlignHCenter
                        running: parent.visible
                    }

                    PlainLabel {
                        text: qsTr("Loading Custom Agent")
                        color: KodosiTheme.textSecondary
                    }
                }

                EmptyPane {
                    objectId:
                        "panel.projectIntel.agents.detail.error"
                    visible:
                        Models.ProjectIntelligence.selectedAgentId
                            .length > 0
                        && !Models.ProjectIntelligence
                            .agentDetailLoading
                        && Models.ProjectIntelligence
                            .agentDetailError.length > 0
                    title: qsTr("Custom Agent is unavailable")
                    detail:
                        Models.ProjectIntelligence.agentDetailError

                    KButton {
                        objectName:
                            "panel.projectIntel.agent.retry"
                        Accessible.id: objectName
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("Retry")
                        onClicked:
                            Models.ProjectIntelligence.selectAgent(
                                Models.ProjectIntelligence
                                    .selectedAgentId)
                    }
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 14
                    visible:
                        Models.ProjectIntelligence.selectedAgentId
                            .length > 0
                        && !Models.ProjectIntelligence
                            .agentDetailLoading
                        && Models.ProjectIntelligence
                            .agentDetailError.length === 0
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true

                        PlainLabel {
                            Layout.fillWidth: true
                            text:
                                Models.ProjectIntelligence
                                    .selectedAgentName
                            color: KodosiTheme.textPrimary
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }

                        KButton {
                            objectName:
                                "panel.projectIntel.agent.open"
                            Accessible.id: objectName
                            text: qsTr("Open")
                            variant: "quiet"
                            onClicked:
                                Models.ProjectIntelligence
                                    .openSelectedAgent()
                        }
                    }

                    KScrollView {
                        id: agentDetailScroll
                        objectName:
                            "panel.projectIntel.agents.detail.scroll"
                        Accessible.id: objectName
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        ColumnLayout {
                            width: agentDetailScroll.availableWidth
                            spacing: 12

                            PlainLabel {
                                Layout.fillWidth: true
                                visible:
                                    Models.ProjectIntelligence
                                        .selectedAgentDescription
                                        .length > 0
                                text:
                                    Models.ProjectIntelligence
                                        .selectedAgentDescription
                                color: KodosiTheme.textSecondary
                                wrapMode: Text.Wrap
                            }

                            PlainLabel {
                                Layout.fillWidth: true
                                visible:
                                    Models.ProjectIntelligence
                                        .selectedAgentModel.length > 0
                                    || Models.ProjectIntelligence
                                        .selectedAgentTools.length > 0
                                text: [
                                    Models.ProjectIntelligence
                                        .selectedAgentModel.length > 0
                                        ? qsTr("Model: %1").arg(
                                              Models.ProjectIntelligence
                                                  .selectedAgentModel)
                                        : "",
                                    Models.ProjectIntelligence
                                        .selectedAgentTools.length > 0
                                        ? qsTr("Tools: %1").arg(
                                              Models.ProjectIntelligence
                                                  .selectedAgentTools
                                                  .join(", "))
                                        : ""
                                ].filter(function(value) {
                                    return value.length > 0
                                }).join(" · ")
                                color: KodosiTheme.textSecondary
                                wrapMode: Text.Wrap
                            }

                            PlainLabel {
                                objectName:
                                    "panel.projectIntel.agents.detail.parseErrors.label"
                                Accessible.id: objectName
                                Layout.fillWidth: true
                                text: qsTr("Parse errors")
                                color:
                                    Models.ProjectIntelligence
                                        .selectedAgentErrorCount > 0
                                    ? KodosiTheme.danger
                                    : KodosiTheme.textSecondary
                                font.weight: Font.DemiBold
                            }

                            PlainLabel {
                                objectName:
                                    "panel.projectIntel.agents.detail.parseErrors"
                                Accessible.id: objectName
                                Layout.fillWidth: true
                                text:
                                    Models.ProjectIntelligence
                                        .agentParseErrors.length > 0
                                    ? Models.ProjectIntelligence
                                          .agentParseErrors.join("\n")
                                    : qsTr("None")
                                color:
                                    Models.ProjectIntelligence
                                        .agentParseErrors.length > 0
                                    ? KodosiTheme.danger
                                    : KodosiTheme.textSecondary
                                wrapMode: Text.Wrap
                            }

                            PlainLabel {
                                objectName:
                                    "panel.projectIntel.agents.detail.frontmatter.label"
                                Accessible.id: objectName
                                Layout.fillWidth: true
                                text: qsTr("Frontmatter")
                                color: KodosiTheme.textSecondary
                                font.weight: Font.DemiBold
                            }

                            KReadOnlyText {
                                objectName:
                                    "panel.projectIntel.agents.detail.frontmatter"
                                Accessible.id: objectName
                                Layout.fillWidth: true
                                text:
                                    Models.ProjectIntelligence
                                        .agentFrontmatter.length > 0
                                    ? Models.ProjectIntelligence
                                          .agentFrontmatter
                                    : qsTr("Not provided")
                                Accessible.name:
                                    qsTr("Custom Agent frontmatter")
                            }

                            PlainLabel {
                                objectName:
                                    "panel.projectIntel.agents.detail.prompt.label"
                                Accessible.id: objectName
                                Layout.fillWidth: true
                                text: qsTr("System prompt")
                                color: KodosiTheme.textSecondary
                                font.weight: Font.DemiBold
                            }

                            KReadOnlyText {
                                objectName:
                                    "panel.projectIntel.agents.detail.prompt"
                                Accessible.id: objectName
                                Layout.fillWidth: true
                                text:
                                    Models.ProjectIntelligence
                                        .agentPrompt.length > 0
                                    ? Models.ProjectIntelligence
                                          .agentPrompt
                                    : qsTr("Not provided")
                                Accessible.name:
                                    qsTr("Custom Agent system prompt")
                            }
                        }
                    }
                }
            }
        }
    }
}
