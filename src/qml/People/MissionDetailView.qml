pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Kodosi.Models 1.0 as Models

Rectangle {
    id: root

    required property string missionId
    required property string missionName
    property int page: 0
    property bool recipientsOpen: false
    property bool chatPinned: true
    property bool attentionExpanded: false
    property bool taskComposerOpen: false
    property bool archivedExpanded: false
    property bool inviteOpen: false
    property bool interruptArmed: false
    property string focusTerminalError: ""
    property string focusTerminalOperationError: ""
    property string pendingRemovalPresentationId: ""
    property string pendingRemovalName: ""
    property string pendingTransitionTaskId: ""
    property string pendingTransitionTaskTitle: ""
    property string pendingTransitionStatus: ""
    property string pendingTransitionLabel: ""
    property string transitionEvidence: ""
    property bool transitionNeedsEvidence: false
    signal closeRequested()

    readonly property bool focused:
        Models.MissionDetail.selectedCrewPresentationId.length > 0
    readonly property bool focusVisible: page === 2 && focused
    readonly property var steeringPresentation: {
        const revision = Models.Steering.stateRevision
        return Models.Steering.presentationForSession(focusSessionId)
    }
    readonly property string focusSessionId:
        Models.MissionDetail.selectedTerminalSessionId

    objectName: "missions.detail"
    Accessible.id: objectName
    Accessible.role: Accessible.Pane
    Accessible.name: missionName
    Accessible.ignored: !visible
    color: KodosiTheme.canvas

    function selectPage(nextPage) {
        if (focused && nextPage !== 2)
            Models.MissionDetail.clearFocus()
        Models.Missions.setMissionChatPinned(
            missionId,
            nextPage === 0 && chatList.atYEnd)
        page = nextPage
        recipientsOpen = false
        inviteOpen = false
    }

    function submitChat() {
        if (Models.MissionActions.chatCanRetry)
            Models.MissionActions.retryChat()
        else if (Models.MissionActions.chatRecipientPresentationIds.length === 0
                 && Models.MissionDetail.crew.dispatchableAgentCount >= 4)
            broadcastDialog.open()
        else
            Models.MissionActions.sendChat()
    }

    function submitTask() {
        if (Models.MissionActions.taskCreateCanRetry)
            Models.MissionActions.retryTaskCreate()
        else
            Models.MissionActions.submitTaskCreate()
    }

    function assignmentIndex(taskId) {
        const revision = Models.MissionActions.assignmentRevision
        const selected =
            Models.MissionActions.assignmentPresentationForTask(taskId)
        const options = Models.MissionActions.assignmentOptions
        for (let index = 0; index < options.length; ++index) {
            if (String(options[index].presentationId) === selected)
                return index
        }
        return options.length > 0 ? 0 : -1
    }

    function requestTransition(taskId, taskTitle, option) {
        pendingTransitionTaskId = taskId
        pendingTransitionTaskTitle = taskTitle
        pendingTransitionStatus = String(option.status)
        pendingTransitionLabel = String(option.label)
        transitionNeedsEvidence = Boolean(option.requiresEvidence)
        transitionEvidence = ""
        if (Boolean(option.destructive))
            archiveDialog.open()
        else if (transitionNeedsEvidence)
            evidenceDialog.open()
        else {
            Models.MissionActions.transitionTask(
                missionId,
                taskId,
                pendingTransitionStatus,
                "")
            clearPendingTransition()
        }
    }

    function clearPendingTransition() {
        pendingTransitionTaskId = ""
        pendingTransitionTaskTitle = ""
        pendingTransitionStatus = ""
        pendingTransitionLabel = ""
        transitionEvidence = ""
        transitionNeedsEvidence = false
    }

    function updateChatPin() {
        if (!chatList.visible)
            return
        chatPinned = chatList.atYEnd
        Models.Missions.setMissionChatPinned(
            missionId,
            chatPinned)
    }

    function synchronizeFocusTerminal() {
        focusTerminalError = ""
        focusTerminalOperationError = ""
        Models.TerminalSurfaces.detach(focusTerminal)
        if (!focusVisible || focusSessionId.length === 0)
            return
        if (!Models.TerminalSurfaces.bind(focusTerminal, focusSessionId))
            focusTerminalError = qsTr("This session is no longer available.")
    }

    Component.onCompleted: {
        Qt.callLater(function() {
            chatList.positionViewAtEnd()
            root.updateChatPin()
        })
    }
    onMissionIdChanged: {
        chatPinned = true
        Qt.callLater(function() {
            if (root.page !== 0)
                return
            chatList.positionViewAtEnd()
            root.updateChatPin()
        })
    }
    onPageChanged: Qt.callLater(synchronizeFocusTerminal)
    Component.onDestruction: {
        Models.Missions.setMissionChatPinned(missionId, false)
        Models.TerminalSurfaces.detach(focusTerminal)
    }

    Connections {
        target: Models.MissionDetail

        function onFocusPresentationChanged() {
            if (!Models.MissionDetail.selectedCanInterrupt)
                root.interruptArmed = false
            if (!Models.MissionDetail.selectedCanSteer)
                steerEditor.visible = false
        }

        function onFocusChanged() {
            root.interruptArmed = false
            steerEditor.visible = false
            Qt.callLater(root.synchronizeFocusTerminal)
        }
    }

    Connections {
        target: Models.TerminalSurfaces

        function onAttachmentReady(surface, sessionId) {
            if (surface === focusTerminal
                    && sessionId === root.focusSessionId)
                root.focusTerminalError = ""
        }

        function onAttachmentRejected(surface, sessionId, reason) {
            if (surface === focusTerminal
                    && sessionId === root.focusSessionId)
                root.focusTerminalError = reason
        }
    }

    Shortcut {
        sequence: "Escape"
        context: Qt.WindowShortcut
        enabled: root.visible
        onActivated: {
            if (root.interruptArmed) {
                root.interruptArmed = false
            } else if (steerEditor.visible) {
                steerEditor.visible = false
            } else if (root.recipientsOpen) {
                root.recipientsOpen = false
            } else if (root.inviteOpen) {
                root.inviteOpen = false
            } else if (root.focused) {
                Models.MissionDetail.clearFocus()
            } else {
                root.closeRequested()
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            Layout.leftMargin: KodosiTheme.spacing3
            Layout.rightMargin: KodosiTheme.spacing3
            spacing: KodosiTheme.spacing2

            KIconButton {
                objectName: "missions.detail.close"
                Accessible.id: objectName
                glyph: "chevron-left"
                Accessible.name: qsTr("Back to Missions")
                onClicked: root.closeRequested()
            }

            PlainLabel {
                Layout.fillWidth: true
                text: root.missionName
                color: KodosiTheme.textPrimary
                font.pixelSize: 13
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            KButton {
                objectName: "missions.detail.chat"
                Accessible.id: objectName
                text: qsTr("Chat")
                compact: true
                variant: "quiet"
                checkable: true
                checked: root.page === 0 && !root.focused
                onClicked: root.selectPage(0)
            }

            KButton {
                objectName: "missions.detail.tasks"
                Accessible.id: objectName
                text: qsTr("Tasks")
                compact: true
                variant: "quiet"
                checkable: true
                checked: root.page === 1 && !root.focused
                onClicked: root.selectPage(1)
            }

            KIconButton {
                objectName: "missions.detail.people"
                Accessible.id: objectName
                glyph: "people"
                Accessible.name: qsTr("Mission people and agents")
                checkable: true
                checked: root.page === 2
                onClicked: root.selectPage(2)
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: KodosiTheme.seam
        }

        RowLayout {
            visible: Models.MissionDetail.lastError.length > 0
            Layout.fillWidth: true
            Layout.leftMargin: KodosiTheme.spacing3
            Layout.rightMargin: KodosiTheme.spacing3
            Layout.topMargin: KodosiTheme.spacing2
            spacing: KodosiTheme.spacing2

            PlainLabel {
                Layout.fillWidth: true
                text: Models.MissionDetail.lastError
                color: KodosiTheme.danger
                wrapMode: Text.Wrap
            }

            KButton {
                objectName: "missions.detail.error.retry"
                Accessible.id: objectName
                text: qsTr("Retry")
                compact: true
                onClicked: Models.MissionDetail.retry()
            }
        }

        ColumnLayout {
            objectName: "missions.attention"
            Accessible.id: objectName
            visible: !root.focusVisible
                && Models.MissionDetail.attention.count > 0
            Layout.fillWidth: true
            spacing: KodosiTheme.spacing2
            Layout.leftMargin: KodosiTheme.spacing4
            Layout.rightMargin: KodosiTheme.spacing4
            Layout.topMargin: KodosiTheme.spacing2
            Layout.bottomMargin: KodosiTheme.spacing2

            RowLayout {
                Layout.fillWidth: true

                PlainLabel {
                    Layout.fillWidth: true
                    text: qsTr("Needs your attention")
                    color: KodosiTheme.textPrimary
                    font.weight: Font.DemiBold
                }

                KButton {
                    objectName: "missions.attention.more"
                    Accessible.id: objectName
                    visible: Models.MissionDetail.attention.totalCount > 1
                    text: root.attentionExpanded
                        ? qsTr("Show less")
                        : qsTr("Show %1 more").arg(
                            Models.MissionDetail.attention.totalCount - 1)
                    compact: true
                    variant: "quiet"
                    onClicked: root.attentionExpanded =
                        !root.attentionExpanded
                }
            }

            ListView {
                id: attentionList
                objectName: "missions.attention.list"
                Accessible.id: objectName
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(
                    contentHeight,
                    root.attentionExpanded ? 260 : 82)
                model: Models.MissionDetail.attention
                interactive: root.attentionExpanded
                clip: true
                spacing: KodosiTheme.spacing2

                delegate: Rectangle {
                    id: attentionCard

                    required property int index
                    required property string actionPresentationId
                    required property string title
                    required property string summary
                    required property string actionName
                    required property bool canApprove
                    required property bool canDeny
                    required property bool canJump

                    visible: root.attentionExpanded || index === 0
                    width: ListView.view.width
                    height: visible ? attentionRow.implicitHeight + 12 : 0
                    radius: KodosiTheme.radiusSmall
                    color: KodosiTheme.surfaceRaised
                    objectName: "missions.attention.item."
                        + actionPresentationId
                    Accessible.id: objectName

                    RowLayout {
                        id: attentionRow
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: KodosiTheme.spacing2

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1

                            PlainLabel {
                                Layout.fillWidth: true
                                text: attentionCard.title
                                color: KodosiTheme.textPrimary
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }

                            PlainLabel {
                                Layout.fillWidth: true
                                text: attentionCard.summary
                                color: KodosiTheme.textSecondary
                                font.pixelSize: KodosiTheme.fontCaption
                                wrapMode: Text.Wrap
                            }
                        }

                        KButton {
                            visible: attentionCard.actionName === "review"
                            text: qsTr("Review")
                            compact: true
                            onClicked: Models.MissionDetail.attention.review(
                                attentionCard.actionPresentationId)
                        }

                        KButton {
                            visible: attentionCard.canJump
                            text: qsTr("Open")
                            compact: true
                            onClicked: {
                                if (Models.MissionDetail.attention.jump(
                                        attentionCard
                                            .actionPresentationId))
                                    root.page = 2
                            }
                        }

                        KButton {
                            visible: attentionCard.canDeny
                            text: qsTr("Deny")
                            compact: true
                            variant: "dangerQuiet"
                            onClicked: Models.MissionDetail.attention.deny(
                                attentionCard.actionPresentationId)
                        }

                        KButton {
                            visible: attentionCard.canApprove
                            text: attentionCard.actionName === "bulkApprove"
                                ? qsTr("Approve all") : qsTr("Approve")
                            compact: true
                            variant: "primary"
                            onClicked: {
                                if (attentionCard.actionName === "bulkApprove")
                                    Models.MissionDetail.attention.approveAll(
                                        attentionCard.actionPresentationId)
                                else
                                    Models.MissionDetail.attention.approve(
                                        attentionCard.actionPresentationId)
                            }
                        }
                    }
                }
            }

            RowLayout {
                visible: root.attentionExpanded
                    && (Models.MissionDetail.attention.canLoadPrevious
                        || Models.MissionDetail.attention.canLoadMore)
                Layout.alignment: Qt.AlignRight

                KButton {
                    visible: Models.MissionDetail.attention.canLoadPrevious
                    text: qsTr("Previous")
                    compact: true
                    onClicked:
                        Models.MissionDetail.attention.loadPrevious()
                }

                KButton {
                    visible: Models.MissionDetail.attention.canLoadMore
                    text: qsTr("More")
                    compact: true
                    onClicked: Models.MissionDetail.attention.loadMore()
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.fill: parent
                visible: root.page === 0
                Accessible.ignored: !visible
                spacing: 0

                RowLayout {
                    objectName: "missions.chat.stale"
                    Accessible.id: objectName
                    visible: Models.MissionDetail.chatStaleDataVisible
                    Layout.fillWidth: true
                    Layout.leftMargin: KodosiTheme.spacing4
                    Layout.rightMargin: KodosiTheme.spacing4
                    Layout.topMargin: KodosiTheme.spacing2

                    KBusyIndicator {
                        running: visible
                        implicitWidth: 18
                        implicitHeight: 18
                    }

                    PlainLabel {
                        Layout.fillWidth: true
                        text: qsTr("Refreshing; showing the last known conversation")
                        color: KodosiTheme.textSecondary
                        font.pixelSize: KodosiTheme.fontCaption
                    }
                }

                ListView {
                    id: chatList
                    objectName: "missions.chat"
                    Accessible.id: objectName
                    Accessible.name: qsTr("Mission chat")
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: Models.MissionDetail.messages
                    clip: true
                    spacing: KodosiTheme.spacing3
                    leftMargin: KodosiTheme.spacing5
                    rightMargin: KodosiTheme.spacing5
                    topMargin: KodosiTheme.spacing4
                    bottomMargin: KodosiTheme.spacing4

                    onMovementEnded: root.updateChatPin()
                    onContentYChanged: root.updateChatPin()
                    onCountChanged: {
                        if (root.chatPinned || count <= 1) {
                            Qt.callLater(function() {
                                chatList.positionViewAtEnd()
                                root.updateChatPin()
                            })
                        }
                    }

                    delegate: Item {
                        id: message

                        required property string messageId
                        required property string authorDisplay
                        required property string body
                        required property string audienceSummary

                        width: ListView.view.width
                            - KodosiTheme.spacing5 * 2
                        height: messageColumn.implicitHeight + 8
                        objectName: "missions.message." + messageId
                        Accessible.id: objectName
                        Accessible.name: authorDisplay
                        Accessible.description: body

                        ColumnLayout {
                            id: messageColumn
                            anchors.left: parent.left
                            anchors.right: parent.right
                            spacing: 2

                            PlainLabel {
                                text: message.authorDisplay
                                color: KodosiTheme.textSecondary
                                font.pixelSize: KodosiTheme.fontCaption
                                font.weight: Font.DemiBold
                            }

                            PlainLabel {
                                Layout.fillWidth: true
                                text: message.body
                                color: KodosiTheme.textPrimary
                                wrapMode: Text.Wrap
                                textFormat: Text.PlainText
                            }

                            PlainLabel {
                                visible: message.audienceSummary.length > 0
                                text: message.audienceSummary
                                color: KodosiTheme.textTertiary
                                font.pixelSize: KodosiTheme.fontCaption
                            }
                        }
                    }

                    footer: Item {
                        width: chatList.width
                        height: emptyChat.visible || loadingChat.visible
                            ? Math.max(120, chatList.height * 0.5) : 0

                        KBusyIndicator {
                            id: loadingChat
                            anchors.centerIn: parent
                            visible: Models.MissionDetail.chatLoading
                                && Models.MissionDetail.messages.count === 0
                            running: visible
                            Accessible.name: qsTr("Loading Mission chat")
                        }

                        PlainLabel {
                            id: emptyChat
                            objectName: "missions.chat.empty"
                            Accessible.id: objectName
                            anchors.centerIn: parent
                            visible: Models.MissionDetail.chatReady
                                && Models.MissionDetail.messages.count === 0
                            text: qsTr("Start with the next shared step")
                            color: KodosiTheme.textSecondary
                        }
                    }
                }

                Rectangle {
                    visible: root.recipientsOpen
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(
                        260,
                        audienceColumn.implicitHeight)
                    color: KodosiTheme.surfaceRaised

                    ColumnLayout {
                        id: audienceColumn
                        anchors.fill: parent
                        anchors.margins: KodosiTheme.spacing2
                        spacing: 2

                        PlainLabel {
                            Layout.fillWidth: true
                            text: qsTr("Targets receive the dispatch. Every Mission member can read it here.")
                            color: KodosiTheme.textSecondary
                            font.pixelSize: KodosiTheme.fontCaption
                            wrapMode: Text.Wrap
                        }

                        KButton {
                            objectName: "missions.chat.audience.broadcast"
                            Accessible.id: objectName
                            Layout.fillWidth: true
                            text: qsTr("Mission")
                            secondaryText: qsTr("Everyone can read it")
                            variant: "quiet"
                            compact: true
                            checkable: true
                            checked: Models.MissionActions
                                .chatRecipientPresentationIds.length === 0
                            contentLeftAligned: true
                            onClicked:
                                Models.MissionActions.selectChatBroadcast()
                        }

                        ListView {
                            id: crewList
                            Layout.fillWidth: true
                            Layout.preferredHeight: Math.min(
                                contentHeight,
                                150)
                            model: Models.MissionDetail.crew
                            clip: true
                            spacing: 2

                            delegate: KButton {
                                required property string presentationId
                                required property string displayName
                                required property bool canDispatch
                                required property bool dispatchSelected

                                visible: canDispatch
                                width: ListView.view.width
                                height: visible ? implicitHeight : 0
                                text: displayName
                                variant: "quiet"
                                compact: true
                                checkable: true
                                checked: dispatchSelected
                                contentLeftAligned: true
                                onClicked: Models.MissionActions
                                    .toggleChatRecipient(presentationId)
                            }
                        }

                        Repeater {
                            model: Models.MissionActions
                                .chatUnavailableRecipients

                            delegate: RowLayout {
                                id: unavailableRecipient

                                required property string presentationId
                                required property string displayName
                                Layout.fillWidth: true

                                PlainLabel {
                                    Layout.fillWidth: true
                                    text: qsTr("%1 · unavailable").arg(
                                        unavailableRecipient.displayName)
                                    color: KodosiTheme.danger
                                    elide: Text.ElideRight
                                }

                                KButton {
                                    objectName:
                                        "missions.chat.audience.unavailable.remove"
                                    Accessible.id: objectName
                                    text: qsTr("Remove")
                                    compact: true
                                    variant: "dangerQuiet"
                                    onClicked:
                                        Models.MissionActions
                                            .removeChatRecipient(
                                                unavailableRecipient
                                                    .presentationId)
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    visible: Models.MissionActions.chatCanCheck
                        || Models.MissionActions.chatCanDiscard
                        || Models.MissionActions.chatError.length > 0
                    Layout.fillWidth: true
                    Layout.leftMargin: KodosiTheme.spacing3
                    Layout.rightMargin: KodosiTheme.spacing3
                    Layout.topMargin: KodosiTheme.spacing2

                    PlainLabel {
                        Layout.fillWidth: true
                        visible: Models.MissionActions.chatError.length > 0
                        text: Models.MissionActions.chatError
                        color: KodosiTheme.danger
                        elide: Text.ElideRight
                    }

                    KButton {
                        objectName: "missions.chat.check"
                        Accessible.id: objectName
                        visible: Models.MissionActions.chatCanCheck
                        text: qsTr("Check outcome")
                        compact: true
                        onClicked: Models.MissionActions.checkChat()
                    }

                    KButton {
                        objectName: "missions.chat.discard"
                        Accessible.id: objectName
                        visible: Models.MissionActions.chatCanDiscard
                        text: qsTr("Discard")
                        compact: true
                        variant: "quiet"
                        onClicked: Models.MissionActions.discardChat()
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: composer.implicitHeight + 16
                    color: KodosiTheme.surface

                    RowLayout {
                        id: composer
                        anchors.fill: parent
                        anchors.margins: KodosiTheme.spacing3
                        spacing: KodosiTheme.spacing2

                        KButton {
                            objectName: "missions.chat.recipients"
                            Accessible.id: objectName
                            iconName: "people"
                            text: Models.MissionActions.chatRecipientSummary.length > 0
                                ? Models.MissionActions.chatRecipientSummary : qsTr("Mission")
                            Layout.maximumWidth: 200
                            compact: true
                            variant: "quiet"
                            checkable: true
                            checked: root.recipientsOpen
                            Accessible.name: qsTr("Dispatch targets: %1").arg(text)
                            Accessible.description: qsTr("Every Mission member can read this message.")
                            onClicked:
                                root.recipientsOpen = !root.recipientsOpen
                        }

                        KTextArea {
                            id: chatComposer
                            objectName: "missions.chat.draft"
                            Accessible.id: objectName
                            Layout.fillWidth: true
                            Layout.preferredHeight: 64
                            placeholderText: qsTr("Message Mission")
                            text: Models.MissionActions.chatDraftBody
                            maximumLength: 4000
                            wrapMode: TextEdit.Wrap
                            onTextChanged: {
                                if (activeFocus
                                        && text !== Models.MissionActions
                                            .chatDraftBody)
                                    Models.MissionActions.setChatDraftBody(text)
                            }
                            Keys.onPressed: event => {
                                if ((event.modifiers & Qt.ControlModifier)
                                        && (event.key === Qt.Key_Return
                                            || event.key === Qt.Key_Enter)) {
                                    if (Models.MissionActions.chatCanSubmit
                                            || Models.MissionActions
                                                .chatCanRetry)
                                        root.submitChat()
                                    event.accepted = true
                                }
                            }
                        }

                        KIconButton {
                            objectName: "missions.chat.send"
                            Accessible.id: objectName
                            glyph: "send"
                            Accessible.name: qsTr("Send")
                            enabled: Models.MissionActions.chatCanSubmit
                                || Models.MissionActions.chatCanRetry
                            onClicked: root.submitChat()
                        }
                    }
                }
            }

            ColumnLayout {
                anchors.fill: parent
                visible: root.page === 1
                Accessible.ignored: !visible
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: KodosiTheme.spacing3
                    Layout.rightMargin: KodosiTheme.spacing3
                    Layout.topMargin: KodosiTheme.spacing2
                    Layout.bottomMargin: KodosiTheme.spacing2

                    PlainLabel {
                        visible: Models.MissionDetail.tasksStaleDataVisible
                        text: qsTr("Updating; showing last known tasks")
                        color: KodosiTheme.textSecondary
                        font.pixelSize: KodosiTheme.fontCaption
                    }

                    Item { Layout.fillWidth: true }

                    KButton {
                        objectName: "missions.task.archived.toggle"
                        Accessible.id: objectName
                        visible: Models.MissionDetail.tasks.count > 0
                        text: root.archivedExpanded
                            ? qsTr("Hide archived")
                            : qsTr("Archived")
                        compact: true
                        variant: "quiet"
                        onClicked: root.archivedExpanded =
                            !root.archivedExpanded
                    }

                    KButton {
                        objectName: "missions.task.composer.toggle"
                        Accessible.id: objectName
                        text: root.taskComposerOpen
                            ? qsTr("Close") : qsTr("Add task")
                        compact: true
                        variant: "quiet"
                        enabled:
                            Models.MissionActions.canCreateSelectedTasks
                        onClicked: root.taskComposerOpen =
                            !root.taskComposerOpen
                    }
                }

                Rectangle {
                    visible: root.taskComposerOpen
                    Layout.fillWidth: true
                    implicitHeight: taskComposer.implicitHeight + 16
                    color: KodosiTheme.surfaceRaised

                    ColumnLayout {
                        id: taskComposer
                        anchors.fill: parent
                        anchors.margins: KodosiTheme.spacing3
                        spacing: KodosiTheme.spacing2

                        KTextField {
                            objectName: "missions.task.title"
                            Accessible.id: objectName
                            Layout.fillWidth: true
                            placeholderText: qsTr("What needs to happen?")
                            text: Models.MissionActions.taskDraftTitle
                            maximumLength: 200
                            onTextEdited:
                                Models.MissionActions.setTaskDraftTitle(text)
                            onAccepted: {
                                if (Models.MissionActions
                                        .taskCreateCanSubmit
                                        || Models.MissionActions
                                            .taskCreateCanRetry)
                                    root.submitTask()
                            }
                        }

                        KTextArea {
                            objectName: "missions.task.description"
                            Accessible.id: objectName
                            Layout.fillWidth: true
                            Layout.preferredHeight: 64
                            placeholderText:
                                qsTr("Brief or acceptance criteria (optional)")
                            text:
                                Models.MissionActions.taskDraftDescription
                            maximumLength: 4000
                            wrapMode: TextEdit.Wrap
                            onTextChanged: {
                                if (activeFocus
                                        && text !== Models.MissionActions
                                            .taskDraftDescription)
                                    Models.MissionActions
                                        .setTaskDraftDescription(text)
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: KodosiTheme.spacing2

                            KComboBox {
                                id: taskAssignee
                                objectName: "missions.task.assignee"
                                Accessible.id: objectName
                                Layout.fillWidth: true
                                model: Models.MissionActions
                                    .taskDraftAssignmentOptions
                                textRole: "name"
                                valueRole: "presentationId"
                                currentIndex: {
                                    const options = Models.MissionActions
                                        .taskDraftAssignmentOptions
                                    const selected = Models.MissionActions
                                        .taskDraftAssignmentPresentationId
                                    for (let index = 0;
                                         index < options.length;
                                         ++index) {
                                        if (String(options[index]
                                                   .presentationId)
                                                === selected)
                                            return index
                                    }
                                    return options.length > 0 ? 0 : -1
                                }
                                Accessible.name: qsTr("Task assignee")
                                onActivated:
                                    Models.MissionActions
                                        .setTaskDraftAssignment(
                                            String(currentValue || ""))
                            }

                            KCheckBox {
                                objectName: "missions.task.due.toggle"
                                Accessible.id: objectName
                                text: qsTr("Due date")
                                checked:
                                    Models.MissionActions.taskDraftHasDueAt
                                onToggled:
                                    Models.MissionActions
                                        .setTaskDraftHasDueAt(checked)
                            }

                            TaskDueDateField {
                                Layout.preferredWidth: 120
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            PlainLabel {
                                Layout.fillWidth: true
                                visible: Models.MissionActions.taskDraftDueDateError.length > 0
                                    || Models.MissionActions.taskCreateError.length > 0
                                text: Models.MissionActions.taskDraftDueDateError.length > 0
                                    ? Models.MissionActions.taskDraftDueDateError
                                    : Models.MissionActions.taskCreateError
                                color: KodosiTheme.danger
                                wrapMode: Text.Wrap
                            }

                            KButton {
                                objectName: "missions.task.check"
                                Accessible.id: objectName
                                visible: Models.MissionActions.taskCreateCanCheck
                                text: qsTr("Check outcome")
                                compact: true
                                onClicked:
                                    Models.MissionActions.checkTaskCreate()
                            }

                            KButton {
                                objectName: "missions.task.discard"
                                Accessible.id: objectName
                                visible: Models.MissionActions
                                    .taskCreateCanDiscard
                                text: qsTr("Discard")
                                compact: true
                                variant: "dangerQuiet"
                                onClicked:
                                    Models.MissionActions.discardTaskCreate()
                            }

                            KButton {
                                objectName: "missions.task.create"
                                Accessible.id: objectName
                                text: qsTr("Add")
                                variant: "primary"
                                compact: true
                                enabled:
                                    Models.MissionActions.taskCreateCanSubmit
                                    || Models.MissionActions
                                        .taskCreateCanRetry
                                onClicked: root.submitTask()
                            }
                        }
                    }
                }

                RowLayout {
                    visible: !root.taskComposerOpen
                        && Models.MissionActions.taskCreateCanDiscard
                    Layout.fillWidth: true
                    Layout.leftMargin: KodosiTheme.spacing3
                    Layout.rightMargin: KodosiTheme.spacing3

                    PlainLabel {
                        Layout.fillWidth: true
                        text: Models.MissionActions.taskCreateError.length > 0
                            ? Models.MissionActions.taskCreateError
                            : qsTr("Mission draft saved.")
                        color: Models.MissionActions.taskCreateError.length > 0
                            ? KodosiTheme.danger
                            : KodosiTheme.textSecondary
                    }

                    KButton {
                        text: qsTr("Resume")
                        compact: true
                        onClicked: root.taskComposerOpen = true
                    }

                    KButton {
                        text: qsTr("Discard")
                        compact: true
                        variant: "dangerQuiet"
                        onClicked:
                            Models.MissionActions.discardTaskCreate()
                    }
                }

                RowLayout {
                    visible: Models.MissionActions.ledgerError.length > 0
                        || Models.MissionActions.ledgerCanCheck
                    Layout.fillWidth: true
                    Layout.leftMargin: KodosiTheme.spacing3
                    Layout.rightMargin: KodosiTheme.spacing3

                    PlainLabel {
                        Layout.fillWidth: true
                        text: Models.MissionActions.ledgerError
                        color: KodosiTheme.danger
                        wrapMode: Text.Wrap
                    }

                    KButton {
                        objectName: "missions.task.mutation.check"
                        Accessible.id: objectName
                        visible: Models.MissionActions.ledgerCanCheck
                        text: qsTr("Check outcome")
                        compact: true
                        onClicked: Models.MissionActions.checkLedger()
                    }
                }

                ListView {
                    objectName: "missions.tasks"
                    Accessible.id: objectName
                    Accessible.name: qsTr("Mission tasks")
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: Models.MissionDetail.tasks
                    clip: true
                    spacing: KodosiTheme.spacing2
                    leftMargin: KodosiTheme.spacing4
                    rightMargin: KodosiTheme.spacing4
                    topMargin: KodosiTheme.spacing3
                    bottomMargin: KodosiTheme.spacing3

                    delegate: Rectangle {
                        id: task

                        required property string taskId
                        required property string title
                        required property string description
                        required property string status
                        required property string statusLabel
                        required property bool knownStatus
                        required property bool contentUnavailable
                        required property string assignmentDisplay
                        required property date dueAt
                        required property string resultEvidence
                        required property string resultAuthorDisplay

                        readonly property bool archived:
                            status === "Archived"
                        visible: !archived || root.archivedExpanded
                        width: ListView.view.width
                            - KodosiTheme.spacing4 * 2
                        height: visible
                            ? taskContent.implicitHeight + 20 : 0
                        radius: KodosiTheme.radiusSmall
                        color: KodosiTheme.surfaceRaised
                        objectName: "missions.task." + taskId
                        Accessible.id: objectName
                        Accessible.name: title
                        Accessible.description: statusLabel

                        ColumnLayout {
                            id: taskContent
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 5

                            RowLayout {
                                Layout.fillWidth: true

                                PlainLabel {
                                    Layout.fillWidth: true
                                    text: task.title
                                    color: task.knownStatus
                                        ? KodosiTheme.textPrimary
                                        : KodosiTheme.danger
                                    font.weight: Font.DemiBold
                                    wrapMode: Text.Wrap
                                }

                                PlainLabel {
                                    text: task.contentUnavailable ? qsTr("Unavailable on this device") : task.statusLabel
                                    color: task.knownStatus
                                        ? KodosiTheme.textSecondary
                                        : KodosiTheme.danger
                                    font.pixelSize: KodosiTheme.fontCaption
                                }
                            }

                            PlainLabel {
                                visible: task.description.length > 0
                                Layout.fillWidth: true
                                text: task.description
                                color: KodosiTheme.textSecondary
                                wrapMode: Text.Wrap
                            }

                            PlainLabel {
                                visible: !task.knownStatus
                                Layout.fillWidth: true
                                text: qsTr("This task uses a state this client does not recognize. It remains visible and cannot be changed here.")
                                color: KodosiTheme.danger
                                wrapMode: Text.Wrap
                            }

                            PlainLabel {
                                visible: task.resultEvidence.length > 0
                                Layout.fillWidth: true
                                text: task.resultAuthorDisplay.length > 0
                                    ? qsTr("%1 · Result by %2")
                                        .arg(task.resultEvidence)
                                        .arg(task.resultAuthorDisplay)
                                    : task.resultEvidence
                                color: KodosiTheme.textPrimary
                                wrapMode: Text.Wrap
                            }

                            RowLayout {
                                visible: task.knownStatus && !task.contentUnavailable
                                Layout.fillWidth: true
                                spacing: KodosiTheme.spacing2

                                KComboBox {
                                    objectName: "missions.task."
                                        + task.taskId + ".assignee"
                                    Accessible.id: objectName
                                    visible: Models.MissionActions
                                        .canCreateSelectedTasks
                                    Layout.preferredWidth: 170
                                    model: Models.MissionActions
                                        .assignmentOptions
                                    textRole: "name"
                                    valueRole: "presentationId"
                                    currentIndex:
                                        root.assignmentIndex(task.taskId)
                                    Accessible.name:
                                        qsTr("Assignee for %1").arg(
                                            task.title)
                                    onActivated:
                                        Models.MissionActions
                                            .assignTaskByPresentationId(
                                                task.taskId,
                                                String(
                                                    currentValue || ""))
                                }

                                PlainLabel {
                                    visible: !Models.MissionActions
                                        .canCreateSelectedTasks
                                        && task.assignmentDisplay.length > 0
                                    text: task.assignmentDisplay
                                    color: KodosiTheme.textSecondary
                                    font.pixelSize: KodosiTheme.fontCaption
                                }

                                Item { Layout.fillWidth: true }

                                PlainLabel {
                                    visible: task.dueAt
                                        && !isNaN(task.dueAt.getTime())
                                    text: visible
                                        ? qsTr("Due %1").arg(
                                            Qt.formatDate(
                                                task.dueAt,
                                                "d MMM"))
                                        : ""
                                    color: task.dueAt < new Date()
                                        ? KodosiTheme.danger
                                        : KodosiTheme.textSecondary
                                    font.pixelSize: KodosiTheme.fontCaption
                                }
                            }

                            Flow {
                                visible: task.knownStatus && !task.contentUnavailable
                                    && Models.MissionActions
                                        .canManageSelectedMission
                                Layout.fillWidth: true
                                spacing: KodosiTheme.spacing2

                                Repeater {
                                    model: Models.MissionActions
                                        .transitionOptionsForTask(
                                            task.taskId)

                                    delegate: KButton {
                                        required property var modelData
                                        text: String(modelData.label)
                                        compact: true
                                        variant: Boolean(
                                            modelData.destructive)
                                            ? "dangerQuiet" : "quiet"
                                        onClicked: root.requestTransition(
                                            task.taskId,
                                            task.title,
                                            modelData)
                                    }
                                }
                            }
                        }
                    }

                    footer: Item {
                        width: parent ? parent.width : 0
                        height: taskLoading.visible || taskEmpty.visible
                            ? 160 : 0

                        KBusyIndicator {
                            id: taskLoading
                            anchors.centerIn: parent
                            visible: Models.MissionDetail.tasksLoading
                                && Models.MissionDetail.tasks.count === 0
                            running: visible
                            Accessible.name: qsTr("Loading Mission tasks")
                        }

                        PlainLabel {
                            id: taskEmpty
                            objectName: "missions.tasks.empty"
                            Accessible.id: objectName
                            anchors.centerIn: parent
                            visible: Models.MissionDetail.tasksReady
                                && Models.MissionDetail.tasks.count === 0
                            text: qsTr("Nothing queued")
                            color: KodosiTheme.textSecondary
                        }
                    }
                }
            }

            ColumnLayout {
                anchors.fill: parent
                visible: root.page === 2 && !root.focused
                Accessible.ignored: !visible
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: KodosiTheme.spacing3
                    Layout.rightMargin: KodosiTheme.spacing3
                    Layout.topMargin: KodosiTheme.spacing2
                    Layout.bottomMargin: KodosiTheme.spacing2

                    PlainLabel {
                        Layout.fillWidth: true
                        text: qsTr("Crew")
                        color: KodosiTheme.textPrimary
                        font.weight: Font.DemiBold
                    }

                    KButton {
                        objectName: "missions.people.invite"
                        Accessible.id: objectName
                        visible: Models.MissionActions
                            .canManageSelectedMission
                        text: root.inviteOpen
                            ? qsTr("Close") : qsTr("Invite")
                        compact: true
                        variant: "quiet"
                        onClicked: root.inviteOpen = !root.inviteOpen
                    }
                }

                Rectangle {
                    visible: root.inviteOpen
                    Layout.fillWidth: true
                    implicitHeight: inviteList.contentHeight > 0
                        ? Math.min(inviteList.contentHeight + 12, 190)
                        : inviteEmpty.implicitHeight + 20
                    color: KodosiTheme.surfaceRaised

                    ListView {
                        id: inviteList
                        anchors.fill: parent
                        anchors.margins: 6
                        model: Models.MissionActions.inviteCandidates
                        clip: true
                        spacing: 2

                        delegate: RowLayout {
                            id: inviteCandidate

                            required property string candidateId
                            required property string displayName
                            required property string handle
                            width: ListView.view.width
                            height: 42

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 1

                                PlainLabel {
                                    Layout.fillWidth: true
                                    text: inviteCandidate.displayName
                                    color: KodosiTheme.textPrimary
                                    elide: Text.ElideRight
                                }

                                PlainLabel {
                                    text: qsTr("@%1").arg(
                                        inviteCandidate.handle)
                                    color: KodosiTheme.textSecondary
                                    font.pixelSize: KodosiTheme.fontCaption
                                }
                            }

                            KButton {
                                objectName:
                                    "missions.people.invite.candidate"
                                Accessible.id: objectName
                                text: qsTr("Invite")
                                compact: true
                                onClicked: {
                                    if (Models.MissionActions
                                            .inviteFriendCandidate(
                                                inviteCandidate.candidateId))
                                        root.inviteOpen = false
                                }
                            }
                        }
                    }

                    PlainLabel {
                        id: inviteEmpty
                        anchors.centerIn: parent
                        visible: Models.MissionActions
                            .inviteCandidates.length === 0
                        text: Models.People.friendCount === 0
                            ? qsTr("Add someone as a friend first.")
                            : qsTr("All friends are already members or invited.")
                        color: KodosiTheme.textSecondary
                    }
                }

                ListView {
                    objectName: "missions.people"
                    Accessible.id: objectName
                    Accessible.name: qsTr("Mission people and agents")
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.leftMargin: KodosiTheme.spacing4
                    Layout.rightMargin: KodosiTheme.spacing4
                    Layout.bottomMargin: KodosiTheme.spacing4
                    model: Models.MissionDetail.crew
                    clip: true
                    spacing: 2

                    delegate: RowLayout {
                        required property string presentationId
                        required property string kindName
                        required property string displayName
                        required property string secondaryLabel
                        required property string status
                        required property int attentionCount
                        required property string deliveryState
                        required property string deliveryDetail

                        width: ListView.view.width
                        height: 48
                        spacing: KodosiTheme.spacing2
                        objectName: "missions.crew." + presentationId
                        Accessible.id: objectName

                        KButton {
                            Layout.fillWidth: true
                            text: parent.displayName
                            secondaryText: parent.kindName === "member"
                                ? parent.secondaryLabel
                                : parent.deliveryState.length > 0
                                    ? parent.deliveryState
                                    : parent.status
                            variant: "quiet"
                            contentLeftAligned: true
                            showLeadingDot: parent.attentionCount > 0
                            leadingDotColor: KodosiTheme.warning
                            enabled: parent.kindName === "agent"
                            onClicked: Models.MissionDetail.selectCrew(
                                parent.presentationId)
                        }

                        KButton {
                            objectName: "missions.crew."
                                + parent.presentationId + ".remove"
                            Accessible.id: objectName
                            visible: parent.kindName === "member"
                                && Models.MissionActions
                                    .canRemoveMemberByPresentationId(
                                        parent.presentationId)
                            text: qsTr("Remove")
                            compact: true
                            variant: "dangerQuiet"
                            onClicked: {
                                root.pendingRemovalPresentationId =
                                    parent.presentationId
                                root.pendingRemovalName =
                                    parent.displayName
                                removeMemberDialog.open()
                            }
                        }
                    }

                    footer: Item {
                        width: parent ? parent.width : 0
                        height: crewEmpty.visible ? 140 : 0

                        PlainLabel {
                            id: crewEmpty
                            anchors.centerIn: parent
                            visible: Models.MissionDetail.membersReady
                                && Models.MissionDetail.crew.count === 0
                            text: qsTr("No agents or people are attached")
                            color: KodosiTheme.textSecondary
                        }
                    }
                }
            }

            ColumnLayout {
                anchors.fill: parent
                visible: root.focusVisible
                Accessible.ignored: !visible
                spacing: 0
                objectName: "missions.focus.actions"
                Accessible.id: objectName

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 44
                    Layout.leftMargin: KodosiTheme.spacing3
                    Layout.rightMargin: KodosiTheme.spacing3

                    PlainLabel {
                        Layout.fillWidth: true
                        text: Models.MissionDetail
                            .selectedSessionDisplayName
                        color: KodosiTheme.textPrimary
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }

                    PlainLabel {
                        text: Models.MissionDetail.selectedDeliveryState
                        color: Models.MissionDetail.selectedDeliveryState
                            === "failed"
                            ? KodosiTheme.danger
                            : KodosiTheme.textSecondary
                        font.pixelSize: KodosiTheme.fontCaption
                    }

                    KIconButton {
                        objectName: "missions.focus.close"
                        Accessible.id: objectName
                        glyph: "close"
                        Accessible.name: qsTr("Close agent focus")
                        onClicked: Models.MissionDetail.clearFocus()
                    }
                }

                Item {
                    objectName: "missions.focus.terminal.surface"
                    Accessible.id: objectName
                    Accessible.role: Accessible.Pane
                    Accessible.name: qsTr("%1 terminal").arg(
                        Models.MissionDetail.selectedSessionDisplayName)
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    Models.TerminalView {
                        id: focusTerminal
                        objectName: "missions.focus.terminal.native"
                        Accessible.id: objectName
                        anchors.fill: parent
                        anchors.topMargin: focusViewportControls.topInset
                        anchors.rightMargin: focusViewportControls.scrollInset
                        anchors.bottomMargin: focusViewportControls.scrollInset
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
                        focusedSizeAuthority: true

                        onTerminalError: message =>
                            root.focusTerminalError = message
                        onOperationError: message =>
                            root.focusTerminalOperationError = message
                        onTerminalClosed: root.focusTerminalError =
                            qsTr("The terminal session has ended.")
                    }

                    TerminalViewportControls {
                        id: focusViewportControls
                        anchors.fill: parent
                        terminalView: focusTerminal
                        identifier: "missions.focus.viewport"
                    }

                    ColumnLayout {
                        anchors.centerIn: parent
                        width: Math.min(360, parent.width - 24)
                        visible: !focusTerminal.terminalReady
                            || root.focusTerminalError.length > 0
                        spacing: KodosiTheme.spacing3

                        KBusyIndicator {
                            visible: root.focusTerminalError.length === 0
                            running: visible
                            Layout.alignment: Qt.AlignHCenter
                        }

                        PlainLabel {
                            Layout.fillWidth: true
                            text: root.focusTerminalError.length > 0
                                ? qsTr("Terminal unavailable")
                                : qsTr("Connecting terminal")
                            color: KodosiTheme.textPrimary
                            horizontalAlignment: Text.AlignHCenter
                        }

                        PlainLabel {
                            visible: root.focusTerminalError.length > 0
                            Layout.fillWidth: true
                            text: root.focusTerminalError
                            color: KodosiTheme.textSecondary
                            wrapMode: Text.Wrap
                            horizontalAlignment: Text.AlignHCenter
                        }

                        KButton {
                            objectName: "missions.focus.terminal.retry"
                            Accessible.id: objectName
                            visible: root.focusTerminalError.length > 0
                            Layout.alignment: Qt.AlignHCenter
                            text: qsTr("Retry terminal")
                            onClicked: {
                                root.focusTerminalError = ""
                                if (!Models.TerminalSurfaces.retry(
                                        focusTerminal,
                                        root.focusSessionId))
                                    root.focusTerminalError = qsTr(
                                        "This session is no longer available.")
                            }
                        }
                    }

                    Rectangle {
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.right: parent.right
                        visible:
                            root.focusTerminalOperationError.length > 0
                        implicitHeight: focusTerminalErrorRow.implicitHeight
                            + 10
                        color: KodosiTheme.surfaceRaised

                        RowLayout {
                            id: focusTerminalErrorRow
                            anchors.fill: parent
                            anchors.leftMargin: KodosiTheme.spacing3
                            anchors.rightMargin: KodosiTheme.spacing2

                            PlainLabel {
                                Layout.fillWidth: true
                                text:
                                    root.focusTerminalOperationError
                                color: KodosiTheme.danger
                                wrapMode: Text.Wrap
                            }

                            KIconButton {
                                glyph: "close"
                                Accessible.name:
                                    qsTr("Dismiss terminal error")
                                onClicked:
                                    root.focusTerminalOperationError = ""
                            }
                        }
                    }
                }

                Rectangle {
                    id: steerEditor
                    visible: false
                    Layout.fillWidth: true
                    implicitHeight: visible
                        ? steerColumn.implicitHeight + 16 : 0
                    color: KodosiTheme.surfaceRaised

                    ColumnLayout {
                        id: steerColumn
                        anchors.fill: parent
                        anchors.margins: KodosiTheme.spacing3
                        spacing: KodosiTheme.spacing2

                        RowLayout {
                            Layout.fillWidth: true

                            KTextArea {
                                objectName:
                                    "missions.focus.steer.text"
                                Accessible.id: objectName
                                Layout.fillWidth: true
                                Layout.preferredHeight: 58
                                text: root.steeringPresentation.draftText
                                placeholderText:
                                    qsTr("Next instruction")
                                maximumLength: 16384
                                wrapMode: TextEdit.Wrap
                                onTextChanged: {
                                    if (activeFocus
                                            && text
                                                !== root.steeringPresentation.draftText)
                                        Models.Steering.saveDraft(
                                            root.focusSessionId,
                                            text,
                                            "steer")
                                }
                                Keys.onPressed: event => {
                                    if ((event.modifiers
                                            & Qt.ControlModifier)
                                            && (event.key
                                                === Qt.Key_Return
                                                || event.key
                                                === Qt.Key_Enter)) {
                                        if (root.steeringPresentation.canSend)
                                            Models.MissionDetail.steerFocused(
                                                text)
                                        event.accepted = true
                                    }
                                }
                            }

                            KButton {
                                objectName:
                                    "missions.focus.steer.send"
                                Accessible.id: objectName
                                text: qsTr("Steer")
                                compact: true
                                variant: "primary"
                                enabled: root.steeringPresentation.canSend
                                onClicked:
                                    Models.MissionDetail.steerFocused(
                                        root.steeringPresentation.draftText)
                            }
                        }

                        RowLayout {
                            visible:
                                root.steeringPresentation.statusText.length > 0
                                || root.steeringPresentation.error.length > 0
                                || root.steeringPresentation.canRetry
                                || root.steeringPresentation.canCancel
                            Layout.fillWidth: true

                            PlainLabel {
                                Layout.fillWidth: true
                                text: root.steeringPresentation.error.length > 0
                                    ? root.steeringPresentation.error
                                    : root.steeringPresentation.statusText
                                color: root.steeringPresentation.error.length > 0
                                    ? KodosiTheme.danger
                                    : KodosiTheme.textSecondary
                                wrapMode: Text.Wrap
                            }

                            KButton {
                                objectName:
                                    "missions.focus.steer.cancel"
                                Accessible.id: objectName
                                visible: root.steeringPresentation.canCancel
                                text: qsTr("Cancel")
                                compact: true
                                onClicked: Models.Steering.cancel(
                                    root.focusSessionId)
                            }

                            KButton {
                                objectName:
                                    "missions.focus.steer.retry"
                                Accessible.id: objectName
                                visible: root.steeringPresentation.canRetry
                                text: qsTr("Retry")
                                compact: true
                                onClicked: Models.Steering.retry(
                                    root.focusSessionId)
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: KodosiTheme.spacing3
                    Layout.rightMargin: KodosiTheme.spacing3
                    Layout.topMargin: KodosiTheme.spacing2
                    Layout.bottomMargin: KodosiTheme.spacing2
                    spacing: KodosiTheme.spacing2

                    KButton {
                        objectName: "missions.focus.terminal"
                        Accessible.id: objectName
                        visible: Models.MissionDetail
                            .selectedCanOpenFullTerminal
                        text: qsTr("Open full terminal")
                        compact: true
                        onClicked: Models.MissionDetail
                            .openFocusedFullTerminal()
                    }

                    KButton {
                        objectName: "missions.focus.dispatch"
                        Accessible.id: objectName
                        visible: Models.MissionDetail
                            .selectedCanToggleDispatch
                        text: Models.MissionDetail
                            .selectedDispatchSelected
                            ? qsTr("Remove from dispatch")
                            : qsTr("Add to dispatch")
                        compact: true
                        onClicked: Models.MissionDetail
                            .toggleFocusedDispatch()
                    }

                    KButton {
                        objectName: "missions.focus.steer"
                        Accessible.id: objectName
                        visible: Models.MissionDetail.selectedCanSteer
                        text: qsTr("Steer")
                        compact: true
                        checkable: true
                        checked: steerEditor.visible
                        onClicked: {
                            if (!steerEditor.visible)
                                Models.Steering.rehydrate(root.focusSessionId)
                            steerEditor.visible = !steerEditor.visible
                        }
                    }

                    Item { Layout.fillWidth: true }

                    KButton {
                        objectName: "missions.focus.interrupt"
                        Accessible.id: objectName
                        visible:
                            Models.MissionDetail.selectedCanInterrupt
                        text: root.interruptArmed
                            ? qsTr("Confirm interrupt")
                            : qsTr("Interrupt")
                        compact: true
                        variant: root.interruptArmed
                            ? "danger" : "dangerQuiet"
                        onClicked: {
                            if (!root.interruptArmed) {
                                root.interruptArmed = true
                                return
                            }
                            if (Models.MissionDetail.interruptFocused())
                                root.interruptArmed = false
                        }
                    }
                }

                PlainLabel {
                    visible: Models.MissionDetail
                        .selectedDeliveryDetail.length > 0
                    Layout.fillWidth: true
                    Layout.leftMargin: KodosiTheme.spacing3
                    Layout.rightMargin: KodosiTheme.spacing3
                    Layout.bottomMargin: KodosiTheme.spacing2
                    text: Models.MissionDetail.selectedDeliveryDetail
                    color: Models.MissionDetail.selectedDeliveryState
                        === "failed"
                        ? KodosiTheme.danger
                        : KodosiTheme.textSecondary
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    KDialog {
        id: broadcastDialog
        objectName: "missions.chat.broadcast.confirmation"
        anchors.centerIn: parent
        width: Math.min(460, root.width - 32)
        title: qsTr("Send to everyone in this Mission?")

        contentItem: ColumnLayout {
            spacing: KodosiTheme.spacing3

            PlainLabel {
                Layout.fillWidth: true
                text: qsTr("This dispatch will reach %1 attached agents and remain visible to every Mission member.")
                    .arg(Models.MissionDetail.crew.dispatchableAgentCount)
                color: KodosiTheme.textSecondary
                wrapMode: Text.Wrap
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight

                KButton {
                    text: qsTr("Review audience")
                    variant: "quiet"
                    onClicked: {
                        broadcastDialog.close()
                        root.recipientsOpen = true
                    }
                }

                KButton {
                    objectName: "missions.chat.broadcast.confirm"
                    Accessible.id: objectName
                    text: qsTr("Send broadcast")
                    variant: "primary"
                    onClicked: {
                        broadcastDialog.close()
                        Models.MissionActions.sendChat()
                    }
                }
            }
        }
    }

    KDialog {
        id: removeMemberDialog
        objectName: "missions.member.remove.confirmation"
        anchors.centerIn: parent
        width: Math.min(440, root.width - 32)
        title: qsTr("Remove this member from the Mission?")

        contentItem: ColumnLayout {
            spacing: KodosiTheme.spacing3

            PlainLabel {
                Layout.fillWidth: true
                text: qsTr("%1 will lose Mission access and active session permissions.")
                    .arg(root.pendingRemovalName)
                color: KodosiTheme.textSecondary
                wrapMode: Text.Wrap
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight

                KButton {
                    text: qsTr("Cancel")
                    variant: "quiet"
                    onClicked: removeMemberDialog.close()
                }

                KButton {
                    objectName: "missions.member.remove.confirm"
                    Accessible.id: objectName
                    text: qsTr("Remove member")
                    variant: "danger"
                    onClicked: {
                        Models.MissionActions
                            .removeMemberByPresentationId(
                                root.pendingRemovalPresentationId)
                        removeMemberDialog.close()
                    }
                }
            }
        }
    }

    KDialog {
        id: evidenceDialog
        objectName: "missions.task.transition.evidence"
        anchors.centerIn: parent
        width: Math.min(500, root.width - 32)
        title: root.pendingTransitionStatus === "Done"
            ? qsTr("Complete Mission")
            : qsTr("Send Mission for review")

        contentItem: ColumnLayout {
            spacing: KodosiTheme.spacing3

            PlainLabel {
                Layout.fillWidth: true
                text: qsTr("Add a brief result, acceptance note, or evidence for “%1”.")
                    .arg(root.pendingTransitionTaskTitle)
                color: KodosiTheme.textSecondary
                wrapMode: Text.Wrap
            }

            KTextArea {
                objectName: "missions.task.transition.evidence.text"
                Accessible.id: objectName
                Layout.fillWidth: true
                Layout.preferredHeight: 90
                text: root.transitionEvidence
                maximumLength: 4000
                wrapMode: TextEdit.Wrap
                onTextChanged: root.transitionEvidence = text
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight

                KButton {
                    text: qsTr("Cancel")
                    variant: "quiet"
                    onClicked: {
                        evidenceDialog.close()
                        root.clearPendingTransition()
                    }
                }

                KButton {
                    objectName: "missions.task.transition.submit"
                    Accessible.id: objectName
                    text: root.pendingTransitionLabel
                    variant: "primary"
                    enabled: root.transitionEvidence.trim().length > 0
                    onClicked: {
                        Models.MissionActions.transitionTask(
                            root.missionId,
                            root.pendingTransitionTaskId,
                            root.pendingTransitionStatus,
                            root.transitionEvidence)
                        evidenceDialog.close()
                        root.clearPendingTransition()
                    }
                }
            }
        }
    }

    KDialog {
        id: archiveDialog
        objectName: "missions.task.archive.confirmation"
        anchors.centerIn: parent
        width: Math.min(440, root.width - 32)
        title: qsTr("Archive “%1”?").arg(
            root.pendingTransitionTaskTitle)

        contentItem: ColumnLayout {
            spacing: KodosiTheme.spacing3

            PlainLabel {
                Layout.fillWidth: true
                text: qsTr("You can restore it from the Archived section after the runtime confirms the change.")
                color: KodosiTheme.textSecondary
                wrapMode: Text.Wrap
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight

                KButton {
                    text: qsTr("Cancel")
                    variant: "quiet"
                    onClicked: {
                        archiveDialog.close()
                        root.clearPendingTransition()
                    }
                }

                KButton {
                    objectName: "missions.task.archive.confirm"
                    Accessible.id: objectName
                    text: qsTr("Archive task")
                    variant: "danger"
                    onClicked: {
                        Models.MissionActions.transitionTask(
                            root.missionId,
                            root.pendingTransitionTaskId,
                            "Archived",
                            "")
                        archiveDialog.close()
                        root.clearPendingTransition()
                    }
                }
            }
        }
    }
}
