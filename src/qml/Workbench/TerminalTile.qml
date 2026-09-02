import QtQuick
import QtQuick.Layouts
import Kodosi.Models 1.0 as Models

Item {
    id: root

    required property string sessionId
    property string sessionName
    property string project
    property string status
    property string mode
    property string terminalError
    property string kind
    property bool canRetainPresentation: false
    property bool isStageReady: false
    property bool focusedSizeAuthority: false
    property bool accessibilitySuppressed: false
    property int desktopRequestSerial: 0
    property string openProjectRequestId
    property string openProjectError
    readonly property bool active:
        Models.DesktopState.selectedSessionId === sessionId
    readonly property bool focusMode:
        Models.DesktopState.stageLayoutMode
            === Models.DesktopState.Focus
    readonly property int chromeTier:
        width < 340 ? 0 : width < 640 ? 1 : 2
    readonly property string projectLabel: {
        const normalized = project.replace(/\/+$/, "")
        const segments = normalized.split("/")
        const name = segments.length > 0
            ? segments[segments.length - 1]
            : ""
        return name.length > 0 ? "~/" + name : project
    }
    readonly property color statusColor:
        status === "active"
        ? KodosiTheme.success
        : status === "blocked"
          ? KodosiTheme.danger
          : status === "reconnecting"
            ? KodosiTheme.reconnecting
            : status === "waiting"
              ? KodosiTheme.warning
              : KodosiTheme.textTertiary
    readonly property string terminalAccessibilityStatus:
        terminalError.length > 0
        ? qsTr("Terminal unavailable: %1").arg(terminalError)
        : !terminal.terminalReady
          ? qsTr("Connecting terminal")
          : status.length > 0
            ? qsTr("Terminal status: %1").arg(status)
            : qsTr("Terminal ready")
    readonly property string value: terminalAccessibilityStatus
    readonly property string sharePhase:
        Models.SessionShareScope.stateRevision >= 0
        ? Models.SessionShareScope.phase(sessionId)
        : "idle"
    readonly property bool canShare:
        Models.SessionShareScope.canChange(sessionId)
        || sharePhase !== "idle"
    signal inspectSessionRequested(string sessionId, string sessionName)
    signal shareSessionRequested(string sessionId, string sessionName)

    objectName: "stage.tile." + sessionId
    Accessible.id: objectName
    Accessible.ignored: accessibilitySuppressed || !visible
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("%1 terminal").arg(
        sessionName.length > 0 ? sessionName : sessionId)
    Accessible.description: terminalAccessibilityStatus
    Accessible.selected: active
    Accessible.readOnly: terminal.readOnly

    function cycleMode() {
        const next = mode === "normal"
            ? "plan"
            : mode === "plan"
              ? "autopilot"
              : "normal"
        Models.SessionActions.setMode(sessionId, next)
    }

    function requestClose() {
        if (Models.SessionActions.closeConfirmationSessionId
                !== sessionId) {
            Models.SessionActions.requestCloseConfirmation(sessionId)
        } else {
            Models.SessionActions.confirmClose(sessionId)
        }
    }

    function openProject() {
        desktopRequestSerial += 1
        const requestId = "stage.tile." + sessionId
            + ".openProject." + desktopRequestSerial
        openProjectRequestId = requestId
        openProjectError = ""
        Models.DesktopFiles.openSessionProject(
            sessionId,
            requestId,
            Models.DesktopFiles.TerminalProject)
    }

    function refreshPresentation() {
        const presentation =
            Models.Sessions.presentationForSession(sessionId)
        if (presentation.sessionId !== sessionId)
            return
        sessionName = presentation.name
        project = presentation.project
        status = presentation.status
        mode = presentation.mode
        kind = presentation.kind
        canRetainPresentation = presentation.canRetainPresentation
        isStageReady = presentation.isStageReady
    }

    function synchronizeTerminal() {
        terminalError = ""
        if (sessionId.length === 0) {
            Models.TerminalSurfaces.detach(terminal)
            return
        }
        if (!Models.TerminalSurfaces.bind(terminal, sessionId))
            terminalError = qsTr("This session is no longer available.")
    }

    function forceTerminalFocus() {
        if (visible && enabled && terminal.visible && terminal.enabled)
            terminal.forceActiveFocus(Qt.ShortcutFocusReason)
    }

    onSessionIdChanged: {
        refreshPresentation()
        Qt.callLater(synchronizeTerminal)
    }
    Component.onCompleted: {
        refreshPresentation()
        Qt.callLater(synchronizeTerminal)
    }
    Component.onDestruction: Models.TerminalSurfaces.detach(terminal)

    Connections {
        target: Models.Sessions

        function onModelReset() {
            root.refreshPresentation()
        }

        function onDataChanged() {
            root.refreshPresentation()
        }

        function onAuthorityStateChanged() {
            root.refreshPresentation()
        }
    }

    Connections {
        target: Models.TerminalSurfaces

        function onAttachmentReady(readySessionId) {
            if (readySessionId === root.sessionId)
                root.terminalError = ""
        }

        function onAttachmentRejected(rejectedSessionId, reason) {
            if (rejectedSessionId === root.sessionId)
                root.terminalError = reason
        }
    }

    Connections {
        target: Models.DesktopFiles

        function onPathOpened(requestId, purpose) {
            if (requestId === root.openProjectRequestId
                    && purpose === Models.DesktopFiles.TerminalProject) {
                root.openProjectRequestId = ""
                root.openProjectError = ""
            }
        }

        function onOperationFailed(requestId, purpose, errorCode, message) {
            if (requestId === root.openProjectRequestId
                    && purpose === Models.DesktopFiles.TerminalProject) {
                root.openProjectRequestId = ""
                root.openProjectError = message
            }
        }
    }

    Rectangle {
        visible: root.active
        x: 4
        y: 6
        width: Math.max(0, root.width - 8)
        height: root.height
        radius: KodosiTheme.radiusSmall
        color: Qt.rgba(
            KodosiTheme.accent.r,
            KodosiTheme.accent.g,
            KodosiTheme.accent.b,
            0.04)
    }

    Rectangle {
        visible: root.active
        x: 2
        y: 3
        width: Math.max(0, root.width - 4)
        height: root.height
        radius: KodosiTheme.radiusSmall
        color: Qt.rgba(
            KodosiTheme.accent.r,
            KodosiTheme.accent.g,
            KodosiTheme.accent.b,
            0.08)
    }

    Rectangle {
        anchors.fill: parent
        radius: KodosiTheme.radiusSmall
        color: KodosiTheme.surfaceElevated
        border.width: 1
        border.color: root.active
            ? KodosiTheme.accentMuted
            : KodosiTheme.seam
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 1
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 34
            color: root.mode === "plan"
                ? KodosiTheme.surfaceRaised
                : root.mode === "autopilot"
                  ? KodosiTheme.surfaceSelected
                  : KodosiTheme.surfaceElevated

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: KodosiTheme.spacing2
                anchors.rightMargin: KodosiTheme.spacing2
                spacing: 2

                KIconButton {
                    objectName: "stage.tile." + root.sessionId + ".remove"
                    Accessible.id: objectName
                    Accessible.ignored:
                        root.accessibilitySuppressed || !visible
                    glyph: "close"
                    size: 26
                    Accessible.name: qsTr("Remove %1 from Stage").arg(
                        root.sessionName)
                    onClicked: {
                        Models.TerminalSurfaces.detach(terminal)
                        Models.DesktopState.unstageSession(root.sessionId)
                    }
                }

                KButton {
                    objectName: "stage.tile." + root.sessionId + ".select"
                    Accessible.id: objectName
                    Accessible.ignored:
                        root.accessibilitySuppressed || !visible
                    Layout.fillWidth: true
                    Layout.minimumWidth: 24
                    compact: true
                    variant: "quiet"
                    uppercase: false
                    contentLeftAligned: true
                    showLeadingDot: true
                    leadingDotColor: root.statusColor
                    text: root.sessionName.length > 0
                        ? root.sessionName
                        : qsTr("Session")
                    secondaryText: root.chromeTier === 2
                        ? root.projectLabel
                        : ""
                    Accessible.name: qsTr("Select session %1").arg(
                        root.sessionName.length > 0
                        ? root.sessionName
                        : qsTr("Session"))
                    Accessible.selected: root.active
                    onClicked:
                        Models.DesktopState.selectSession(root.sessionId)
                }

                KIconButton {
                    objectName:
                        "stage.tile." + root.sessionId + ".openProject"
                    Accessible.id: objectName
                    Accessible.ignored:
                        root.accessibilitySuppressed || !visible
                    glyph: "folder"
                    size: 26
                    visible: root.chromeTier === 2
                        && Models.DesktopFiles
                            .canOpenSessionProject(root.sessionId)
                    Accessible.name: qsTr("Open project for %1").arg(
                        root.sessionName)
                    onClicked: root.openProject()
                }

                KIconButton {
                    objectName: "stage.tile." + root.sessionId + ".intel"
                    Accessible.id: objectName
                    Accessible.ignored:
                        root.accessibilitySuppressed || !visible
                    glyph: "intel"
                    size: 26
                    visible: root.chromeTier === 2
                    Accessible.name: qsTr("Open Agent Intelligence for %1").arg(
                        root.sessionName)
                    onClicked: root.inspectSessionRequested(
                        root.sessionId,
                        root.sessionName)
                }

                KButton {
                    objectName: "stage.tile." + root.sessionId + ".mode"
                    Accessible.id: objectName
                    Accessible.ignored:
                        root.accessibilitySuppressed || !visible
                    implicitWidth: 30
                    visible: root.chromeTier === 2
                    compact: true
                    variant: "quiet"
                    text: root.mode.length > 0
                        ? root.mode.substring(0, 1).toUpperCase()
                        : qsTr("N")
                    enabled:
                        Models.SessionActions.availabilityRevision >= 0
                        && Models.SessionActions.canSetMode(root.sessionId)
                    Accessible.name: qsTr("Change session mode from %1").arg(
                        root.mode)
                    onClicked: root.cycleMode()
                }

                KIconButton {
                    objectName:
                        "stage.tile." + root.sessionId + ".interrupt"
                    Accessible.id: objectName
                    Accessible.ignored:
                        root.accessibilitySuppressed || !visible
                    glyph: "interrupt"
                    size: 26
                    visible: root.chromeTier === 2
                    enabled:
                        Models.SessionActions.availabilityRevision >= 0
                        && Models.SessionActions.canInterrupt(root.sessionId)
                    Accessible.name: qsTr("Interrupt %1").arg(
                        root.sessionName)
                    onClicked:
                        Models.SessionActions.interrupt(root.sessionId)
                }

                KIconButton {
                    objectName: "stage.tile." + root.sessionId + ".focus"
                    Accessible.id: objectName
                    Accessible.ignored:
                        root.accessibilitySuppressed || !visible
                    glyph: root.focusMode ? "grid" : "focus"
                    size: 26
                    visible: root.chromeTier >= 1
                    Accessible.name: root.focusMode
                        ? qsTr("Exit terminal focus")
                        : qsTr("Focus %1").arg(root.sessionName)
                    onClicked: {
                        if (root.focusMode)
                            Models.DesktopState.exitFocusMode()
                        else
                            Models.DesktopState.enterFocusMode(
                                root.sessionId)
                    }
                }

                KIconButton {
                    objectName: "stage.tile." + root.sessionId + ".share"
                    Accessible.id: objectName
                    Accessible.ignored:
                        root.accessibilitySuppressed || !visible
                    glyph: "share"
                    size: 26
                    visible: root.chromeTier === 2
                        && root.canShare
                    enabled: root.canShare
                    Accessible.name: qsTr("Share %1").arg(
                        root.sessionName)
                    onClicked: root.shareSessionRequested(
                        root.sessionId,
                        root.sessionName)
                }

                PlainLabel {
                    Accessible.ignored:
                        root.accessibilitySuppressed || !visible
                    visible: terminal.terminalReady
                        && terminal.readOnly
                    text: qsTr("Read only")
                    color: KodosiTheme.textSecondary
                    font.pixelSize: 9
                    font.weight: Font.DemiBold
                }

                KIconButton {
                    objectName: "stage.tile." + root.sessionId + ".close"
                    Accessible.id: objectName
                    Accessible.ignored:
                        root.accessibilitySuppressed || !visible
                    glyph: "close"
                    size: 26
                    visible: root.chromeTier === 2
                    variant:
                        Models.SessionActions.closeConfirmationSessionId
                            === root.sessionId
                        ? "danger"
                        : "dangerQuiet"
                    enabled:
                        Models.SessionActions.availabilityRevision >= 0
                        && Models.SessionActions.canClose(root.sessionId)
                    Accessible.name:
                        Models.SessionActions.closeConfirmationSessionId
                            === root.sessionId
                        ? qsTr("Confirm close %1").arg(root.sessionName)
                        : qsTr("Close %1").arg(root.sessionName)
                    onClicked: root.requestClose()
                }

                KIconButton {
                    objectName:
                        "stage.tile." + root.sessionId + ".overflow"
                    Accessible.id: objectName
                    Accessible.ignored:
                        root.accessibilitySuppressed || !visible
                    glyph: "more"
                    size: 26
                    visible: root.chromeTier < 2
                    Accessible.name: qsTr("More actions for %1").arg(
                        root.sessionName)
                    onClicked: overflowMenu.open()

                    KMenu {
                        id: overflowMenu

                        KMenuItem {
                            objectName:
                                "stage.tile." + root.sessionId
                                + ".overflow.openProject"
                            Accessible.id: objectName
                            Accessible.ignored: !visible
                            visible: overflowMenu.visible
                                && Models.DesktopFiles
                                    .canOpenSessionProject(root.sessionId)
                            text: qsTr("Open Project")
                            onTriggered: root.openProject()
                        }

                        KMenuItem {
                            objectName:
                                "stage.tile." + root.sessionId
                                + ".overflow.focus"
                            Accessible.id: objectName
                            Accessible.ignored: !visible
                            visible: root.chromeTier === 0
                            text: root.focusMode
                                ? qsTr("Return to grid")
                                : qsTr("Focus terminal")
                            onTriggered: {
                                if (root.focusMode)
                                    Models.DesktopState.exitFocusMode()
                                else
                                    Models.DesktopState.enterFocusMode(
                                        root.sessionId)
                            }
                        }

                        KMenuItem {
                            objectName:
                                "stage.tile." + root.sessionId
                                + ".overflow.intel"
                            Accessible.id: objectName
                            Accessible.ignored: !visible
                            text: qsTr("Agent Intelligence")
                            onTriggered: root.inspectSessionRequested(
                                root.sessionId,
                                root.sessionName)
                        }

                        KMenuItem {
                            objectName:
                                "stage.tile." + root.sessionId
                                + ".overflow.mode"
                            Accessible.id: objectName
                            Accessible.ignored: !visible
                            text: qsTr("Change mode")
                            enabled:
                                Models.SessionActions.availabilityRevision >= 0
                                && Models.SessionActions.canSetMode(
                                    root.sessionId)
                            onTriggered: root.cycleMode()
                        }

                        KMenuItem {
                            objectName:
                                "stage.tile." + root.sessionId
                                + ".overflow.interrupt"
                            Accessible.id: objectName
                            Accessible.ignored: !visible
                            text: qsTr("Interrupt")
                            enabled:
                                Models.SessionActions.availabilityRevision >= 0
                                && Models.SessionActions.canInterrupt(
                                    root.sessionId)
                            onTriggered:
                                Models.SessionActions.interrupt(root.sessionId)
                        }

                        KMenuItem {
                            objectName:
                                "stage.tile." + root.sessionId
                                + ".overflow.share"
                            Accessible.id: objectName
                            Accessible.ignored: !visible
                            visible: root.canShare
                            text: qsTr("Share...")
                            enabled: root.canShare
                            onTriggered: root.shareSessionRequested(
                                root.sessionId,
                                root.sessionName)
                        }

                        KMenuSeparator {}

                        KMenuItem {
                            objectName:
                                "stage.tile." + root.sessionId
                                + ".overflow.close"
                            Accessible.id: objectName
                            Accessible.ignored: !visible
                            text:
                                Models.SessionActions
                                    .closeConfirmationSessionId
                                    === root.sessionId
                                ? qsTr("Confirm close")
                                : qsTr("Close...")
                            enabled:
                                Models.SessionActions.availabilityRevision >= 0
                                && Models.SessionActions.canClose(
                                    root.sessionId)
                            onTriggered: root.requestClose()
                        }
                    }
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: root.active
                    ? KodosiTheme.accentMuted
                    : KodosiTheme.seam
            }
        }

        Rectangle {
            visible: root.openProjectError.length > 0
            Layout.fillWidth: true
            implicitHeight: openProjectErrorRow.implicitHeight + 10
            color: Qt.rgba(
                KodosiTheme.danger.r,
                KodosiTheme.danger.g,
                KodosiTheme.danger.b,
                0.08)

            RowLayout {
                id: openProjectErrorRow
                anchors.fill: parent
                anchors.leftMargin: KodosiTheme.spacing3
                anchors.rightMargin: KodosiTheme.spacing2
                spacing: KodosiTheme.spacing2

                PlainLabel {
                    Layout.fillWidth: true
                    text: root.openProjectError
                    color: KodosiTheme.danger
                    font.pixelSize: 9
                    wrapMode: Text.Wrap
                    Accessible.name: text
                }

                KIconButton {
                    objectName: "stage.tile." + root.sessionId
                        + ".openProject.error.dismiss"
                    Accessible.id: objectName
                    Accessible.ignored: !visible
                    glyph: "close"
                    size: 24
                    Accessible.name:
                        qsTr("Dismiss project open error")
                    onClicked: root.openProjectError = ""
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Models.TerminalView {
                id: terminal
                objectName:
                    "stage.tile." + root.sessionId + ".terminal"
                Accessible.id: objectName
                Accessible.ignored:
                    root.accessibilitySuppressed || !visible
                anchors.fill: parent
                fontFamily: Models.DesktopSettings.fontFamily
                fontPixelSize: Models.DesktopSettings.fontSize
                lineHeight: Models.DesktopSettings.lineHeight
                cursorStyle: Models.DesktopSettings.cursorStyle
                cursorBlink: Models.DesktopSettings.cursorBlink
                scrollbackLines: Models.DesktopSettings.scrollbackLines
                selectionBackground: KodosiTheme.accent
                selectionForeground: KodosiTheme.accentForeground
                preeditBackground: KodosiTheme.accent
                preeditForeground: KodosiTheme.accentForeground
                focusedSizeAuthority: root.focusedSizeAuthority

                onActiveFocusChanged: {
                    if (activeFocus)
                        Models.DesktopState.selectSession(root.sessionId)
                }
                onTerminalError: message =>
                    root.terminalError = message
                onTerminalClosed: root.terminalError =
                    qsTr("The terminal session has ended.")
            }

            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(360, parent.width - 24)
                spacing: KodosiTheme.spacing3
                visible: !terminal.terminalReady
                    || root.terminalError.length > 0

                KBusyIndicator {
                    objectName:
                        "stage.tile." + root.sessionId + ".connecting"
                    Accessible.id: objectName
                    Accessible.name: qsTr("Connecting terminal")
                    Accessible.ignored:
                        root.accessibilitySuppressed || !visible
                    Layout.alignment: Qt.AlignHCenter
                    visible: root.terminalError.length === 0
                    running: visible
                    implicitWidth: 24
                    implicitHeight: 24
                }

                KIcon {
                    Accessible.ignored: true
                    Layout.alignment: Qt.AlignHCenter
                    visible: root.terminalError.length > 0
                    Layout.preferredWidth: 24
                    Layout.preferredHeight: 24
                    name: "terminal"
                    color: KodosiTheme.danger
                }

                PlainLabel {
                    Accessible.ignored:
                        root.accessibilitySuppressed || !visible
                    Layout.fillWidth: true
                    text: root.terminalError.length > 0
                        ? qsTr("Terminal unavailable")
                        : qsTr("Connecting terminal")
                    color: KodosiTheme.textPrimary
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                }

                PlainLabel {
                    Accessible.ignored:
                        root.accessibilitySuppressed || !visible
                    Layout.fillWidth: true
                    visible: root.terminalError.length > 0
                    text: root.terminalError
                    color: KodosiTheme.textSecondary
                    font.pixelSize: 10
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                }

                KButton {
                    objectName:
                        "stage.tile." + root.sessionId + ".retry"
                    Accessible.id: objectName
                    Accessible.ignored:
                        root.accessibilitySuppressed || !visible
                    Layout.alignment: Qt.AlignHCenter
                    visible: root.terminalError.length > 0
                    text: qsTr("Retry")
                    variant: "directional"
                    iconName: "refresh"
                    Accessible.name: qsTr("Retry terminal connection")
                    onClicked: {
                        root.terminalError = ""
                        if (root.kind === "remote"
                                && root.canRetainPresentation
                                && !root.isStageReady) {
                            if (!Models.SessionActions.restoreRemote(
                                    root.sessionId)) {
                                root.terminalError =
                                    Models.SessionActions.lastError
                            }
                        } else if (!Models.TerminalSurfaces.retry(
                                       terminal,
                                       root.sessionId)) {
                            root.terminalError = qsTr(
                                "This session is no longer available.")
                        }
                    }
                }
            }
        }
    }
}
