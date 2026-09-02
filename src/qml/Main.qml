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
    readonly property bool blockingOverlayOpen:
        settingsDrawer.opened
        || attentionPanel.opened
        || agentIntelDrawer.opened
        || diagnosticsDrawer.opened
        || authOverlay.visible

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
        if (!requestSessionSelection(sessionId, false))
            return false
        Models.DesktopState.activeView = 0
        return true
    }

    function openAgentIntel(sessionId, approvalIdentityToken) {
        const session = Models.Sessions.presentationForSession(sessionId)
        if (!session.sessionId)
            return false
        if (!activateSession(sessionId))
            return false
        attentionPanel.close()
        settingsDrawer.close()
        diagnosticsDrawer.close()
        agentIntelDrawer.openForSession(
            sessionId,
            session.name,
            approvalIdentityToken || "")
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
                        onClicked: window.showDevices()
                    }

                    KIconButton {
                        id: settingsButton
                        objectName: "header.settings"
                        Accessible.id: objectName
                        glyph: "settings"
                        Accessible.name: qsTr("Open settings")
                        onClicked: {
                            attentionPanel.close()
                            agentIntelDrawer.close()
                            diagnosticsDrawer.close()
                            settingsDrawer.open()
                        }
                    }

                    KButton {
                        id: accountButton
                        objectName: "header.account"
                        Accessible.id: objectName
                        variant: Models.AuthState.signedIn
                            ? "secondary"
                            : "directional"
                        iconName: Models.AuthState.signedIn
                            ? "account"
                            : "login"
                        iconTrailing: !Models.AuthState.signedIn
                        text: Models.AuthState.signedIn
                            ? qsTr("Sign out")
                            : Models.AuthActions.busy
                              ? qsTr("Signing in")
                              : qsTr("Sign in")
                        Accessible.name: text
                        enabled: !Models.AuthActions.busy
                        compact: window.width < 1000
                        onClicked: {
                            if (Models.AuthState.signedIn)
                                Models.AuthActions.signOut()
                            else
                                Models.AuthActions.beginSignIn()
                        }
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
            }

            DevicesView {
                Layout.fillWidth: true
                Layout.fillHeight: true
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
        onVisibleChanged: {
            if (!visible)
                return
            attentionPanel.close()
            settingsDrawer.close()
            agentIntelDrawer.close()
            diagnosticsDrawer.close()
        }
    }

    Shortcut {
        sequence: "Ctrl+Shift+A"
        enabled: !window.blockingOverlayOpen
        onActivated: window.toggleAttention()
    }

    Shortcut {
        sequence: "Ctrl+B"
        enabled: !window.blockingOverlayOpen
            && Models.DesktopState.activeView === 0
        onActivated: Models.DesktopState.sidebarOpen =
            !Models.DesktopState.sidebarOpen
    }

    Shortcut {
        sequence: "Ctrl+Shift+D"
        enabled: !authOverlay.visible || diagnosticsDrawer.opened
        onActivated: window.toggleDiagnostics()
    }

    Shortcut {
        sequence: "Ctrl+Shift+Enter"
        enabled: !window.blockingOverlayOpen
            && Models.DesktopState.activeView === 0
            && Models.DesktopState.selectedSessionId.length > 0
        onActivated:
            Models.DesktopState.toggleFocusForSelectedSession()
    }

    Shortcut {
        sequences: ["Ctrl+Alt+Right", "Ctrl+Alt+Down"]
        enabled: !window.blockingOverlayOpen
            && Models.DesktopState.activeView === 0
        onActivated: Models.DesktopState.selectAdjacentSession(1)
    }

    Shortcut {
        sequences: ["Ctrl+Alt+Left", "Ctrl+Alt+Up"]
        enabled: !window.blockingOverlayOpen
            && Models.DesktopState.activeView === 0
        onActivated: Models.DesktopState.selectAdjacentSession(-1)
    }

    Shortcut {
        sequence: "Escape"
        enabled: !window.blockingOverlayOpen
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

    SettingsDrawer {
        id: settingsDrawer
        agentIntelAvailable:
            Models.DesktopState.selectedSessionId.length > 0
        onOpenDevicesRequested: window.showDevices()
        onOpenAgentIntelRequested: {
            if (Models.DesktopState.selectedSessionId.length > 0)
                window.openAgentIntel(
                    Models.DesktopState.selectedSessionId,
                    "")
        }
        onOpenDiagnosticsRequested: window.toggleDiagnostics()
    }

    AgentIntelDrawer {
        id: agentIntelDrawer
    }

    DiagnosticsDrawer {
        id: diagnosticsDrawer
    }
}
