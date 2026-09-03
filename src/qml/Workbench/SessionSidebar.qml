pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kodosi.Models 1.0 as Models

Item {
    id: root
    objectName: "sidebar.sessions"
    readonly property bool modalOpen:
        shareDialog.opened
        || deleteConfirmation.visible
        || leaveConfirmation.visible
        || revokeConfirmation.visible
    Accessible.id: objectName
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Sessions")

    property string selectedSessionId
    property bool createOpen: false
    property bool hiddenOpen: false
    property int desktopRequestSerial: 0
    property string directoryPickerRequestId
    property string desktopFileRequestId
    property string desktopFileError
    onCreateOpenChanged: {
        if (createOpen) {
            desktopFileError = ""
            createDirectory.text =
                Models.SessionActions.defaultWorkingDirectory
        } else if (directoryPickerRequestId.length > 0) {
            Models.DesktopFiles.cancelDirectory(
                directoryPickerRequestId,
                Models.DesktopFiles.NewSessionWorkingDirectory)
        }
    }
    property string shareSessionId
    property string shareSessionName
    signal sessionSelectionRequested(string sessionId, bool openRemote)
    signal projectIntelligenceRequested(string sessionId)

    function selectDelegate(item) {
        if (!item)
            return
        sessionSelectionRequested(item.sessionId, false)
    }

    function activateDelegate(item) {
        if (!item)
            return
        sessionSelectionRequested(item.sessionId, true)
    }

    function openShare(item) {
        if (!item)
            return
        openShareByIdentity(item.sessionId, item.sessionName)
    }

    function openShareByIdentity(sessionId, sessionName) {
        if (sessionId.length === 0)
            return
        shareSessionId = sessionId
        shareSessionName = sessionName
        accessHandle.clear()
        Models.SessionAccess.inspect(sessionId)
        shareDialog.open()
    }

    function closeConflictingOverlays() {
        createOpen = false
        hiddenOpen = false
        shareDialog.close()
        Models.SessionActions.cancelDeleteConfirmation()
        Models.SessionAccess.cancelLeaveConfirmation()
        Models.SessionAccess.cancelRevokeConfirmation()
    }

    function nextDesktopRequestId(suffix) {
        desktopRequestSerial += 1
        return "sidebar." + suffix + "." + desktopRequestSerial
    }

    function browseCreateDirectory() {
        const requestId = nextDesktopRequestId("create.directory")
        directoryPickerRequestId = requestId
        desktopFileError = ""
        Models.DesktopFiles.requestDirectory(
            requestId,
            Models.DesktopFiles.NewSessionWorkingDirectory,
            createDirectory.text)
    }

    function openSessionProject(sessionId) {
        const requestId = nextDesktopRequestId("session.project")
        desktopFileRequestId = requestId
        Models.DesktopFiles.openSessionProject(
            sessionId,
            requestId,
            Models.DesktopFiles.SessionProject)
    }

    Component.onDestruction: {
        if (directoryPickerRequestId.length > 0) {
            Models.DesktopFiles.cancelDirectory(
                directoryPickerRequestId,
                Models.DesktopFiles.NewSessionWorkingDirectory)
        }
    }

    Connections {
        target: Models.SessionActions

        function onSessionCreated() {
            root.createOpen = false
            createName.clear()
            createDirectory.text =
                Models.SessionActions.defaultWorkingDirectory
        }
    }

    Connections {
        target: Models.SessionShareScope

        function onTransitionSucceeded(sessionId) {
            if (root.shareSessionId === sessionId)
                shareDialog.close()
        }
    }

    Connections {
        target: Models.SessionAccess

        function onPresentationContextChanged() {
            accessHandle.clear()
        }
    }

    Connections {
        target: Models.DesktopFiles

        function onDirectoryPicked(requestId, purpose, canonicalDirectory) {
            if (requestId !== root.directoryPickerRequestId
                    || purpose
                        !== Models.DesktopFiles
                            .NewSessionWorkingDirectory)
                return
            root.directoryPickerRequestId = ""
            createDirectory.text = canonicalDirectory
            root.desktopFileError = ""
        }

        function onDirectoryPickCancelled(requestId, purpose) {
            if (requestId !== root.directoryPickerRequestId
                    || purpose
                        !== Models.DesktopFiles
                            .NewSessionWorkingDirectory)
                return
            root.directoryPickerRequestId = ""
        }

        function onPathOpened(requestId, purpose) {
            if (requestId === root.desktopFileRequestId
                    && purpose === Models.DesktopFiles.SessionProject) {
                root.desktopFileRequestId = ""
                root.desktopFileError = ""
            }
        }

        function onOperationFailed(requestId, purpose, errorCode, message) {
            if (requestId === root.directoryPickerRequestId
                    && purpose
                        === Models.DesktopFiles
                            .NewSessionWorkingDirectory) {
                root.directoryPickerRequestId = ""
                root.desktopFileError = message
            } else if (requestId === root.desktopFileRequestId
                    && purpose === Models.DesktopFiles.SessionProject) {
                root.desktopFileRequestId = ""
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: KodosiTheme.surface
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 104

            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: KodosiTheme.spacing5
                anchors.rightMargin: KodosiTheme.spacing5
                anchors.topMargin: KodosiTheme.spacing5
                anchors.bottomMargin: KodosiTheme.spacing4
                spacing: KodosiTheme.spacing2

                KButton {
                    id: newSessionButton
                    objectName: "sidebar.session.new"
                    Accessible.id: objectName
                    Layout.fillWidth: true
                    variant: "primary"
                    iconName: "plus"
                    text: qsTr("New Session")
                    Accessible.name: qsTr("Create session")
                    enabled: !Models.SessionActions.creating
                    onClicked: root.createOpen = !root.createOpen
                }

                KButton {
                    objectName: "sidebar.session.resumeAgentWork"
                    Accessible.id: objectName
                    Layout.fillWidth: true
                    iconName: "history"
                    text: qsTr("Resume Agent Work")
                    Accessible.name: text
                    onClicked: {
                        root.hiddenOpen = true
                        Models.SessionActions.refreshHidden()
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

        AttentionRail {
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: KodosiTheme.spacing5
            Layout.rightMargin: KodosiTheme.spacing4
            Layout.topMargin: KodosiTheme.spacing3
            spacing: KodosiTheme.spacing2

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                PlainLabel {
                    text: qsTr("SESSIONS")
                    color: KodosiTheme.textTertiary
                    font.pixelSize: 9
                    font.weight: Font.DemiBold
                    font.letterSpacing: 1.1
                }
                PlainLabel {
                    text: Models.Sessions.count === 1
                        ? qsTr("1 active record")
                        : qsTr("%1 active records").arg(Models.Sessions.count)
                    color: KodosiTheme.textSecondary
                    font.pixelSize: 10
                }
            }

            KButton {
                objectName: "sidebar.session.hidden"
                Accessible.id: objectName
                compact: true
                variant: "quiet"
                iconName: "eye"
                text: qsTr("%1 hidden").arg(
                    Models.SessionActions.hiddenSessions.count)
                Accessible.name: text
                onClicked: {
                    root.hiddenOpen = !root.hiddenOpen
                    if (root.hiddenOpen)
                        Models.SessionActions.refreshHidden()
                }
            }
        }

        Rectangle {
            visible: root.createOpen || Models.SessionActions.creating
            Layout.fillWidth: true
            implicitHeight: visible ? createForm.implicitHeight + 20 : 0
            color: KodosiTheme.surfaceElevated

            ColumnLayout {
                id: createForm
                anchors.fill: parent
                anchors.margins: 10
                spacing: 6

                PlainLabel {
                    text: qsTr("Start a local agent")
                    color: KodosiTheme.textPrimary
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                }

                KTextField {
                    id: createName
                    objectName: "session.create.name"
                    Accessible.id: objectName
                    Layout.fillWidth: true
                    placeholderText: qsTr("Session name")
                    Accessible.name: placeholderText
                    enabled: !Models.SessionActions.creating
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: KodosiTheme.spacing2

                    KTextField {
                        id: createDirectory
                        objectName: "session.create.directory"
                        Accessible.id: objectName
                        Layout.fillWidth: true
                        text: Models.SessionActions.defaultWorkingDirectory
                        placeholderText: qsTr("Working directory")
                        Accessible.name: placeholderText
                        maximumLength: 4096
                        enabled: !Models.SessionActions.creating
                    }

                    KButton {
                        objectName: "session.create.directory.browse"
                        Accessible.id: objectName
                        text: qsTr("Browse")
                        compact: true
                        Accessible.name:
                            qsTr("Browse for session working directory")
                        enabled: !Models.SessionActions.creating
                            && !Models.DesktopFiles.busy
                        onClicked: root.browseCreateDirectory()
                    }
                }

                RowLayout {
                    KButton {
                        objectName: "session.create.submit"
                        Accessible.id: objectName
                        text: Models.SessionActions.creating
                            ? qsTr("Starting")
                            : qsTr("Start")
                        variant: "directional"
                        iconName: "chevron-right"
                        enabled: !Models.SessionActions.creating
                            && createName.text.trim().length > 0
                            && createDirectory.text.trim().length > 0
                        Accessible.name: text
                        onClicked: Models.SessionActions.create(
                            createName.text,
                            createDirectory.text)
                    }

                    KButton {
                        objectName: "session.create.cancel"
                        Accessible.id: objectName
                        text: qsTr("Cancel")
                        variant: "quiet"
                        enabled: !Models.SessionActions.creating
                        Accessible.name: text
                        onClicked: root.createOpen = false
                    }
                }

                PlainLabel {
                    visible: Models.SessionActions.lastError.length > 0
                    Layout.fillWidth: true
                    text: Models.SessionActions.lastError
                    color: KodosiTheme.danger
                    font.pixelSize: 9
                    wrapMode: Text.Wrap
                }

                Rectangle {
                    visible: root.desktopFileError.length > 0
                    Layout.fillWidth: true
                    implicitHeight: desktopFileErrorRow.implicitHeight + 12
                    color: Qt.rgba(
                        KodosiTheme.danger.r,
                        KodosiTheme.danger.g,
                        KodosiTheme.danger.b,
                        0.08)
                    border.width: 1
                    border.color: KodosiTheme.danger
                    radius: KodosiTheme.radiusSmall

                    RowLayout {
                        id: desktopFileErrorRow
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: KodosiTheme.spacing2

                        PlainLabel {
                            Layout.fillWidth: true
                            text: root.desktopFileError
                            color: KodosiTheme.danger
                            font.pixelSize: 9
                            wrapMode: Text.Wrap
                            Accessible.name: text
                        }

                        KIconButton {
                            objectName: "sidebar.desktopFile.error.dismiss"
                            Accessible.id: objectName
                            glyph: "close"
                            size: 24
                            Accessible.name:
                                qsTr("Dismiss filesystem action error")
                            onClicked: root.desktopFileError = ""
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

        PlainLabel {
            visible: Models.SessionActions.lastError.length > 0
                && !root.createOpen
            Layout.fillWidth: true
            Layout.leftMargin: KodosiTheme.spacing5
            Layout.rightMargin: KodosiTheme.spacing5
            text: Models.SessionActions.lastError
            color: KodosiTheme.danger
            font.pixelSize: 9
            wrapMode: Text.Wrap
        }

        Rectangle {
            visible: root.hiddenOpen
            Layout.fillWidth: true
            Layout.preferredHeight: visible
                ? Math.min(170, hiddenList.contentHeight + 20)
                : 0
            color: KodosiTheme.surfaceElevated

            ListView {
                id: hiddenList
                anchors.fill: parent
                anchors.margins: 8
                model: Models.SessionActions.hiddenSessions
                spacing: 2
                clip: true

                delegate: KItemDelegate {
                    id: hiddenSession
                    required property string sessionId
                    required property string name
                    required property string project
                    required property string owner

                    objectName: "sidebar.session.hidden." + sessionId
                    Accessible.id: objectName
                    width: ListView.view.width
                    height: 44
                    Accessible.name: qsTr("Hidden session %1").arg(name)

                    contentItem: RowLayout {
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            PlainLabel {
                                text: hiddenSession.name
                                color: KodosiTheme.textPrimary
                                font.pixelSize: 10
                                elide: Text.ElideRight
                            }
                            PlainLabel {
                                text: hiddenSession.owner + " · "
                                    + hiddenSession.project
                                color: KodosiTheme.textSecondary
                                font.pixelSize: 9
                                elide: Text.ElideMiddle
                            }
                        }
                        KButton {
                            objectName: "sidebar.session.hidden.show."
                                + hiddenSession.sessionId
                            Accessible.id: objectName
                            text: qsTr("Show")
                            Accessible.name: qsTr("Show %1").arg(
                                hiddenSession.name)
                            onClicked: Models.SessionActions.unhide(
                                hiddenSession.sessionId)
                        }
                    }
                }
            }
        }

        ListView {
            id: sessionList
            objectName: "sidebar.session.list"
            Accessible.id: objectName
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: Models.Sessions
            spacing: 2
            topMargin: KodosiTheme.spacing3
            bottomMargin: KodosiTheme.spacing5

            delegate: KItemDelegate {
                id: sessionRow

                required property int index
                required property string sessionId
                required property string kind
                required property string name
                required property string project
                required property string status
                required property string recovery
                required property string mode
                required property string scope
                required property string roomName
                required property int permissions
                required property bool commandable
                property bool renameOpen: false
                property string renameText

                onSessionIdChanged: {
                    renameOpen = false
                    renameText = ""
                }

                readonly property bool canRename:
                    Models.SessionActions.availabilityRevision >= 0
                    && Models.SessionActions.canRename(sessionId)
                readonly property bool canReopen:
                    Models.SessionActions.availabilityRevision >= 0
                    && Models.SessionActions.canReopen(sessionId)
                readonly property bool canDelete:
                    Models.SessionActions.availabilityRevision >= 0
                    && Models.SessionActions.canDelete(sessionId)
                readonly property bool canOpenRemote:
                    Models.SessionActions.availabilityRevision >= 0
                    && Models.SessionActions.canOpenRemote(sessionId)
                readonly property bool canHide:
                    Models.SessionActions.availabilityRevision >= 0
                    && Models.SessionActions.canHide(sessionId)
                readonly property bool canLeave:
                    Models.SessionAccess.stateRevision >= 0
                    && Models.SessionAccess.canLeave(sessionId)
                readonly property bool leaveNeedsRetry:
                    Models.SessionAccess.stateRevision >= 0
                    && Models.SessionAccess.leaveNeedsRetry(sessionId)
                readonly property string sharePhase:
                    Models.SessionShareScope.stateRevision >= 0
                    ? Models.SessionShareScope.phase(sessionId)
                    : "idle"
                readonly property bool canShare:
                    kind === "local"
                    && (Models.SessionShareScope.canChange(sessionId)
                        || sharePhase !== "idle")

                readonly property string sessionName: name
                readonly property string sessionStatus: status
                readonly property bool selected:
                    root.selectedSessionId === sessionId

                objectName: "sidebar.session." + sessionId
                Accessible.id: objectName
                Accessible.name: name
                Accessible.description:
                    qsTr("%1 session in %2").arg(status).arg(project)
                Accessible.selected: selected
                x: KodosiTheme.spacing3
                width: ListView.view.width - KodosiTheme.spacing3 * 2
                height: 60
                leftPadding: KodosiTheme.spacing5
                rightPadding: KodosiTheme.spacing4
                onClicked: root.activateDelegate(sessionRow)

                function commitRename() {
                    if (Models.SessionActions.rename(
                            sessionId,
                            renameText)) {
                        renameOpen = false
                    }
                }

                contentItem: RowLayout {
                    spacing: KodosiTheme.spacing3

                    Rectangle {
                        Layout.preferredWidth: 7
                        Layout.preferredHeight: 7
                        radius: 4
                        color: sessionRow.status === "active"
                            ? KodosiTheme.success
                            : sessionRow.status === "blocked"
                              ? KodosiTheme.danger
                              : sessionRow.status === "reconnecting"
                                ? KodosiTheme.reconnecting
                                : KodosiTheme.warning
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        PlainLabel {
                            visible: !sessionRow.renameOpen
                            Layout.fillWidth: true
                            text: sessionRow.name
                            color: sessionRow.selected
                                ? KodosiTheme.textPrimary
                                : KodosiTheme.textSecondary
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }

                        KTextField {
                            objectName: "sidebar.session.rename."
                                + sessionRow.sessionId
                            Accessible.id: objectName
                            visible: sessionRow.renameOpen
                            Layout.fillWidth: true
                            text: sessionRow.renameText
                            onTextChanged: sessionRow.renameText = text
                            maximumLength: 128
                            Accessible.name: qsTr("Session name")
                            onAccepted: sessionRow.commitRename()
                            Keys.onEscapePressed: sessionRow.renameOpen = false
                        }

                        PlainLabel {
                            Layout.fillWidth: true
                            text: sessionRow.project
                            color: KodosiTheme.textSecondary
                            opacity: 0.72
                            font.pixelSize: 10
                            elide: Text.ElideMiddle
                        }
                    }

                    PlainLabel {
                        text: sessionRow.mode.substring(0, 1).toUpperCase()
                        color: sessionRow.mode === "autopilot"
                            ? KodosiTheme.success
                            : sessionRow.mode === "plan"
                              ? KodosiTheme.reconnecting
                              : KodosiTheme.textSecondary
                        font.pixelSize: 9
                        font.weight: Font.Bold
                    }

                    KButton {
                        objectName: "sidebar.session.actions."
                            + sessionRow.sessionId
                        Accessible.id: objectName
                        visible: sessionRow.canRename
                            || sessionRow.canReopen
                            || sessionRow.canDelete
                            || sessionRow.canHide
                            || sessionRow.canLeave
                            || sessionRow.leaveNeedsRetry
                            || sessionRow.canShare
                            || sessionRow.selected
                        text: ""
                        iconName: "more"
                        variant: "quiet"
                        Accessible.name: qsTr("More session actions")
                        implicitWidth: 30
                        onClicked: sessionActionsMenu.open()

                        KMenu {
                            id: sessionActionsMenu

                            KMenuItem {
                                objectName: "sidebar.session.openProject."
                                    + sessionRow.sessionId
                                Accessible.id: objectName
                                Accessible.ignored: !visible
                                visible: sessionRow.selected
                                    && sessionActionsMenu.visible
                                    && Models.DesktopFiles
                                        .canOpenSessionProject(
                                            sessionRow.sessionId)
                                text: qsTr("Open Project")
                                onTriggered: root.openSessionProject(
                                    sessionRow.sessionId)
                            }

                            KMenuItem {
                                objectName:
                                    "sidebar.session.projectIntelligence."
                                    + sessionRow.sessionId
                                Accessible.id: objectName
                                text: qsTr("Project Intelligence")
                                enabled: sessionRow.kind === "local"
                                onTriggered:
                                    root.projectIntelligenceRequested(
                                        sessionRow.sessionId)
                            }

                            KMenuItem {
                                objectName: "sidebar.session.renameAction."
                                    + sessionRow.sessionId
                                Accessible.id: objectName
                                text: qsTr("Rename")
                                enabled: sessionRow.canRename
                                onTriggered: {
                                    sessionRow.renameText = sessionRow.name
                                    sessionRow.renameOpen = true
                                }
                            }

                            KMenuItem {
                                objectName: "sidebar.session.reopen."
                                    + sessionRow.sessionId
                                Accessible.id: objectName
                                text: qsTr("Reopen")
                                enabled: sessionRow.canReopen
                                onTriggered: {
                                    root.selectDelegate(sessionRow)
                                    Models.SessionActions.reopen(
                                        sessionRow.sessionId)
                                }
                            }

                            KMenuItem {
                                objectName: "sidebar.session.hide."
                                    + sessionRow.sessionId
                                Accessible.id: objectName
                                text: qsTr("Hide for now")
                                enabled: sessionRow.canHide
                                onTriggered: Models.SessionActions.hide(
                                    sessionRow.sessionId)
                            }

                            KMenuItem {
                                objectName:
                                    "sidebar.session.share." + sessionRow.sessionId
                                Accessible.id: objectName
                                text: sessionRow.sharePhase === "unknown"
                                    || sessionRow.sharePhase === "error"
                                    ? qsTr("Review sharing...")
                                    : qsTr("Share...")
                                Accessible.name: text
                                enabled: sessionRow.canShare
                                onTriggered: root.openShare(sessionRow)
                            }

                            KMenuSeparator {}

                            KMenuItem {
                                objectName: "sidebar.session.leave."
                                    + sessionRow.sessionId
                                Accessible.id: objectName
                                visible: sessionRow.canLeave
                                    || sessionRow.leaveNeedsRetry
                                text: sessionRow.leaveNeedsRetry
                                    ? qsTr("Check leave outcome")
                                    : qsTr("Leave shared session...")
                                enabled: sessionRow.canLeave
                                    || sessionRow.leaveNeedsRetry
                                onTriggered: {
                                    if (sessionRow.leaveNeedsRetry) {
                                        Models.SessionAccess.retryLeave(
                                            sessionRow.sessionId)
                                    } else {
                                        Models.SessionAccess
                                            .requestLeaveConfirmation(
                                                sessionRow.sessionId)
                                    }
                                }
                            }

                            KMenuItem {
                                objectName: "sidebar.session.delete."
                                    + sessionRow.sessionId
                                Accessible.id: objectName
                                text: qsTr("Delete permanently...")
                                enabled: sessionRow.canDelete
                                onTriggered: Models.SessionActions
                                    .requestDeleteConfirmation(
                                        sessionRow.sessionId)
                            }
                        }
                    }
                }

                background: Rectangle {
                    color: sessionRow.selected
                        ? KodosiTheme.surfaceSelected
                        : sessionRow.hovered ? KodosiTheme.surfaceElevated
                                             : "transparent"
                    border.width: sessionRow.activeFocus ? 1 : 0
                    border.color: KodosiTheme.focusRing
                    radius: KodosiTheme.radiusMedium
                }

            }

            PlainLabel {
                anchors.centerIn: parent
                visible: Models.Sessions.count === 0
                text: qsTr("No sessions yet")
                color: KodosiTheme.textSecondary
                font.pixelSize: 12
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 38
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: KodosiTheme.spacing5
                anchors.rightMargin: KodosiTheme.spacing5

                Rectangle {
                    Layout.preferredWidth: 6
                    Layout.preferredHeight: 6
                    radius: 3
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

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 1
                color: KodosiTheme.seam
            }
        }

    }

    KDialog {
        id: shareDialog
        objectName: "sidebar.share.dialog"
        anchors.centerIn: parent
        width: 390
        modal: true
        title: qsTr("Share %1").arg(root.shareSessionName)
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        readonly property string currentScope:
            Models.SessionShareScope.stateRevision >= 0
            ? Models.SessionShareScope.currentScope(root.shareSessionId)
            : ""
        readonly property string currentMissionId:
            Models.SessionShareScope.stateRevision >= 0
            ? Models.SessionShareScope.currentMissionId(root.shareSessionId)
            : ""
        readonly property string mutationPhase:
            Models.SessionShareScope.stateRevision >= 0
            ? Models.SessionShareScope.phase(root.shareSessionId)
            : "idle"
        readonly property bool mutationActive:
            mutationPhase === "pending" || mutationPhase === "accepted"
                || mutationPhase === "unknown"
        readonly property bool canChange:
            Models.SessionShareScope.stateRevision >= 0
            && Models.SessionShareScope.canChange(root.shareSessionId)
        readonly property bool canManageAccess:
            Models.SessionAccess.stateRevision >= 0
            && Models.SessionAccess.canManage(root.shareSessionId)
        readonly property string accessMutationPhase:
            Models.SessionAccess.stateRevision >= 0
            ? Models.SessionAccess.mutationPhase(root.shareSessionId)
            : "idle"

        function scopeLabel(scope) {
            if (scope === "justMe")
                return qsTr("Just Me")
            if (scope === "myDevices")
                return qsTr("My Devices")
            if (scope === "room")
                return qsTr("Mission")
            if (scope === "friends")
                return qsTr("Friends (legacy)")
            return qsTr("Unavailable")
        }

        function accessLevelLabel(level) {
            if (level === "view")
                return qsTr("View")
            if (level === "suggest")
                return qsTr("Suggest")
            if (level === "inject")
                return qsTr("Inject")
            if (level === "approve")
                return qsTr("Approve")
            return qsTr("Unknown")
        }

        onClosed: {
            accessHandle.clear()
            Models.SessionAccess.cancelRevokeConfirmation()
            Models.SessionAccess.clearInspection()
            root.shareSessionId = ""
            root.shareSessionName = ""
        }

        contentItem: ColumnLayout {
            objectName: "sidebar.share.dialog"
            Accessible.id: objectName
            spacing: KodosiTheme.spacing4

            PlainLabel {
                Layout.fillWidth: true
                text: qsTr("Current audience: %1").arg(
                    shareDialog.scopeLabel(shareDialog.currentScope))
                color: KodosiTheme.textSecondary
                wrapMode: Text.Wrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: KodosiTheme.spacing3

                KButton {
                    objectName: "sidebar.share.justMe"
                    Accessible.id: objectName
                    Layout.fillWidth: true
                    text: qsTr("Just Me")
                    Accessible.name: qsTr("Share with just me")
                    enabled: shareDialog.canChange
                        && !shareDialog.mutationActive
                        && shareDialog.currentScope !== "justMe"
                    onClicked: Models.SessionShareScope.setScope(
                        root.shareSessionId,
                        "justMe",
                        "")
                }

                KButton {
                    objectName: "sidebar.share.myDevices"
                    Accessible.id: objectName
                    Layout.fillWidth: true
                    text: qsTr("My Devices")
                    Accessible.name: qsTr("Share with my devices")
                    enabled: shareDialog.canChange
                        && !shareDialog.mutationActive
                        && shareDialog.currentScope !== "myDevices"
                    onClicked: Models.SessionShareScope.setScope(
                        root.shareSessionId,
                        "myDevices",
                        "")
                }
            }

            PlainLabel {
                text: qsTr("Mission")
                color: KodosiTheme.textPrimary
                font.weight: Font.DemiBold
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: KodosiTheme.spacing3

                KComboBox {
                    id: missionChooser
                    objectName: "sidebar.share.mission"
                    Accessible.id: objectName
                    Layout.fillWidth: true
                    model: Models.Missions
                    textRole: "name"
                    valueRole: "missionId"
                    Accessible.name: qsTr("Mission audience")
                    enabled: shareDialog.canChange
                        && !shareDialog.mutationActive
                        && Models.Missions.count > 0
                    displayText: Models.Missions.count > 0
                        ? currentText
                        : qsTr("No Missions available")
                }

                KButton {
                    objectName: "sidebar.share.mission.submit"
                    Accessible.id: objectName
                    text: qsTr("Share")
                    Accessible.name: qsTr("Share with selected Mission")
                    enabled: missionChooser.enabled
                        && String(missionChooser.currentValue).length > 0
                        && !(shareDialog.currentScope === "room"
                            && shareDialog.currentMissionId
                                === String(missionChooser.currentValue))
                    onClicked: Models.SessionShareScope.setScope(
                        root.shareSessionId,
                        "room",
                        String(missionChooser.currentValue))
                }
            }

            RowLayout {
                visible: shareDialog.mutationPhase === "pending"
                    || shareDialog.mutationPhase === "accepted"
                spacing: KodosiTheme.spacing3

                KBusyIndicator {
                    running: parent.visible
                    implicitWidth: 22
                    implicitHeight: 22
                }

                PlainLabel {
                    text: shareDialog.mutationPhase === "accepted"
                        ? qsTr("Audience accepted; waiting for the session...")
                        : qsTr("Updating audience...")
                    color: KodosiTheme.textSecondary
                }
            }

            RowLayout {
                visible: shareDialog.mutationPhase === "unknown"
                    || shareDialog.mutationPhase === "error"
                Layout.fillWidth: true
                spacing: KodosiTheme.spacing3

                PlainLabel {
                    Layout.fillWidth: true
                    text: Models.SessionShareScope.stateRevision >= 0
                        ? Models.SessionShareScope.message(
                            root.shareSessionId)
                        : ""
                    color: KodosiTheme.danger
                    wrapMode: Text.Wrap
                }

                KButton {
                    objectName: "sidebar.share.retry"
                    Accessible.id: objectName
                    text: qsTr("Retry")
                    Accessible.name: qsTr("Retry sharing change")
                    onClicked: Models.SessionShareScope.retry(
                        root.shareSessionId)
                }
            }

            Rectangle {
                Layout.fillWidth: true
                visible: shareDialog.canManageAccess
                    || Models.SessionAccess.loading
                    || Models.SessionAccess.grants.count > 0
                implicitHeight: accessLayout.implicitHeight
                    + KodosiTheme.spacing4 * 2
                radius: KodosiTheme.radiusSmall
                color: KodosiTheme.surfaceElevated
                border.color: KodosiTheme.seam

                ColumnLayout {
                    id: accessLayout
                    anchors.fill: parent
                    anchors.margins: KodosiTheme.spacing4
                    spacing: KodosiTheme.spacing3

                    RowLayout {
                        Layout.fillWidth: true

                        PlainLabel {
                            Layout.fillWidth: true
                            text: qsTr("Explicit access")
                            color: KodosiTheme.textPrimary
                            font.weight: Font.DemiBold
                        }

                        KButton {
                            objectName: "sidebar.share.access.refresh"
                            Accessible.id: objectName
                            text: qsTr("Refresh")
                            Accessible.name: qsTr("Refresh access grants")
                            enabled: !Models.SessionAccess.loading
                            onClicked: Models.SessionAccess.refresh(
                                root.shareSessionId)
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: KodosiTheme.spacing2

                        KTextField {
                            id: accessHandle
                            objectName: "sidebar.share.access.handle"
                            Accessible.id: objectName
                            Layout.fillWidth: true
                            placeholderText: qsTr("Friend handle")
                            maximumLength: 128
                            Accessible.name: qsTr("Friend handle")
                        }

                        KComboBox {
                            id: accessLevel
                            objectName: "sidebar.share.access.level"
                            Accessible.id: objectName
                            model: ["view", "suggest", "inject", "approve"]
                            Accessible.name: qsTr("Access level")
                            displayText: shareDialog.accessLevelLabel(
                                currentText)
                            delegate: KItemDelegate {
                                required property string modelData
                                objectName:
                                    "sidebar.share.access.level.option."
                                    + modelData
                                Accessible.id: objectName
                                width: accessLevel.width
                                text: shareDialog.accessLevelLabel(modelData)
                            }
                        }

                        KButton {
                            objectName: "sidebar.share.access.grant"
                            Accessible.id: objectName
                            text: qsTr("Grant")
                            Accessible.name: qsTr("Grant session access")
                            enabled: shareDialog.canManageAccess
                                && accessHandle.text.trim().length > 0
                                && shareDialog.accessMutationPhase !== "pending"
                                && shareDialog.accessMutationPhase !== "accepted"
                                && shareDialog.accessMutationPhase
                                    !== "acknowledging"
                            onClicked: Models.SessionAccess.grant(
                                root.shareSessionId,
                                accessHandle.text,
                                accessLevel.currentText)
                        }
                    }

                    RowLayout {
                        visible: Models.SessionAccess.loading
                            || Models.SessionAccess.stale
                        spacing: KodosiTheme.spacing2

                        KBusyIndicator {
                            visible: Models.SessionAccess.loading
                            running: visible
                            implicitWidth: 18
                            implicitHeight: 18
                        }

                        PlainLabel {
                            text: Models.SessionAccess.stale
                                ? qsTr("Refreshing; showing stale grants...")
                                : qsTr("Loading access grants...")
                            color: KodosiTheme.textSecondary
                            font.pixelSize: 10
                        }
                    }

                    PlainLabel {
                        Layout.fillWidth: true
                        visible: Models.SessionAccess.error.length > 0
                        text: Models.SessionAccess.error
                        color: KodosiTheme.danger
                        wrapMode: Text.Wrap
                        font.pixelSize: 10
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        visible: shareDialog.accessMutationPhase !== "idle"
                        spacing: KodosiTheme.spacing2

                        KBusyIndicator {
                            visible: shareDialog.accessMutationPhase
                                !== "exhausted"
                                && shareDialog.accessMutationPhase !== "error"
                            running: visible
                            implicitWidth: 18
                            implicitHeight: 18
                        }

                        PlainLabel {
                            Layout.fillWidth: true
                            text: Models.SessionAccess.mutationMessage(
                                root.shareSessionId).length > 0
                                ? Models.SessionAccess.mutationMessage(
                                    root.shareSessionId)
                                : shareDialog.accessMutationPhase
                                    === "reconciling"
                                  ? qsTr(
                                      "Access applied; waiting for the grant list...")
                                  : qsTr("Checking the access change...")
                            color: shareDialog.accessMutationPhase
                                === "exhausted"
                                || shareDialog.accessMutationPhase === "error"
                                ? KodosiTheme.danger
                                : KodosiTheme.textSecondary
                            wrapMode: Text.Wrap
                            font.pixelSize: 10
                        }

                        KButton {
                            objectName: "sidebar.share.access.check"
                            Accessible.id: objectName
                            visible: shareDialog.accessMutationPhase
                                === "exhausted"
                            text: qsTr("Check")
                            Accessible.name: qsTr(
                                "Check latest access change outcome")
                            onClicked:
                                Models.SessionAccess.retryCurrentMutation(
                                    root.shareSessionId)
                        }
                    }

                    PlainLabel {
                        Layout.fillWidth: true
                        visible: !Models.SessionAccess.loading
                            && Models.SessionAccess.grants.count === 0
                            && Models.SessionAccess.error.length === 0
                        text: qsTr("No one has explicit access.")
                        color: KodosiTheme.textSecondary
                        font.pixelSize: 10
                    }

                    ListView {
                        id: accessGrantList
                        objectName: "sidebar.share.access.grants"
                        Accessible.id: objectName
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(
                            contentHeight,
                            168)
                        visible: Models.SessionAccess.grants.count > 0
                        clip: true
                        spacing: KodosiTheme.spacing2
                        model: Models.SessionAccess.grants

                        delegate: Rectangle {
                            id: grantDelegate

                            required property string handle
                            required property string displayName
                            required property string accessLevel
                            required property string grantedAt
                            required property string expiresAt

                            readonly property string actorPhase:
                                Models.SessionAccess.stateRevision >= 0
                                ? Models.SessionAccess.actorMutationPhase(
                                    root.shareSessionId,
                                    handle)
                                : "idle"
                            readonly property string actorMessage:
                                Models.SessionAccess.stateRevision >= 0
                                ? Models.SessionAccess.actorMutationMessage(
                                    root.shareSessionId,
                                    handle)
                                : ""

                            width: ListView.view.width
                            height: grantRow.implicitHeight
                                + KodosiTheme.spacing2 * 2
                            radius: KodosiTheme.radiusSmall
                            color: KodosiTheme.surface

                            RowLayout {
                                id: grantRow
                                anchors.fill: parent
                                anchors.margins: KodosiTheme.spacing2
                                spacing: KodosiTheme.spacing2

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 1

                                    PlainLabel {
                                        Layout.fillWidth: true
                                        text: grantDelegate.displayName.length > 0
                                            ? grantDelegate.displayName
                                            : "@" + grantDelegate.handle
                                        color: KodosiTheme.textPrimary
                                        elide: Text.ElideRight
                                        font.pixelSize: 11
                                    }

                                    PlainLabel {
                                        Layout.fillWidth: true
                                        text: grantDelegate.displayName.length > 0
                                            ? "@" + grantDelegate.handle
                                            : ""
                                        visible: text.length > 0
                                        color: KodosiTheme.textSecondary
                                        font.pixelSize: 9
                                    }

                                    PlainLabel {
                                        Layout.fillWidth: true
                                        text: grantDelegate.actorPhase === "idle"
                                            ? (grantDelegate.expiresAt.length > 0
                                                ? qsTr("Expires %1").arg(
                                                    grantDelegate.expiresAt)
                                                : qsTr("No expiry"))
                                            : grantDelegate.actorMessage.length > 0
                                              ? grantDelegate.actorMessage
                                              : qsTr("Updating access...")
                                        color: grantDelegate.actorPhase
                                            === "exhausted"
                                            || grantDelegate.actorPhase
                                                === "error"
                                            ? KodosiTheme.danger
                                            : KodosiTheme.textSecondary
                                        wrapMode: Text.Wrap
                                        font.pixelSize: 9
                                    }
                                }

                                PlainLabel {
                                    text: shareDialog.accessLevelLabel(
                                        grantDelegate.accessLevel)
                                    color: KodosiTheme.textSecondary
                                    font.pixelSize: 10
                                }

                                KButton {
                                    objectName:
                                        "sidebar.share.access.revoke."
                                        + grantDelegate.handle
                                    Accessible.id: objectName
                                    text: qsTr("Revoke")
                                    Accessible.name: qsTr(
                                        "Revoke access for %1").arg(
                                            grantDelegate.displayName.length > 0
                                            ? grantDelegate.displayName
                                            : grantDelegate.handle)
                                    enabled: shareDialog.canManageAccess
                                        && grantDelegate.actorPhase === "idle"
                                    onClicked:
                                        Models.SessionAccess
                                            .requestRevokeConfirmation(
                                                root.shareSessionId,
                                                grantDelegate.handle)
                                }

                                KButton {
                                    objectName:
                                        "sidebar.share.access.check."
                                        + grantDelegate.handle
                                    Accessible.id: objectName
                                    visible: grantDelegate.actorPhase
                                        === "exhausted"
                                    text: qsTr("Check")
                                    Accessible.name: qsTr(
                                        "Check access change outcome")
                                    onClicked:
                                        Models.SessionAccess.retryMutation(
                                            root.shareSessionId,
                                            grantDelegate.handle)
                                }
                            }
                        }
                    }
                }
            }

            RowLayout {
                Item { Layout.fillWidth: true }

                KButton {
                    objectName: "sidebar.share.close"
                    Accessible.id: objectName
                    text: qsTr("Close")
                    Accessible.name: qsTr("Close sharing")
                    onClicked: shareDialog.close()
                }
            }
        }
    }

    KDialog {
        id: deleteConfirmation
        objectName: "sidebar.session.delete.confirmation"
        anchors.centerIn: parent
        width: 380
        modal: true
        visible:
            Models.SessionActions.deleteConfirmationSessionId.length > 0
        title: qsTr("Delete this session permanently?")
        closePolicy: Popup.NoAutoClose

        contentItem: ColumnLayout {
            objectName: "sidebar.session.delete.confirmation"
            Accessible.id: objectName
            spacing: KodosiTheme.spacing4

            PlainLabel {
                Layout.fillWidth: true
                text: qsTr(
                    "Its local history and recovery record will be removed. This cannot be undone.")
                color: KodosiTheme.textSecondary
                wrapMode: Text.Wrap
            }

            RowLayout {
                Item { Layout.fillWidth: true }

                KButton {
                    objectName: "sidebar.session.delete.cancel"
                    Accessible.id: objectName
                    text: qsTr("Cancel")
                    Accessible.name: text
                    onClicked:
                        Models.SessionActions.cancelDeleteConfirmation()
                }

                KButton {
                    objectName: "sidebar.session.delete.confirm"
                    Accessible.id: objectName
                    text: qsTr("Delete permanently")
                    variant: "danger"
                    Accessible.name: text
                    onClicked: Models.SessionActions.confirmDelete(
                        Models.SessionActions.deleteConfirmationSessionId)
                }
            }
        }
    }

    KDialog {
        id: leaveConfirmation
        objectName: "sidebar.session.leave.confirmation"
        anchors.centerIn: parent
        width: 380
        modal: true
        visible:
            Models.SessionAccess.leaveConfirmationSessionId.length > 0
        title: qsTr("Leave this shared session?")
        closePolicy: Popup.NoAutoClose

        contentItem: ColumnLayout {
            objectName: "sidebar.session.leave.confirmation"
            Accessible.id: objectName
            spacing: KodosiTheme.spacing4

            PlainLabel {
                Layout.fillWidth: true
                text: qsTr(
                    "Your access and local terminal state for this shared session will be removed.")
                color: KodosiTheme.textSecondary
                wrapMode: Text.Wrap
            }

            RowLayout {
                Item { Layout.fillWidth: true }

                KButton {
                    objectName: "sidebar.session.leave.cancel"
                    Accessible.id: objectName
                    text: qsTr("Cancel")
                    Accessible.name: text
                    onClicked:
                        Models.SessionAccess.cancelLeaveConfirmation()
                }

                KButton {
                    objectName: "sidebar.session.leave.confirm"
                    Accessible.id: objectName
                    text: qsTr("Leave session")
                    variant: "danger"
                    Accessible.name: text
                    onClicked: Models.SessionAccess.confirmLeave(
                        Models.SessionAccess.leaveConfirmationSessionId)
                }
            }
        }
    }

    KDialog {
        id: revokeConfirmation
        objectName: "sidebar.share.access.revoke.confirmation"
        anchors.centerIn: parent
        width: 380
        modal: true
        visible:
            Models.SessionAccess.revokeConfirmationSessionId.length > 0
        title: qsTr("Revoke access for @%1?").arg(
            Models.SessionAccess.revokeConfirmationHandle)
        closePolicy: Popup.NoAutoClose

        contentItem: ColumnLayout {
            objectName: "sidebar.share.access.revoke.confirmation"
            Accessible.id: objectName
            spacing: KodosiTheme.spacing4

            PlainLabel {
                Layout.fillWidth: true
                text: qsTr(
                    "This friend will lose explicit access to the session.")
                color: KodosiTheme.textSecondary
                wrapMode: Text.Wrap
            }

            RowLayout {
                Item { Layout.fillWidth: true }

                KButton {
                    objectName: "sidebar.share.access.revoke.cancel"
                    Accessible.id: objectName
                    text: qsTr("Cancel")
                    Accessible.name: text
                    onClicked:
                        Models.SessionAccess.cancelRevokeConfirmation()
                }

                KButton {
                    objectName: "sidebar.share.access.revoke.confirm"
                    Accessible.id: objectName
                    text: qsTr("Revoke access")
                    variant: "danger"
                    Accessible.name: text
                    onClicked: Models.SessionAccess.confirmRevoke(
                        Models.SessionAccess.revokeConfirmationSessionId,
                        Models.SessionAccess.revokeConfirmationHandle)
                }
            }
        }
    }
}
