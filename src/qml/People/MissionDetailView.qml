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
    property bool steerOpen: false
    property bool interruptArmed: false
    property string steerDraft: ""
    signal closeRequested()

    objectName: "missions.detail"
    Accessible.id: objectName
    Accessible.role: Accessible.Pane
    Accessible.name: missionName
    color: KodosiTheme.canvas

    Connections {
        target: Models.MissionDetail

        function onFocusChanged() {
            root.steerOpen = false
            root.interruptArmed = false
            root.steerDraft = ""
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
                checked: root.page === 0
                onClicked: root.page = 0
            }

            KButton {
                objectName: "missions.detail.tasks"
                Accessible.id: objectName
                text: qsTr("Tasks")
                compact: true
                variant: "quiet"
                checkable: true
                checked: root.page === 1
                onClicked: root.page = 1
            }

            KIconButton {
                objectName: "missions.detail.people"
                Accessible.id: objectName
                glyph: "people"
                Accessible.name: qsTr("Mission people and agents")
                checkable: true
                checked: root.page === 2
                onClicked: root.page = 2
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: KodosiTheme.seam
        }

        PlainLabel {
            visible: Models.MissionDetail.lastError.length > 0
            Layout.fillWidth: true
            Layout.margins: KodosiTheme.spacing3
            text: Models.MissionDetail.lastError
            color: KodosiTheme.danger
            wrapMode: Text.Wrap
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.fill: parent
                visible: root.page === 0
                spacing: 0

                ListView {
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
                                font.pixelSize: 9
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
                                visible:
                                    message.audienceSummary.length > 0
                                text: message.audienceSummary
                                color: KodosiTheme.textTertiary
                                font.pixelSize: 9
                            }
                        }
                    }
                }

                Rectangle {
                    visible: root.recipientsOpen
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(180, crewList.contentHeight)
                    color: KodosiTheme.surfaceRaised

                    ListView {
                        id: crewList
                        anchors.fill: parent
                        anchors.margins: KodosiTheme.spacing2
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
                }

                RowLayout {
                    visible: Models.MissionActions.chatCanCheck
                        || Models.MissionActions.chatCanRetry
                        || (Models.MissionActions.chatCanDiscard
                            && Models.MissionActions.chatOutcome
                                !== Models.MissionActions.Idle)
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
                        text: qsTr("Check")
                        compact: true
                        onClicked: Models.MissionActions.checkChat()
                    }

                    KButton {
                        objectName: "missions.chat.retry"
                        Accessible.id: objectName
                        visible: Models.MissionActions.chatCanRetry
                        text: qsTr("Retry")
                        compact: true
                        onClicked: Models.MissionActions.retryChat()
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

                        KIconButton {
                            objectName: "missions.chat.recipients"
                            Accessible.id: objectName
                            glyph: "people"
                            checkable: true
                            checked: root.recipientsOpen
                            Accessible.name: qsTr("Choose recipients")
                            onClicked:
                                root.recipientsOpen = !root.recipientsOpen
                        }

                        KTextField {
                            objectName: "missions.chat.draft"
                            Accessible.id: objectName
                            Layout.fillWidth: true
                            placeholderText:
                                Models.MissionActions.chatRecipientSummary
                                .length > 0
                                ? Models.MissionActions.chatRecipientSummary
                                : qsTr("Message Mission")
                            text: Models.MissionActions.chatDraftBody
                            maximumLength: 4000
                            onTextEdited:
                                Models.MissionActions.setChatDraftBody(text)
                            onAccepted:
                                Models.MissionActions.sendChat()
                        }

                        KIconButton {
                            objectName: "missions.chat.send"
                            Accessible.id: objectName
                            glyph: "send"
                            Accessible.name: qsTr("Send")
                            enabled: Models.MissionActions.chatCanSubmit
                            onClicked: Models.MissionActions.sendChat()
                        }
                    }
                }
            }

            ColumnLayout {
                anchors.fill: parent
                visible: root.page === 1
                spacing: 0

                ListView {
                    objectName: "missions.tasks"
                    Accessible.id: objectName
                    Accessible.name: qsTr("Mission tasks")
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: Models.MissionDetail.tasks
                    clip: true
                    spacing: 2
                    leftMargin: KodosiTheme.spacing4
                    rightMargin: KodosiTheme.spacing4
                    topMargin: KodosiTheme.spacing3

                    delegate: KItemDelegate {
                        id: task

                        required property string taskId
                        required property string title
                        required property string statusLabel
                        required property bool knownStatus

                        width: ListView.view.width
                            - KodosiTheme.spacing4 * 2
                        implicitHeight: 44
                        objectName: "missions.task." + taskId
                        Accessible.id: objectName
                        Accessible.name: title
                        Accessible.description: statusLabel

                        contentItem: RowLayout {
                            PlainLabel {
                                Layout.fillWidth: true
                                text: task.title
                                color: task.knownStatus
                                    ? KodosiTheme.textPrimary
                                    : KodosiTheme.danger
                                elide: Text.ElideRight
                            }

                            PlainLabel {
                                text: task.statusLabel
                                color: KodosiTheme.textSecondary
                                font.pixelSize: 9
                            }
                        }
                    }
                }

                RowLayout {
                    visible: Models.MissionActions.taskCreateCanCheck
                        || Models.MissionActions.taskCreateCanRetry
                        || (Models.MissionActions.taskCreateCanDiscard
                            && Models.MissionActions.taskCreateOutcome
                                !== Models.MissionActions.Idle)
                    Layout.fillWidth: true
                    Layout.leftMargin: KodosiTheme.spacing3
                    Layout.rightMargin: KodosiTheme.spacing3
                    Layout.topMargin: KodosiTheme.spacing2

                    PlainLabel {
                        Layout.fillWidth: true
                        visible:
                            Models.MissionActions.taskCreateError.length > 0
                        text: Models.MissionActions.taskCreateError
                        color: KodosiTheme.danger
                        elide: Text.ElideRight
                    }

                    KButton {
                        objectName: "missions.task.check"
                        Accessible.id: objectName
                        visible: Models.MissionActions.taskCreateCanCheck
                        text: qsTr("Check")
                        compact: true
                        onClicked: Models.MissionActions.checkTaskCreate()
                    }

                    KButton {
                        objectName: "missions.task.retry"
                        Accessible.id: objectName
                        visible: Models.MissionActions.taskCreateCanRetry
                        text: qsTr("Retry")
                        compact: true
                        onClicked: Models.MissionActions.retryTaskCreate()
                    }

                    KButton {
                        objectName: "missions.task.discard"
                        Accessible.id: objectName
                        visible: Models.MissionActions.taskCreateCanDiscard
                        text: qsTr("Discard")
                        compact: true
                        variant: "quiet"
                        onClicked: Models.MissionActions.discardTaskCreate()
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: taskComposer.implicitHeight + 16
                    color: KodosiTheme.surface

                    RowLayout {
                        id: taskComposer
                        anchors.fill: parent
                        anchors.margins: KodosiTheme.spacing3

                        KTextField {
                            objectName: "missions.task.title"
                            Accessible.id: objectName
                            Layout.fillWidth: true
                            placeholderText: qsTr("New task")
                            text: Models.MissionActions.taskDraftTitle
                            maximumLength: 200
                            onTextEdited:
                                Models.MissionActions.setTaskDraftTitle(text)
                            onAccepted: {
                                if (Models.MissionActions
                                    .taskCreateCanSubmit)
                                    Models.MissionActions.submitTaskCreate()
                            }
                        }

                        KIconButton {
                            objectName: "missions.task.create"
                            Accessible.id: objectName
                            glyph: "plus"
                            Accessible.name: qsTr("Create task")
                            enabled:
                                Models.MissionActions.taskCreateCanSubmit
                            onClicked:
                                Models.MissionActions.submitTaskCreate()
                        }
                    }
                }
            }

            ListView {
                objectName: "missions.people"
                Accessible.id: objectName
                Accessible.name: qsTr("Mission people and agents")
                anchors.fill: parent
                anchors.margins: KodosiTheme.spacing4
                anchors.bottomMargin: focusActions.visible
                    ? focusActions.height + KodosiTheme.spacing5
                    : KodosiTheme.spacing4
                visible: root.page === 2
                model: Models.MissionDetail.crew
                clip: true
                spacing: 2

                delegate: KButton {
                    required property string presentationId
                    required property string displayName
                    required property int attentionCount

                    width: ListView.view.width
                    implicitHeight: 44
                    text: displayName
                    variant: "quiet"
                    contentLeftAligned: true
                    showLeadingDot: attentionCount > 0
                    leadingDotColor: KodosiTheme.warning
                    onClicked:
                        Models.MissionDetail.selectCrew(presentationId)
                }
            }

            Rectangle {
                id: focusActions

                objectName: "missions.focus.actions"
                Accessible.id: objectName
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: KodosiTheme.spacing4
                visible: root.page === 2
                    && Models.MissionDetail
                        .selectedCrewPresentationId.length > 0
                height: focusColumn.implicitHeight + KodosiTheme.spacing4
                radius: KodosiTheme.radiusSmall
                color: KodosiTheme.surfaceRaised

                ColumnLayout {
                    id: focusColumn
                    anchors.fill: parent
                    anchors.margins: KodosiTheme.spacing2
                    spacing: KodosiTheme.spacing2

                    RowLayout {
                        Layout.fillWidth: true

                        PlainLabel {
                            Layout.fillWidth: true
                            text: Models.MissionDetail
                                .selectedSessionDisplayName
                            color: KodosiTheme.textPrimary
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }

                        KIconButton {
                            objectName: "missions.focus.close"
                            Accessible.id: objectName
                            glyph: "close"
                            Accessible.name: qsTr("Close agent actions")
                            onClicked: Models.MissionDetail.clearFocus()
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: KodosiTheme.spacing2

                        KButton {
                            objectName: "missions.focus.terminal"
                            Accessible.id: objectName
                            visible: Models.MissionDetail
                                .selectedCanOpenFullTerminal
                            text: qsTr("Terminal")
                            compact: true
                            onClicked: Models.MissionDetail
                                .openFocusedFullTerminal()
                        }

                        KButton {
                            objectName: "missions.focus.dispatch"
                            Accessible.id: objectName
                            visible: Models.MissionDetail
                                .selectedCanToggleDispatch
                            text: qsTr("Dispatch")
                            compact: true
                            Accessible.name: qsTr("Toggle dispatch")
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
                            checked: root.steerOpen
                            onClicked: root.steerOpen = !root.steerOpen
                        }

                        Item { Layout.fillWidth: true }

                        KButton {
                            objectName: "missions.focus.interrupt"
                            Accessible.id: objectName
                            visible: Models.MissionDetail.selectedCanInterrupt
                            text: root.interruptArmed
                                ? qsTr("Confirm")
                                : qsTr("Interrupt")
                            compact: true
                            variant: root.interruptArmed
                                ? "danger"
                                : "dangerQuiet"
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

                    RowLayout {
                        visible: root.steerOpen
                        Layout.fillWidth: true

                        KTextField {
                            objectName: "missions.focus.steer.text"
                            Accessible.id: objectName
                            Layout.fillWidth: true
                            text: root.steerDraft
                            placeholderText: qsTr("Steer agent")
                            maximumLength: 4000
                            onTextEdited: root.steerDraft = text
                            onAccepted: steerButton.clicked()
                        }

                        KButton {
                            id: steerButton
                            objectName: "missions.focus.steer.send"
                            Accessible.id: objectName
                            text: qsTr("Send")
                            compact: true
                            enabled: root.steerDraft.trim().length > 0
                            onClicked: {
                                if (Models.MissionDetail.steerFocused(
                                        root.steerDraft)) {
                                    root.steerDraft = ""
                                    root.steerOpen = false
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
