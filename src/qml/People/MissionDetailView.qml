pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Kodosi.Models 1.0 as Models

Rectangle {
    id: root

    required property string missionId
    property string chatDraftText
    property string taskTitleText
    property string taskDescriptionText
    property string inviteHandleText
    signal closeRequested()

    onMissionIdChanged: {
        chatDraftText = ""
        taskTitleText = ""
        taskDescriptionText = ""
        inviteHandleText = ""
    }

    Connections {
        target: Models.MissionActions

        function onChatCompleted(missionId) {
            if (missionId === root.missionId)
                root.chatDraftText = ""
        }

        function onTaskCompleted(missionId) {
            if (missionId === root.missionId) {
                root.taskTitleText = ""
                root.taskDescriptionText = ""
            }
        }

        function onInvitationSent(missionId) {
            if (missionId === root.missionId)
                root.inviteHandleText = ""
        }
    }

    radius: KodosiTheme.radiusSmall
    color: KodosiTheme.surface
    border.width: 1
    border.color: KodosiTheme.seam

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: KodosiTheme.spacing5
        spacing: KodosiTheme.spacing5

        RowLayout {
            Layout.fillWidth: true

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                PlainLabel {
                    text: qsTr("Mission detail")
                    color: KodosiTheme.textPrimary
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }

                PlainLabel {
                    text: Models.MissionDetail.loading
                        ? qsTr("Synchronizing roster, chat, and tasks")
                        : qsTr("%1 members · %2 messages · %3 tasks")
                              .arg(Models.MissionDetail.members.count)
                              .arg(Models.MissionDetail.messages.count)
                              .arg(Models.MissionDetail.tasks.count)
                    color: KodosiTheme.textSecondary
                    font.pixelSize: 10
                }
            }

            KButton {
                objectName: "missions.detail.close"
                Accessible.id: objectName
                text: qsTr("Close")
                Accessible.name: text
                onClicked: root.closeRequested()
            }
        }

        PlainLabel {
            visible: Models.MissionDetail.lastError.length > 0
            Layout.fillWidth: true
            text: Models.MissionDetail.lastError
            color: KodosiTheme.danger
            font.pixelSize: 11
            wrapMode: Text.Wrap
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: KodosiTheme.spacing5

            ColumnLayout {
                Layout.preferredWidth: 190
                Layout.fillHeight: true
                spacing: KodosiTheme.spacing3

                PlainLabel {
                    text: qsTr("Roster")
                    color: KodosiTheme.textPrimary
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                }

                RowLayout {
                    visible: Models.MissionActions.canManageSelectedMission
                    Layout.fillWidth: true
                    spacing: 4

                    KTextField {
                        id: inviteHandle
                        objectName: "missions.invite.handle"
                        Accessible.id: objectName
                        Layout.fillWidth: true
                        placeholderText: qsTr("Friend @username")
                        text: root.inviteHandleText
                        onTextChanged: root.inviteHandleText = text
                        Accessible.name: qsTr("Friend to invite")
                        enabled: Models.MissionActions.pendingMissionIds
                            .indexOf(root.missionId) === -1
                    }

                    KButton {
                        objectName: "missions.invite.send"
                        Accessible.id: objectName
                        text: qsTr("Invite")
                        Accessible.name: text
                        enabled: Models.MissionActions.pendingMissionIds
                            .indexOf(root.missionId) === -1
                            && inviteHandle.text.trim().length > 0
                        onClicked: Models.MissionActions.inviteFriend(
                            root.missionId,
                            inviteHandle.text)
                    }
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: Models.MissionDetail.members
                    spacing: 2
                    clip: true

                    delegate: KItemDelegate {
                        id: member
                        required property string userId
                        required property string resolvedName
                        required property string memberRole
                        property bool confirmingRemoval: false

                        objectName: "missions.member." + userId
                        Accessible.id: objectName
                        Accessible.name: resolvedName
                        width: ListView.view.width
                        height: 42

                        contentItem: RowLayout {
                            spacing: 4

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 1
                                PlainLabel {
                                    text: member.resolvedName
                                    color: KodosiTheme.textPrimary
                                    font.pixelSize: 11
                                    font.weight: Font.DemiBold
                                }
                                PlainLabel {
                                    text: member.memberRole
                                    color: KodosiTheme.textSecondary
                                    font.pixelSize: 9
                                }
                            }

                            KButton {
                                objectName: "missions.member.remove."
                                    + member.userId
                                Accessible.id: objectName
                                visible: Models.MissionActions
                                    .canManageSelectedMission
                                    && Models.MissionActions.canRemoveMember(
                                        root.missionId,
                                        member.userId)
                                text: member.confirmingRemoval
                                    ? qsTr("Confirm")
                                    : qsTr("Remove")
                                variant: member.confirmingRemoval
                                    ? "danger"
                                    : "secondary"
                                Accessible.name: member.confirmingRemoval
                                    ? qsTr("Confirm removing %1").arg(
                                        member.resolvedName)
                                    : qsTr("Remove %1").arg(
                                        member.resolvedName)
                                enabled: Models.MissionActions.pendingMissionIds
                                    .indexOf(root.missionId) === -1
                                onClicked: {
                                    if (!member.confirmingRemoval) {
                                        member.confirmingRemoval = true
                                    } else {
                                        Models.MissionActions.removeMember(
                                            root.missionId,
                                            member.userId)
                                        member.confirmingRemoval = false
                                    }
                                }
                            }
                        }

                        background: Rectangle {
                            radius: KodosiTheme.radiusSmall
                            color: member.hovered
                                ? KodosiTheme.surfaceElevated
                                : "transparent"
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
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: KodosiTheme.spacing3

                PlainLabel {
                    text: qsTr("Chat")
                    color: KodosiTheme.textPrimary
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: Models.MissionDetail.messages
                    spacing: KodosiTheme.spacing3
                    clip: true

                    delegate: Rectangle {
                        id: chatMessage
                        required property string messageId
                        required property string authorUserId
                        required property string authorKind
                        required property string body

                        objectName: "missions.message." + messageId
                        Accessible.id: objectName
                        Accessible.name: authorKind + " " + authorUserId
                        Accessible.description: body
                        width: ListView.view.width
                        height: messageColumn.implicitHeight + 12
                        radius: KodosiTheme.radiusSmall
                        color: KodosiTheme.surfaceElevated

                        ColumnLayout {
                            id: messageColumn
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 6
                            spacing: 2

                            PlainLabel {
                                text: chatMessage.authorKind + " · "
                                    + chatMessage.authorUserId
                                color: chatMessage.authorKind === "Agent"
                                    ? KodosiTheme.accent
                                    : KodosiTheme.success
                                font.pixelSize: 9
                                font.weight: Font.DemiBold
                            }
                            PlainLabel {
                                Layout.fillWidth: true
                                text: chatMessage.body
                                color: KodosiTheme.textPrimary
                                font.pixelSize: 11
                                wrapMode: Text.Wrap
                            }
                        }

                    }
                }

                KTextArea {
                    id: chatDraft
                    objectName: "missions.chat.draft"
                    Accessible.id: objectName
                    Layout.fillWidth: true
                    Layout.preferredHeight: 64
                    placeholderText: qsTr("Message the Mission")
                    text: root.chatDraftText
                    onTextChanged: root.chatDraftText = text
                    Accessible.name: placeholderText
                    wrapMode: TextEdit.Wrap
                    enabled: Models.MissionActions.pendingMissionIds
                        .indexOf(root.missionId) === -1
                }

                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    KButton {
                        objectName: "missions.chat.send"
                        Accessible.id: objectName
                        text: qsTr("Send")
                        Accessible.name: text
                        enabled: Models.MissionActions.pendingMissionIds
                            .indexOf(root.missionId) === -1
                            && chatDraft.text.trim().length > 0
                        onClicked: Models.MissionActions.postBroadcast(
                            root.missionId,
                            chatDraft.text)
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                color: KodosiTheme.seam
            }

            ColumnLayout {
                Layout.preferredWidth: 330
                Layout.fillHeight: true
                spacing: KodosiTheme.spacing3

                PlainLabel {
                    text: qsTr("Tasks")
                    color: KodosiTheme.textPrimary
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: Models.MissionDetail.tasks
                    spacing: 3
                    clip: true

                    delegate: Rectangle {
                        id: missionTask
                        required property string taskId
                        required property string title
                        required property string status
                        required property string assignedSessionId
                        property string targetStatus: status
                        property string transitionEvidence
                        property bool confirmingArchive: false

                        onStatusChanged: {
                            targetStatus = status
                            transitionEvidence = ""
                            confirmingArchive = false
                        }

                        objectName: "missions.task." + taskId
                        Accessible.id: objectName
                        Accessible.name: title
                        Accessible.description: status
                        width: ListView.view.width
                        height: taskContent.implicitHeight + 14
                        radius: KodosiTheme.radiusSmall
                        color: KodosiTheme.surfaceElevated
                        border.width: 1
                        border.color: KodosiTheme.seam

                        ColumnLayout {
                            id: taskContent
                            anchors.fill: parent
                            anchors.margins: 7
                            spacing: 5

                            RowLayout {
                                Layout.fillWidth: true

                                Rectangle {
                                    Layout.preferredWidth: 6
                                    Layout.preferredHeight: 6
                                    radius: 3
                                    color: missionTask.status === "Done"
                                        ? KodosiTheme.success
                                        : missionTask.status === "Review"
                                          ? KodosiTheme.warning
                                          : KodosiTheme.accent
                                }
                                PlainLabel {
                                    Layout.fillWidth: true
                                    text: missionTask.title
                                    color: KodosiTheme.textPrimary
                                    font.pixelSize: 10
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                                PlainLabel {
                                    text: missionTask.status
                                    color: KodosiTheme.textSecondary
                                    font.pixelSize: 9
                                }
                            }

                            KComboBox {
                                id: assignment
                                objectName: "missions.task.assignment."
                                    + missionTask.taskId
                                Accessible.id: objectName
                                visible: Models.MissionActions
                                    .canActOnSelectedTasks
                                Layout.fillWidth: true
                                model: Models.MissionActions.assignmentOptions
                                textRole: "name"
                                valueRole: "sessionId"
                                property double assignmentRevision:
                                    Models.MissionActions.assignmentRevision
                                property string stableAssignment: {
                                    if (assignmentRevision >= 0) {
                                        return Models.MissionActions
                                            .assignmentForTask(
                                                missionTask.taskId)
                                    }
                                    return ""
                                }
                                currentIndex: missionTask.assignedSessionId.length > 0
                                        && stableAssignment.length === 0
                                    ? -1
                                    : indexOfValue(stableAssignment)
                                displayText: currentIndex < 0
                                    ? qsTr("Unavailable agent")
                                    : currentText
                                Accessible.name: qsTr(
                                    "Agent assigned to %1").arg(
                                        missionTask.title)
                                enabled: Models.MissionActions
                                    .pendingMissionIds.indexOf(
                                        root.missionId) === -1
                                onActivated: {
                                    if (currentValue !== stableAssignment
                                            || (currentValue.length === 0
                                                && missionTask
                                                    .assignedSessionId
                                                    .length > 0)) {
                                        Models.MissionActions.assignTask(
                                            root.missionId,
                                            missionTask.taskId,
                                            currentValue)
                                    }
                                }
                            }

                            RowLayout {
                                visible: Models.MissionActions
                                    .canManageSelectedMission
                                    && Models.MissionActions
                                        .canActOnSelectedTasks
                                Layout.fillWidth: true
                                spacing: 4

                                KComboBox {
                                    id: nextStatus
                                    objectName: "missions.task.status."
                                        + missionTask.taskId
                                    Accessible.id: objectName
                                    Layout.preferredWidth: 110
                                    model: [
                                        "Open",
                                        "InProgress",
                                        "Review",
                                        "Done",
                                        "Archived"
                                    ]
                                    currentIndex: model.indexOf(
                                        missionTask.targetStatus)
                                    Accessible.name: qsTr(
                                        "Next status for %1").arg(
                                            missionTask.title)
                                    onActivated: {
                                        missionTask.targetStatus = currentText
                                        missionTask.transitionEvidence = ""
                                        missionTask.confirmingArchive = false
                                    }
                                }

                                KTextField {
                                    objectName: "missions.task.evidence."
                                        + missionTask.taskId
                                    Accessible.id: objectName
                                    visible: missionTask.targetStatus === "Review"
                                        || missionTask.targetStatus === "Done"
                                    Layout.fillWidth: true
                                    placeholderText: qsTr("Evidence")
                                    text: missionTask.transitionEvidence
                                    onTextChanged:
                                        missionTask.transitionEvidence = text
                                    maximumLength: 4000
                                    Accessible.name: qsTr(
                                        "Evidence for %1").arg(
                                            missionTask.title)
                                }

                                Item {
                                    visible: missionTask.targetStatus !== "Review"
                                        && missionTask.targetStatus !== "Done"
                                    Layout.fillWidth: true
                                }

                                KButton {
                                    objectName: "missions.task.apply."
                                        + missionTask.taskId
                                    Accessible.id: objectName
                                    text: missionTask.confirmingArchive
                                        ? qsTr("Confirm archive")
                                        : qsTr("Apply")
                                    Accessible.name: text
                                    enabled: Models.MissionActions
                                        .pendingMissionIds.indexOf(
                                            root.missionId) === -1
                                        && missionTask.targetStatus
                                            !== missionTask.status
                                        && ((missionTask.targetStatus
                                                !== "Review"
                                                && missionTask.targetStatus
                                                    !== "Done")
                                            || missionTask.transitionEvidence
                                                .trim().length > 0)
                                    onClicked: {
                                        if (missionTask.targetStatus
                                                === "Archived"
                                            && !missionTask
                                                .confirmingArchive) {
                                            missionTask.confirmingArchive = true
                                            return
                                        }
                                        Models.MissionActions.transitionTask(
                                            root.missionId,
                                            missionTask.taskId,
                                            missionTask.targetStatus,
                                            missionTask.transitionEvidence)
                                        missionTask.confirmingArchive = false
                                    }
                                }
                            }
                        }

                    }
                }

                KTextField {
                    id: taskTitle
                    objectName: "missions.task.title"
                    Accessible.id: objectName
                    Layout.fillWidth: true
                    placeholderText: qsTr("New task title")
                    text: root.taskTitleText
                    onTextChanged: root.taskTitleText = text
                    Accessible.name: placeholderText
                    enabled: Models.MissionActions.pendingMissionIds
                        .indexOf(root.missionId) === -1
                        && Models.MissionActions.canCreateSelectedTasks
                }

                KTextArea {
                    id: taskDescription
                    objectName: "missions.task.description"
                    Accessible.id: objectName
                    Layout.fillWidth: true
                    Layout.preferredHeight: 54
                    placeholderText: qsTr("Task description")
                    text: root.taskDescriptionText
                    onTextChanged: root.taskDescriptionText = text
                    Accessible.name: placeholderText
                    wrapMode: TextEdit.Wrap
                    enabled: Models.MissionActions.pendingMissionIds
                        .indexOf(root.missionId) === -1
                        && Models.MissionActions.canCreateSelectedTasks
                }

                KButton {
                    objectName: "missions.task.create"
                    Accessible.id: objectName
                    Layout.alignment: Qt.AlignRight
                    text: qsTr("Create task")
                    Accessible.name: text
                    enabled: Models.MissionActions.pendingMissionIds
                        .indexOf(root.missionId) === -1
                        && Models.MissionActions.canCreateSelectedTasks
                        && taskTitle.text.trim().length > 0
                    onClicked: Models.MissionActions.createTask(
                        root.missionId,
                        taskTitle.text,
                        taskDescription.text)
                }
            }
        }
    }
}
