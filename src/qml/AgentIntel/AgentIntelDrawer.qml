pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Kodosi.Models 1.0 as Models

KPopover {
    id: root
    objectName: "panel.agentIntel"

    signal openProjectIntelRequested(string sessionId)

    property string sessionId
    property string sessionName
    property string approvalIdentityToken
    property bool approvalMissing: false
    property var intel: ({})
    property var sessionInfo: ({})
    property var approval: ({})
    property bool approvalActionsAvailable:
        !approvalMissing
        && approval.identityToken !== undefined
        && (approvalIdentityToken.length === 0
            || approval.identityToken === approvalIdentityToken)
    property int surfaceIndex: 0

    parent: Overlay.overlay
    width: Math.min(920, parent ? parent.width - 48 : 920)
    height: Math.min(680, parent ? parent.height - 48 : 680)
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    padding: 0
    modal: true
    dim: true
    focus: true
    closePolicy: Popup.CloseOnEscape

    function openForSession(
        sessionId,
        sessionName,
        approvalIdentityToken,
        approvalMissing) {
        Models.AgentCustomAgents.close()
        Models.AgentMemory.close()
        root.sessionId = sessionId
        root.sessionName = sessionName
        root.approvalIdentityToken = approvalIdentityToken || ""
        root.approvalMissing = approvalMissing === true
        root.surfaceIndex = 0
        Models.AgentConversation.close()
        refreshPresentation()
        Models.Steering.inspect(sessionId)
        open()
    }

    function refreshPresentation() {
        intel = Models.AgentSessionIntel.presentationForSession(sessionId)
        sessionInfo = Models.Sessions.presentationForSession(sessionId)
        if (opened && sessionId.length > 0 && !sessionInfo.sessionId) {
            close()
            return
        }
        approval = approvalMissing
            ? ({})
            : approvalIdentityToken.length > 0
            ? Models.PendingPermissions.presentationForIdentityToken(
                approvalIdentityToken)
            : Models.PendingPermissions.presentationForSession(sessionId)
    }

    function showSurface(index) {
        if (index !== 1)
            Models.AgentConversation.close()
        if (index !== 2)
            Models.AgentMemory.close()
        if (index !== 3)
            Models.AgentCustomAgents.close()
        root.surfaceIndex = index
        if (index === 1)
            Models.AgentConversation.inspect(root.sessionId)
        else if (index === 2)
            Models.AgentMemory.inspect(root.sessionId)
        else if (index === 3)
            Models.AgentCustomAgents.inspect(root.sessionId)
    }

    function activityText() {
        if (intel.currentActivity)
            return intel.currentActivity
        switch (intel.lifecycle) {
        case "starting": return qsTr("Starting the agent")
        case "working": return qsTr("Working")
        case "waiting": return qsTr("Waiting for input")
        case "completed": return qsTr("Work completed")
        case "failed": return qsTr("Agent failed")
        case "stopped": return qsTr("Agent stopped")
        case "offline": return qsTr("Agent offline")
        default: return qsTr("Idle")
        }
    }

    function steeringModeText(mode) {
        switch (mode) {
        case "queue": return qsTr("Queue")
        case "steer": return qsTr("Steer")
        case "stopAndSend": return qsTr("Stop & Send")
        default: return qsTr("Unavailable")
        }
    }

    onOpened: {
        refreshPresentation()
        Qt.callLater(function() {
            if (!root.opened)
                return
            if (overviewTab.visible && overviewTab.enabled)
                overviewTab.forceActiveFocus()
            else
                closeButton.forceActiveFocus()
        })
    }
    onClosed: {
        approvalIdentityToken = ""
        approvalMissing = false
        approval = ({})
        surfaceIndex = 0
        Models.AgentConversation.close()
        Models.AgentCustomAgents.close()
        Models.AgentMemory.close()
        Models.Steering.clearInspection()
    }

    Connections {
        target: Models.AgentSessionIntel

        function onModelReset() {
            root.refreshPresentation()
        }

        function onDataChanged() {
            root.refreshPresentation()
        }

        function onHydrationStateChanged() {
            root.refreshPresentation()
        }
    }

    Connections {
        target: Models.PendingPermissions

        function onModelReset() {
            root.refreshPresentation()
        }

        function onDataChanged() {
            root.refreshPresentation()
        }
    }

    Connections {
        target: Models.Sessions

        function onModelReset() {
            root.refreshPresentation()
        }

        function onDataChanged() {
            root.refreshPresentation()
        }

        function onRowsRemoved() {
            root.refreshPresentation()
        }
    }

    Overlay.modal: Rectangle {
        color: KodosiTheme.overlayDim
    }

    background: Rectangle {
        color: KodosiTheme.surface
        radius: KodosiTheme.radiusLarge
    }

    contentItem: ColumnLayout {
        objectName: "panel.agentIntel"
        Accessible.id: objectName
        spacing: 0
        Accessible.role: Accessible.Dialog
        Accessible.name: qsTr("Agent Intelligence for %1").arg(
            root.sessionName.length > 0
                ? root.sessionName
                : root.sessionId)

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 70
            color: KodosiTheme.surfaceElevated
            topLeftRadius: KodosiTheme.radiusLarge
            topRightRadius: KodosiTheme.radiusLarge

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: KodosiTheme.spacing6
                anchors.rightMargin: KodosiTheme.spacing6
                spacing: KodosiTheme.spacing4

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    PlainLabel {
                        text: qsTr("Agent Intelligence")
                        color: KodosiTheme.textPrimary
                        font.pixelSize: 17
                        font.weight: Font.DemiBold
                    }

                    PlainLabel {
                        text: root.sessionName.length > 0
                            ? root.sessionName
                            : root.sessionId
                        color: KodosiTheme.textSecondary
                        font.pixelSize: 11
                        elide: Text.ElideMiddle
                    }
                }

                StatusPill {
                    visible: root.intel.lifecycle !== undefined
                    text: root.intel.lifecycle || qsTr("Loading")
                    tone: root.intel.lifecycle === "working"
                        ? KodosiTheme.success
                        : root.intel.lifecycle === "failed"
                          ? KodosiTheme.danger
                          : KodosiTheme.warning
                }

                KButton {
                    objectName: "panel.agentIntel.project"
                    Accessible.id: objectName
                    text: qsTr("Project")
                    variant: "quiet"
                    iconName: "folder"
                    enabled: root.sessionId.length > 0
                    Accessible.name: qsTr("Open Project Intelligence")
                    onClicked: root.openProjectIntelRequested(root.sessionId)
                }

                KButton {
                    objectName: "panel.agentIntel.interrupt"
                    Accessible.id: objectName
                    text: qsTr("Interrupt")
                    variant: "dangerQuiet"
                    iconName: "interrupt"
                    visible: root.sessionId.length > 0
                    enabled: Models.SessionActions.availabilityRevision >= 0
                        && Models.SessionActions.canInterrupt(root.sessionId)
                    Accessible.name: qsTr("Interrupt this agent")
                    onClicked: Models.SessionActions.interrupt(root.sessionId)
                }

                KButton {
                    id: closeButton
                    objectName: "panel.agentIntel.close"
                    Accessible.id: objectName
                    text: qsTr("Close")
                    variant: "quiet"
                    iconName: "close"
                    Accessible.name: qsTr("Close Agent Intelligence")
                    onClicked: root.close()
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 42
            color: KodosiTheme.surface

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: KodosiTheme.spacing6
                anchors.rightMargin: KodosiTheme.spacing6
                spacing: KodosiTheme.spacing2

                KButton {
                    id: overviewTab
                    objectName: "panel.agentIntel.tab.overview"
                    Accessible.id: objectName
                    text: qsTr("Overview")
                    flat: true
                    checkable: true
                    checked: root.surfaceIndex === 0
                    Accessible.name: qsTr("Show Agent Intelligence overview")
                    onClicked: root.showSurface(0)
                }

                KButton {
                    objectName: "panel.agentIntel.tab.history"
                    Accessible.id: objectName
                    text: qsTr("History")
                    flat: true
                    checkable: true
                    checked: root.surfaceIndex === 1
                    Accessible.name: qsTr("Show conversation history")
                    onClicked: root.showSurface(1)
                }

                KButton {
                    objectName: "panel.agentIntel.tab.memory"
                    Accessible.id: objectName
                    text: qsTr("Memory")
                    flat: true
                    checkable: true
                    checked: root.surfaceIndex === 2
                    Accessible.name: qsTr("Show Project Memory")
                    onClicked: root.showSurface(2)
                }

                KButton {
                    objectName: "panel.agentIntel.tab.agents"
                    Accessible.id: objectName
                    text: qsTr("Agents")
                    flat: true
                    checkable: true
                    checked: root.surfaceIndex === 3
                    Accessible.name: qsTr("Show Custom Agents")
                    onClicked: root.showSurface(3)
                }

                Item {
                    Layout.fillWidth: true
                }
            }
        }

        KScrollView {
            visible: root.surfaceIndex === 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth

            Item {
                width: parent.width
                implicitHeight: Math.max(content.implicitHeight + 48, 420)

                ColumnLayout {
                    id: emptyState
                    anchors.centerIn: parent
                    width: Math.min(440, parent.width - 48)
                    spacing: KodosiTheme.spacing3
                    visible: root.intel.sessionId === undefined

                    KBusyIndicator {
                        Layout.alignment: Qt.AlignHCenter
                        running: visible
                        visible: Models.AgentSessionIntel.hydrationState
                            === Models.AgentSessionIntel.Loading
                    }

                    PlainLabel {
                        Layout.fillWidth: true
                        text: Models.AgentSessionIntel.hydrationState
                            === Models.AgentSessionIntel.Failed
                            ? qsTr("Live intelligence is unavailable")
                            : qsTr("No live intelligence for this session")
                        color: KodosiTheme.textPrimary
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                    }

                    PlainLabel {
                        Layout.fillWidth: true
                        text: Models.AgentSessionIntel.hydrationError.length > 0
                            ? Models.AgentSessionIntel.hydrationError
                            : qsTr("Start Claude or Copilot in this session to see its current activity.")
                        color: KodosiTheme.textSecondary
                        wrapMode: Text.Wrap
                        horizontalAlignment: Text.AlignHCenter
                    }

                    KButton {
                        objectName: "panel.agentIntel.retry"
                        Accessible.id: objectName
                        Layout.alignment: Qt.AlignHCenter
                        visible: Models.AgentSessionIntel.hydrationState
                            === Models.AgentSessionIntel.Failed
                        text: qsTr("Retry")
                        Accessible.name: qsTr("Retry live Agent Intelligence")
                        onClicked: Models.AgentSessionIntel.refresh()
                    }
                }

                RowLayout {
                    id: content
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: KodosiTheme.spacing6
                    spacing: KodosiTheme.spacing5
                    visible: !emptyState.visible

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        spacing: KodosiTheme.spacing4

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 9

                            Rectangle {
                                Layout.preferredWidth: 7
                                Layout.preferredHeight: 7
                                radius: 4
                                color: root.intel.lifecycle === "working"
                                    ? KodosiTheme.accent
                                    : KodosiTheme.success
                            }

                            PlainLabel {
                                Layout.fillWidth: true
                                text: root.activityText()
                                color: KodosiTheme.textPrimary
                                font.pixelSize: 22
                                font.weight: Font.DemiBold
                                wrapMode: Text.Wrap
                            }
                        }

                        SurfaceCard {
                            visible: root.intel.hasAttention === true
                            Layout.fillWidth: true
                            implicitHeight: attentionColumn.implicitHeight + 24

                            ColumnLayout {
                                id: attentionColumn
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 5

                                PlainLabel {
                                    text: qsTr("Needs attention · %1")
                                        .arg(root.intel.attentionKind || "")
                                    color: KodosiTheme.warning
                                    font.pixelSize: 11
                                    font.weight: Font.DemiBold
                                }
                                PlainLabel {
                                    Layout.fillWidth: true
                                    text: root.intel.attentionSummary || ""
                                    color: KodosiTheme.textPrimary
                                    wrapMode: Text.Wrap
                                }
                            }
                        }

                        SurfaceCard {
                            visible: root.approval.identityToken !== undefined
                                || root.approvalIdentityToken.length > 0
                                || root.approvalMissing
                            Layout.fillWidth: true
                            implicitHeight: approvalColumn.implicitHeight + 24

                            ColumnLayout {
                                id: approvalColumn
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 7

                                PlainLabel {
                                    text: root.approvalMissing
                                        ? qsTr("This approval is no longer pending.")
                                        : root.approval.identityToken !== undefined
                                        ? qsTr("Approval · %1")
                                            .arg(root.approval.toolName || "")
                                        : qsTr("This approval is no longer pending.")
                                    color: root.approval.risk === "destructive"
                                        || root.approval.risk === "credential"
                                        ? KodosiTheme.danger
                                        : KodosiTheme.textPrimary
                                    font.weight: Font.DemiBold
                                }

                                PlainLabel {
                                    Layout.fillWidth: true
                                    visible: root.approvalActionsAvailable
                                    text: root.approval.decisionMessage
                                        || root.approval.toolInputSummary
                                        || qsTr("Permission request waiting")
                                    color: KodosiTheme.textSecondary
                                    wrapMode: Text.Wrap
                                }

                                RowLayout {
                                    visible: root.approvalActionsAvailable

                                    KButton {
                                        objectName:
                                            "panel.agentIntel.approval.deny."
                                            + root.approval.identityToken
                                        Accessible.id: objectName
                                        text: qsTr("Deny")
                                        visible: root.approvalActionsAvailable
                                        enabled: root.approvalActionsAvailable
                                            && root.approval.actionable === true
                                        Accessible.name: qsTr("Deny this tool request")
                                        onClicked: {
                                            if (root.approvalActionsAvailable)
                                                Models.PendingPermissions.deny(
                                                    root.approval.identityToken)
                                        }
                                    }
                                    KButton {
                                        objectName:
                                            "panel.agentIntel.approval.allow."
                                            + root.approval.identityToken
                                        Accessible.id: objectName
                                        text: qsTr("Allow")
                                        visible: root.approvalActionsAvailable
                                        enabled: root.approvalActionsAvailable
                                            && root.approval.actionable === true
                                        Accessible.name: qsTr("Allow this tool request")
                                        onClicked: {
                                            if (root.approvalActionsAvailable)
                                                Models.PendingPermissions.approve(
                                                    root.approval.identityToken)
                                        }
                                    }
                                }

                            }
                        }

                        SurfaceCard {
                            visible: root.intel.hasExceptionalState === true
                                || root.intel.sourceDegraded === true
                            Layout.fillWidth: true
                            implicitHeight: interruptionColumn.implicitHeight + 24

                            ColumnLayout {
                                id: interruptionColumn
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 5

                                PlainLabel {
                                    text: qsTr("Runtime notice")
                                    color: KodosiTheme.danger
                                    font.weight: Font.DemiBold
                                }
                                PlainLabel {
                                    Layout.fillWidth: true
                                    text: root.intel.hasExceptionalState
                                        ? (root.intel.exceptionalSummary || "")
                                        : (root.intel.sourceDetail || "")
                                    color: KodosiTheme.textSecondary
                                    wrapMode: Text.Wrap
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.preferredWidth: 1
                        Layout.fillHeight: true
                        color: KodosiTheme.seam
                    }

                    ColumnLayout {
                        Layout.preferredWidth: 340
                        Layout.alignment: Qt.AlignTop
                        spacing: KodosiTheme.spacing4

                        PlainLabel {
                            text: qsTr("SESSION DETAILS")
                            color: KodosiTheme.textSecondary
                            font.pixelSize: 10
                            font.weight: Font.Bold
                            font.letterSpacing: 1.4
                        }

                        GridLayout {
                            columns: 2
                            columnSpacing: KodosiTheme.spacing4
                            rowSpacing: KodosiTheme.spacing3

                            PlainLabel { text: qsTr("Agent"); color: KodosiTheme.textSecondary }
                            PlainLabel {
                                text: root.intel.agentType
                                    + (root.intel.version
                                       ? " " + root.intel.version : "")
                                color: KodosiTheme.textPrimary
                            }
                            PlainLabel { text: qsTr("Model"); color: KodosiTheme.textSecondary }
                            PlainLabel {
                                text: root.intel.model || qsTr("Not reported")
                                color: KodosiTheme.textPrimary
                            }
                            PlainLabel { text: qsTr("Directory"); color: KodosiTheme.textSecondary }
                            PlainLabel {
                                Layout.fillWidth: true
                                text: root.intel.cwd || root.sessionInfo.project || ""
                                color: KodosiTheme.textPrimary
                                elide: Text.ElideMiddle
                            }
                            PlainLabel { text: qsTr("Source"); color: KodosiTheme.textSecondary }
                            PlainLabel {
                                text: root.intel.sourceKind || ""
                                color: KodosiTheme.textPrimary
                            }
                            PlainLabel { text: qsTr("Mode"); color: KodosiTheme.textSecondary }
                            PlainLabel {
                                text: root.sessionInfo.mode || ""
                                color: KodosiTheme.textPrimary
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 1
                            color: KodosiTheme.seam
                        }

                        PlainLabel {
                            text: qsTr("Workers")
                            color: KodosiTheme.textPrimary
                            font.weight: Font.DemiBold
                        }

                        PlainLabel {
                            Layout.fillWidth: true
                            text: qsTr("%1 active · %2 blocked · %3 failed · %4 completed")
                                .arg(root.intel.activeWorkers || 0)
                                .arg(root.intel.blockedWorkers || 0)
                                .arg(root.intel.failedWorkers || 0)
                                .arg(root.intel.completedWorkers || 0)
                            color: KodosiTheme.textSecondary
                            wrapMode: Text.Wrap
                        }

                        PlainLabel {
                            visible: root.intel.hasOutcome === true
                            Layout.fillWidth: true
                            text: root.intel.outcomeSummary || ""
                            color: root.intel.outcomeKind === "failed"
                                ? KodosiTheme.danger
                                : KodosiTheme.success
                            wrapMode: Text.Wrap
                        }

                        PlainLabel {
                            visible: root.intel.hasPendingInteraction === true
                                && root.intel.canFocus === true
                            Layout.fillWidth: true
                            text: root.intel.pendingInteractionSummary
                                || qsTr("Respond in the terminal to continue.")
                            color: KodosiTheme.warning
                            wrapMode: Text.Wrap
                        }
                    }
                }

            }
        }

        Item {
            id: memorySurface
            objectName: "panel.agentIntel.memory"
            Accessible.id: objectName
            visible: root.surfaceIndex === 2
            Layout.fillWidth: true
            Layout.fillHeight: true
            Accessible.role: Accessible.Pane
            Accessible.name: qsTr("Project Memory")

            readonly property bool wide: width >= 620

            GridLayout {
                anchors.fill: parent
                anchors.margins: KodosiTheme.spacing5
                columns: memorySurface.wide ? 2 : 1
                rows: memorySurface.wide ? 1 : 2
                columnSpacing: KodosiTheme.spacing3
                rowSpacing: KodosiTheme.spacing3

                Rectangle {
                    Layout.row: 0
                    Layout.column: 0
                    Layout.fillWidth: !memorySurface.wide
                    Layout.fillHeight: memorySurface.wide
                    Layout.preferredWidth: memorySurface.wide ? 228 : -1
                    Layout.preferredHeight: memorySurface.wide ? -1 : 190
                    color: KodosiTheme.surfaceElevated
                    radius: KodosiTheme.radiusSmall

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: KodosiTheme.spacing3
                        spacing: KodosiTheme.spacing2

                        RowLayout {
                            objectName: "panel.agentIntel.memory.notice"
                            Accessible.id: objectName
                            visible: Models.AgentMemory.error.length > 0
                                && Models.AgentMemory.count > 0
                            Layout.fillWidth: true
                            spacing: KodosiTheme.spacing2

                            PlainLabel {
                                Layout.fillWidth: true
                                text: Models.AgentMemory.error
                                color: KodosiTheme.danger
                                wrapMode: Text.Wrap
                                Accessible.name: text
                            }

                            KButton {
                                objectName: "panel.agentIntel.memory.notice.retry"
                                Accessible.id: objectName
                                text: qsTr("Retry")
                                enabled: !Models.AgentMemory.loading
                                Accessible.name: qsTr("Retry Project Memory inventory")
                                onClicked: Models.AgentMemory.retry()
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            visible: Models.AgentMemory.count === 0

                            ColumnLayout {
                                anchors.centerIn: parent
                                width: Math.min(420, parent.width - 24)
                                spacing: KodosiTheme.spacing3

                                KBusyIndicator {
                                    Layout.alignment: Qt.AlignHCenter
                                    running: Models.AgentMemory.loading
                                    visible: running
                                }

                                PlainLabel {
                                    objectName: "panel.agentIntel.memory.empty"
                                    Accessible.id: objectName
                                    Layout.fillWidth: true
                                    text: Models.AgentMemory.state
                                        === Models.AgentMemory.Failed
                                        ? qsTr("Project Memory is unavailable")
                                        : Models.AgentMemory.state
                                          === Models.AgentMemory.Ineligible
                                          ? qsTr("Project Memory is not available")
                                          : Models.AgentMemory.loading
                                            ? qsTr("Preparing Project Memory")
                                            : qsTr("No memory files")
                                    color: KodosiTheme.textPrimary
                                    font.pixelSize: 15
                                    font.weight: Font.DemiBold
                                    horizontalAlignment: Text.AlignHCenter
                                    wrapMode: Text.Wrap
                                }

                                PlainLabel {
                                    objectName: "panel.agentIntel.memory.status"
                                    Accessible.id: objectName
                                    Layout.fillWidth: true
                                    text: Models.AgentMemory.error.length > 0
                                        ? Models.AgentMemory.error
                                        : Models.AgentMemory.statusMessage.length > 0
                                          ? Models.AgentMemory.statusMessage
                                          : qsTr("Claude has no Project Memory files for this session.")
                                    color: Models.AgentMemory.error.length > 0
                                        ? KodosiTheme.danger
                                        : KodosiTheme.textSecondary
                                    horizontalAlignment: Text.AlignHCenter
                                    wrapMode: Text.Wrap
                                    Accessible.name: text
                                }

                                KButton {
                                    objectName: "panel.agentIntel.memory.retry"
                                    Accessible.id: objectName
                                    visible: Models.AgentMemory.state
                                        === Models.AgentMemory.Failed
                                    Layout.alignment: Qt.AlignHCenter
                                    text: qsTr("Retry")
                                    Accessible.name: qsTr("Retry Project Memory")
                                    onClicked: Models.AgentMemory.retry()
                                }
                            }
                        }

                        ListView {
                            objectName: "panel.agentIntel.memory.list"
                            Accessible.id: objectName
                            visible: Models.AgentMemory.count > 0
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            spacing: 2
                            model: Models.AgentMemory
                            Accessible.role: Accessible.List
                            Accessible.name: qsTr("Project Memory files")

                            delegate: KButton {
                                id: memoryItem
                                required property int index
                                required property string filename
                                required property string kind
                                required property string description

                                objectName: "panel.agentIntel.memory.item."
                                    + memoryItem.filename
                                Accessible.id: objectName
                                width: ListView.view.width
                                height: 52
                                checkable: true
                                checked: Models.AgentMemory.selectedFilename
                                    === memoryItem.filename
                                enabled: !Models.AgentMemory.loading
                                Accessible.name: memoryItem.filename + ", "
                                    + memoryItem.description
                                onClicked: Models.AgentMemory.select(
                                    memoryItem.filename)

                                background: Rectangle {
                                    color: memoryItem.checked
                                        ? KodosiTheme.surface
                                        : memoryItem.hovered
                                          ? KodosiTheme.canvas
                                          : KodosiTheme.surface
                                    radius: KodosiTheme.radiusSmall
                                }

                                contentItem: ColumnLayout {
                                    spacing: 2

                                    PlainLabel {
                                        Layout.fillWidth: true
                                        text: memoryItem.filename
                                        color: memoryItem.checked
                                            ? KodosiTheme.accent
                                            : KodosiTheme.textPrimary
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideMiddle
                                    }

                                    PlainLabel {
                                        Layout.fillWidth: true
                                        text: memoryItem.description
                                        color: KodosiTheme.textSecondary
                                        font.pixelSize: 11
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    objectName: "panel.agentIntel.memory.preview"
                    Accessible.id: objectName
                    Layout.row: memorySurface.wide ? 0 : 1
                    Layout.column: memorySurface.wide ? 1 : 0
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: KodosiTheme.terminal
                    radius: KodosiTheme.radiusSmall

                    Item {
                        anchors.fill: parent
                        visible: Models.AgentMemory.selectedFilename.length === 0

                        ColumnLayout {
                            anchors.centerIn: parent
                            width: Math.min(420, parent.width - 32)
                            spacing: KodosiTheme.spacing3

                            PlainLabel {
                                Layout.fillWidth: true
                                text: qsTr("Select a memory file to preview")
                                color: KodosiTheme.textPrimary
                                font.pixelSize: 15
                                font.weight: Font.DemiBold
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.Wrap
                            }

                            PlainLabel {
                                Layout.fillWidth: true
                                text: qsTr("The preview is loaded only when you select an item.")
                                color: KodosiTheme.textSecondary
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.Wrap
                            }
                        }
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: KodosiTheme.spacing4
                        visible: Models.AgentMemory.selectedFilename.length > 0
                        spacing: KodosiTheme.spacing3

                        PlainLabel {
                            objectName: "panel.agentIntel.memory.preview.title"
                            Accessible.id: objectName
                            Layout.fillWidth: true
                            text: Models.AgentMemory.selectedFilename
                            color: KodosiTheme.textPrimary
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                            elide: Text.ElideMiddle
                            Accessible.name: text
                        }

                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            KBusyIndicator {
                                objectName: "panel.agentIntel.memory.preview.loading"
                                Accessible.id: objectName
                                anchors.centerIn: parent
                                running: Models.AgentMemory.contentLoading
                                visible: running
                            }

                            ColumnLayout {
                                anchors.centerIn: parent
                                width: Math.min(440, parent.width - 24)
                                visible: Models.AgentMemory.contentError.length > 0
                                spacing: KodosiTheme.spacing3

                                PlainLabel {
                                    objectName: "panel.agentIntel.memory.preview.error"
                                    Accessible.id: objectName
                                    Layout.fillWidth: true
                                    text: Models.AgentMemory.contentError
                                    color: KodosiTheme.danger
                                    horizontalAlignment: Text.AlignHCenter
                                    wrapMode: Text.Wrap
                                    Accessible.name: text
                                }

                                KButton {
                                    objectName: "panel.agentIntel.memory.preview.retry"
                                    Accessible.id: objectName
                                    Layout.alignment: Qt.AlignHCenter
                                    text: qsTr("Refresh and retry")
                                    Accessible.name: qsTr("Refresh Project Memory and retry preview")
                                    onClicked: Models.AgentMemory.retry()
                                }
                            }

                            KScrollView {
                                anchors.fill: parent
                                visible: Models.AgentMemory.contentLoaded
                                    && !Models.AgentMemory.contentLoading
                                    && Models.AgentMemory.contentError.length === 0
                                clip: true
                                contentWidth: availableWidth

                                KReadOnlyText {
                                    objectName: "panel.agentIntel.memory.preview.content"
                                    Accessible.id: objectName
                                    width: parent.width
                                    text: Models.AgentMemory.content
                                    color: KodosiTheme.textPrimary
                                    font.family: "monospace"
                                    Accessible.name: Models.AgentMemory.content
                                }
                            }
                        }
                    }
                }
            }
        }

        AgentCustomAgentsSurface {
            visible: root.surfaceIndex === 3
            Layout.fillWidth: true
            Layout.fillHeight: true
        }

        Item {
            objectName: "panel.agentIntel.history"
            Accessible.id: objectName
            visible: root.surfaceIndex === 1
            Layout.fillWidth: true
            Layout.fillHeight: true
            Accessible.role: Accessible.Pane
            Accessible.name: qsTr("Conversation history")

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: KodosiTheme.spacing5
                spacing: KodosiTheme.spacing3

                RowLayout {
                    Layout.fillWidth: true
                    visible: Models.AgentConversation.hasEarlier
                        || Models.AgentConversation.degradedWarning.length > 0
                        || (Models.AgentConversation.error.length > 0
                            && Models.AgentConversation.count > 0)
                    spacing: KodosiTheme.spacing3

                    KBusyIndicator {
                        running: Models.AgentConversation.loading
                        visible: running
                        Layout.preferredWidth: 22
                        Layout.preferredHeight: 22
                    }

                    PlainLabel {
                        objectName: "panel.agentIntel.history.notice"
                        Accessible.id: objectName
                        Layout.fillWidth: true
                        text: Models.AgentConversation.error.length > 0
                            ? Models.AgentConversation.error
                            : Models.AgentConversation.degradedWarning.length > 0
                              ? Models.AgentConversation.degradedWarning
                              : qsTr("Earlier transcript entries are available on demand.")
                        color: Models.AgentConversation.error.length > 0
                            ? KodosiTheme.danger
                            : KodosiTheme.textSecondary
                        wrapMode: Text.Wrap
                        Accessible.name: text
                    }

                    KButton {
                        objectName: "panel.agentIntel.history.loadEarlier"
                        Accessible.id: objectName
                        visible: Models.AgentConversation.hasEarlier
                        text: Models.AgentConversation.error.length > 0
                            ? qsTr("Retry")
                            : qsTr("Load earlier")
                        enabled: !Models.AgentConversation.loading
                        Accessible.name: Models.AgentConversation.error.length > 0
                            ? qsTr("Retry loading earlier conversation entries")
                            : qsTr("Load earlier conversation entries")
                        onClicked: {
                            if (Models.AgentConversation.error.length > 0)
                                Models.AgentConversation.retry()
                            else
                                Models.AgentConversation.loadEarlier()
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: Models.AgentConversation.count === 0

                    ColumnLayout {
                        anchors.centerIn: parent
                        width: Math.min(440, parent.width - 32)
                        spacing: KodosiTheme.spacing3

                        KBusyIndicator {
                            Layout.alignment: Qt.AlignHCenter
                            running: Models.AgentConversation.loading
                            visible: running
                        }

                        PlainLabel {
                            objectName: "panel.agentIntel.history.empty"
                            Accessible.id: objectName
                            Layout.fillWidth: true
                            text: Models.AgentConversation.error.length > 0
                                ? qsTr("Conversation history is unavailable")
                                : Models.AgentConversation.loading
                                  ? qsTr("Loading conversation history")
                                  : qsTr("No conversation entries.")
                            color: KodosiTheme.textPrimary
                            font.pixelSize: 16
                            font.weight: Font.DemiBold
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                        }

                        PlainLabel {
                            objectName: "panel.agentIntel.history.error"
                            Accessible.id: objectName
                            visible: Models.AgentConversation.error.length > 0
                            Layout.fillWidth: true
                            text: Models.AgentConversation.error
                            color: KodosiTheme.textSecondary
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            Accessible.name: text
                        }

                        KButton {
                            objectName: "panel.agentIntel.history.retry"
                            Accessible.id: objectName
                            visible: Models.AgentConversation.error.length > 0
                            Layout.alignment: Qt.AlignHCenter
                            text: qsTr("Retry")
                            Accessible.name: qsTr("Retry conversation history")
                            onClicked: Models.AgentConversation.retry()
                        }
                    }
                }

                ListView {
                    id: historyList
                    objectName: "panel.agentIntel.history.list"
                    Accessible.id: objectName
                    visible: Models.AgentConversation.count > 0
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: KodosiTheme.spacing3
                    model: Models.AgentConversation
                    Accessible.role: Accessible.List
                    Accessible.name: qsTr("Conversation entries")

                    delegate: Rectangle {
                        id: conversationDelegate
                        required property int index
                        required property string role
                        required property string content
                        required property string toolName
                        required property string timestamp

                        width: ListView.view.width
                        height: conversationRow.implicitHeight + 20
                        radius: KodosiTheme.radiusSmall
                        color: KodosiTheme.surfaceElevated
                        Accessible.role: Accessible.ListItem
                        Accessible.name: roleLabel.text + ": "
                            + conversationDelegate.content

                        ColumnLayout {
                            id: conversationRow
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 10
                            spacing: 5

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6

                                PlainLabel {
                                    id: roleLabel
                                    text: {
                                        switch (conversationDelegate.role) {
                                        case "user": return qsTr("User")
                                        case "assistant": return qsTr("Assistant")
                                        case "tool_use": return qsTr("Tool")
                                        default: return qsTr("Entry")
                                        }
                                    }
                                    color: conversationDelegate.role === "user"
                                        ? KodosiTheme.accent
                                        : conversationDelegate.role === "assistant"
                                          ? KodosiTheme.success
                                          : KodosiTheme.textSecondary
                                    font.weight: Font.DemiBold
                                }

                                PlainLabel {
                                    visible: conversationDelegate.toolName.length > 0
                                    text: "· " + conversationDelegate.toolName
                                    color: KodosiTheme.textSecondary
                                    font.family: "monospace"
                                    elide: Text.ElideRight
                                }

                                Item {
                                    Layout.fillWidth: true
                                }

                                PlainLabel {
                                    visible: conversationDelegate.timestamp.length > 0
                                    text: conversationDelegate.timestamp
                                    color: KodosiTheme.textSecondary
                                    font.pixelSize: 10
                                }
                            }

                            KReadOnlyText {
                                objectName: "panel.agentIntel.history.row.content."
                                    + conversationDelegate.index
                                Accessible.id: objectName
                                Layout.fillWidth: true
                                Layout.preferredHeight: contentHeight
                                text: conversationDelegate.content
                                color: KodosiTheme.textPrimary
                                Accessible.name: conversationDelegate.content
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            objectName: "panel.agentIntel.steer"
            Accessible.id: objectName
            visible: root.sessionId.length > 0
            Layout.fillWidth: true
            implicitHeight: visible ? steerFooter.implicitHeight + 18 : 0
            color: KodosiTheme.surfaceElevated
            bottomLeftRadius: Models.SessionActions.lastError.length === 0
                ? KodosiTheme.radiusLarge
                : 0
            bottomRightRadius: bottomLeftRadius

            ColumnLayout {
                id: steerFooter
                anchors.fill: parent
                anchors.leftMargin: KodosiTheme.spacing4
                anchors.rightMargin: KodosiTheme.spacing4
                anchors.topMargin: 9
                anchors.bottomMargin: 9
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    spacing: KodosiTheme.spacing2

                    KTextField {
                        id: steerInput
                        objectName: "panel.agentIntel.steer.input"
                        Accessible.id: objectName
                        Layout.fillWidth: true
                        text: Models.Steering.draftText
                        placeholderText: qsTr("Steer this session")
                        Accessible.name: qsTr("Steering message")
                        onTextEdited: Models.Steering.saveDraft(
                            root.sessionId,
                            text,
                            steerMode.currentValue || Models.Steering.draftMode)
                        onAccepted: Models.Steering.send(root.sessionId)
                    }

                    KComboBox {
                        id: steerMode
                        objectName: "panel.agentIntel.steer.mode"
                        Accessible.id: objectName
                        Layout.preferredWidth: 132
                        model: Models.Steering.availableModes
                        currentIndex: {
                            const modeIndex = Models.Steering.availableModes.indexOf(
                                Models.Steering.draftMode)
                            return modeIndex >= 0 ? modeIndex : (count > 0 ? 0 : -1)
                        }
                        displayText: currentIndex >= 0
                            ? root.steeringModeText(currentValue)
                            : root.steeringModeText("")
                        Accessible.name: qsTr("Steering delivery mode")
                        onActivated: Models.Steering.saveDraft(
                            root.sessionId,
                            steerInput.text,
                            currentValue)
                    }

                    KButton {
                        objectName: "panel.agentIntel.steer.send"
                        Accessible.id: objectName
                        text: qsTr("Send")
                        enabled: Models.Steering.canSend
                        Accessible.name: qsTr("Send steering message")
                        onClicked: Models.Steering.send(root.sessionId)
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: Models.Steering.statusText.length > 0
                        || Models.Steering.error.length > 0
                        || Models.Steering.canRetry
                    spacing: KodosiTheme.spacing3

                    PlainLabel {
                        objectName: "panel.agentIntel.steer.status"
                        Accessible.id: objectName
                        Layout.fillWidth: true
                        text: Models.Steering.error.length > 0
                            ? Models.Steering.error
                            : Models.Steering.statusText
                        color: Models.Steering.error.length > 0
                            ? KodosiTheme.danger
                            : KodosiTheme.textSecondary
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                        Accessible.name: text
                    }

                    KButton {
                        objectName: "panel.agentIntel.steer.cancel"
                        Accessible.id: objectName
                        visible: Models.Steering.canCancel
                        text: qsTr("Cancel")
                        Accessible.name: qsTr("Cancel selected steering request")
                        onClicked: Models.Steering.cancel(root.sessionId)
                    }

                    KButton {
                        objectName: "panel.agentIntel.steer.retry"
                        Accessible.id: objectName
                        visible: Models.Steering.canRetry
                        text: qsTr("Retry")
                        Accessible.name: qsTr("Retry steering reconciliation")
                        onClicked: Models.Steering.retry(root.sessionId)
                    }

                    KButton {
                        objectName: "panel.agentIntel.steer.error.dismiss"
                        Accessible.id: objectName
                        visible: Models.Steering.error.length > 0
                        text: qsTr("Dismiss")
                        Accessible.name: qsTr("Dismiss steering error")
                        onClicked: Models.Steering.clearError(root.sessionId)
                    }
                }
            }
        }

        Rectangle {
            visible: Models.SessionActions.lastError.length > 0
            Layout.fillWidth: true
            implicitHeight: visible ? intelActionError.implicitHeight + 16 : 0
            color: KodosiTheme.surfaceElevated
            bottomLeftRadius: KodosiTheme.radiusLarge
            bottomRightRadius: KodosiTheme.radiusLarge

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: KodosiTheme.spacing4
                anchors.rightMargin: KodosiTheme.spacing4

                PlainLabel {
                    id: intelActionError
                    Layout.fillWidth: true
                    text: Models.SessionActions.lastError
                    color: KodosiTheme.danger
                    wrapMode: Text.Wrap
                    Accessible.name: text
                }

                KButton {
                    objectName: "panel.agentIntel.error.dismiss"
                    Accessible.id: objectName
                    text: qsTr("Dismiss")
                    Accessible.name: qsTr("Dismiss Agent Intelligence error")
                    onClicked: Models.SessionActions.clearError()
                }
            }
        }
    }
}
