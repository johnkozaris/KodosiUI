import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kodosi.Models 1.0 as Models

ApplicationWindow {
    id: window
    objectName: "window.main"

    property string selectedSessionName
    property string selectedProject
    property string selectedStatus
    property string selectedMode
    readonly property int themeMotionFast: KodosiTheme.motionFast
    readonly property int themeMotionNormal: KodosiTheme.motionNormal
    readonly property color themeCanvas: KodosiTheme.canvas
    readonly property color themeSurface: KodosiTheme.surface
    readonly property color themeTextPrimary: KodosiTheme.textPrimary
    readonly property color themeTextSecondary: KodosiTheme.textSecondary
    readonly property color themeAccent: KodosiTheme.accent
    readonly property color themeAccentForeground:
        KodosiTheme.accentForeground
    readonly property string shortcutSelectedSessionId:
        Models.DesktopState.selectedSessionId
    readonly property var selectedSessionPresentation: {
        const revision = Models.SessionActions.availabilityRevision
        return revision >= 0
            ? Models.Sessions.presentationForSession(
                  shortcutSelectedSessionId)
            : ({})
    }
    readonly property bool selectedSessionAvailable:
        shortcutSelectedSessionId.length > 0
        && Models.SessionActions.availabilityRevision >= 0
        && Models.Sessions.authorityState === Models.Sessions.Loaded
        && selectedSessionPresentation.sessionId
            === shortcutSelectedSessionId
    readonly property bool canInspectSelectedSession:
        selectedSessionAvailable
    readonly property bool canShareSelectedSession:
        selectedSessionAvailable
        && Models.SessionShareScope.stateRevision >= 0
        && (Models.SessionShareScope.canChange(shortcutSelectedSessionId)
            || Models.SessionShareScope.phase(shortcutSelectedSessionId)
                !== "idle")
    readonly property bool canCloseSelectedSession:
        selectedSessionAvailable
        && Models.SessionActions.canClose(shortcutSelectedSessionId)
    readonly property bool blockingOverlayOpen:
        settingsDrawer.opened
        || utilityMenu.opened
        || keyboardShortcutsOverlay.opened
        || attentionPanel.opened
        || agentIntelDrawer.opened
        || projectIntelModal.opened
        || diagnosticsDrawer.opened
        || closeSessionConfirmation.opened
        || sessionSidebar.modalOpen
        || authOverlay.visible
    readonly property bool shortcutContextAvailable:
        !blockingOverlayOpen && !textInputOwnsShortcuts()

    function textInputOwnsShortcuts() {
        const item = window.activeFocusItem
        return item instanceof TextInput || item instanceof TextEdit
    }

    function synchronizeSelectedSessionMetadata() {
        const selectedSessionId = Models.DesktopState.selectedSessionId
        if (selectedSessionId.length === 0) {
            selectedSessionName = ""
            selectedProject = ""
            selectedStatus = ""
            selectedMode = ""
            return
        }
        if (Models.Sessions.authorityState !== Models.Sessions.Loaded)
            return
        const session =
            Models.Sessions.presentationForSession(selectedSessionId)
        if (session.sessionId !== selectedSessionId) {
            selectedSessionName = ""
            selectedProject = ""
            selectedStatus = ""
            selectedMode = ""
            return
        }
        selectedSessionName = session.name
        selectedProject = session.project
        selectedStatus = session.status
        selectedMode = session.mode
    }

    function requestSessionSelection(sessionId, openRemote) {
        if (sessionId.length === 0)
            return false
        const session = Models.Sessions.presentationForSession(sessionId)
        if (session.sessionId !== sessionId
                || !Models.DesktopState.selectSession(sessionId))
            return false
        synchronizeSelectedSessionMetadata()
        if (openRemote && Models.SessionActions.canOpenRemote(sessionId))
            Models.SessionActions.openRemote(sessionId)
        return true
    }

    function activateSession(sessionId) {
        return activateSessionWithRemote(sessionId, false)
    }

    function activateSessionWithRemote(sessionId, openRemote) {
        if (!requestSessionSelection(sessionId, openRemote === true))
            return false
        Models.DesktopState.activeView = 0
        return true
    }

    function openAgentIntel(sessionId, approvalIdentityToken) {
        return openAgentIntelWithRemote(
            sessionId,
            approvalIdentityToken,
            false)
    }

    function openAgentIntelWithRemote(
            sessionId,
            approvalIdentityToken,
            openRemote) {
        const session = Models.Sessions.presentationForSession(sessionId)
        if (!session.sessionId)
            return false
        if (!activateSessionWithRemote(sessionId, openRemote))
            return false
        attentionPanel.close()
        settingsDrawer.close()
        diagnosticsDrawer.close()
        agentIntelDrawer.openForSession(
            sessionId,
            session.name,
            approvalIdentityToken || "",
            false)
        return true
    }

    function closeDeepLinkOverlays() {
        utilityMenu.close()
        keyboardShortcutsOverlay.close()
        attentionPanel.close()
        settingsDrawer.close()
        agentIntelDrawer.close()
        projectIntelModal.close()
        diagnosticsDrawer.close()
        Models.SessionActions.cancelCloseConfirmation()
        sessionSidebar.closeConflictingOverlays()
    }

    function openDeepLinkedSession(sessionId) {
        closeDeepLinkOverlays()
        const opened = activateSessionWithRemote(sessionId, true)
        if (!opened)
            Models.DeepLinks.reportNavigationResult(false)
        return opened
    }

    function openDeepLinkedApproval(sessionId, identityToken) {
        closeDeepLinkOverlays()
        const opened = openAgentIntelWithRemote(
            sessionId,
            identityToken,
            true)
        if (!opened)
            Models.DeepLinks.reportNavigationResult(false)
        return opened
    }

    function openMissingDeepLinkedApproval(sessionId) {
        closeDeepLinkOverlays()
        const session = Models.Sessions.presentationForSession(sessionId)
        const opened = session.sessionId === sessionId
            && activateSessionWithRemote(sessionId, true)
        if (opened) {
            agentIntelDrawer.openForSession(
                sessionId,
                session.name,
                "",
                true)
        }
        if (!opened)
            Models.DeepLinks.reportNavigationResult(false)
        return opened
    }

    function deepLinkStatusText(code) {
        switch (code) {
        case "tooLong":
            return qsTr("The link is too long to open safely.")
        case "malformedEncoding":
            return qsTr("The link contains malformed text encoding.")
        case "unsafeText":
            return qsTr("The link contains unsafe text.")
        case "unsupportedParameters":
            return qsTr("The link contains unsupported parameters.")
        case "queueFull":
            return qsTr("Too many links are waiting to open. Try the link again.")
        case "waitingAccount":
            return qsTr("Waiting for the active account before opening the link…")
        case "waitingSessions":
            return qsTr("Waiting for the authoritative session list…")
        case "accountChanged":
            return qsTr("The link expired because the active account changed.")
        case "runtimeRestarted":
            return qsTr("The link expired because the runtime restarted.")
        case "expired":
            return qsTr("The link expired before its authoritative data became available.")
        case "sessionsUnavailable":
            return qsTr("Sessions are unavailable, so the link could not be opened.")
        case "sessionMissing":
            return qsTr("The linked session is no longer available.")
        case "sessionRestarted":
            return qsTr("The link expired because the session restarted.")
        case "waitingApprovals":
            return qsTr("Waiting for the authoritative approval list…")
        case "approvalMissing":
            return qsTr("This approval is no longer pending.")
        case "approvalsUnavailable":
            return qsTr("Pending approvals are unavailable. The linked session was opened instead.")
        case "navigationFailed":
            return qsTr("The linked session could not be staged. Close another staged session and try again.")
        case "invalid":
            return qsTr("This Kodosi link is invalid or unsupported.")
        default:
            return ""
        }
    }

    function openProjectIntelForSession(sessionId) {
        if (sessionId.length === 0)
            return false
        attentionPanel.close()
        settingsDrawer.close()
        diagnosticsDrawer.close()
        agentIntelDrawer.close()
        projectIntelModal.openForSession(sessionId)
        return true
    }

    function openProjectIntelSource(sourceId) {
        attentionPanel.close()
        settingsDrawer.close()
        diagnosticsDrawer.close()
        agentIntelDrawer.close()
        projectIntelModal.openForSource(sourceId)
        return true
    }

    function openAgentSettings() {
        attentionPanel.close()
        agentIntelDrawer.close()
        diagnosticsDrawer.close()
        settingsDrawer.openAgents()
        return true
    }

    function reviewApproval(identityToken, sessionId) {
        if (Models.PendingPermissions.rowForIdentityToken(identityToken) < 0)
            return false
        return openAgentIntel(sessionId, identityToken)
    }

    function toggleAttention() {
        agentIntelDrawer.close()
        settingsDrawer.close()
        diagnosticsDrawer.close()
        if (attentionPanel.opened)
            attentionPanel.close()
        else
            attentionPanel.open()
    }

    function showDevices() {
        attentionPanel.close()
        agentIntelDrawer.close()
        settingsDrawer.close()
        diagnosticsDrawer.close()
        Models.DesktopState.activeView = 2
    }

    function toggleDiagnostics() {
        if (authOverlay.visible && !diagnosticsDrawer.opened)
            return
        attentionPanel.close()
        agentIntelDrawer.close()
        settingsDrawer.close()
        if (diagnosticsDrawer.opened)
            diagnosticsDrawer.close()
        else
            diagnosticsDrawer.open()
    }

    function openSettings() {
        utilityMenu.close()
        attentionPanel.close()
        agentIntelDrawer.close()
        diagnosticsDrawer.close()
        settingsDrawer.open()
    }

    function openAccountSettings() {
        utilityMenu.close()
        attentionPanel.close()
        agentIntelDrawer.close()
        diagnosticsDrawer.close()
        settingsDrawer.openAccount()
    }

    function toggleKeyboardShortcuts() {
        if (keyboardShortcutsOverlay.opened) {
            keyboardShortcutsOverlay.close()
            return
        }
        if (blockingOverlayOpen || textInputOwnsShortcuts())
            return
        keyboardShortcutsOverlay.open()
    }

    function createDefaultSession() {
        if (!shortcutContextAvailable || Models.SessionActions.creating)
            return false
        Models.DesktopState.activeView = 0
        return Models.SessionActions.createDefault()
    }

    function openSelectedAgentIntel() {
        if (!shortcutContextAvailable || !canInspectSelectedSession)
            return false
        return openAgentIntel(shortcutSelectedSessionId, "")
    }

    function shareSelectedSession() {
        if (!shortcutContextAvailable || !canShareSelectedSession)
            return false
        Models.DesktopState.activeView = 0
        Models.DesktopState.sidebarOpen = true
        sessionSidebar.openShareByIdentity(
            shortcutSelectedSessionId,
            selectedSessionPresentation.name)
        return true
    }

    function requestCloseSelectedSession() {
        if (!shortcutContextAvailable || !canCloseSelectedSession)
            return false
        return Models.SessionActions.requestCloseConfirmation(
            shortcutSelectedSessionId)
    }

    function toggleUtilityMenu() {
        attentionPanel.close()
        agentIntelDrawer.close()
        settingsDrawer.close()
        diagnosticsDrawer.close()
        if (utilityMenu.opened)
            utilityMenu.close()
        else
            utilityMenu.openAt(utilityButton)
    }

    function setShellAccessibilityIgnored(item, ignored) {
        if (!item)
            return
        item.Accessible.ignored = ignored
        const children = item.children || []
        for (let index = 0; index < children.length; ++index)
            setShellAccessibilityIgnored(children[index], ignored)
    }

    function refreshShellAccessibility() {
        setShellAccessibilityIgnored(shellContent, false)
        if (blockingOverlayOpen)
            setShellAccessibilityIgnored(shellContent, true)
        else if (!Models.DesktopState.sidebarOpen)
            setShellAccessibilityIgnored(sessionSidebar, true)
    }

    onBlockingOverlayOpenChanged:
        Qt.callLater(window.refreshShellAccessibility)

    visible: false
    color: KodosiTheme.canvas
    title: qsTr("Kodosi")

    ColumnLayout {
        id: shellContent
        objectName: "window.main"
        Accessible.id: objectName
        anchors.fill: parent
        spacing: 0
        enabled: !window.blockingOverlayOpen
        Accessible.name: qsTr("Kodosi mission control")

        Rectangle {
            objectName: "app.header"
            Accessible.id: objectName
            Layout.fillWidth: true
            Layout.preferredHeight: KodosiTheme.headerHeight
            color: KodosiTheme.surface

            RowLayout {
                anchors.fill: parent
                spacing: 0

                RowLayout {
                    Layout.minimumWidth: Models.DesktopState.activeView === 0
                        && Models.DesktopState.sidebarOpen
                        ? KodosiTheme.sidebarWidth
                        : 180
                    Layout.maximumWidth: Layout.minimumWidth
                    Layout.fillHeight: true
                    Layout.leftMargin: KodosiTheme.spacing5
                    Layout.rightMargin: KodosiTheme.spacing5
                    spacing: KodosiTheme.spacing3

                    KIconButton {
                        objectName: "header.sidebar.toggle"
                        Accessible.id: objectName
                        visible: Models.DesktopState.activeView === 0
                        glyph: "sidebar"
                        checkable: true
                        checked: Models.DesktopState.sidebarOpen
                        Accessible.name: qsTr("Toggle sidebar")
                        Accessible.description: Models.DesktopState.sidebarOpen
                            ? qsTr("Collapse My Agents sidebar")
                            : qsTr("Expand My Agents sidebar")
                        onClicked: Models.DesktopState.sidebarOpen =
                            !Models.DesktopState.sidebarOpen
                    }

                    Image {
                        objectName: "header.logo"
                        Accessible.id: objectName
                        Accessible.role: Accessible.Graphic
                        Accessible.name: qsTr("Kodosi")
                        Layout.preferredWidth: 28
                        Layout.preferredHeight: 28
                        source: "assets/kodosi-logo-dark.png"
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                        mipmap: true
                    }

                    Item { Layout.fillWidth: true }
                }

                Rectangle {
                    visible: Models.DesktopState.activeView === 0
                        && Models.DesktopState.sidebarOpen
                    Layout.preferredWidth: 1
                    Layout.fillHeight: true
                    color: KodosiTheme.seam
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    KSegmentedBar {
                        anchors.centerIn: parent
                        width: 304
                        height: 42

                        NavTab {
                            Layout.preferredWidth: 149
                            accessibleId: "my-agents"
                            iconName: "command"
                            title: qsTr("My Agents")
                            caption: qsTr("Workbench")
                            selected: Models.DesktopState.activeView === 0
                            badgeCount: Models.Attention.count
                            onClicked: Models.DesktopState.activeView = 0
                        }

                        NavTab {
                            Layout.preferredWidth: 149
                            accessibleId: "missions"
                            iconName: "people"
                            title: qsTr("Missions")
                            caption: qsTr("Collaborate")
                            selected: Models.DesktopState.activeView === 1
                            badgeCount: Models.Missions.invitations.incomingCount
                            onClicked: Models.DesktopState.activeView = 1
                        }
                    }
                }

                RowLayout {
                    Layout.minimumWidth: window.width < 1000 ? 218 : 304
                    Layout.maximumWidth: Layout.minimumWidth
                    Layout.fillHeight: true
                    Layout.rightMargin: KodosiTheme.spacing5
                    spacing: KodosiTheme.spacing2

                    StatusPill {
                        visible: window.width >= 1100
                        text: Models.AuthState.signedIn
                            ? qsTr("Connected")
                            : qsTr("Local")
                        tone: Models.AuthState.signedIn
                            ? KodosiTheme.success
                            : KodosiTheme.warning
                    }

                    KIconButton {
                        id: attentionButton
                        objectName: "header.attention"
                        Accessible.id: objectName
                        glyph: "bell"
                        Accessible.name: Models.Attention.count > 0
                            ? qsTr("Open Attention, %1 items").arg(
                                Models.Attention.count)
                            : qsTr("Open Attention")
                        onClicked: window.toggleAttention()

                        Rectangle {
                            visible: Models.Attention.count > 0
                            anchors.right: parent.right
                            anchors.top: parent.top
                            width: 14
                            height: 14
                            radius: 7
                            color: KodosiTheme.accent
                            border.width: 2
                            border.color: KodosiTheme.surface
                        }
                    }

                    KIconButton {
                        objectName: "header.tab.devices"
                        Accessible.id: objectName
                        glyph: "devices"
                        Accessible.name: qsTr("Open Account and Devices")
                        visible: Models.AuthState.signedIn
                        onClicked: window.showDevices()
                    }

                    KButton {
                        objectName: "header.auth.signIn"
                        Accessible.id: objectName
                        visible: !Models.AuthState.signedIn
                        variant: "secondary"
                        iconName: "login"
                        text: window.width < 1000
                            ? ""
                            : Models.AuthActions.busy
                              ? qsTr("Signing in")
                              : qsTr("Sign in")
                        compact: window.width < 1000
                        enabled: !Models.AuthActions.busy
                        Accessible.name: qsTr("Sign in to Kodosi")
                        onClicked: Models.AuthActions.beginSignIn()
                    }

                    KIconButton {
                        id: settingsButton
                        objectName: "header.settings"
                        Accessible.id: objectName
                        glyph: "settings"
                        Accessible.name: qsTr("Open settings")
                        onClicked: window.openSettings()
                    }

                    KButton {
                        id: utilityButton
                        objectName: "header.utility.menu"
                        Accessible.id: objectName
                        variant: "secondary"
                        iconName: "account"
                        text: window.width < 1000
                            ? ""
                            : Models.AuthState.signedIn
                              ? qsTr("Account")
                              : qsTr("Kodosi")
                        Accessible.name: Models.AuthState.signedIn
                            ? qsTr("Account menu")
                            : qsTr("App menu")
                        enabled: !Models.AuthActions.busy
                        compact: window.width < 1000
                        onClicked: window.toggleUtilityMenu()
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

        AuthErrorBanner {
            Layout.fillWidth: true
            onOpenAccountRequested:
                Qt.callLater(window.openAccountSettings)
        }

        DesktopFileErrorBanner {
            Layout.fillWidth: true
        }

        RuntimeHealthBanner {
            Layout.fillWidth: true
            onOpenDiagnosticsRequested: window.toggleDiagnostics()
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            objectName: "shell.views"
            Accessible.id: objectName
            currentIndex: Models.DesktopState.activeView

            RowLayout {
                spacing: 0

                SessionSidebar {
                    id: sessionSidebar
                    selectedSessionId:
                        Models.DesktopState.selectedSessionId
                    visible: Models.DesktopState.sidebarOpen
                    Layout.fillHeight: true
                    Layout.preferredWidth: KodosiTheme.sidebarWidth
                    onSessionSelectionRequested: (sessionId, openRemote) =>
                        window.requestSessionSelection(sessionId, openRemote)
                    onProjectIntelligenceRequested: sessionId =>
                        window.openProjectIntelForSession(sessionId)
                }

                Rectangle {
                    visible: Models.DesktopState.sidebarOpen
                    Layout.preferredWidth: 1
                    Layout.fillHeight: true
                    color: KodosiTheme.seam
                }

                TerminalStage {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    sidebarOpen: Models.DesktopState.sidebarOpen
                    interactionEnabled: !window.blockingOverlayOpen
                        && Models.DesktopState.activeView === 0
                    onNewSessionRequested: {
                        Models.DesktopState.sidebarOpen = true
                        sessionSidebar.createOpen = true
                    }
                    onShowSidebarRequested:
                        Models.DesktopState.sidebarOpen = true
                    onInspectSessionRequested: (sessionId, sessionName) => {
                        window.openAgentIntel(sessionId, "")
                    }
                    onShareSessionRequested: (sessionId, sessionName) => {
                        Models.DesktopState.sidebarOpen = true
                        sessionSidebar.openShareByIdentity(
                            sessionId,
                            sessionName)
                    }
                }
            }

            PeopleView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                onSignInRequested: Models.AuthActions.beginSignIn()
            }

            DevicesView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                onSignInRequested: Models.AuthActions.beginSignIn()
            }
        }
    }

    Timer {
        interval: 50
        repeat: true
        running: window.blockingOverlayOpen
            || !Models.DesktopState.sidebarOpen
        onTriggered: {
            if (window.blockingOverlayOpen)
                window.setShellAccessibilityIgnored(shellContent, true)
            else
                window.setShellAccessibilityIgnored(sessionSidebar, true)
        }
    }

    AuthOverlay {
        id: authOverlay
        accountRecoverySurfaceOpen:
            settingsDrawer.opened
            && settingsDrawer.selectedCategory === "account"
            && settingsDrawer.identityRecoveryActive
        onAccountRecoveryRequested:
            Qt.callLater(settingsDrawer.openAccount)
        onVisibleChanged: {
            if (!visible)
                return
            attentionPanel.close()
            settingsDrawer.close()
            agentIntelDrawer.close()
            diagnosticsDrawer.close()
        }
    }

    Rectangle {
        id: deepLinkStatus
        objectName: "deepLink.status"
        Accessible.id: objectName
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: KodosiTheme.spacing3
        implicitHeight: visible
            ? Math.max(44, deepLinkStatusRow.implicitHeight
                + KodosiTheme.spacing4)
            : 0
        height: implicitHeight
        visible: Models.DeepLinks.statusCode.length > 0
        z: 10000
        radius: KodosiTheme.radiusMedium
        color: KodosiTheme.surfaceElevated
        border.width: 1
        border.color: KodosiTheme.seam
        Accessible.role: Accessible.AlertMessage
        Accessible.name: deepLinkStatusLabel.text
        Accessible.ignored: !visible

        RowLayout {
            id: deepLinkStatusRow
            anchors.fill: parent
            anchors.leftMargin: KodosiTheme.spacing5
            anchors.rightMargin: KodosiTheme.spacing5
            spacing: KodosiTheme.spacing3

            PlainLabel {
                id: deepLinkStatusLabel
                objectName: "deepLink.status.label"
                Accessible.id: objectName
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                verticalAlignment: Text.AlignVCenter
                text: window.deepLinkStatusText(
                    Models.DeepLinks.statusCode)
                color: KodosiTheme.textPrimary
                wrapMode: Text.Wrap
            }

            KIconButton {
                objectName: "deepLink.status.dismiss"
                Accessible.id: objectName
                glyph: "close"
                Accessible.name: qsTr("Dismiss link status")
                onClicked: Models.DeepLinks.clearStatus()
            }
        }
    }

    Shortcut {
        objectName: "shortcut.settings"
        sequence: "Ctrl+,"
        context: Qt.ApplicationShortcut
        enabled: window.shortcutContextAvailable
        onActivated: window.openSettings()
    }

    Shortcut {
        objectName: "shortcut.session.new"
        sequence: "Ctrl+S"
        context: Qt.ApplicationShortcut
        enabled: window.shortcutContextAvailable
            && !Models.SessionActions.creating
        onActivated: window.createDefaultSession()
    }

    Shortcut {
        objectName: "shortcut.session.intelligence"
        sequence: "Ctrl+I"
        context: Qt.ApplicationShortcut
        enabled: window.shortcutContextAvailable
            && window.canInspectSelectedSession
        onActivated: window.openSelectedAgentIntel()
    }

    Shortcut {
        objectName: "shortcut.session.share"
        sequence: "Ctrl+Shift+S"
        context: Qt.ApplicationShortcut
        enabled: window.shortcutContextAvailable
            && window.canShareSelectedSession
        onActivated: window.shareSelectedSession()
    }

    Shortcut {
        objectName: "shortcut.session.close"
        sequence: "Ctrl+Shift+W"
        context: Qt.ApplicationShortcut
        enabled: window.shortcutContextAvailable
            && window.canCloseSelectedSession
        onActivated: window.requestCloseSelectedSession()
    }

    Shortcut {
        objectName: "shortcut.sidebar"
        sequence: "Ctrl+B"
        context: Qt.ApplicationShortcut
        enabled: window.shortcutContextAvailable
            && Models.DesktopState.activeView === 0
        onActivated: Models.DesktopState.sidebarOpen =
            !Models.DesktopState.sidebarOpen
    }

    Shortcut {
        objectName: "shortcut.keyboardOverlay"
        sequence: "Ctrl+Shift+/"
        context: Qt.ApplicationShortcut
        enabled: keyboardShortcutsOverlay.opened
            || window.shortcutContextAvailable
        onActivated: window.toggleKeyboardShortcuts()
    }

    Shortcut {
        objectName: "shortcut.diagnostics"
        sequence: "Ctrl+Shift+D"
        context: Qt.ApplicationShortcut
        enabled: diagnosticsDrawer.opened
            || window.shortcutContextAvailable
        onActivated: window.toggleDiagnostics()
    }

    Shortcut {
        objectName: "shortcut.stage.focus"
        sequence: "Ctrl+Shift+Enter"
        context: Qt.ApplicationShortcut
        enabled: window.shortcutContextAvailable
            && Models.DesktopState.activeView === 0
            && Models.DesktopState.selectedSessionId.length > 0
        onActivated:
            Models.DesktopState.toggleFocusForSelectedSession()
    }

    Shortcut {
        objectName: "shortcut.stage.next"
        sequences: ["Ctrl+Alt+Right", "Ctrl+Alt+Down"]
        context: Qt.ApplicationShortcut
        enabled: window.shortcutContextAvailable
            && Models.DesktopState.activeView === 0
        onActivated: Models.DesktopState.selectAdjacentSession(1)
    }

    Shortcut {
        objectName: "shortcut.stage.previous"
        sequences: ["Ctrl+Alt+Left", "Ctrl+Alt+Up"]
        context: Qt.ApplicationShortcut
        enabled: window.shortcutContextAvailable
            && Models.DesktopState.activeView === 0
        onActivated: Models.DesktopState.selectAdjacentSession(-1)
    }

    Shortcut {
        objectName: "shortcut.stage.exitFocus"
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        enabled: window.shortcutContextAvailable
            && Models.DesktopState.stageLayoutMode
                === Models.DesktopState.Focus
        onActivated: Models.DesktopState.exitFocusMode()
    }

    Connections {
        target: Models.Attention

        function onNavigationRequested(sessionId, approvalIdentityToken) {
            attentionPanel.close()
            if (approvalIdentityToken.length > 0)
                window.openAgentIntel(sessionId, approvalIdentityToken)
            else
                window.activateSession(sessionId)
        }
    }

    Connections {
        target: Models.DeepLinks

        function onOpenSessionRequested(sessionId) {
            window.openDeepLinkedSession(sessionId)
        }

        function onReviewApprovalRequested(sessionId, identityToken) {
            window.openDeepLinkedApproval(sessionId, identityToken)
        }

        function onMissingApprovalRequested(sessionId) {
            window.openMissingDeepLinkedApproval(sessionId)
        }
    }

    Connections {
        target: Models.DesktopState

        function onSidebarOpenChanged() {
            Qt.callLater(window.refreshShellAccessibility)
        }

        function onSelectedSessionIdChanged() {
            window.synchronizeSelectedSessionMetadata()
        }

    }

    Connections {
        target: Models.Sessions

        function onModelReset() {
            window.synchronizeSelectedSessionMetadata()
        }

        function onDataChanged() {
            window.synchronizeSelectedSessionMetadata()
        }

        function onAuthorityStateChanged() {
            window.synchronizeSelectedSessionMetadata()
        }
    }

    AttentionPanel {
        id: attentionPanel
    }

    AppearanceMenu {
        id: utilityMenu
        signedIn: Models.AuthState.signedIn
        authBusy: Models.AuthActions.busy
        onOpenSettingsRequested: window.openSettings()
        onOpenShortcutsRequested:
            Qt.callLater(window.toggleKeyboardShortcuts)
        onSignInRequested: Models.AuthActions.beginSignIn()
        onSignOutRequested: Models.AuthActions.signOut()
    }

    KeyboardShortcutsOverlay {
        id: keyboardShortcutsOverlay
    }

    SettingsDrawer {
        id: settingsDrawer
        agentIntelAvailable:
            Models.DesktopState.selectedSessionId.length > 0
        onOpenDevicesRequested: window.showDevices()
        onSignInRequested: Models.AuthActions.beginSignIn()
        onOpenAgentIntelRequested: {
            if (Models.DesktopState.selectedSessionId.length > 0)
                window.openAgentIntel(
                    Models.DesktopState.selectedSessionId,
                    "")
        }
        onOpenDiagnosticsRequested: window.toggleDiagnostics()
        onOpenProjectIntelRequested: function(sourceId) {
            settingsDrawer.close()
            if (sourceId.length > 0)
                projectIntelModal.openForSource(sourceId)
            else
                projectIntelModal.openBrowser()
        }
    }

    KDialog {
        id: closeSessionConfirmation
        objectName: "session.close.confirmation"
        anchors.centerIn: parent
        width: Math.min(400, parent ? parent.width - 32 : 400)
        modal: true
        visible:
            Models.SessionActions.closeConfirmationSessionId.length > 0
        title: qsTr("Close selected session?")
        closePolicy: Popup.NoAutoClose

        onOpened:
            closeSessionCancel.forceActiveFocus(Qt.PopupFocusReason)

        contentItem: ColumnLayout {
            objectName: "session.close.confirmation.content"
            Accessible.id: objectName
            spacing: KodosiTheme.spacing4

            PlainLabel {
                Layout.fillWidth: true
                text: qsTr(
                    "The agent and terminal will stop. This action requires confirmation.")
                color: KodosiTheme.textSecondary
                wrapMode: Text.Wrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: KodosiTheme.spacing3

                Item { Layout.fillWidth: true }

                KButton {
                    id: closeSessionCancel
                    objectName: "session.close.confirmation.cancel"
                    Accessible.id: objectName
                    text: qsTr("Cancel")
                    variant: "quiet"
                    Accessible.name: text
                    onClicked:
                        Models.SessionActions.cancelCloseConfirmation()
                }

                KButton {
                    objectName: "session.close.confirmation.confirm"
                    Accessible.id: objectName
                    text: qsTr("Close session")
                    variant: "danger"
                    enabled: Models.SessionActions.canClose(
                        Models.SessionActions.closeConfirmationSessionId)
                        && Models.SessionActions.availabilityRevision >= 0
                    Accessible.name: text
                    onClicked: Models.SessionActions.confirmClose(
                        Models.SessionActions.closeConfirmationSessionId)
                }
            }
        }
    }

    AgentIntelDrawer {
        id: agentIntelDrawer
        onOpenProjectIntelRequested:
            window.openProjectIntelForSession(sessionId)
    }

    ProjectIntelligenceModal {
        id: projectIntelModal
    }

    DiagnosticsDrawer {
        id: diagnosticsDrawer
    }
}
