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
        || leaveConfirmation.visible
    Accessible.id: objectName
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Sessions")

    property string selectedSessionId
    property bool hiddenOpen: false
    property int desktopRequestSerial: 0
    property string directoryPickerRequestId
    property string shareSessionId
    property string shareSessionName
    signal sessionSelectionRequested(string sessionId, bool openRemote)
    signal projectIntelligenceRequested(string sessionId)
    signal resumeAgentWorkRequested()

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
        shareDialog.open()
    }

    function closeConflictingOverlays() {
        hiddenOpen = false
        shareDialog.close()
        Models.SessionAccess.cancelLeaveConfirmation()
    }

    function chooseSessionFolder() {
        desktopRequestSerial += 1
        directoryPickerRequestId =
            "sidebar.create.directory." + desktopRequestSerial
        Models.DesktopFiles.requestDirectory(
            directoryPickerRequestId,
            Models.DesktopFiles.NewSessionWorkingDirectory,
            Models.SessionActions.defaultWorkingDirectory)
    }

    Component.onDestruction: {
        if (directoryPickerRequestId.length > 0) {
            Models.DesktopFiles.cancelDirectory(
                directoryPickerRequestId,
                Models.DesktopFiles.NewSessionWorkingDirectory)
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
        target: Models.DesktopFiles

        function onDirectoryPicked(requestId, purpose, canonicalDirectory) {
            if (requestId !== root.directoryPickerRequestId
                    || purpose
                        !== Models.DesktopFiles
                            .NewSessionWorkingDirectory)
                return
            root.directoryPickerRequestId = ""
            Models.SessionActions.createInDirectory(canonicalDirectory)
        }

        function onDirectoryPickCancelled(requestId, purpose) {
            if (requestId !== root.directoryPickerRequestId
                    || purpose
                        !== Models.DesktopFiles
                            .NewSessionWorkingDirectory)
                return
            root.directoryPickerRequestId = ""
        }

        function onOperationFailed(requestId, purpose) {
            if (requestId === root.directoryPickerRequestId
                    && purpose
                        === Models.DesktopFiles
                            .NewSessionWorkingDirectory)
                root.directoryPickerRequestId = ""
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
            Layout.preferredHeight: 94

            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: KodosiTheme.spacing5
                anchors.rightMargin: KodosiTheme.spacing5
                anchors.topMargin: KodosiTheme.spacing5
                anchors.bottomMargin: KodosiTheme.spacing3
                spacing: KodosiTheme.spacing2

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 34
                    radius: KodosiTheme.radiusSmall
                    color: KodosiTheme.accent

                    RowLayout {
                        anchors.fill: parent
                        spacing: 0

                        KButton {
                            id: newSessionButton
                            objectName: "sidebar.session.new"
                            Accessible.id: objectName
                            Layout.fillWidth: true
                            implicitHeight: 34
                            text: qsTr("New Session")
                            iconName: "plus"
                            variant: "primary"
                            background: Item {}
                            Accessible.name: qsTr("New Session")
                            onClicked: Models.SessionActions.createDefault()
                        }

                        KIconButton {
                            objectName: "sidebar.session.chooseFolder"
                            Accessible.id: objectName
                            glyph: "folder-plus"
                            glyphColor: KodosiTheme.accentForeground
                            size: 34
                            Accessible.name: qsTr("New Session in Folder")
                            enabled: root.directoryPickerRequestId.length === 0
                                && !Models.DesktopFiles.busy
                            onClicked: root.chooseSessionFolder()
                        }
                    }
                }

                KButton {
                    objectName: "sidebar.session.resumeAgentWork"
                    Accessible.id: objectName
                    Layout.fillWidth: true
                    implicitHeight: 34
                    text: qsTr("Resume Agent Work")
                    iconName: "history"
                    Accessible.name: text
                    onClicked: root.resumeAgentWorkRequested()
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

        KButton {
            objectName: "sidebar.session.hidden"
            Accessible.id: objectName
            visible: Models.SessionActions.hiddenSessions.count > 0
            Layout.fillWidth: true
            Layout.leftMargin: KodosiTheme.spacing5
            Layout.rightMargin: KodosiTheme.spacing5
            compact: true
            variant: "quiet"
            contentLeftAligned: true
            iconName: "eye"
            text: qsTr("Hidden sessions")
            Accessible.name: text
            onClicked: {
                root.hiddenOpen = !root.hiddenOpen
                if (root.hiddenOpen)
                    Models.SessionActions.refreshHidden()
            }
        }

        PlainLabel {
            visible: Models.SessionActions.lastError.length > 0
            Layout.fillWidth: true
            Layout.leftMargin: KodosiTheme.spacing5
            Layout.rightMargin: KodosiTheme.spacing5
            text: Models.SessionActions.lastError
            color: KodosiTheme.danger
            font.pixelSize: 9
            wrapMode: Text.Wrap
        }

        Repeater {
            model: Models.SessionActions.pendingCreations

            delegate: RowLayout {
                id: pendingCreation
                required property var modelData

                Layout.fillWidth: true
                Layout.leftMargin: KodosiTheme.spacing5
                Layout.rightMargin: KodosiTheme.spacing5
                spacing: KodosiTheme.spacing2

                KBusyIndicator {
                    implicitWidth: 16
                    implicitHeight: 16
                    running: true
                }

                PlainLabel {
                    Layout.fillWidth: true
                    text: pendingCreation.modelData.name
                    color: KodosiTheme.textSecondary
                    font.pixelSize: 10
                    elide: Text.ElideRight
                    Accessible.name:
                        qsTr("Starting session %1").arg(
                            pendingCreation.modelData.name)
                }
            }
        }

        RowLayout {
            visible: Models.Sessions.count > 0
                && Models.Sessions.authorityState
                    !== Models.Sessions.Loaded
            Layout.fillWidth: true
            Layout.leftMargin: KodosiTheme.spacing5
            Layout.rightMargin: KodosiTheme.spacing3
            spacing: KodosiTheme.spacing2

            PlainLabel {
                Layout.fillWidth: true
                text: Models.Sessions.authorityState
                    === Models.Sessions.Loading
                    ? qsTr("Refreshing sessions...")
                    : qsTr("Sessions may be out of date.")
                color: Models.Sessions.authorityState
                    === Models.Sessions.Failed
                    ? KodosiTheme.warning
                    : KodosiTheme.textSecondary
                font.pixelSize: 9
            }

            KButton {
                visible: Models.Sessions.authorityState
                    === Models.Sessions.Failed
                text: qsTr("Retry")
                compact: true
                onClicked: Models.SessionActions.refresh()
            }
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
                readonly property bool canClose:
                    Models.SessionActions.availabilityRevision >= 0
                    && Models.SessionActions.canClose(sessionId)
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
                readonly property bool needsAttention:
                    Models.Attention.runningCount >= 0
                    && Models.Attention.sessionNeedsAttention(sessionId)

                objectName: "sidebar.session." + sessionId
                Accessible.id: objectName
                Accessible.name: name
                Accessible.description:
                    qsTr("%1 session in %2").arg(status).arg(project)
                Accessible.selected: selected
                x: KodosiTheme.spacing3
                width: ListView.view.width - KodosiTheme.spacing3 * 2
                height: 38
                leftPadding: KodosiTheme.spacing3
                rightPadding: KodosiTheme.spacing3
                topPadding: 7
                bottomPadding: 7
                onClicked: root.activateDelegate(sessionRow)

                function commitRename() {
                    if (renameText.trim() === name) {
                        renameOpen = false
                        return
                    }
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
                        Layout.alignment: Qt.AlignVCenter
                        radius: 4
                        color: sessionRow.needsAttention
                            ? KodosiTheme.warning
                            : sessionRow.status === "active"
                            ? KodosiTheme.success
                            : sessionRow.status === "blocked"
                              ? KodosiTheme.danger
                              : sessionRow.status === "reconnecting"
                                ? KodosiTheme.reconnecting
                                : KodosiTheme.warning
                    }

                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        PlainLabel {
                            visible: !sessionRow.renameOpen
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
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
                            anchors.fill: parent
                            text: sessionRow.renameText
                            onTextChanged: sessionRow.renameText = text
                            maximumLength: 128
                            Accessible.name: qsTr("Session name")
                            onAccepted: sessionRow.commitRename()
                            Keys.onEscapePressed: sessionRow.renameOpen = false
                        }
                    }

                    KIconButton {
                        id: sessionActionsButton
                        objectName: "sidebar.session.actions."
                            + sessionRow.sessionId
                        Accessible.id: objectName
                        visible: sessionRow.canRename
                            || sessionRow.canClose
                            || sessionRow.canHide
                            || sessionRow.canLeave
                            || sessionRow.leaveNeedsRetry
                            || sessionRow.canShare
                        glyph: "more"
                        size: 22
                        opacity: sessionRow.hovered
                            || sessionRow.activeFocus
                            || activeFocus
                            || sessionActionsMenu.opened
                            ? 1
                            : 0
                        enabled: opacity > 0
                        Layout.alignment: Qt.AlignVCenter
                        Accessible.name: qsTr("More session actions")
                        onClicked: sessionActionsMenu.open()

                        KMenu {
                            id: sessionActionsMenu

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
                                objectName: "sidebar.session.close."
                                    + sessionRow.sessionId
                                Accessible.id: objectName
                                text: qsTr("Close Session...")
                                enabled: sessionRow.canClose
                                onTriggered: Models.SessionActions
                                    .requestCloseConfirmation(
                                        sessionRow.sessionId)
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

                        }
                    }
                }

                background: Rectangle {
                    color: sessionRow.selected
                        ? KodosiTheme.surfaceSelected
                        : sessionRow.hovered ? KodosiTheme.surfaceElevated
                                             : KodosiTheme.surface
                    radius: KodosiTheme.radiusMedium
                }

            }

            PlainLabel {
                anchors.centerIn: parent
                visible: Models.Sessions.count === 0
                    && Models.Sessions.authorityState
                        === Models.Sessions.Loaded
                text: qsTr("No sessions yet")
                color: KodosiTheme.textSecondary
                font.pixelSize: 12
            }

            KBusyIndicator {
                anchors.centerIn: parent
                visible: Models.Sessions.count === 0
                    && Models.Sessions.authorityState
                        === Models.Sessions.Loading
                running: visible
            }

            ColumnLayout {
                anchors.centerIn: parent
                visible: Models.Sessions.count === 0
                    && Models.Sessions.authorityState
                        === Models.Sessions.Failed
                spacing: KodosiTheme.spacing2

                PlainLabel {
                    Layout.maximumWidth: 210
                    text: Models.Sessions.authorityError
                    color: KodosiTheme.danger
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                }

                KButton {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Retry")
                    compact: true
                    onClicked: Models.SessionActions.refresh()
                }
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

        onClosed: {
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
}
