import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kodosi.Models 1.0 as Models

ApplicationWindow {
    id: window
    objectName: "window.main"

    property var optimisticRemoteOpens: ({})
    readonly property int themeMotionFast: KodosiTheme.motionFast
    readonly property int themeMotionNormal: KodosiTheme.motionNormal
    readonly property real sidebarActualWidth:
        Math.min(KodosiTheme.sidebarWidth, Math.max(190, width * 0.28))
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
        startupOverlay.visible
        || settingsDrawer.opened
        || keyboardShortcutsOverlay.opened
        || agentIntelDrawer.opened
        || projectIntelModal.opened
        || resumeAgentWorkModal.opened
        || closeSessionConfirmation.opened
        || sessionSidebar.modalOpen
        || authOverlay.visible
    readonly property bool shortcutContextAvailable:
        !blockingOverlayOpen && !textInputOwnsShortcuts()

    function textInputOwnsShortcuts() {
        const item = window.activeFocusItem
        return item instanceof TextInput || item instanceof TextEdit
    }

    function requestSessionSelection(sessionId, openRemote) {
        if (sessionId.length === 0)
            return false
        const session = Models.Sessions.presentationForSession(sessionId)
        const wasStaged =
            Models.DesktopState.stagedSessionIds.indexOf(sessionId) >= 0
        if (session.sessionId !== sessionId
                || !Models.DesktopState.selectSession(sessionId))
            return false
        if (openRemote && Models.SessionActions.canOpenRemote(sessionId)) {
            if (!wasStaged) {
                const pending = Object.assign(
                    {},
                    optimisticRemoteOpens)
                pending[sessionId] = true
                optimisticRemoteOpens = pending
            }
            if (!Models.SessionActions.openRemote(sessionId)) {
                if (!wasStaged)
                    Models.DesktopState.unstageSession(sessionId)
                const failed = Object.assign(
                    {},
                    optimisticRemoteOpens)
                delete failed[sessionId]
                optimisticRemoteOpens = failed
                return false
            }
        }
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

    function prepareApprovalReview() {
        if (!resumeAgentWorkModal.closeModal())
            return false
        keyboardShortcutsOverlay.close()
        settingsDrawer.close()
        diagnosticsDrawer.close()
        projectIntelModal.close()
        return true
    }

    function openAgentIntelWithRemote(
            sessionId,
            approvalIdentityToken,
            openRemote) {
        const session = Models.Sessions.presentationForSession(sessionId)
        if (!session.sessionId)
            return false
        if (!prepareApprovalReview())
            return false
        if (!activateSessionWithRemote(sessionId, openRemote))
            return false
        agentIntelDrawer.openForSession(
            sessionId,
            session.name,
            approvalIdentityToken || "",
            false)
        return true
    }

    function openDeepLinkedSession(sessionId) {
        const opened = activateSessionWithRemote(sessionId, true)
        if (!opened)
            Models.DeepLinks.reportNavigationResult(false)
        return opened
    }

    function openDeepLinkedApproval(sessionId, identityToken) {
        const opened = openAgentIntelWithRemote(
            sessionId,
            identityToken,
            true)
        if (!opened)
            Models.DeepLinks.reportNavigationResult(false)
        return opened
    }

    function openMissingDeepLinkedApproval(sessionId) {
        const session = Models.Sessions.presentationForSession(sessionId)
        const opened = session.sessionId === sessionId
            && prepareApprovalReview()
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

    function openProjectIntelForSession(sessionId) {
        if (sessionId.length === 0)
            return false
        settingsDrawer.close()
        diagnosticsDrawer.close()
        agentIntelDrawer.close()
        projectIntelModal.openForSession(sessionId)
        return true
    }

    function openProjectIntelSource(sourceId) {
        settingsDrawer.close()
        diagnosticsDrawer.close()
        agentIntelDrawer.close()
        projectIntelModal.openForSource(sourceId)
        return true
    }

    function openResumeAgentWork() {
        settingsDrawer.close()
        diagnosticsDrawer.close()
        agentIntelDrawer.close()
        projectIntelModal.close()
        resumeAgentWorkModal.openModal()
        return true
    }

    function reviewApproval(identityToken, sessionId) {
        if (Models.PendingPermissions.rowForIdentityToken(identityToken) < 0)
            return false
        return openAgentIntel(sessionId, identityToken)
    }

    function toggleDiagnostics() {
        if ((authOverlay.visible || startupOverlay.visible)
                && !diagnosticsDrawer.opened)
            return
        if (diagnosticsDrawer.opened)
            diagnosticsDrawer.close()
        else
            diagnosticsDrawer.open()
    }

    function openStartupDiagnostics() {
        if (!startupOverlay.failed)
            return
        utilityMenu.close()
        keyboardShortcutsOverlay.close()
        settingsDrawer.close()
        agentIntelDrawer.close()
        projectIntelModal.close()
        resumeAgentWorkModal.closeModal()
        diagnosticsDrawer.open()
    }

    function openSettings() {
        utilityMenu.close()
        agentIntelDrawer.close()
        diagnosticsDrawer.close()
        settingsDrawer.open()
    }

    function openAccountSettings() {
        utilityMenu.close()
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
        if (!shortcutContextAvailable)
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

    function handleTopApproval(approve) {
        const request = Models.PendingPermissions.presentationForSession(
            Models.DesktopState.selectedSessionId)
        if (request.identityToken === undefined)
            return false
        if (!approve)
            return Models.PendingPermissions.deny(request.identityToken)
        if (request.risk === "safe" || request.risk === "network")
            return Models.PendingPermissions.approve(request.identityToken)
        return openAgentIntel(request.sessionId, request.identityToken)
    }

    function toggleUtilityMenu() {
        agentIntelDrawer.close()
        settingsDrawer.close()
        diagnosticsDrawer.close()
        if (utilityMenu.opened)
            utilityMenu.close()
        else
            utilityMenu.openAt(utilityButton)
    }

    visible: false
    color: KodosiTheme.canvas
    title: qsTr("Kodosi")

    Models.AccessibilityScope {
        anchors.fill: parent
        suppressed: window.blockingOverlayOpen
        Accessible.name: qsTr("Kodosi mission control")

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
                            ? window.sidebarActualWidth
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
                            width: 224
                            height: 36

                            NavTab {
                                Layout.preferredWidth: 110
                                accessibleId: "my-agents"
                                iconName: "command"
                                title: qsTr("My Agents")
                                selected: Models.DesktopState.activeView === 0
                                onClicked: Models.DesktopState.activeView = 0
                            }

                            NavTab {
                                Layout.preferredWidth: 110
                                accessibleId: "missions"
                                iconName: "people"
                                title: qsTr("Missions")
                                selected: Models.DesktopState.activeView === 1
                                onClicked: Models.DesktopState.activeView = 1
                            }
                        }
                    }

                    RowLayout {
                        Layout.minimumWidth: 48
                        Layout.maximumWidth: Layout.minimumWidth
                        Layout.fillHeight: true
                        Layout.rightMargin: KodosiTheme.spacing5
                        spacing: 0

                        KIconButton {
                            id: utilityButton
                            objectName: "header.utility.menu"
                            Accessible.id: objectName
                            glyph: "account"
                            Accessible.name: Models.AuthState.signedIn
                                ? qsTr("Account menu")
                                : qsTr("App menu")
                            enabled: !Models.AuthActions.busy
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

                    Models.AccessibilityScope {
                        visible: Models.DesktopState.sidebarOpen
                        Layout.fillHeight: true
                        Layout.preferredWidth: window.sidebarActualWidth
                        Accessible.name: qsTr("My Agents sidebar")

                        SessionSidebar {
                            id: sessionSidebar
                            anchors.fill: parent
                            selectedSessionId:
                                Models.DesktopState.selectedSessionId
                            onSessionSelectionRequested: (sessionId, openRemote) =>
                                window.requestSessionSelection(sessionId, openRemote)
                            onProjectIntelligenceRequested: sessionId =>
                                window.openProjectIntelForSession(sessionId)
                            onResumeAgentWorkRequested:
                                window.openResumeAgentWork()
                        }
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
                        onNewSessionRequested: window.createDefaultSession()
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

            }
        }

    }

    AuthOverlay {
        id: authOverlay
        suppressed: startupOverlay.visible
        accountRecoverySurfaceOpen:
            settingsDrawer.opened
            && settingsDrawer.selectedCategory === "account"
            && settingsDrawer.identityRecoveryActive
        onAccountRecoveryRequested:
            Qt.callLater(settingsDrawer.openAccount)
        onVisibleChanged: {
            if (!visible)
                return
            settingsDrawer.close()
            agentIntelDrawer.close()
            diagnosticsDrawer.close()
        }
    }

    Shortcut {
        objectName: "shortcut.settings"
        sequence: "Ctrl+Shift+,"
        context: Qt.ApplicationShortcut
        enabled: window.shortcutContextAvailable
        onActivated: window.openSettings()
    }

    Shortcut {
        objectName: "shortcut.session.new"
        sequence: "Ctrl+Shift+N"
        context: Qt.ApplicationShortcut
        enabled: window.shortcutContextAvailable
        onActivated: window.createDefaultSession()
    }

    Shortcut {
        objectName: "shortcut.session.resumeAgentWork"
        sequence: "Ctrl+Shift+R"
        context: Qt.ApplicationShortcut
        enabled: window.shortcutContextAvailable
        onActivated: window.openResumeAgentWork()
    }

    Shortcut {
        objectName: "shortcut.approval.approve"
        sequence: "Ctrl+Shift+A"
        context: Qt.ApplicationShortcut
        enabled: window.shortcutContextAvailable
        onActivated: window.handleTopApproval(true)
    }

    Shortcut {
        objectName: "shortcut.approval.deny"
        sequence: "Ctrl+Shift+X"
        context: Qt.ApplicationShortcut
        enabled: window.shortcutContextAvailable
        onActivated: window.handleTopApproval(false)
    }

    Shortcut {
        objectName: "shortcut.session.intelligence"
        sequence: "Ctrl+Shift+I"
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
        sequence: "Ctrl+Shift+B"
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
        target: Models.SessionActions

        function onSessionCreated(sessionId) {
            Models.DesktopState.activeView = 0
            Models.DesktopState.selectSession(sessionId)
        }

        function onRemoteOpenSucceeded(sessionId) {
            const pending = Object.assign(
                {},
                window.optimisticRemoteOpens)
            delete pending[sessionId]
            window.optimisticRemoteOpens = pending
        }

        function onRemoteOpenFailed(sessionId) {
            if (window.optimisticRemoteOpens[sessionId] === true)
                Models.DesktopState.unstageSession(sessionId)
            const pending = Object.assign(
                {},
                window.optimisticRemoteOpens)
            delete pending[sessionId]
            window.optimisticRemoteOpens = pending
        }
    }

    Connections {
        target: Models.Attention

        function onNavigationRequested(sessionId, approvalIdentityToken) {
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

    ResumeAgentWorkModal {
        id: resumeAgentWorkModal
    }

    DiagnosticsDrawer {
        id: diagnosticsDrawer
        onClosed: {
            if (startupOverlay.visible)
                Qt.callLater(startupOverlay.restorePrimaryFocus)
        }
    }

    Connections {
        target: Models.ApplicationLifecycle

        function onStateChanged() {
            if (Models.ApplicationLifecycle.state
                    === Models.ApplicationLifecycle.Ready)
                return
            utilityMenu.close()
            keyboardShortcutsOverlay.close()
            settingsDrawer.close()
            agentIntelDrawer.close()
            projectIntelModal.close()
            resumeAgentWorkModal.closeModal()
            diagnosticsDrawer.close()
            Models.SessionActions.cancelCloseConfirmation()
            sessionSidebar.closeConflictingOverlays()
        }
    }

    StartupOverlay {
        id: startupOverlay
        diagnosticsOpen: diagnosticsDrawer.opened
        onDiagnosticsRequested: window.openStartupDiagnostics()
    }
}
