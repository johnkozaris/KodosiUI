#include "models/MissionActions.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QUuid>
#include <QtTest/QTest>

class FakeMissionMutationDispatcher final : public kodosi::CommandDispatcher {
public:
    QVector<QJsonObject> commands;
    QString rejectType;

    Result send(const kodosi::CommandLane lane, const QByteArrayView json) override
    {
        const auto document = QJsonDocument::fromJson(json.toByteArray());
        if (lane != kodosi::CommandLane::Rooms || !document.isObject()
            || document.object().value(QStringLiteral("type")).toString()
                == rejectType) {
            return std::unexpected(kodosi::RuntimeFailure {
                .code = kodosi::RuntimeFailure::Code::FfiRejected,
                .ffiResult = -1,
                .message = QStringLiteral("Rejected"),
            });
        }
        commands.push_back(document.object());
        return {};
    }
};

class MissionActionsTest final : public QObject {
    Q_OBJECT

private slots:
    void correlatesChatAndTaskReceipts();
    void authoritativeSnapshotsReconcile();
    void membershipCommandsUseCurrentAuthority();
    void receiptMutationsRequireBoundFingerprint();
    void correlationFailureOnlyRejectsMatchingMissionLedger();
    void projectionRefreshPrecedesReceiptAck();
    void durableReceiptWaitsForExactProjection();
    void durableInvitationRequiresExactProjection();
    void rejectedReceiptAckRetainsMutation();
    void recoveredReceiptMutationsReconcile();
    void recoveredTaskReceiptsWithoutIntentReconcile();
    void recoveredTaskReceiptMalformedProjectionStaysUnknown();
    void acceptsRuntimeLedgerCapacity();
    void retiredReceiptReplayRestoresRecovery();
    void retirementTombstonesAreBounded();
    void durableUnknownCannotBeDiscarded();
    void taskActionsUseNativeRevisionsAndIncarnations();
    void reconcilesTaskAfterMissionSwitch();
    void transientRuntimeResetPreservesDraftsAndCorrelations();
    void accountTransitionDropsLateResults();
    void reconcilesMissingReceipts();
    void createsMissionWithExactSlugAndEntityReconciliation();
    void directedChatUsesOpaqueCurrentRecipientAuthority();
    void taskDraftUsesExactAssignmentDueAndRevisionClearing();
    void archiveAndRestoreUseTaskTransitions();
    void directCreateChatAndTaskCanRunConcurrently();
    void directSuccessRequiresExactProjection();
    void projectionFailureRetainsRecoveryState();
    void directDraftsFenceDiscardAndSubmission();
    void successfulCreateDoesNotLeaveDraft();
    void syntheticUnknownRecoveryUsesReadOnlyCommands();
    void validatesUtf16CodeUnitLimits();
    void handlesEveryActionResultStatus();
    void validatesMissionInputs();
    void preservesUnavailableRecipientsAndUsesTypedCandidates();
};

namespace {

QByteArray auth(const QString& user = QStringLiteral("me"), const int epoch = 1)
{
    return QJsonDocument(QJsonObject {
        {QStringLiteral("type"), QStringLiteral("auth.ready")},
        {QStringLiteral("userId"), user},
        {QStringLiteral("accountEpoch"), epoch},
    }).toJson(QJsonDocument::Compact);
}

QByteArray envelope(QJsonObject event)
{
    event.insert(QStringLiteral("authority"), QStringLiteral("accountContext"));
    event.insert(QStringLiteral("accountUserId"), QStringLiteral("me"));
    event.insert(QStringLiteral("accountEpoch"), 1);
    return QJsonDocument(event).toJson(QJsonDocument::Compact);
}

void seed(kodosi::MissionDirectoryModel& directory)
{
    directory.ingestAuthEvent(auth());
    directory.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.snapshot")},
        {QStringLiteral("rooms"),
         QJsonArray {
             QJsonObject {
                 {QStringLiteral("id"), QStringLiteral("mission")},
                 {QStringLiteral("name"), QStringLiteral("Mission")},
                 {QStringLiteral("slug"), QStringLiteral("mission")},
                 {QStringLiteral("ownerUserId"), QStringLiteral("me")},
                 {QStringLiteral("rosterGeneration"), 1},
             },
             QJsonObject {
                 {QStringLiteral("id"), QStringLiteral("other-mission")},
                 {QStringLiteral("name"), QStringLiteral("Other Mission")},
                 {QStringLiteral("slug"), QStringLiteral("other-mission")},
                 {QStringLiteral("ownerUserId"), QStringLiteral("me")},
                 {QStringLiteral("rosterGeneration"), 1},
             },
         }},
    }));
    directory.ingestRoomEvent(envelope({
         {QStringLiteral("type"), QStringLiteral("room.invitations")},
         {QStringLiteral("incoming"), QJsonArray {}},
         {QStringLiteral("outgoing"), QJsonArray {}},
    }));
}

void seedPeople(kodosi::PeopleModel& people)
{
    people.ingestAuthEvent(auth());
    people.ingestFriendsEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("friends.snapshot")},
        {QStringLiteral("friends"),
         QJsonArray {
             QJsonObject {
                 {QStringLiteral("userId"), QStringLiteral("friend")},
                 {QStringLiteral("handle"), QStringLiteral("alice")},
                 {QStringLiteral("displayName"), QStringLiteral("Alice")},
             },
         }},
        {QStringLiteral("incoming"), QJsonArray {}},
        {QStringLiteral("outgoing"), QJsonArray {}},
    }));
}

QJsonObject invitation(
    const QString& id,
    const QString& inviteeUserId,
    const QString& invitedByUserId)
{
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("roomId"), QStringLiteral("mission")},
        {QStringLiteral("roomName"), QStringLiteral("Mission")},
        {QStringLiteral("roomSlug"), QStringLiteral("mission")},
        {QStringLiteral("inviteeUserId"), inviteeUserId},
        {QStringLiteral("inviteeHandle"),
         inviteeUserId == QStringLiteral("me")
             ? QStringLiteral("me")
             : QStringLiteral("alice")},
        {QStringLiteral("inviteeDisplayName"), QStringLiteral("Invitee")},
        {QStringLiteral("invitedByUserId"), invitedByUserId},
        {QStringLiteral("invitedByHandle"), QStringLiteral("owner")},
        {QStringLiteral("invitedByDisplayName"), QStringLiteral("Owner")},
        {QStringLiteral("status"), QStringLiteral("Pending")},
        {QStringLiteral("baseRosterGeneration"), 7},
        {QStringLiteral("proposedRosterGeneration"), 8},
        {QStringLiteral("createdAt"), QStringLiteral("2026-08-31T08:00:00Z")},
    };
}

QJsonObject roomEntity(
    const QString& id,
    const QString& name,
    const QString& slug,
    const qint64 rosterGeneration = 1)
{
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("name"), name},
        {QStringLiteral("slug"), slug},
        {QStringLiteral("ownerUserId"), QStringLiteral("me")},
        {QStringLiteral("rosterGeneration"), rosterGeneration},
    };
}

QJsonObject messageEntity(
    const QString& id,
    const QString& body,
    QJsonArray recipientSessionIds = {},
    QJsonArray recipientUserIds = {})
{
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("roomId"), QStringLiteral("mission")},
        {QStringLiteral("authorUserId"), QStringLiteral("me")},
        {QStringLiteral("authorSessionId"), QJsonValue::Null},
        {QStringLiteral("authorKind"), QStringLiteral("Human")},
        {QStringLiteral("body"), body},
        {QStringLiteral("recipientSessionIds"),
         std::move(recipientSessionIds)},
        {QStringLiteral("recipientUserIds"),
         std::move(recipientUserIds)},
        {QStringLiteral("seq"), 1},
        {QStringLiteral("postedAt"),
         QStringLiteral("2026-08-31T08:00:00Z")},
    };
}

QJsonObject taskEntity(
    const QString& id,
    const QString& title,
    const QString& description = {},
    const QString& assignedSessionId = {},
    const QString& assignedSessionIncarnationId = {},
    const QString& dueAt = {})
{
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("roomId"), QStringLiteral("mission")},
        {QStringLiteral("createdByUserId"), QStringLiteral("me")},
        {QStringLiteral("title"), title},
        {QStringLiteral("description"),
         description.isEmpty() ? QJsonValue(QJsonValue::Null)
                               : QJsonValue(description)},
        {QStringLiteral("status"), QStringLiteral("Open")},
        {QStringLiteral("revision"), 1},
        {QStringLiteral("assignedSessionId"),
         assignedSessionId.isEmpty()
             ? QJsonValue(QJsonValue::Null)
             : QJsonValue(assignedSessionId)},
        {QStringLiteral("assignedSessionIncarnationId"),
         assignedSessionIncarnationId.isEmpty()
             ? QJsonValue(QJsonValue::Null)
             : QJsonValue(assignedSessionIncarnationId)},
        {QStringLiteral("dueAt"),
         dueAt.isEmpty() ? QJsonValue(QJsonValue::Null)
                         : QJsonValue(dueAt)},
        {QStringLiteral("createdAt"),
         QStringLiteral("2026-08-31T08:00:00Z")},
        {QStringLiteral("updatedAt"),
         QStringLiteral("2026-08-31T08:00:00Z")},
        {QStringLiteral("completedAt"), QJsonValue::Null},
        {QStringLiteral("result"), QJsonValue::Null},
        {QStringLiteral("resultAuthorUserId"), QJsonValue::Null},
    };
}

void seedInvitations(kodosi::MissionDirectoryModel& directory)
{
    directory.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.invitations")},
        {QStringLiteral("incoming"),
         QJsonArray {
             invitation(
                 QStringLiteral("incoming"),
                 QStringLiteral("me"),
                 QStringLiteral("owner")),
         }},
        {QStringLiteral("outgoing"),
         QJsonArray {
             invitation(
                 QStringLiteral("outgoing"),
                 QStringLiteral("pending-friend"),
                 QStringLiteral("me")),
         }},
    }));
}

void seedMembers(
    FakeMissionMutationDispatcher& dispatcher,
    kodosi::MissionDetailModel& detail)
{
    QVERIFY(detail.openMission(QStringLiteral("mission")));
    QJsonObject membersCommand;
    for (auto command = dispatcher.commands.crbegin();
         command != dispatcher.commands.crend();
         ++command) {
        if (command->value(QStringLiteral("type")).toString()
            == QStringLiteral("room.refreshMembers")) {
            membersCommand = *command;
            break;
        }
    }
    QVERIFY(!membersCommand.isEmpty());
    detail.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.members")},
        {QStringLiteral("room_id"), QStringLiteral("mission")},
        {QStringLiteral("hydration_id"),
         membersCommand.value(QStringLiteral("hydration_id"))},
        {QStringLiteral("members"),
         QJsonArray {
             QJsonObject {
                 {QStringLiteral("roomId"), QStringLiteral("mission")},
                 {QStringLiteral("userId"), QStringLiteral("me")},
                 {QStringLiteral("role"), QStringLiteral("owner")},
                 {QStringLiteral("displayName"), QStringLiteral("Me")},
             },
             QJsonObject {
                 {QStringLiteral("roomId"), QStringLiteral("mission")},
                 {QStringLiteral("userId"), QStringLiteral("member")},
                 {QStringLiteral("role"), QStringLiteral("member")},
                 {QStringLiteral("displayName"), QStringLiteral("Member")},
             },
         }},
    }));
}

void seedTask(
    FakeMissionMutationDispatcher& dispatcher,
    kodosi::MissionDetailModel& detail,
    const qint64 revision,
    const bool assigned,
    const QString& assignedIncarnation =
        QStringLiteral("backend-incarnation"))
{
    QJsonObject task {
        {QStringLiteral("id"), QStringLiteral("task")},
        {QStringLiteral("roomId"), QStringLiteral("mission")},
        {QStringLiteral("createdByUserId"), QStringLiteral("me")},
        {QStringLiteral("title"), QStringLiteral("Ship Linux")},
        {QStringLiteral("description"), QStringLiteral("Package it")},
        {QStringLiteral("status"), QStringLiteral("InProgress")},
        {QStringLiteral("revision"), revision},
        {QStringLiteral("createdAt"), QStringLiteral("2026-08-31T08:00:00Z")},
        {QStringLiteral("updatedAt"), QStringLiteral("2026-08-31T09:00:00Z")},
    };
    if (assigned) {
        task.insert(
            QStringLiteral("assignedSessionId"),
            QStringLiteral("backend-agent"));
        task.insert(
            QStringLiteral("assignedSessionIncarnationId"),
            assignedIncarnation);
    }
    QJsonObject tasksCommand;
    for (auto command = dispatcher.commands.crbegin();
         command != dispatcher.commands.crend();
         ++command) {
        if (command->value(QStringLiteral("type")).toString()
                == QStringLiteral("room.tasks.list")
            && command->value(QStringLiteral("room_id")).toString()
                == QStringLiteral("mission")) {
            tasksCommand = *command;
            break;
        }
    }
    QVERIFY(!tasksCommand.isEmpty());
    detail.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.tasks.page")},
        {QStringLiteral("room_id"), QStringLiteral("mission")},
        {QStringLiteral("hydration_id"),
         tasksCommand.value(QStringLiteral("hydration_id"))},
        {QStringLiteral("request_offset"),
         tasksCommand.value(QStringLiteral("offset"))},
        {QStringLiteral("has_more"), false},
        {QStringLiteral("tasks"), QJsonArray {task}},
    }));
}

void seedSessions(
    kodosi::SessionCatalogModel& sessions,
    const QString& backendIncarnation =
        QStringLiteral("backend-incarnation"),
    const QString& runtimeIncarnation =
        QStringLiteral("runtime-incarnation"))
{
    sessions.ingestAuthEvent(auth());
    sessions.ingestSessionEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("session.list")},
        {QStringLiteral("sessions"),
         QJsonArray {
             QJsonObject {
                 {QStringLiteral("kind"), QStringLiteral("local")},
                 {QStringLiteral("id"), QStringLiteral("local-agent")},
                 {QStringLiteral("incarnationId"),
                  runtimeIncarnation},
                 {QStringLiteral("name"), QStringLiteral("Agent")},
                 {QStringLiteral("project"), QStringLiteral("/repo")},
                 {QStringLiteral("mode"), QStringLiteral("normal")},
                 {QStringLiteral("status"), QStringLiteral("active")},
                 {QStringLiteral("recovery"), QStringLiteral("live")},
                 {QStringLiteral("scope"), QStringLiteral("room")},
                 {QStringLiteral("access"), QStringLiteral("inject")},
                 {QStringLiteral("roomId"), QStringLiteral("mission")},
                 {QStringLiteral("backendSessionId"),
                  QStringLiteral("backend-agent")},
                 {QStringLiteral("backendIncarnationId"),
                  backendIncarnation},
             },
         }},
    }));
}

QByteArray actionEvent(
    const QString& type,
    const QString& requestId,
    const QString& operation,
    const QString& fingerprint,
    const QString& status = {},
    const QString& entityId = {})
{
    auto resolvedEntityId = entityId;
    if (resolvedEntityId.isEmpty()
        && !fingerprint.isEmpty()
        && status == QStringLiteral("succeeded")) {
        if (operation == QStringLiteral("removeMember")) {
            resolvedEntityId = QStringLiteral("member");
        } else if (
            operation == QStringLiteral("acceptInvitation")
            || operation == QStringLiteral("declineInvitation")) {
            resolvedEntityId = QStringLiteral("incoming");
        } else if (
            operation == QStringLiteral("cancelInvitation")) {
            resolvedEntityId = QStringLiteral("outgoing");
        } else if (
            operation == QStringLiteral("tasks.transition")
            || operation == QStringLiteral("tasks.assign")) {
            resolvedEntityId = QStringLiteral("task");
        }
    }
    QJsonObject event {
        {QStringLiteral("type"), type},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("operation"), operation},
        {QStringLiteral("roomId"), QStringLiteral("mission")},
    };
    if (!fingerprint.isEmpty()) {
        event.insert(QStringLiteral("fingerprint"), fingerprint);
    }
    if (!status.isEmpty()) {
        event.insert(QStringLiteral("status"), status);
    }
    if (!resolvedEntityId.isEmpty()) {
        event.insert(QStringLiteral("entityId"), resolvedEntityId);
    }
    return envelope(event);
}

QJsonObject lastCommand(
    const FakeMissionMutationDispatcher& dispatcher,
    const QString& type)
{
    const auto found = std::ranges::find(
        dispatcher.commands.crbegin(),
        dispatcher.commands.crend(),
        type,
        [](const QJsonObject& command) {
            return command.value(QStringLiteral("type")).toString();
        });
    return found == dispatcher.commands.crend() ? QJsonObject {} : *found;
}

QString crewPresentation(
    const kodosi::MissionDetailModel& detail,
    const kodosi::MissionCrewModel::Kind kind,
    const QString& displayName = {})
{
    const auto* crew = const_cast<kodosi::MissionDetailModel&>(detail).crew();
    for (auto row = 0; row < crew->rowCount(); ++row) {
        const auto index = crew->index(row);
        if (crew->data(index, kodosi::MissionCrewModel::KindRole).toInt()
                != static_cast<int>(kind)
            || (!displayName.isEmpty()
                && crew
                        ->data(
                            index,
                            kodosi::MissionCrewModel::DisplayNameRole)
                        .toString()
                    != displayName)) {
            continue;
        }
        return crew
            ->data(index, kodosi::MissionCrewModel::PresentationIdRole)
            .toString();
    }
    return {};
}

} // namespace

void MissionActionsTest::correlatesChatAndTaskReceipts()
{
    FakeMissionMutationDispatcher dispatcher;
    kodosi::MissionDirectoryModel directory(dispatcher);
    seed(directory);
    kodosi::MissionDetailModel detail(dispatcher, directory);
    detail.ingestAuthEvent(auth());
    seedMembers(dispatcher, detail);
    kodosi::PeopleModel people;
    people.ingestAuthEvent(auth());
    kodosi::SessionCatalogModel sessions;
    kodosi::MissionActions actions(
        dispatcher, directory, detail, people, sessions);
    actions.ingestAuthEvent(auth());
    QSignalSpy chat(&actions, &kodosi::MissionActions::chatCompleted);
    QSignalSpy task(&actions, &kodosi::MissionActions::taskCompleted);

    QVERIFY(actions.postBroadcast(
        QStringLiteral("mission"),
        QStringLiteral(" hello ")));
    const auto chatCommand = dispatcher.commands.back();
    QCOMPARE(
        chatCommand.value(QStringLiteral("body")).toString(),
        QStringLiteral("hello"));
    const auto chatId =
        chatCommand.value(QStringLiteral("requestId")).toString();
    actions.ingestRoomEvent(actionEvent(
        QStringLiteral("room.action.result"),
        chatId,
        QStringLiteral("chat.post"),
        {},
        QStringLiteral("succeeded")));
    QCOMPARE(
        actions.chatOutcome(),
        kodosi::MissionActions::Outcome::AcceptedAwaitingProjection);
    actions.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.chat.snapshot")},
        {QStringLiteral("room_id"), QStringLiteral("mission")},
        {QStringLiteral("messages"),
         QJsonArray {
             messageEntity(chatId, QStringLiteral("hello")),
         }},
    }));
    QCOMPARE(chat.count(), 1);

    QVERIFY(actions.createTask(
        QStringLiteral("mission"),
        QStringLiteral(" Ship "),
        QStringLiteral(" Linux ")));
    const auto taskCommand = dispatcher.commands.back();
    const auto taskId =
        taskCommand.value(QStringLiteral("requestId")).toString();
    actions.ingestRoomEvent(actionEvent(
        QStringLiteral("room.action.result"),
        taskId,
        QStringLiteral("tasks.create"),
        QStringLiteral("wrong"),
        QStringLiteral("succeeded")));
    QVERIFY(actions.busy());
    actions.ingestRoomEvent(actionEvent(
        QStringLiteral("room.action.result"),
        taskId,
        QStringLiteral("tasks.create"),
        {},
        QStringLiteral("succeeded")));
    actions.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.tasks.snapshot")},
        {QStringLiteral("room_id"), QStringLiteral("mission")},
        {QStringLiteral("tasks"),
         QJsonArray {
             taskEntity(
                 taskId,
                 QStringLiteral("Ship"),
                 QStringLiteral("Linux")),
         }},
    }));
    QCOMPARE(task.count(), 1);
    QVERIFY(!actions.busy());
}

void MissionActionsTest::authoritativeSnapshotsReconcile()
{
    FakeMissionMutationDispatcher dispatcher;
    kodosi::MissionDirectoryModel directory(dispatcher);
    seed(directory);
    kodosi::MissionDetailModel detail(dispatcher, directory);
    detail.ingestAuthEvent(auth());
    seedMembers(dispatcher, detail);
    kodosi::PeopleModel people;
    people.ingestAuthEvent(auth());
    kodosi::SessionCatalogModel sessions;
    kodosi::MissionActions actions(
        dispatcher, directory, detail, people, sessions);
    actions.ingestAuthEvent(auth());
    QSignalSpy chat(&actions, &kodosi::MissionActions::chatCompleted);
    QSignalSpy task(&actions, &kodosi::MissionActions::taskCompleted);

    QVERIFY(actions.postBroadcast(
        QStringLiteral("mission"),
        QStringLiteral("hello")));
    const auto chatId =
        dispatcher.commands.back().value(QStringLiteral("requestId")).toString();
    actions.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.chat.snapshot")},
        {QStringLiteral("room_id"), QStringLiteral("mission")},
        {QStringLiteral("messages"),
         QJsonArray {
             messageEntity(chatId, QStringLiteral("hello")),
         }},
    }));
    QCOMPARE(chat.count(), 1);
    QVERIFY(!actions.busy());

    QVERIFY(actions.createTask(
        QStringLiteral("mission"),
        QStringLiteral("Ship"),
        QStringLiteral("Linux")));
    const auto taskId =
        dispatcher.commands.back().value(QStringLiteral("requestId")).toString();
    actions.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.tasks.snapshot")},
        {QStringLiteral("room_id"), QStringLiteral("mission")},
        {QStringLiteral("tasks"),
         QJsonArray {
             taskEntity(
                 taskId,
                 QStringLiteral("Ship"),
                 QStringLiteral("Linux")),
         }},
    }));
    QCOMPARE(task.count(), 1);
    QVERIFY(!actions.busy());
}

void MissionActionsTest::membershipCommandsUseCurrentAuthority()
{
    FakeMissionMutationDispatcher dispatcher;
    kodosi::MissionDirectoryModel directory(dispatcher);
    seed(directory);
    seedInvitations(directory);
    kodosi::MissionDetailModel detail(dispatcher, directory);
    detail.ingestAuthEvent(auth());
    seedMembers(dispatcher, detail);
    kodosi::PeopleModel people;
    seedPeople(people);
    kodosi::SessionCatalogModel sessions;
    kodosi::MissionActions actions(
        dispatcher, directory, detail, people, sessions);
    actions.ingestAuthEvent(auth());
    QSignalSpy invitationSent(
        &actions,
        &kodosi::MissionActions::invitationSent);

    QVERIFY(actions.canManageSelectedMission());
    QVERIFY(actions.canRemoveMember(
        QStringLiteral("mission"),
        QStringLiteral("member")));
    QVERIFY(!actions.canRemoveMember(
        QStringLiteral("mission"),
        QStringLiteral("me")));

    QVERIFY(actions.inviteFriend(
        QStringLiteral("mission"),
        QStringLiteral("@alice")));
    auto command = dispatcher.commands.back();
    QCOMPARE(
        command.value(QStringLiteral("type")).toString(),
        QStringLiteral("room.invite"));
    QCOMPARE(
        command.value(QStringLiteral("invitee_user_id")).toString(),
        QStringLiteral("friend"));
    const auto inviteRequest =
        command.value(QStringLiteral("requestId")).toString();
    auto recoveredInvitation =
        invitation(inviteRequest, QStringLiteral("friend"), QStringLiteral("me"));
    actions.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.invitations")},
        {QStringLiteral("incoming"), QJsonArray {}},
        {QStringLiteral("outgoing"), QJsonArray {recoveredInvitation}},
    }));
    QCOMPARE(invitationSent.count(), 1);

    const auto checkInvitationCommand =
        [&](const auto invoke,
            const QString& type,
            const QString& invitationId) {
            seedInvitations(directory);
            QVERIFY(invoke());
            const auto invitationCommand = dispatcher.commands.back();
            QCOMPARE(
                invitationCommand.value(QStringLiteral("type")).toString(),
                type);
            QCOMPARE(
                invitationCommand.value(QStringLiteral("invitation_id"))
                    .toString(),
                invitationId);
            QCOMPARE(
                invitationCommand
                    .value(QStringLiteral("expected_roster_generation"))
                    .toInteger(),
                7);
            const auto requestId =
                invitationCommand.value(QStringLiteral("requestId")).toString();
            actions.ingestRoomEvent(actionEvent(
                QStringLiteral("room.action.result"),
                requestId,
                type.sliced(5),
                {},
                QStringLiteral("failed")));
        };
    checkInvitationCommand(
        [&] { return actions.acceptInvitation(QStringLiteral("incoming")); },
        QStringLiteral("room.acceptInvitation"),
        QStringLiteral("incoming"));
    checkInvitationCommand(
        [&] { return actions.declineInvitation(QStringLiteral("incoming")); },
        QStringLiteral("room.declineInvitation"),
        QStringLiteral("incoming"));
    checkInvitationCommand(
        [&] { return actions.cancelInvitation(QStringLiteral("outgoing")); },
        QStringLiteral("room.cancelInvitation"),
        QStringLiteral("outgoing"));

    seedMembers(dispatcher, detail);
    QVERIFY(actions.removeMember(
        QStringLiteral("mission"),
        QStringLiteral("member")));
    command = dispatcher.commands.back();
    QCOMPARE(
        command.value(QStringLiteral("type")).toString(),
        QStringLiteral("room.removeMember"));
    QCOMPARE(
        command.value(QStringLiteral("user_id")).toString(),
        QStringLiteral("member"));
    QCOMPARE(
        command.value(QStringLiteral("expected_roster_generation")).toInteger(),
        1);
}

void MissionActionsTest::receiptMutationsRequireBoundFingerprint()
{
    FakeMissionMutationDispatcher dispatcher;
    kodosi::MissionDirectoryModel directory(dispatcher);
    seed(directory);
    kodosi::MissionDetailModel detail(dispatcher, directory);
    detail.ingestAuthEvent(auth());
    seedMembers(dispatcher, detail);
    kodosi::PeopleModel people;
    seedPeople(people);
    kodosi::SessionCatalogModel sessions;
    kodosi::MissionActions actions(
        dispatcher, directory, detail, people, sessions);
    actions.ingestAuthEvent(auth());

    QVERIFY(actions.removeMember(
        QStringLiteral("mission"),
        QStringLiteral("member")));
    const auto requestId =
        dispatcher.commands.back().value(QStringLiteral("requestId")).toString();
    const auto fingerprint = QString(64, u'a');
    actions.ingestRoomEvent(actionEvent(
        QStringLiteral("room.action.accepted"),
        requestId,
        QStringLiteral("removeMember"),
        fingerprint));
    QVERIFY(actions.busy());
    actions.ingestRoomEvent(actionEvent(
        QStringLiteral("room.action.result"),
        requestId,
        QStringLiteral("removeMember"),
        QString(64, u'b'),
        QStringLiteral("succeeded")));
    QVERIFY(!actions.busy());
    QVERIFY(actions.lastError().contains(QStringLiteral("did not match")));

    QVERIFY(actions.removeMember(
        QStringLiteral("mission"),
        QStringLiteral("member")));
    const auto exactRequest =
        dispatcher.commands.back().value(QStringLiteral("requestId")).toString();
    actions.ingestRoomEvent(actionEvent(
        QStringLiteral("room.action.accepted"),
        exactRequest,
        QStringLiteral("removeMember"),
        fingerprint));
    const auto acknowledgementsBefore = std::ranges::count(
        dispatcher.commands,
        QStringLiteral("room.mutations.reconcile"),
        [](const QJsonObject& command) {
            return command.value(QStringLiteral("type")).toString();
        });
    actions.ingestRoomEvent(actionEvent(
        QStringLiteral("room.action.result"),
        exactRequest,
        QStringLiteral("removeMember"),
        fingerprint,
        QStringLiteral("succeeded")));
    QVERIFY(actions.busy());
    QCOMPARE(
        actions.ledgerOutcome(),
        kodosi::MissionActions::Outcome::AcceptedAwaitingProjection);
    QVERIFY(!actions.ledgerCanCheck());
    QCOMPARE(
        std::ranges::count(
            dispatcher.commands,
            QStringLiteral("room.mutations.reconcile"),
            [](const QJsonObject& command) {
                return command.value(QStringLiteral("type")).toString();
            }),
        acknowledgementsBefore);
}

void MissionActionsTest::correlationFailureOnlyRejectsMatchingMissionLedger()
{
    FakeMissionMutationDispatcher dispatcher;
    kodosi::MissionDirectoryModel directory(dispatcher);
    seed(directory);
    kodosi::MissionDetailModel detail(dispatcher, directory);
    detail.ingestAuthEvent(auth());
    kodosi::PeopleModel people;
    people.ingestAuthEvent(auth());
    kodosi::SessionCatalogModel sessions;
    kodosi::MissionActions actions(
        dispatcher, directory, detail, people, sessions);
    actions.ingestAuthEvent(auth());

    const auto firstRequest =
        QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    const auto secondRequest =
        QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    actions.ingestRoomEvent(envelope({
        {QStringLiteral("type"),
         QStringLiteral("room.mutation.recovered")},
        {QStringLiteral("requestId"), firstRequest},
        {QStringLiteral("operation"), QStringLiteral("removeMember")},
        {QStringLiteral("roomId"), QStringLiteral("mission")},
        {QStringLiteral("fingerprint"), QString(64, u'a')},
    }));
    actions.ingestRoomEvent(envelope({
        {QStringLiteral("type"),
         QStringLiteral("room.mutation.recovered")},
        {QStringLiteral("requestId"), secondRequest},
        {QStringLiteral("operation"), QStringLiteral("removeMember")},
        {QStringLiteral("roomId"), QStringLiteral("other-mission")},
        {QStringLiteral("fingerprint"), QString(64, u'c')},
    }));

    actions.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.action.result")},
        {QStringLiteral("requestId"), firstRequest},
        {QStringLiteral("operation"), QStringLiteral("removeMember")},
        {QStringLiteral("roomId"), QStringLiteral("mission")},
        {QStringLiteral("fingerprint"), QString(64, u'b')},
        {QStringLiteral("status"), QStringLiteral("succeeded")},
    }));

    QVERIFY(detail.openMission(QStringLiteral("mission")));
    QCOMPARE(
        actions.ledgerOutcome(),
        kodosi::MissionActions::Outcome::Conflict);
    QVERIFY(detail.openMission(QStringLiteral("other-mission")));
    QCOMPARE(
        actions.ledgerOutcome(),
        kodosi::MissionActions::Outcome::Reconciling);
    QVERIFY(actions.busy());
}

void MissionActionsTest::projectionRefreshPrecedesReceiptAck()
{
    FakeMissionMutationDispatcher dispatcher;
    kodosi::MissionDirectoryModel directory(dispatcher);
    seed(directory);
    seedInvitations(directory);
    kodosi::MissionDetailModel detail(dispatcher, directory);
    detail.ingestAuthEvent(auth());
    seedMembers(dispatcher, detail);
    kodosi::PeopleModel people;
    seedPeople(people);
    kodosi::SessionCatalogModel sessions;
    kodosi::MissionActions actions(
        dispatcher, directory, detail, people, sessions, 1);
    actions.ingestAuthEvent(auth());

    QVERIFY(actions.removeMember(
        QStringLiteral("mission"),
        QStringLiteral("member")));
    const auto requestId =
        dispatcher.commands.back().value(QStringLiteral("requestId")).toString();
    const auto fingerprint = QString(64, u'd');
    actions.ingestRoomEvent(actionEvent(
        QStringLiteral("room.action.accepted"),
        requestId,
        QStringLiteral("removeMember"),
        fingerprint));
    const auto acknowledgementsBefore = std::ranges::count(
        dispatcher.commands,
        QStringLiteral("room.mutations.reconcile"),
        [](const QJsonObject& command) {
            return command.value(QStringLiteral("type")).toString();
        });
    dispatcher.rejectType = QStringLiteral("room.chat.list");
    actions.ingestRoomEvent(actionEvent(
        QStringLiteral("room.action.result"),
        requestId,
        QStringLiteral("removeMember"),
        fingerprint,
        QStringLiteral("succeeded")));
    QVERIFY(actions.busy());
    QVERIFY(!detail.membersReady());
    QVERIFY(!directory.invitationsReady());
    QCOMPARE(
        actions.ledgerOutcome(),
        kodosi::MissionActions::Outcome::AcceptedAwaitingProjection);
    QCOMPARE(
        std::ranges::count(
            dispatcher.commands,
            QStringLiteral("room.mutations.reconcile"),
            [](const QJsonObject& command) {
                return command.value(QStringLiteral("type")).toString();
            }),
        acknowledgementsBefore);

    dispatcher.rejectType.clear();
    QVERIFY(actions.busy());
    QCOMPARE(detail.missionId(), QStringLiteral("mission"));
    QCOMPARE(
        std::ranges::count(
            dispatcher.commands,
            QStringLiteral("room.mutations.reconcile"),
            [](const QJsonObject& command) {
                return command.value(QStringLiteral("type")).toString();
            }),
        acknowledgementsBefore);
}

void MissionActionsTest::durableReceiptWaitsForExactProjection()
{
    FakeMissionMutationDispatcher dispatcher;
    kodosi::MissionDirectoryModel directory(dispatcher);
    seed(directory);
    seedInvitations(directory);
    kodosi::MissionDetailModel detail(dispatcher, directory);
    detail.ingestAuthEvent(auth());
    seedMembers(dispatcher, detail);
    kodosi::PeopleModel people;
    seedPeople(people);
    kodosi::SessionCatalogModel sessions;
    kodosi::MissionActions actions(
        dispatcher, directory, detail, people, sessions);
    actions.ingestAuthEvent(auth());

    QVERIFY(actions.removeMember(
        QStringLiteral("mission"),
        QStringLiteral("member")));
    const auto requestId =
        dispatcher.commands.back().value(QStringLiteral("requestId")).toString();
    const auto fingerprint = QString(64, u'e');
    actions.ingestRoomEvent(actionEvent(
        QStringLiteral("room.action.accepted"),
        requestId,
        QStringLiteral("removeMember"),
        fingerprint));
    const auto acknowledgementsBefore = std::ranges::count(
        dispatcher.commands,
        QStringLiteral("room.mutations.reconcile"),
        [](const QJsonObject& command) {
            return command.value(QStringLiteral("type")).toString();
        });

    actions.ingestRoomEvent(actionEvent(
        QStringLiteral("room.action.result"),
        requestId,
        QStringLiteral("removeMember"),
        fingerprint,
        QStringLiteral("succeeded"),
        QStringLiteral("member")));
    QVERIFY(actions.busy());
    QCOMPARE(
        actions.ledgerOutcome(),
        kodosi::MissionActions::Outcome::AcceptedAwaitingProjection);
    QCOMPARE(
        std::ranges::count(
            dispatcher.commands,
            QStringLiteral("room.mutations.reconcile"),
            [](const QJsonObject& command) {
                return command.value(QStringLiteral("type")).toString();
            }),
        acknowledgementsBefore);

    const auto membersRefresh =
        lastCommand(dispatcher, QStringLiteral("room.refreshMembers"));
    QVERIFY(!membersRefresh.isEmpty());
    const auto hydrationId =
        membersRefresh.value(QStringLiteral("hydration_id")).toString();
    QVERIFY(!hydrationId.isEmpty());
    actions.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.members")},
        {QStringLiteral("room_id"), QStringLiteral("mission")},
        {QStringLiteral("hydration_id"),
         QStringLiteral("stale-hydration")},
        {QStringLiteral("members"),
         QJsonArray {
             QJsonObject {
                 {QStringLiteral("roomId"), QStringLiteral("mission")},
                 {QStringLiteral("userId"), QStringLiteral("me")},
                 {QStringLiteral("role"), QStringLiteral("owner")},
                 {QStringLiteral("displayName"), QStringLiteral("Me")},
             },
             QJsonObject {
                 {QStringLiteral("roomId"), QStringLiteral("mission")},
                 {QStringLiteral("userId"), QStringLiteral("member")},
                 {QStringLiteral("role"), QStringLiteral("member")},
                 {QStringLiteral("displayName"), QStringLiteral("Member")},
             },
         }},
    }));
    QVERIFY(actions.busy());
    QCOMPARE(
        std::ranges::count(
            dispatcher.commands,
            QStringLiteral("room.mutations.reconcile"),
            [](const QJsonObject& command) {
                return command.value(QStringLiteral("type")).toString();
            }),
        acknowledgementsBefore);

    actions.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.snapshot")},
        {QStringLiteral("rooms"),
         QJsonArray {
             roomEntity(
                 QStringLiteral("mission"),
                 QStringLiteral("Mission"),
                 QStringLiteral("mission"),
                 2),
         }},
    }));
    QVERIFY(actions.busy());
    actions.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.members")},
        {QStringLiteral("room_id"), QStringLiteral("mission")},
        {QStringLiteral("hydration_id"), hydrationId},
        {QStringLiteral("members"),
         QJsonArray {
             QJsonObject {
                 {QStringLiteral("roomId"), QStringLiteral("mission")},
                 {QStringLiteral("userId"), QStringLiteral("me")},
                 {QStringLiteral("role"), QStringLiteral("owner")},
                 {QStringLiteral("displayName"), QStringLiteral("Me")},
             },
         }},
    }));
    QVERIFY(!actions.busy());
    QCOMPARE(
        actions.ledgerOutcome(),
        kodosi::MissionActions::Outcome::Succeeded);
    QCOMPARE(
        dispatcher.commands.back().value(QStringLiteral("type")).toString(),
        QStringLiteral("room.mutations.reconcile"));
    QCOMPARE(
        dispatcher.commands.back().value(QStringLiteral("requestId")).toString(),
        requestId);
}

void MissionActionsTest::durableInvitationRequiresExactProjection()
{
    FakeMissionMutationDispatcher dispatcher;
    kodosi::MissionDirectoryModel directory(dispatcher);
    seed(directory);
    seedInvitations(directory);
    kodosi::MissionDetailModel detail(dispatcher, directory);
    detail.ingestAuthEvent(auth());
    QVERIFY(detail.openMission(QStringLiteral("mission")));
    kodosi::PeopleModel people;
    seedPeople(people);
    kodosi::SessionCatalogModel sessions;
    kodosi::MissionActions actions(
        dispatcher, directory, detail, people, sessions);
    actions.ingestAuthEvent(auth());

    QVERIFY(actions.acceptInvitation(QStringLiteral("incoming")));
    const auto requestId =
        dispatcher.commands.back().value(QStringLiteral("requestId")).toString();
    const auto fingerprint = QString(64, u'9');
    actions.ingestRoomEvent(actionEvent(
        QStringLiteral("room.action.accepted"),
        requestId,
        QStringLiteral("acceptInvitation"),
        fingerprint));
    const auto acknowledgementsBefore = std::ranges::count(
        dispatcher.commands,
        QStringLiteral("room.mutations.reconcile"),
        [](const QJsonObject& command) {
            return command.value(QStringLiteral("type")).toString();
        });
    actions.ingestRoomEvent(actionEvent(
        QStringLiteral("room.action.result"),
        requestId,
        QStringLiteral("acceptInvitation"),
        fingerprint,
        QStringLiteral("succeeded"),
        QStringLiteral("incoming")));
    QVERIFY(actions.busy());
    QCOMPARE(
        actions.ledgerOutcome(),
        kodosi::MissionActions::Outcome::AcceptedAwaitingProjection);
    QCOMPARE(
        std::ranges::count(
            dispatcher.commands,
            QStringLiteral("room.mutations.reconcile"),
            [](const QJsonObject& command) {
                return command.value(QStringLiteral("type")).toString();
            }),
        acknowledgementsBefore);
    QCOMPARE(
        dispatcher.commands.back().value(QStringLiteral("type")).toString(),
        QStringLiteral("room.refreshInvitations"));

    actions.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.invitations")},
        {QStringLiteral("incoming"), QJsonArray {}},
        {QStringLiteral("outgoing"),
         QJsonArray {
             invitation(
                 QStringLiteral("outgoing"),
                 QStringLiteral("pending-friend"),
                 QStringLiteral("me")),
         }},
    }));
    QVERIFY(actions.busy());
    QCOMPARE(
        actions.ledgerOutcome(),
        kodosi::MissionActions::Outcome::AcceptedAwaitingProjection);
    actions.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.snapshot")},
        {QStringLiteral("rooms"),
         QJsonArray {
             roomEntity(
                 QStringLiteral("mission"),
                 QStringLiteral("Mission"),
                 QStringLiteral("mission")),
         }},
    }));
    QVERIFY(!actions.busy());
    QCOMPARE(
        actions.ledgerOutcome(),
        kodosi::MissionActions::Outcome::Succeeded);
    QCOMPARE(
        dispatcher.commands.back().value(QStringLiteral("type")).toString(),
        QStringLiteral("room.mutations.reconcile"));
    QCOMPARE(
        dispatcher.commands.back().value(QStringLiteral("requestId")).toString(),
        requestId);
}

void MissionActionsTest::rejectedReceiptAckRetainsMutation()
{
    FakeMissionMutationDispatcher dispatcher;
    kodosi::MissionDirectoryModel directory(dispatcher);
    seed(directory);
    seedInvitations(directory);
    kodosi::MissionDetailModel detail(dispatcher, directory);
    detail.ingestAuthEvent(auth());
    seedMembers(dispatcher, detail);
    kodosi::PeopleModel people;
    seedPeople(people);
    kodosi::SessionCatalogModel sessions;
    kodosi::MissionActions actions(
        dispatcher, directory, detail, people, sessions, 1);
    actions.ingestAuthEvent(auth());

    QVERIFY(actions.removeMember(
        QStringLiteral("mission"),
        QStringLiteral("member")));
    const auto requestId =
        dispatcher.commands.back().value(QStringLiteral("requestId")).toString();
    const auto fingerprint = QString(64, u'f');
    actions.ingestRoomEvent(actionEvent(
        QStringLiteral("room.action.accepted"),
        requestId,
        QStringLiteral("removeMember"),
        fingerprint));
    dispatcher.rejectType = QStringLiteral("room.mutations.reconcile");
    actions.ingestRoomEvent(actionEvent(
        QStringLiteral("room.action.result"),
        requestId,
        QStringLiteral("removeMember"),
        fingerprint,
        QStringLiteral("succeeded")));
    const auto membersRefresh =
        lastCommand(dispatcher, QStringLiteral("room.refreshMembers"));
    actions.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.snapshot")},
        {QStringLiteral("rooms"),
         QJsonArray {
             roomEntity(
                 QStringLiteral("mission"),
                 QStringLiteral("Mission"),
                 QStringLiteral("mission"),
                 2),
         }},
    }));
    actions.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.members")},
        {QStringLiteral("room_id"), QStringLiteral("mission")},
        {QStringLiteral("hydration_id"),
         membersRefresh.value(QStringLiteral("hydration_id"))},
        {QStringLiteral("members"),
         QJsonArray {
             QJsonObject {
                 {QStringLiteral("roomId"), QStringLiteral("mission")},
                 {QStringLiteral("userId"), QStringLiteral("me")},
                 {QStringLiteral("role"), QStringLiteral("owner")},
                 {QStringLiteral("displayName"), QStringLiteral("Me")},
             },
         }},
    }));
    QVERIFY(!actions.busy());
    QCOMPARE(
        actions.ledgerOutcome(),
        kodosi::MissionActions::Outcome::Succeeded);
    QVERIFY(actions.ledgerError().contains(
        QStringLiteral("no positive retirement acknowledgement"),
        Qt::CaseInsensitive));
}

void MissionActionsTest::recoveredReceiptMutationsReconcile()
{
    FakeMissionMutationDispatcher dispatcher;
    kodosi::MissionDirectoryModel directory(dispatcher);
    seed(directory);
    kodosi::MissionDetailModel detail(dispatcher, directory);
    detail.ingestAuthEvent(auth());
    kodosi::PeopleModel people;
    seedPeople(people);
    kodosi::SessionCatalogModel sessions;
    kodosi::MissionActions actions(
        dispatcher, directory, detail, people, sessions, 1);
    actions.ingestAuthEvent(auth());
    QCOMPARE(
        dispatcher.commands.back().value(QStringLiteral("type")).toString(),
        QStringLiteral("room.mutations.recover"));

    const auto requestId =
        QUuid::createUuidV7().toString(QUuid::WithoutBraces);
    const auto fingerprint = QString(64, u'c');
    actions.ingestRoomEvent(actionEvent(
        QStringLiteral("room.mutation.recovered"),
        requestId,
        QStringLiteral("removeMember"),
        fingerprint));
    QVERIFY(actions.busy());
    QVERIFY(std::ranges::find(
        dispatcher.commands,
        QStringLiteral("room.mutations.reconcile"),
        [](const QJsonObject& command) {
            return command.value(QStringLiteral("type")).toString();
        }) != dispatcher.commands.end());
    actions.ingestRoomEvent(actionEvent(
        QStringLiteral("room.action.result"),
        requestId,
        QStringLiteral("removeMember"),
        fingerprint,
        QStringLiteral("succeeded")));
    QVERIFY(actions.busy());
    const auto membersRefresh =
        lastCommand(dispatcher, QStringLiteral("room.refreshMembers"));
    actions.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.members")},
        {QStringLiteral("room_id"), QStringLiteral("mission")},
        {QStringLiteral("hydration_id"),
         membersRefresh.value(QStringLiteral("hydration_id"))},
        {QStringLiteral("members"), QJsonArray {}},
    }));
    QVERIFY(!actions.busy());
}

void MissionActionsTest::recoveredTaskReceiptsWithoutIntentReconcile()
{
    const auto exercise =
        [](const QString& operation,
           QJsonObject projectedTask,
           const bool startsUnknown) {
            FakeMissionMutationDispatcher dispatcher;
            kodosi::MissionDirectoryModel directory(dispatcher);
            seed(directory);
            kodosi::MissionDetailModel detail(dispatcher, directory);
            detail.ingestAuthEvent(auth());
            QVERIFY(detail.openMission(QStringLiteral("mission")));
            kodosi::PeopleModel people;
            seedPeople(people);
            kodosi::SessionCatalogModel sessions;
            kodosi::MissionActions actions(
                dispatcher, directory, detail, people, sessions, 1);
            actions.ingestAuthEvent(auth());

            const auto requestId =
                QUuid::createUuidV7().toString(QUuid::WithoutBraces);
            const auto fingerprint = QString(64, u'a');
            actions.ingestRoomEvent(actionEvent(
                QStringLiteral("room.mutation.recovered"),
                requestId,
                operation,
                fingerprint,
                startsUnknown ? QStringLiteral("unknown")
                              : QStringLiteral("succeeded"),
                QStringLiteral("task")));
            QVERIFY(actions.busy());
            if (startsUnknown) {
                QCOMPARE(
                    dispatcher.commands.back()
                        .value(QStringLiteral("type"))
                        .toString(),
                    QStringLiteral("room.mutations.reconcile"));
                actions.ingestRoomEvent(actionEvent(
                    QStringLiteral("room.action.result"),
                    requestId,
                    operation,
                    fingerprint,
                    QStringLiteral("succeeded"),
                    QStringLiteral("task")));
            }

            const auto refresh =
                lastCommand(dispatcher, QStringLiteral("room.tasks.list"));
            QVERIFY(!refresh.isEmpty());
            const auto event = envelope({
                {QStringLiteral("type"),
                 QStringLiteral("room.tasks.page")},
                {QStringLiteral("room_id"),
                 QStringLiteral("mission")},
                {QStringLiteral("hydration_id"),
                 refresh.value(QStringLiteral("hydration_id"))},
                {QStringLiteral("request_offset"), 0},
                {QStringLiteral("has_more"), false},
                {QStringLiteral("tasks"),
                 QJsonArray {std::move(projectedTask)}},
            });
            detail.ingestRoomEvent(event);
            actions.ingestRoomEvent(event);
            QVERIFY(!actions.busy());
            QCOMPARE(
                actions.ledgerOutcome(),
                kodosi::MissionActions::Outcome::Succeeded);
            QVERIFY(actions.ledgerError().contains(
                QStringLiteral("protocol v37"),
                Qt::CaseInsensitive));
        };

    auto transitioned = taskEntity(
        QStringLiteral("task"),
        QStringLiteral("Ship"),
        QStringLiteral("Recovered transition"));
    transitioned.insert(QStringLiteral("status"), QStringLiteral("Review"));
    transitioned.insert(QStringLiteral("revision"), 8);
    transitioned.insert(QStringLiteral("result"), QStringLiteral("evidence"));
    exercise(
        QStringLiteral("tasks.transition"),
        std::move(transitioned),
        true);

    auto assigned = taskEntity(
        QStringLiteral("task"),
        QStringLiteral("Ship"),
        QStringLiteral("Recovered assignment"),
        QStringLiteral("backend-agent"),
        QStringLiteral("backend-incarnation"));
    assigned.insert(QStringLiteral("revision"), 9);
    exercise(
        QStringLiteral("tasks.assign"),
        std::move(assigned),
        false);

    auto unassigned = taskEntity(
        QStringLiteral("task"),
        QStringLiteral("Ship"),
        QStringLiteral("Recovered unassignment"));
    unassigned.insert(QStringLiteral("revision"), 10);
    exercise(
        QStringLiteral("tasks.assign"),
        std::move(unassigned),
        false);
}

void MissionActionsTest::recoveredTaskReceiptMalformedProjectionStaysUnknown()
{
    {
        FakeMissionMutationDispatcher dispatcher;
        kodosi::MissionDirectoryModel directory(dispatcher);
        seed(directory);
        kodosi::MissionDetailModel detail(dispatcher, directory);
        detail.ingestAuthEvent(auth());
        QVERIFY(detail.openMission(QStringLiteral("mission")));
        kodosi::PeopleModel people;
        seedPeople(people);
        kodosi::SessionCatalogModel sessions;
        kodosi::MissionActions actions(
            dispatcher, directory, detail, people, sessions, 1);
        actions.ingestAuthEvent(auth());

        const auto requestId =
            QUuid::createUuidV7().toString(QUuid::WithoutBraces);
        actions.ingestRoomEvent(envelope({
            {QStringLiteral("type"),
             QStringLiteral("room.mutation.recovered")},
            {QStringLiteral("requestId"), requestId},
            {QStringLiteral("operation"),
             QStringLiteral("tasks.transition")},
            {QStringLiteral("roomId"), QStringLiteral("mission")},
            {QStringLiteral("fingerprint"), QString(64, u'b')},
            {QStringLiteral("status"), QStringLiteral("succeeded")},
        }));
        QVERIFY(actions.busy());
        QCOMPARE(
            actions.ledgerOutcome(),
            kodosi::MissionActions::Outcome::Unknown);
        QVERIFY(actions.ledgerError().contains(
            QStringLiteral("entity"),
            Qt::CaseInsensitive));
    }

    {
        FakeMissionMutationDispatcher dispatcher;
        kodosi::MissionDirectoryModel directory(dispatcher);
        seed(directory);
        kodosi::MissionDetailModel detail(dispatcher, directory);
        detail.ingestAuthEvent(auth());
        QVERIFY(detail.openMission(QStringLiteral("mission")));
        kodosi::PeopleModel people;
        seedPeople(people);
        kodosi::SessionCatalogModel sessions;
        kodosi::MissionActions actions(
            dispatcher, directory, detail, people, sessions, 1);
        actions.ingestAuthEvent(auth());

        const auto requestId =
            QUuid::createUuidV7().toString(QUuid::WithoutBraces);
        actions.ingestRoomEvent(actionEvent(
            QStringLiteral("room.mutation.recovered"),
            requestId,
            QStringLiteral("tasks.assign"),
            QString(64, u'c'),
            QStringLiteral("succeeded"),
            QStringLiteral("task")));
        const auto refresh =
            lastCommand(dispatcher, QStringLiteral("room.tasks.list"));
        QVERIFY(!refresh.isEmpty());
        auto malformed = taskEntity(
            QStringLiteral("task"),
            QStringLiteral("Malformed"));
        malformed.remove(QStringLiteral("updatedAt"));
        actions.ingestRoomEvent(envelope({
            {QStringLiteral("type"),
             QStringLiteral("room.tasks.page")},
            {QStringLiteral("room_id"), QStringLiteral("mission")},
            {QStringLiteral("hydration_id"),
             refresh.value(QStringLiteral("hydration_id"))},
            {QStringLiteral("request_offset"), 0},
            {QStringLiteral("has_more"), false},
            {QStringLiteral("tasks"), QJsonArray {malformed}},
        }));
        QVERIFY(actions.busy());
        QCOMPARE(
            actions.ledgerOutcome(),
            kodosi::MissionActions::Outcome::Unknown);
        QVERIFY(actions.ledgerError().contains(
            QStringLiteral("invalid"),
            Qt::CaseInsensitive));
    }
}

void MissionActionsTest::acceptsRuntimeLedgerCapacity()
{
    FakeMissionMutationDispatcher dispatcher;
    kodosi::MissionDirectoryModel directory(dispatcher);
    kodosi::MissionDetailModel detail(dispatcher, directory);
    kodosi::PeopleModel people;
    kodosi::SessionCatalogModel sessions;
    kodosi::MissionActions actions(
        dispatcher, directory, detail, people, sessions, 60'000);
    actions.ingestAuthEvent(auth());

    for (auto index = 0; index < 256; ++index) {
        actions.ingestRoomEvent(envelope({
            {QStringLiteral("type"),
             QStringLiteral("room.mutation.recovered")},
            {QStringLiteral("requestId"),
             QUuid::createUuidV7().toString(QUuid::WithoutBraces)},
            {QStringLiteral("operation"),
             QStringLiteral("tasks.assign")},
            {QStringLiteral("roomId"),
             QStringLiteral("mission-%1").arg(index)},
            {QStringLiteral("fingerprint"), QString(64, u'c')},
            {QStringLiteral("status"), QStringLiteral("unknown")},
        }));
    }
    QCOMPARE(actions.pendingMissionIds().size(), 256);
    QVERIFY(actions.busy());

    actions.clearError();
    actions.ingestRoomEvent(envelope({
        {QStringLiteral("type"),
         QStringLiteral("room.mutation.recovered")},
        {QStringLiteral("requestId"),
         QUuid::createUuidV7().toString(QUuid::WithoutBraces)},
        {QStringLiteral("operation"), QStringLiteral("tasks.assign")},
        {QStringLiteral("roomId"), QStringLiteral("mission-overflow")},
        {QStringLiteral("fingerprint"), QString(64, u'd')},
        {QStringLiteral("status"), QStringLiteral("unknown")},
    }));
    QCOMPARE(actions.pendingMissionIds().size(), 256);
    QVERIFY(actions.lastError().contains(
        QStringLiteral("Too many recovered Mission changes")));
}

void MissionActionsTest::retiredReceiptReplayRestoresRecovery()
{
    FakeMissionMutationDispatcher dispatcher;
    kodosi::MissionDirectoryModel directory(dispatcher);
    seed(directory);
    kodosi::MissionDetailModel detail(dispatcher, directory);
    detail.ingestAuthEvent(auth());
    seedMembers(dispatcher, detail);
    seedTask(dispatcher, detail, 7, true);
    kodosi::PeopleModel people;
    seedPeople(people);
    kodosi::SessionCatalogModel sessions;
    seedSessions(sessions);
    kodosi::MissionActions actions(
        dispatcher, directory, detail, people, sessions);
    actions.ingestAuthEvent(auth());

    QVERIFY(actions.transitionTask(
        QStringLiteral("mission"),
        QStringLiteral("task"),
        QStringLiteral("Review"),
        QStringLiteral("evidence")));
    const auto requestId =
        dispatcher.commands.back().value(QStringLiteral("requestId")).toString();
    const auto fingerprint = QString(64, u'd');
    actions.ingestRoomEvent(actionEvent(
        QStringLiteral("room.action.accepted"),
        requestId,
        QStringLiteral("tasks.transition"),
        fingerprint));
    actions.ingestRoomEvent(actionEvent(
        QStringLiteral("room.action.result"),
        requestId,
        QStringLiteral("tasks.transition"),
        fingerprint,
        QStringLiteral("succeeded"),
        QStringLiteral("task")));
    const auto projectTransition = [&] {
        const auto refresh =
            lastCommand(dispatcher, QStringLiteral("room.tasks.list"));
        auto projected = taskEntity(
            QStringLiteral("task"),
            QStringLiteral("Ship Linux"),
            QStringLiteral("Package it"),
            QStringLiteral("backend-agent"),
            QStringLiteral("backend-incarnation"));
        projected.insert(QStringLiteral("status"), QStringLiteral("Review"));
        projected.insert(QStringLiteral("revision"), 8);
        projected.insert(QStringLiteral("result"), QStringLiteral("evidence"));
        const auto event = envelope({
            {QStringLiteral("type"),
             QStringLiteral("room.tasks.page")},
            {QStringLiteral("room_id"), QStringLiteral("mission")},
            {QStringLiteral("hydration_id"),
             refresh.value(QStringLiteral("hydration_id"))},
            {QStringLiteral("request_offset"), 0},
            {QStringLiteral("has_more"), false},
            {QStringLiteral("tasks"), QJsonArray {projected}},
        });
        detail.ingestRoomEvent(event);
        actions.ingestRoomEvent(event);
    };
    projectTransition();
    QVERIFY(!actions.busy());
    QCOMPARE(
        actions.ledgerOutcome(),
        kodosi::MissionActions::Outcome::Succeeded);

    QVERIFY(actions.assignTask(
        QStringLiteral("mission"),
        QStringLiteral("task"),
        QString {}));
    const auto nextRequest =
        dispatcher.commands.back().value(QStringLiteral("requestId")).toString();
    actions.ingestRoomEvent(actionEvent(
        QStringLiteral("room.action.result"),
        nextRequest,
        QStringLiteral("tasks.assign"),
        {},
        QStringLiteral("failed")));
    QVERIFY(!actions.busy());

    actions.resetRuntimeAuthority();
    actions.ingestAuthEvent(auth());
    actions.ingestRoomEvent(actionEvent(
        QStringLiteral("room.mutation.recovered"),
        requestId,
        QStringLiteral("tasks.transition"),
        fingerprint,
        QStringLiteral("succeeded"),
        QStringLiteral("task")));
    QVERIFY(actions.busy());
    QCOMPARE(
        actions.ledgerOutcome(),
        kodosi::MissionActions::Outcome::Reconciling);
    QVERIFY(actions.ledgerError().contains(
        QStringLiteral("no positive retirement acknowledgement"),
        Qt::CaseInsensitive));
    projectTransition();
    QVERIFY(!actions.busy());
    QCOMPARE(
        actions.ledgerOutcome(),
        kodosi::MissionActions::Outcome::Succeeded);
}

void MissionActionsTest::retirementTombstonesAreBounded()
{
    FakeMissionMutationDispatcher dispatcher;
    kodosi::MissionDirectoryModel directory(dispatcher);
    kodosi::MissionDetailModel detail(dispatcher, directory);
    kodosi::PeopleModel people;
    kodosi::SessionCatalogModel sessions;
    kodosi::MissionActions actions(
        dispatcher, directory, detail, people, sessions, 60'000);
    actions.ingestAuthEvent(auth());

    QString firstRequestId;
    QString lastRequestId;
    QString firstMissionId;
    QString lastMissionId;
    for (auto index = 0; index < 257; ++index) {
        const auto requestId =
            QUuid::createUuidV7().toString(QUuid::WithoutBraces);
        const auto missionId =
            QStringLiteral("retired-mission-%1").arg(index);
        const auto entityId =
            QStringLiteral("retired-task-%1").arg(index);
        if (index == 0) {
            firstRequestId = requestId;
            firstMissionId = missionId;
        }
        lastRequestId = requestId;
        lastMissionId = missionId;
        actions.ingestRoomEvent(envelope({
            {QStringLiteral("type"),
             QStringLiteral("room.mutation.recovered")},
            {QStringLiteral("requestId"), requestId},
            {QStringLiteral("operation"),
             QStringLiteral("tasks.assign")},
            {QStringLiteral("roomId"), missionId},
            {QStringLiteral("fingerprint"), QString(64, u'e')},
            {QStringLiteral("status"), QStringLiteral("succeeded")},
            {QStringLiteral("entityId"), entityId},
        }));
        const auto refresh =
            lastCommand(dispatcher, QStringLiteral("room.tasks.list"));
        auto projected = taskEntity(
            entityId,
            QStringLiteral("Retired task"));
        projected.insert(QStringLiteral("roomId"), missionId);
        actions.ingestRoomEvent(envelope({
            {QStringLiteral("type"),
             QStringLiteral("room.tasks.page")},
            {QStringLiteral("room_id"), missionId},
            {QStringLiteral("hydration_id"),
             refresh.value(QStringLiteral("hydration_id"))},
            {QStringLiteral("request_offset"), 0},
            {QStringLiteral("has_more"), false},
            {QStringLiteral("tasks"), QJsonArray {projected}},
        }));
        QVERIFY(!actions.busy());
    }

    actions.clearError();
    actions.ingestRoomEvent(envelope({
        {QStringLiteral("type"),
         QStringLiteral("room.mutation.recovered")},
        {QStringLiteral("requestId"), firstRequestId},
        {QStringLiteral("operation"), QStringLiteral("tasks.assign")},
        {QStringLiteral("roomId"), firstMissionId},
        {QStringLiteral("fingerprint"), QString(64, u'e')},
        {QStringLiteral("status"), QStringLiteral("unknown")},
    }));
    QVERIFY(actions.lastError().isEmpty());

    actions.ingestRoomEvent(envelope({
        {QStringLiteral("type"),
         QStringLiteral("room.mutation.recovered")},
        {QStringLiteral("requestId"), lastRequestId},
        {QStringLiteral("operation"), QStringLiteral("tasks.assign")},
        {QStringLiteral("roomId"), lastMissionId},
        {QStringLiteral("fingerprint"), QString(64, u'e')},
        {QStringLiteral("status"), QStringLiteral("unknown")},
    }));
    QVERIFY(actions.lastError().contains(
        QStringLiteral("locally retired receipt"),
        Qt::CaseInsensitive));
}

void MissionActionsTest::durableUnknownCannotBeDiscarded()
{
    FakeMissionMutationDispatcher dispatcher;
    kodosi::MissionDirectoryModel directory(dispatcher);
    seed(directory);
    kodosi::MissionDetailModel detail(dispatcher, directory);
    detail.ingestAuthEvent(auth());
    seedMembers(dispatcher, detail);
    kodosi::PeopleModel people;
    seedPeople(people);
    kodosi::SessionCatalogModel sessions;
    kodosi::MissionActions actions(
        dispatcher, directory, detail, people, sessions, 1);
    actions.ingestAuthEvent(auth());
    const auto recoveriesBeforeTimeout = std::ranges::count(
        dispatcher.commands,
        QStringLiteral("room.mutations.recover"),
        [](const QJsonObject& command) {
            return command.value(QStringLiteral("type")).toString();
        });

    QVERIFY(actions.removeMember(
        QStringLiteral("mission"),
        QStringLiteral("member")));
    const auto requestId =
        dispatcher.commands.back().value(QStringLiteral("requestId")).toString();
    actions.ingestRoomEvent(actionEvent(
        QStringLiteral("room.action.accepted"),
        requestId,
        QStringLiteral("removeMember"),
        QString(64, u'e')));
    QTRY_VERIFY_WITH_TIMEOUT(actions.canRetryUnknown(), 500);
    QVERIFY(std::ranges::count(
        dispatcher.commands,
        QStringLiteral("room.mutations.recover"),
        [](const QJsonObject& command) {
            return command.value(QStringLiteral("type")).toString();
        }) > recoveriesBeforeTimeout);
    QVERIFY(actions.busy());
    QVERIFY(actions.canRetryUnknown());
    QVERIFY(!actions.canDiscardUnknown());
    QVERIFY(!actions.discardUnknown());
    QVERIFY(!actions.removeMember(
        QStringLiteral("mission"),
        QStringLiteral("member")));
    QVERIFY(actions.retryUnknown());
    QVERIFY(!actions.canRetryUnknown());
    QVERIFY(actions.busy());
}

void MissionActionsTest::taskActionsUseNativeRevisionsAndIncarnations()
{
    FakeMissionMutationDispatcher dispatcher;
    kodosi::MissionDirectoryModel directory(dispatcher);
    seed(directory);
    kodosi::MissionDetailModel detail(dispatcher, directory);
    detail.ingestAuthEvent(auth());
    seedMembers(dispatcher, detail);
    seedTask(dispatcher, detail, 7, true);
    kodosi::PeopleModel people;
    people.ingestAuthEvent(auth());
    kodosi::SessionCatalogModel sessions;
    seedSessions(sessions);
    kodosi::MissionActions actions(
        dispatcher, directory, detail, people, sessions);
    actions.ingestAuthEvent(auth());
    const auto projectTask =
        [&](QJsonObject projectedTask) {
            const auto refresh =
                lastCommand(
                    dispatcher,
                    QStringLiteral("room.tasks.list"));
            QVERIFY(!refresh.isEmpty());
            const auto event = envelope({
                {QStringLiteral("type"),
                 QStringLiteral("room.tasks.page")},
                {QStringLiteral("room_id"),
                 QStringLiteral("mission")},
                {QStringLiteral("hydration_id"),
                 refresh.value(QStringLiteral("hydration_id"))},
                {QStringLiteral("request_offset"),
                 refresh.value(QStringLiteral("offset"))},
                {QStringLiteral("has_more"), false},
                {QStringLiteral("tasks"),
                 QJsonArray {std::move(projectedTask)}},
            });
            detail.ingestRoomEvent(event);
            actions.ingestRoomEvent(event);
        };

    QVERIFY(actions.canActOnSelectedTasks());
    QCOMPARE(actions.assignmentOptions().size(), 2);
    const auto transitionOptions =
        actions.transitionOptionsForTask(QStringLiteral("task"));
    QCOMPARE(transitionOptions.size(), 4);
    QCOMPARE(
        transitionOptions.at(0)
            .toMap()
            .value(QStringLiteral("status"))
            .toString(),
        QStringLiteral("Open"));
    QCOMPARE(
        transitionOptions.at(1)
            .toMap()
            .value(QStringLiteral("status"))
            .toString(),
        QStringLiteral("Review"));
    QVERIFY(
        transitionOptions.at(1)
            .toMap()
            .value(QStringLiteral("requiresEvidence"))
            .toBool());
    QCOMPARE(
        transitionOptions.constLast()
            .toMap()
            .value(QStringLiteral("status"))
            .toString(),
        QStringLiteral("Archived"));
    QCOMPARE(
        actions.assignmentForTask(QStringLiteral("task")),
        QStringLiteral("local-agent"));
    QVERIFY(!actions.transitionTask(
        QStringLiteral("mission"),
        QStringLiteral("task"),
        QStringLiteral("Review"),
        QString {}));
    QVERIFY(actions.transitionTask(
        QStringLiteral("mission"),
        QStringLiteral("task"),
        QStringLiteral("Review"),
        QStringLiteral(" evidence ")));
    auto command = dispatcher.commands.back();
    QCOMPARE(
        command.value(QStringLiteral("type")).toString(),
        QStringLiteral("room.tasks.transition"));
    QCOMPARE(
        command.value(QStringLiteral("expected_task_revision")).toInteger(),
        7);
    QCOMPARE(
        command.value(QStringLiteral("to_status")).toString(),
        QStringLiteral("Review"));
    QCOMPARE(
        command.value(QStringLiteral("result")).toString(),
        QStringLiteral("evidence"));
    QVERIFY(!command.contains(QStringLiteral("actor_session_id")));
    const auto transitionId =
        command.value(QStringLiteral("requestId")).toString();
    actions.ingestRoomEvent(actionEvent(
        QStringLiteral("room.action.accepted"),
        transitionId,
        QStringLiteral("tasks.transition"),
        QString(64, u'a')));
    actions.ingestRoomEvent(actionEvent(
        QStringLiteral("room.action.result"),
        transitionId,
        QStringLiteral("tasks.transition"),
        QString(64, u'a'),
        QStringLiteral("succeeded")));
    QVERIFY(actions.busy());
    auto transitioned = taskEntity(
        QStringLiteral("task"),
        QStringLiteral("Ship Linux"),
        QStringLiteral("Package it"),
        QStringLiteral("backend-agent"),
        QStringLiteral("backend-incarnation"));
    transitioned.insert(QStringLiteral("status"), QStringLiteral("Review"));
    transitioned.insert(QStringLiteral("revision"), 8);
    transitioned.insert(QStringLiteral("result"), QStringLiteral("evidence"));
    projectTask(std::move(transitioned));
    QVERIFY(!actions.busy());

    QVERIFY(actions.assignTask(
        QStringLiteral("mission"),
        QStringLiteral("task"),
        QString {}));
    command = dispatcher.commands.back();
    QCOMPARE(
        command.value(QStringLiteral("type")).toString(),
        QStringLiteral("room.tasks.assign"));
    QVERIFY(!command.contains(QStringLiteral("session_id")));
    QVERIFY(!command.contains(QStringLiteral("session_incarnation_id")));
    const auto unassignId =
        command.value(QStringLiteral("requestId")).toString();
    actions.ingestRoomEvent(actionEvent(
        QStringLiteral("room.action.accepted"),
        unassignId,
        QStringLiteral("tasks.assign"),
        QString(64, u'c')));
    actions.ingestRoomEvent(actionEvent(
        QStringLiteral("room.action.result"),
        unassignId,
        QStringLiteral("tasks.assign"),
        QString(64, u'c'),
        QStringLiteral("succeeded")));
    QVERIFY(actions.busy());
    auto unassigned = taskEntity(
        QStringLiteral("task"),
        QStringLiteral("Ship Linux"),
        QStringLiteral("Package it"));
    unassigned.insert(QStringLiteral("status"), QStringLiteral("Review"));
    unassigned.insert(QStringLiteral("revision"), 9);
    unassigned.insert(QStringLiteral("result"), QStringLiteral("evidence"));
    projectTask(std::move(unassigned));
    QVERIFY(!actions.busy());

    const auto assignmentRevision = actions.assignmentRevision();
    seedSessions(sessions, QStringLiteral("backend-incarnation-2"));
    QVERIFY(actions.assignmentRevision() > assignmentRevision);
    QVERIFY(actions.assignmentForTask(QStringLiteral("task")).isEmpty());

    QVERIFY(actions.assignTask(
        QStringLiteral("mission"),
        QStringLiteral("task"),
        QStringLiteral("local-agent")));
    command = dispatcher.commands.back();
    QCOMPARE(
        command.value(QStringLiteral("expected_task_revision")).toInteger(),
        9);
    QCOMPARE(
        command.value(QStringLiteral("session_id")).toString(),
        QStringLiteral("backend-agent"));
    QCOMPARE(
        command.value(QStringLiteral("session_incarnation_id")).toString(),
        QStringLiteral("backend-incarnation-2"));
    const auto assignmentId =
        command.value(QStringLiteral("requestId")).toString();
    const auto assignmentFingerprint = QString(64, u'e');
    actions.ingestRoomEvent(actionEvent(
        QStringLiteral("room.action.accepted"),
        assignmentId,
        QStringLiteral("tasks.assign"),
        assignmentFingerprint));
    actions.ingestRoomEvent(actionEvent(
        QStringLiteral("room.action.result"),
        assignmentId,
        QStringLiteral("tasks.assign"),
        assignmentFingerprint,
        QStringLiteral("succeeded")));
    QVERIFY(actions.busy());
    QCOMPARE(detail.tasks()->rowCount(), 1);
    QVERIFY(detail.membersReady());
    QVERIFY(!detail.tasksReady());
    QVERIFY(!actions.canActOnSelectedTasks());

    auto assigned = taskEntity(
        QStringLiteral("task"),
        QStringLiteral("Ship Linux"),
        QStringLiteral("Package it"),
        QStringLiteral("backend-agent"),
        QStringLiteral("backend-incarnation-2"));
    assigned.insert(QStringLiteral("status"), QStringLiteral("Review"));
    assigned.insert(QStringLiteral("revision"), 10);
    assigned.insert(QStringLiteral("result"), QStringLiteral("evidence"));
    projectTask(std::move(assigned));
    QVERIFY(!actions.busy());
    QVERIFY(detail.tasksReady());
    QVERIFY(actions.canActOnSelectedTasks());
    QCOMPARE(
        actions.assignmentForTask(QStringLiteral("task")),
        QStringLiteral("local-agent"));

    auto futureTask = taskEntity(
        QStringLiteral("task"),
        QStringLiteral("Ship Linux"),
        QStringLiteral("Package it"),
        QStringLiteral("backend-agent"),
        QStringLiteral("backend-incarnation-2"));
    futureTask.insert(
        QStringLiteral("status"),
        QStringLiteral("FutureRuntimeStatus"));
    futureTask.insert(QStringLiteral("revision"), 11);
    QVERIFY(detail.refreshTasks());
    projectTask(std::move(futureTask));
    const auto commandsBeforeUnknownAssignment = dispatcher.commands.size();
    QVERIFY(!actions.assignTask(
        QStringLiteral("mission"),
        QStringLiteral("task"),
        QString {}));
    QVERIFY(!actions.assignTask(
        QStringLiteral("mission"),
        QStringLiteral("task"),
        QStringLiteral("local-agent")));
    QCOMPARE(dispatcher.commands.size(), commandsBeforeUnknownAssignment);
}

void MissionActionsTest::reconcilesTaskAfterMissionSwitch()
{
    FakeMissionMutationDispatcher dispatcher;
    kodosi::MissionDirectoryModel directory(dispatcher);
    seed(directory);
    kodosi::MissionDetailModel detail(dispatcher, directory);
    detail.ingestAuthEvent(auth());
    seedMembers(dispatcher, detail);
    kodosi::PeopleModel people;
    people.ingestAuthEvent(auth());
    kodosi::SessionCatalogModel sessions;
    kodosi::MissionActions actions(
        dispatcher, directory, detail, people, sessions, 50);
    actions.ingestAuthEvent(auth());
    QSignalSpy completed(&actions, &kodosi::MissionActions::taskCompleted);
    const auto reconciliationCommandCount = [&] {
        return std::ranges::count_if(
            dispatcher.commands,
            [](const QJsonObject& command) {
                return command.value(QStringLiteral("type")).toString()
                        == QStringLiteral("room.tasks.list")
                    && command.value(QStringLiteral("room_id")).toString()
                        == QStringLiteral("mission");
            });
    };
    const auto taskListsBeforeMutation = reconciliationCommandCount();

    QVERIFY(actions.createTask(
        QStringLiteral("mission"),
        QStringLiteral("Ship"),
        QStringLiteral("Linux")));
    const auto requestId =
        dispatcher.commands.back().value(QStringLiteral("requestId")).toString();
    QVERIFY(detail.openMission(QStringLiteral("other-mission")));
    QTRY_VERIFY_WITH_TIMEOUT(
        reconciliationCommandCount() > taskListsBeforeMutation,
        500);

    QJsonObject firstPageCommand;
    for (auto command = dispatcher.commands.crbegin();
         command != dispatcher.commands.crend();
         ++command) {
        if (command->value(QStringLiteral("type")).toString()
                == QStringLiteral("room.tasks.list")
            && command->value(QStringLiteral("room_id")).toString()
                == QStringLiteral("mission")) {
            firstPageCommand = *command;
            break;
        }
    }
    QVERIFY(!firstPageCommand.isEmpty());
    const auto hydrationId =
        firstPageCommand.value(QStringLiteral("hydration_id"));
    actions.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.tasks.page")},
        {QStringLiteral("room_id"), QStringLiteral("mission")},
        {QStringLiteral("hydration_id"), hydrationId},
        {QStringLiteral("request_offset"), 0},
        {QStringLiteral("has_more"), true},
        {QStringLiteral("next_offset"), 1},
        {QStringLiteral("tasks"), QJsonArray {}},
    }));
    QCOMPARE(
        dispatcher.commands.back().value(QStringLiteral("offset")).toInteger(),
        1);
    actions.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.tasks.page")},
        {QStringLiteral("room_id"), QStringLiteral("mission")},
        {QStringLiteral("hydration_id"), hydrationId},
        {QStringLiteral("request_offset"), 1},
        {QStringLiteral("has_more"), false},
        {QStringLiteral("tasks"),
         QJsonArray {
             taskEntity(
                 requestId,
                 QStringLiteral("Ship"),
                 QStringLiteral("Linux")),
         }},
    }));
    QCOMPARE(completed.count(), 1);
    QVERIFY(!actions.busy());
}

void MissionActionsTest::transientRuntimeResetPreservesDraftsAndCorrelations()
{
    FakeMissionMutationDispatcher dispatcher;
    kodosi::MissionDirectoryModel directory(dispatcher);
    seed(directory);
    kodosi::MissionDetailModel detail(dispatcher, directory);
    detail.ingestAuthEvent(auth());
    seedMembers(dispatcher, detail);
    kodosi::PeopleModel people;
    people.ingestAuthEvent(auth());
    kodosi::SessionCatalogModel sessions;
    kodosi::MissionActions actions(
        dispatcher, directory, detail, people, sessions);
    actions.ingestAuthEvent(auth());

    actions.setCreateMissionName(QStringLiteral("Restart-safe Mission"));
    actions.setCreateMissionSlug(QStringLiteral("restart-safe-mission"));
    actions.setChatDraftBody(QStringLiteral("Preserve this chat draft"));
    actions.setTaskDraftTitle(QStringLiteral("Preserve this task"));
    actions.setTaskDraftDescription(QStringLiteral("Still here after restart"));
    QVERIFY(actions.createMission());
    QVERIFY(actions.busy());

    actions.resetRuntimeAuthority();
    QCOMPARE(
        actions.createMissionOutcome(),
        kodosi::MissionActions::Outcome::Unknown);
    QCOMPARE(
        actions.createMissionName(),
        QStringLiteral("Restart-safe Mission"));
    QCOMPARE(
        actions.chatDraftBody(),
        QStringLiteral("Preserve this chat draft"));
    QCOMPARE(
        actions.taskDraftTitle(),
        QStringLiteral("Preserve this task"));
    QCOMPARE(
        actions.taskDraftDescription(),
        QStringLiteral("Still here after restart"));
    QVERIFY(actions.busy());
    QVERIFY(!actions.createMissionCanSubmit());

    const auto refreshesBefore = std::ranges::count(
        dispatcher.commands,
        QStringLiteral("room.refresh"),
        [](const QJsonObject& command) {
            return command.value(QStringLiteral("type")).toString();
        });
    actions.ingestAuthEvent(auth());
    QCOMPARE(
        actions.createMissionOutcome(),
        kodosi::MissionActions::Outcome::Reconciling);
    QCOMPARE(
        std::ranges::count(
            dispatcher.commands,
            QStringLiteral("room.refresh"),
            [](const QJsonObject& command) {
                return command.value(QStringLiteral("type")).toString();
            }),
        refreshesBefore + 1);
    QVERIFY(actions.busy());
    QVERIFY(!actions.createMissionCanSubmit());
}

void MissionActionsTest::accountTransitionDropsLateResults()
{
    FakeMissionMutationDispatcher dispatcher;
    kodosi::MissionDirectoryModel directory(dispatcher);
    seed(directory);
    kodosi::MissionDetailModel detail(dispatcher, directory);
    detail.ingestAuthEvent(auth());
    kodosi::PeopleModel people;
    people.ingestAuthEvent(auth());
    kodosi::SessionCatalogModel sessions;
    kodosi::MissionActions actions(
        dispatcher, directory, detail, people, sessions);
    actions.ingestAuthEvent(auth());
    QSignalSpy completed(&actions, &kodosi::MissionActions::chatCompleted);

    QVERIFY(actions.postBroadcast(
        QStringLiteral("mission"),
        QStringLiteral("hello")));
    const auto requestId =
        dispatcher.commands.back().value(QStringLiteral("requestId")).toString();
    actions.ingestAuthEvent(auth(QStringLiteral("other"), 2));
    QVERIFY(!actions.busy());
    actions.ingestRoomEvent(actionEvent(
        QStringLiteral("room.action.result"),
        requestId,
        QStringLiteral("chat.post"),
        {},
        QStringLiteral("succeeded")));
    QCOMPARE(completed.count(), 0);
    QVERIFY(!actions.busy());
}

void MissionActionsTest::reconcilesMissingReceipts()
{
    FakeMissionMutationDispatcher dispatcher;
    kodosi::MissionDirectoryModel directory(dispatcher);
    seed(directory);
    kodosi::MissionDetailModel detail(dispatcher, directory);
    detail.ingestAuthEvent(auth());
    QVERIFY(detail.openMission(QStringLiteral("mission")));
    kodosi::PeopleModel people;
    people.ingestAuthEvent(auth());
    kodosi::SessionCatalogModel sessions;
    kodosi::MissionActions actions(
        dispatcher, directory, detail, people, sessions, 1);
    actions.ingestAuthEvent(auth());
    QVERIFY(actions.postBroadcast(
        QStringLiteral("mission"),
        QStringLiteral("hello")));
    QVERIFY(detail.openMission(QStringLiteral("other-mission")));
    QTRY_VERIFY_WITH_TIMEOUT(actions.canDiscardUnknown(), 500);
    QVERIFY(std::ranges::find(
        dispatcher.commands,
        QStringLiteral("room.chat.list"),
        [](const QJsonObject& command) {
            return command.value(QStringLiteral("type")).toString();
        }) != dispatcher.commands.end());
    QVERIFY(actions.busy());
    QVERIFY(actions.lastError().contains(QStringLiteral("unconfirmed")));
    QVERIFY(actions.discardUnknown());
    QVERIFY(!actions.busy());
    QVERIFY(!actions.canDiscardUnknown());
}

void MissionActionsTest::createsMissionWithExactSlugAndEntityReconciliation()
{
    FakeMissionMutationDispatcher dispatcher;
    kodosi::MissionDirectoryModel directory(dispatcher);
    seed(directory);
    kodosi::MissionDetailModel detail(dispatcher, directory);
    kodosi::PeopleModel people;
    kodosi::SessionCatalogModel sessions;
    kodosi::MissionActions actions(
        dispatcher, directory, detail, people, sessions);
    actions.ingestAuthEvent(auth());
    QSignalSpy created(&actions, &kodosi::MissionActions::missionCreated);

    actions.setCreateMissionName(QStringLiteral("Launch room"));
    actions.setCreateMissionSlug(QStringLiteral(" Launch-room "));
    QVERIFY(!actions.createMission());
    actions.setCreateMissionSlug(QStringLiteral("Launch-room"));
    QVERIFY(!actions.createMission());
    actions.setCreateMissionSlug(QStringLiteral("launch-room"));
    QVERIFY(actions.createMission());

    const auto command = dispatcher.commands.back();
    QCOMPARE(
        command.value(QStringLiteral("type")).toString(),
        QStringLiteral("room.create"));
    QCOMPARE(
        command.value(QStringLiteral("name")).toString(),
        QStringLiteral("Launch room"));
    QCOMPARE(
        command.value(QStringLiteral("slug")).toString(),
        QStringLiteral("launch-room"));
    const auto requestId =
        command.value(QStringLiteral("requestId")).toString();
    QCOMPARE(
        QUuid(requestId).version(),
        QUuid::UnixEpoch);
    QCOMPARE(directory.rowCount(), 2);

    actions.setCreateMissionName(QStringLiteral("Next Mission"));
    actions.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.snapshot")},
        {QStringLiteral("rooms"),
         QJsonArray {
             QJsonObject {
                 {QStringLiteral("id"), requestId},
                 {QStringLiteral("name"), QStringLiteral("Launch room")},
                 {QStringLiteral("slug"), QStringLiteral("launch-room")},
             },
         }},
    }));
    QCOMPARE(created.count(), 0);
    QCOMPARE(
        actions.createMissionOutcome(),
        kodosi::MissionActions::Outcome::Pending);
    QCOMPARE(
        actions.createMissionName(),
        QStringLiteral("Next Mission"));
    actions.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.snapshot")},
        {QStringLiteral("rooms"),
         QJsonArray {
             roomEntity(
                 requestId,
                 QStringLiteral("Launch room"),
                 QStringLiteral("launch-room")),
         }},
    }));
    QCOMPARE(created.count(), 1);
    QCOMPARE(
        actions.createMissionOutcome(),
        kodosi::MissionActions::Outcome::Succeeded);
    QCOMPARE(
        actions.createMissionName(),
        QStringLiteral("Next Mission"));

    actions.setCreateMissionName(QStringLiteral("Ambiguous"));
    actions.setCreateMissionSlug(QStringLiteral("ambiguous"));
    QVERIFY(actions.createMission());
    const auto ambiguousId =
        dispatcher.commands.back().value(QStringLiteral("requestId")).toString();
    actions.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.snapshot")},
        {QStringLiteral("rooms"),
         QJsonArray {
             roomEntity(
                 ambiguousId,
                 QStringLiteral("Ambiguous"),
                 QStringLiteral("ambiguous")),
             roomEntity(
                 ambiguousId,
                 QStringLiteral("Ambiguous"),
                 QStringLiteral("ambiguous")),
         }},
    }));
    QCOMPARE(
        actions.createMissionOutcome(),
        kodosi::MissionActions::Outcome::Unknown);
    QVERIFY(actions.createMissionCanCheck());
    QVERIFY(actions.createMissionError().contains(
        QStringLiteral("ambiguous"),
        Qt::CaseInsensitive));
}

void MissionActionsTest::directedChatUsesOpaqueCurrentRecipientAuthority()
{
    FakeMissionMutationDispatcher dispatcher;
    kodosi::MissionDirectoryModel directory(dispatcher);
    seed(directory);
    kodosi::PeopleModel people;
    people.ingestAuthEvent(auth());
    kodosi::SessionCatalogModel sessions;
    seedSessions(sessions);
    kodosi::MissionDetailModel::Dependencies dependencies;
    dependencies.people = &people;
    dependencies.sessions = &sessions;
    kodosi::MissionDetailModel detail(
        dispatcher,
        directory,
        dependencies);
    detail.ingestAuthEvent(auth());
    seedMembers(dispatcher, detail);
    kodosi::MissionActions actions(
        dispatcher, directory, detail, people, sessions);
    actions.ingestAuthEvent(auth());

    const auto agent = crewPresentation(
        detail,
        kodosi::MissionCrewModel::Kind::Agent);
    const auto member = crewPresentation(
        detail,
        kodosi::MissionCrewModel::Kind::Member,
        QStringLiteral("Member"));
    QVERIFY(!agent.isEmpty());
    QVERIFY(!member.isEmpty());
    QVERIFY(QUuid(agent).isNull());
    QVERIFY(actions.toggleChatRecipient(member));
    QVERIFY(actions.toggleChatRecipient(agent));
    actions.setChatDraftBody(QStringLiteral(" directed "));
    QVERIFY(actions.sendChat());

    const auto command = dispatcher.commands.back();
    QCOMPARE(
        command.value(QStringLiteral("recipient_session_ids")).toArray(),
        QJsonArray {QStringLiteral("local-agent")});
    QCOMPARE(
        command.value(QStringLiteral("recipient_user_ids")).toArray(),
        QJsonArray {QStringLiteral("member")});
    const auto requestId =
        command.value(QStringLiteral("requestId")).toString();
    QSignalSpy completed(
        &actions,
        &kodosi::MissionActions::chatCompleted);

    actions.setChatDraftBody(QStringLiteral("next draft"));
    actions.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.chat.posted")},
        {QStringLiteral("room_id"), QStringLiteral("mission")},
        {QStringLiteral("message"),
         [&] {
             auto malformed = messageEntity(
                 requestId,
                 QStringLiteral("directed"),
                 QJsonArray {QStringLiteral("local-agent")},
                 QJsonArray {QStringLiteral("member")});
             malformed.remove(QStringLiteral("recipientUserIds"));
             return malformed;
         }()},
    }));
    QCOMPARE(completed.count(), 0);
    QCOMPARE(
        actions.chatOutcome(),
        kodosi::MissionActions::Outcome::Pending);
    QCOMPARE(actions.chatDraftBody(), QStringLiteral("next draft"));
    actions.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.chat.posted")},
        {QStringLiteral("room_id"), QStringLiteral("mission")},
        {QStringLiteral("message"),
         [&] {
             auto malformed = messageEntity(
                 requestId,
                 QStringLiteral("directed"),
                 QJsonArray {QStringLiteral("local-agent")},
                 QJsonArray {QStringLiteral("member")});
             malformed.insert(
                 QStringLiteral("authorUserId"),
                 QJsonValue::Null);
             return malformed;
         }()},
    }));
    QCOMPARE(completed.count(), 0);
    QCOMPARE(
        actions.chatOutcome(),
        kodosi::MissionActions::Outcome::Pending);
    actions.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.chat.posted")},
        {QStringLiteral("room_id"), QStringLiteral("mission")},
        {QStringLiteral("message"),
         messageEntity(
             requestId,
             QStringLiteral("directed"),
             QJsonArray {QStringLiteral("local-agent")},
             QJsonArray {QStringLiteral("member")})},
    }));
    QCOMPARE(completed.count(), 1);
    QCOMPARE(actions.chatDraftBody(), QStringLiteral("next draft"));
    QCOMPARE(
        actions.chatOutcome(),
        kodosi::MissionActions::Outcome::Succeeded);

    QVERIFY(detail.openMission(QStringLiteral("other-mission")));
    actions.setChatDraftBody(QStringLiteral("other draft"));
    QVERIFY(detail.openMission(QStringLiteral("mission")));
    QCOMPARE(actions.chatDraftBody(), QStringLiteral("next draft"));
    QVERIFY(actions.chatRecipientPresentationIds().contains(agent));
    QVERIFY(actions.chatRecipientPresentationIds().contains(member));
    seedMembers(dispatcher, detail);
    QVERIFY(actions.chatRecipientPresentationIds().contains(agent));
    QVERIFY(actions.chatRecipientPresentationIds().contains(member));

    seedSessions(
        sessions,
        QStringLiteral("replacement-backend-incarnation"),
        QStringLiteral("replacement-runtime-incarnation"));
    QVERIFY(actions.chatRecipientPresentationIds().contains(agent));
    QVERIFY(!detail.crew()->containsPresentationId(agent));
    QVERIFY(actions.chatHasUnavailableRecipients());
    QVERIFY(!actions.chatCanSubmit());
    QVERIFY(actions.removeChatRecipient(agent));
}

void MissionActionsTest::taskDraftUsesExactAssignmentDueAndRevisionClearing()
{
    FakeMissionMutationDispatcher dispatcher;
    kodosi::MissionDirectoryModel directory(dispatcher);
    seed(directory);
    kodosi::PeopleModel people;
    people.ingestAuthEvent(auth());
    kodosi::SessionCatalogModel sessions;
    seedSessions(sessions);
    kodosi::MissionDetailModel::Dependencies dependencies;
    dependencies.people = &people;
    dependencies.sessions = &sessions;
    kodosi::MissionDetailModel detail(
        dispatcher,
        directory,
        dependencies);
    detail.ingestAuthEvent(auth());
    seedMembers(dispatcher, detail);
    kodosi::MissionActions actions(
        dispatcher, directory, detail, people, sessions);
    actions.ingestAuthEvent(auth());

    const auto assignee = crewPresentation(
        detail,
        kodosi::MissionCrewModel::Kind::Agent);
    QVERIFY(actions.setTaskDraftAssignment(assignee));
    actions.setTaskDraftTitle(QStringLiteral(" Ship "));
    actions.setTaskDraftDescription(QStringLiteral(" Evidence "));
    actions.setTaskDraftHasDueAt(true);
    const auto due =
        QDateTime::fromString(
            QStringLiteral("2030-01-02T03:04:05.000Z"),
            Qt::ISODateWithMs);
    actions.setTaskDraftDueAt(due);
    QVERIFY(actions.submitTaskCreate());

    const auto command = dispatcher.commands.back();
    QCOMPARE(
        command.value(QStringLiteral("title")).toString(),
        QStringLiteral("Ship"));
    QCOMPARE(
        command.value(QStringLiteral("description")).toString(),
        QStringLiteral("Evidence"));
    QCOMPARE(
        command.value(QStringLiteral("assigned_session_id")).toString(),
        QStringLiteral("backend-agent"));
    QCOMPARE(
        command.value(QStringLiteral("assigned_session_incarnation_id"))
            .toString(),
        QStringLiteral("backend-incarnation"));
    QCOMPARE(
        command.value(QStringLiteral("due_at")).toString(),
        QStringLiteral("2030-01-02T03:04:05.000Z"));
    const auto requestId =
        command.value(QStringLiteral("requestId")).toString();
    QSignalSpy completed(
        &actions,
        &kodosi::MissionActions::taskCompleted);

    actions.setTaskDraftTitle(QStringLiteral("Follow-up"));
    actions.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.tasks.upserted")},
        {QStringLiteral("room_id"), QStringLiteral("mission")},
        {QStringLiteral("task"),
         QJsonObject {
             {QStringLiteral("id"), requestId},
             {QStringLiteral("title"), QStringLiteral("Ship")},
             {QStringLiteral("description"), QStringLiteral("Evidence")},
             {QStringLiteral("assignedSessionId"),
              QStringLiteral("backend-agent")},
             {QStringLiteral("assignedSessionIncarnationId"),
              QStringLiteral("backend-incarnation")},
             {QStringLiteral("dueAt"),
              QStringLiteral("2030-01-02T03:04:05Z")},
         }},
    }));
    QCOMPARE(completed.count(), 0);
    QCOMPARE(
        actions.taskCreateOutcome(),
        kodosi::MissionActions::Outcome::Pending);
    QCOMPARE(actions.taskDraftTitle(), QStringLiteral("Follow-up"));
    actions.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.tasks.upserted")},
        {QStringLiteral("room_id"), QStringLiteral("mission")},
        {QStringLiteral("task"),
         taskEntity(
             requestId,
             QStringLiteral("Ship"),
             QStringLiteral("Evidence"),
             QStringLiteral("backend-agent"),
             QStringLiteral("backend-incarnation"),
             QStringLiteral("2030-01-02T03:04:05Z"))},
    }));
    QCOMPARE(completed.count(), 1);
    QCOMPARE(actions.taskDraftTitle(), QStringLiteral("Follow-up"));
    QCOMPARE(
        actions.taskCreateOutcome(),
        kodosi::MissionActions::Outcome::Succeeded);

    QVERIFY(actions.discardTaskCreate());
    actions.setTaskDraftTitle(QStringLiteral("Invalid projection"));
    actions.setTaskDraftHasDueAt(true);
    actions.setTaskDraftDueAt(due);
    QVERIFY(actions.submitTaskCreate());
    const auto invalidProjectionRequest =
        dispatcher.commands.back().value(QStringLiteral("requestId")).toString();
    actions.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.tasks.upserted")},
        {QStringLiteral("room_id"), QStringLiteral("mission")},
        {QStringLiteral("task"),
         [&] {
             auto malformed = taskEntity(
                 invalidProjectionRequest,
                 QStringLiteral("Invalid projection"),
                 {},
                 {},
                 {},
                 QStringLiteral("2030-01-02T03:04:05Z"));
             malformed.insert(QStringLiteral("result"), 7);
             return malformed;
         }()},
    }));
    QCOMPARE(
        actions.taskCreateOutcome(),
        kodosi::MissionActions::Outcome::Pending);
    QCOMPARE(completed.count(), 1);
    QCOMPARE(
        actions.taskDraftTitle(),
        QStringLiteral("Invalid projection"));
    actions.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.tasks.upserted")},
        {QStringLiteral("room_id"), QStringLiteral("mission")},
        {QStringLiteral("task"),
         taskEntity(
             invalidProjectionRequest,
             QStringLiteral("Invalid projection"),
             {},
             {},
             {},
             QStringLiteral("2030-01-02T03:04:05Z"))},
    }));
    QCOMPARE(completed.count(), 2);
    QVERIFY(actions.discardTaskCreate());

    actions.setTaskDraftTitle(QStringLiteral("Past due"));
    actions.setTaskDraftHasDueAt(true);
    actions.setTaskDraftDueAt(
        QDateTime::fromString(
            QStringLiteral("2020-01-01T00:00:00.000Z"),
            Qt::ISODateWithMs));
    QVERIFY(!actions.submitTaskCreate());
    QVERIFY(actions.taskCreateError().contains(
        QStringLiteral("future")));
}

void MissionActionsTest::archiveAndRestoreUseTaskTransitions()
{
    FakeMissionMutationDispatcher dispatcher;
    kodosi::MissionDirectoryModel directory(dispatcher);
    seed(directory);
    kodosi::MissionDetailModel detail(dispatcher, directory);
    detail.ingestAuthEvent(auth());
    seedMembers(dispatcher, detail);
    seedTask(dispatcher, detail, 7, false);
    kodosi::PeopleModel people;
    people.ingestAuthEvent(auth());
    kodosi::SessionCatalogModel sessions;
    kodosi::MissionActions actions(
        dispatcher, directory, detail, people, sessions);
    actions.ingestAuthEvent(auth());

    QVERIFY(actions.transitionTask(
        QStringLiteral("mission"),
        QStringLiteral("task"),
        QStringLiteral("Archived"),
        {}));
    auto command = dispatcher.commands.back();
    QCOMPARE(
        command.value(QStringLiteral("to_status")).toString(),
        QStringLiteral("Archived"));
    QVERIFY(!command.contains(QStringLiteral("result")));
    const auto archiveRequest =
        command.value(QStringLiteral("requestId")).toString();
    actions.ingestRoomEvent(actionEvent(
        QStringLiteral("room.action.accepted"),
        archiveRequest,
        QStringLiteral("tasks.transition"),
        QString(64, u'a')));
    actions.ingestRoomEvent(actionEvent(
        QStringLiteral("room.action.result"),
        archiveRequest,
        QStringLiteral("tasks.transition"),
        QString(64, u'b'),
        QStringLiteral("succeeded")));
    QVERIFY(!actions.busy());

    const QJsonObject archived {
        {QStringLiteral("id"), QStringLiteral("task")},
        {QStringLiteral("roomId"), QStringLiteral("mission")},
        {QStringLiteral("createdByUserId"), QStringLiteral("me")},
        {QStringLiteral("title"), QStringLiteral("Ship Linux")},
        {QStringLiteral("description"), QStringLiteral("Package it")},
        {QStringLiteral("status"), QStringLiteral("Archived")},
        {QStringLiteral("revision"), 8},
        {QStringLiteral("createdAt"), QStringLiteral("2026-08-31T08:00:00Z")},
        {QStringLiteral("updatedAt"), QStringLiteral("2026-08-31T10:00:00Z")},
    };
    detail.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.tasks.upserted")},
        {QStringLiteral("room_id"), QStringLiteral("mission")},
        {QStringLiteral("task"), archived},
    }));
    QVERIFY(actions.transitionTask(
        QStringLiteral("mission"),
        QStringLiteral("task"),
        QStringLiteral("Open"),
        {}));
    command = dispatcher.commands.back();
    QCOMPARE(
        command.value(QStringLiteral("to_status")).toString(),
        QStringLiteral("Open"));
    QCOMPARE(
        command.value(QStringLiteral("expected_task_revision")).toInteger(),
        8);
    QVERIFY(!command.contains(QStringLiteral("result")));
}

void MissionActionsTest::directCreateChatAndTaskCanRunConcurrently()
{
    FakeMissionMutationDispatcher dispatcher;
    kodosi::MissionDirectoryModel directory(dispatcher);
    seed(directory);
    kodosi::MissionDetailModel detail(dispatcher, directory);
    detail.ingestAuthEvent(auth());
    seedMembers(dispatcher, detail);
    kodosi::PeopleModel people;
    people.ingestAuthEvent(auth());
    kodosi::SessionCatalogModel sessions;
    kodosi::MissionActions actions(
        dispatcher, directory, detail, people, sessions);
    actions.ingestAuthEvent(auth());

    actions.setCreateMissionName(QStringLiteral("Concurrent"));
    actions.setCreateMissionSlug(QStringLiteral("concurrent"));
    QVERIFY(actions.createMission());
    actions.setChatDraftBody(QStringLiteral("hello"));
    QVERIFY(actions.sendChat());
    actions.setTaskDraftTitle(QStringLiteral("Task"));
    QVERIFY(actions.submitTaskCreate());
    QVERIFY(actions.busy());

    QSet<QString> types;
    for (const auto& command : dispatcher.commands) {
        types.insert(command.value(QStringLiteral("type")).toString());
    }
    QVERIFY(types.contains(QStringLiteral("room.create")));
    QVERIFY(types.contains(QStringLiteral("room.chat.post")));
    QVERIFY(types.contains(QStringLiteral("room.tasks.create")));
}

void MissionActionsTest::directSuccessRequiresExactProjection()
{
    FakeMissionMutationDispatcher dispatcher;
    kodosi::MissionDirectoryModel directory(dispatcher);
    seed(directory);
    kodosi::MissionDetailModel detail(dispatcher, directory);
    detail.ingestAuthEvent(auth());
    seedMembers(dispatcher, detail);
    kodosi::PeopleModel people;
    people.ingestAuthEvent(auth());
    kodosi::SessionCatalogModel sessions;
    kodosi::MissionActions actions(
        dispatcher, directory, detail, people, sessions);
    actions.ingestAuthEvent(auth());
    QSignalSpy created(&actions, &kodosi::MissionActions::missionCreated);
    QSignalSpy chatted(&actions, &kodosi::MissionActions::chatCompleted);
    QSignalSpy tasked(&actions, &kodosi::MissionActions::taskCompleted);

    actions.setCreateMissionName(QStringLiteral("Projected"));
    actions.setCreateMissionSlug(QStringLiteral("projected"));
    QVERIFY(actions.createMission());
    const auto createRequest =
        dispatcher.commands.back().value(QStringLiteral("requestId")).toString();
    actions.setChatDraftBody(QStringLiteral("projected chat"));
    QVERIFY(actions.sendChat());
    const auto chatRequest =
        dispatcher.commands.back().value(QStringLiteral("requestId")).toString();
    actions.setTaskDraftTitle(QStringLiteral("Projected task"));
    QVERIFY(actions.submitTaskCreate());
    const auto taskRequest =
        dispatcher.commands.back().value(QStringLiteral("requestId")).toString();

    const auto createEntity = QStringLiteral("created-mission");
    const auto chatEntity = QStringLiteral("created-message");
    const auto taskEntityId = QStringLiteral("created-task");
    actions.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.action.result")},
        {QStringLiteral("requestId"), createRequest},
        {QStringLiteral("operation"), QStringLiteral("create")},
        {QStringLiteral("roomId"), createEntity},
        {QStringLiteral("status"), QStringLiteral("succeeded")},
        {QStringLiteral("entityId"), createEntity},
    }));
    actions.ingestRoomEvent(actionEvent(
        QStringLiteral("room.action.result"),
        chatRequest,
        QStringLiteral("chat.post"),
        {},
        QStringLiteral("succeeded"),
        chatEntity));
    actions.ingestRoomEvent(actionEvent(
        QStringLiteral("room.action.result"),
        taskRequest,
        QStringLiteral("tasks.create"),
        {},
        QStringLiteral("succeeded"),
        taskEntityId));

    QCOMPARE(
        actions.createMissionOutcome(),
        kodosi::MissionActions::Outcome::AcceptedAwaitingProjection);
    QCOMPARE(
        actions.chatOutcome(),
        kodosi::MissionActions::Outcome::AcceptedAwaitingProjection);
    QCOMPARE(
        actions.taskCreateOutcome(),
        kodosi::MissionActions::Outcome::AcceptedAwaitingProjection);
    QVERIFY(!actions.createMissionCanCheck());
    QVERIFY(!actions.chatCanCheck());
    QVERIFY(!actions.taskCreateCanCheck());
    QCOMPARE(created.count(), 0);
    QCOMPARE(chatted.count(), 0);
    QCOMPARE(tasked.count(), 0);
    QVERIFY(actions.busy());

    actions.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.chat.posted")},
        {QStringLiteral("room_id"), QStringLiteral("mission")},
        {QStringLiteral("message"),
         messageEntity(
             QStringLiteral("other-message"),
             QStringLiteral("projected chat"))},
    }));
    actions.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.tasks.upserted")},
        {QStringLiteral("room_id"), QStringLiteral("mission")},
        {QStringLiteral("task"),
         taskEntity(
             QStringLiteral("other-task"),
             QStringLiteral("Projected task"))},
    }));
    QCOMPARE(chatted.count(), 0);
    QCOMPARE(tasked.count(), 0);

    actions.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.snapshot")},
        {QStringLiteral("rooms"),
         QJsonArray {
             roomEntity(
                 createEntity,
                 QStringLiteral("Projected"),
                 QStringLiteral("projected")),
         }},
    }));
    actions.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.chat.posted")},
        {QStringLiteral("room_id"), QStringLiteral("mission")},
        {QStringLiteral("message"),
         messageEntity(
             chatEntity,
             QStringLiteral("projected chat"))},
    }));
    actions.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.tasks.upserted")},
        {QStringLiteral("room_id"), QStringLiteral("mission")},
        {QStringLiteral("task"),
         taskEntity(
             taskEntityId,
             QStringLiteral("Projected task"))},
    }));

    QCOMPARE(created.count(), 1);
    QCOMPARE(created.constFirst().constFirst().toString(), createEntity);
    QCOMPARE(chatted.count(), 1);
    QCOMPARE(tasked.count(), 1);
    QVERIFY(!actions.busy());
}

void MissionActionsTest::projectionFailureRetainsRecoveryState()
{
    FakeMissionMutationDispatcher dispatcher;
    kodosi::MissionDirectoryModel directory(dispatcher);
    seed(directory);
    kodosi::MissionDetailModel detail(dispatcher, directory);
    detail.ingestAuthEvent(auth());
    QVERIFY(detail.openMission(QStringLiteral("mission")));
    kodosi::PeopleModel people;
    people.ingestAuthEvent(auth());
    kodosi::SessionCatalogModel sessions;
    kodosi::MissionActions actions(
        dispatcher, directory, detail, people, sessions);
    actions.ingestAuthEvent(auth());
    QSignalSpy completed(&actions, &kodosi::MissionActions::chatCompleted);

    actions.setChatDraftBody(QStringLiteral("await projection"));
    QVERIFY(actions.sendChat());
    const auto requestId =
        dispatcher.commands.back().value(QStringLiteral("requestId")).toString();
    actions.ingestRoomEvent(actionEvent(
        QStringLiteral("room.action.result"),
        requestId,
        QStringLiteral("chat.post"),
        {},
        QStringLiteral("succeeded"),
        QStringLiteral("projected-message")));
    QCOMPARE(
        actions.chatOutcome(),
        kodosi::MissionActions::Outcome::AcceptedAwaitingProjection);

    actions.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.error")},
        {QStringLiteral("room_id"), QStringLiteral("mission")},
        {QStringLiteral("operation"), QStringLiteral("chat.list")},
        {QStringLiteral("message"),
         QStringLiteral("Projection hydration failed")},
    }));
    QVERIFY(actions.busy());
    QCOMPARE(completed.count(), 0);
    QCOMPARE(
        actions.chatOutcome(),
        kodosi::MissionActions::Outcome::Unknown);
    QVERIFY(actions.chatCanCheck());
    QVERIFY(actions.chatError().contains(
        QStringLiteral("Projection hydration failed")));
}

void MissionActionsTest::directDraftsFenceDiscardAndSubmission()
{
    FakeMissionMutationDispatcher dispatcher;
    kodosi::MissionDirectoryModel directory(dispatcher);
    seed(directory);
    kodosi::MissionDetailModel detail(dispatcher, directory);
    detail.ingestAuthEvent(auth());
    seedMembers(dispatcher, detail);
    kodosi::PeopleModel people;
    people.ingestAuthEvent(auth());
    kodosi::SessionCatalogModel sessions;
    kodosi::MissionActions actions(
        dispatcher, directory, detail, people, sessions);
    actions.ingestAuthEvent(auth());

    QVERIFY(!actions.createMissionCanSubmit());
    actions.setCreateMissionName(QStringLiteral("Submission gate"));
    QVERIFY(!actions.createMissionCanSubmit());
    actions.setCreateMissionSlug(QStringLiteral("submission-gate"));
    QVERIFY(actions.createMissionCanSubmit());
    QVERIFY(actions.createMission());
    const auto createRequest =
        dispatcher.commands.back().value(QStringLiteral("requestId")).toString();
    QVERIFY(!actions.createMissionCanSubmit());
    QVERIFY(!actions.createMissionCanDiscard());
    QVERIFY(!actions.discardCreateMission());
    QVERIFY(actions.busy());

    QVERIFY(!actions.chatCanSubmit());
    actions.setChatDraftBody(QStringLiteral("pending message"));
    QVERIFY(actions.chatCanSubmit());
    QVERIFY(actions.sendChat());
    const auto chatRequest =
        dispatcher.commands.back().value(QStringLiteral("requestId")).toString();
    QVERIFY(!actions.chatCanSubmit());
    QVERIFY(!actions.chatCanDiscard());
    QVERIFY(!actions.discardChat());
    QVERIFY(actions.busy());

    QVERIFY(!actions.taskCreateCanSubmit());
    actions.setTaskDraftTitle(QStringLiteral("Pending task"));
    QVERIFY(actions.taskCreateCanSubmit());
    QVERIFY(actions.submitTaskCreate());
    const auto taskRequest =
        dispatcher.commands.back().value(QStringLiteral("requestId")).toString();
    QVERIFY(!actions.taskCreateCanSubmit());
    QVERIFY(!actions.taskCreateCanDiscard());
    QVERIFY(!actions.discardTaskCreate());
    QVERIFY(actions.busy());

    actions.ingestRoomEvent(actionEvent(
        QStringLiteral("room.action.result"),
        chatRequest,
        QStringLiteral("chat.post"),
        {},
        QStringLiteral("succeeded")));
    QCOMPARE(
        actions.chatOutcome(),
        kodosi::MissionActions::Outcome::AcceptedAwaitingProjection);
    QVERIFY(!actions.chatCanSubmit());
    QVERIFY(!actions.chatCanDiscard());
    QVERIFY(!actions.discardChat());
    QVERIFY(actions.busy());
    actions.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.chat.snapshot")},
        {QStringLiteral("room_id"), QStringLiteral("mission")},
        {QStringLiteral("messages"),
         QJsonArray {
             messageEntity(
                 chatRequest,
                 QStringLiteral("pending message")),
         }},
    }));
    QCOMPARE(
        actions.chatOutcome(),
        kodosi::MissionActions::Outcome::Succeeded);
    QVERIFY(actions.chatCanDiscard());
    QVERIFY(actions.discardChat());

    actions.setChatDraftBody(QStringLiteral("unknown message"));
    QVERIFY(actions.chatCanSubmit());
    QVERIFY(actions.sendChat());
    const auto unknownRequest =
        dispatcher.commands.back().value(QStringLiteral("requestId")).toString();
    actions.ingestRoomEvent(actionEvent(
        QStringLiteral("room.action.result"),
        unknownRequest,
        QStringLiteral("chat.post"),
        {},
        QStringLiteral("unknown")));
    QCOMPARE(
        actions.chatOutcome(),
        kodosi::MissionActions::Outcome::Unknown);
    QVERIFY(!actions.chatCanSubmit());
    QVERIFY(actions.chatCanDiscard());
    QVERIFY(actions.checkChat());
    QCOMPARE(
        actions.chatOutcome(),
        kodosi::MissionActions::Outcome::Reconciling);
    QVERIFY(!actions.chatCanSubmit());
    QVERIFY(!actions.chatCanDiscard());
    QVERIFY(!actions.discardChat());
    actions.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.chat.snapshot")},
        {QStringLiteral("room_id"), QStringLiteral("mission")},
        {QStringLiteral("messages"),
         QJsonArray {
             messageEntity(
                 unknownRequest,
                 QStringLiteral("unknown message")),
         }},
    }));
    QVERIFY(actions.discardChat());

    actions.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.action.result")},
        {QStringLiteral("requestId"), createRequest},
        {QStringLiteral("operation"), QStringLiteral("create")},
        {QStringLiteral("roomId"), createRequest},
        {QStringLiteral("status"), QStringLiteral("failed")},
    }));
    QVERIFY(actions.createMissionCanDiscard());
    QVERIFY(actions.discardCreateMission());
    actions.ingestRoomEvent(actionEvent(
        QStringLiteral("room.action.result"),
        taskRequest,
        QStringLiteral("tasks.create"),
        {},
        QStringLiteral("failed")));
    QVERIFY(actions.taskCreateCanDiscard());
    QVERIFY(actions.discardTaskCreate());
    QVERIFY(!actions.busy());
}

void MissionActionsTest::successfulCreateDoesNotLeaveDraft()
{
    FakeMissionMutationDispatcher dispatcher;
    kodosi::MissionDirectoryModel directory(dispatcher);
    seed(directory);
    kodosi::MissionDetailModel detail(dispatcher, directory);
    kodosi::PeopleModel people;
    kodosi::SessionCatalogModel sessions;
    kodosi::MissionActions actions(
        dispatcher, directory, detail, people, sessions);
    actions.ingestAuthEvent(auth());

    actions.setCreateMissionName(QStringLiteral("Fresh next time"));
    actions.setCreateMissionSlug(QStringLiteral("fresh-next-time"));
    QVERIFY(actions.createMission());
    const auto requestId =
        dispatcher.commands.back().value(QStringLiteral("requestId")).toString();
    actions.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.action.result")},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("operation"), QStringLiteral("create")},
        {QStringLiteral("roomId"), requestId},
        {QStringLiteral("status"), QStringLiteral("succeeded")},
        {QStringLiteral("entityId"), requestId},
    }));
    actions.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.snapshot")},
        {QStringLiteral("rooms"),
         QJsonArray {
             roomEntity(
                 requestId,
                 QStringLiteral("Fresh next time"),
                 QStringLiteral("fresh-next-time")),
         }},
    }));

    QCOMPARE(
        actions.createMissionOutcome(),
        kodosi::MissionActions::Outcome::Succeeded);
    QCOMPARE(actions.createMissionName(), QString {});
    QCOMPARE(actions.createMissionSlug(), QString {});
    QVERIFY(
        actions.metaObject()->indexOfProperty("hasCreateMissionDraft")
        >= 0);
    QVERIFY(!actions.property("hasCreateMissionDraft").toBool());
}

void MissionActionsTest::syntheticUnknownRecoveryUsesReadOnlyCommands()
{
    FakeMissionMutationDispatcher dispatcher;
    kodosi::MissionDirectoryModel directory(dispatcher);
    seed(directory);
    kodosi::MissionDetailModel detail(dispatcher, directory);
    detail.ingestAuthEvent(auth());
    seedMembers(dispatcher, detail);
    kodosi::PeopleModel people;
    people.ingestAuthEvent(auth());
    kodosi::SessionCatalogModel sessions;
    kodosi::MissionActions actions(
        dispatcher, directory, detail, people, sessions);
    actions.ingestAuthEvent(auth());

    actions.installSyntheticPresentation(
        kodosi::MissionActions::SyntheticPresentation::Unknown);
    QVERIFY(actions.busy());
    QVERIFY(actions.chatCanCheck());
    QVERIFY(actions.taskCreateCanCheck());
    QVERIFY(actions.createMissionCanCheck());
    QVERIFY(actions.ledgerCanCheck());
    const auto commandsBeforeBlockedSubmit = dispatcher.commands.size();
    QVERIFY(!actions.createMission());
    QVERIFY(!actions.submitTaskCreate());
    QCOMPARE(
        actions.createMissionOutcome(),
        kodosi::MissionActions::Outcome::Unknown);
    QCOMPARE(
        actions.taskCreateOutcome(),
        kodosi::MissionActions::Outcome::Unknown);
    QVERIFY(!actions.createMissionCanRetry());
    QVERIFY(!actions.taskCreateCanRetry());
    QCOMPARE(dispatcher.commands.size(), commandsBeforeBlockedSubmit);

    const auto commandCount = [&](const QString& type) {
        return std::ranges::count(
            dispatcher.commands,
            type,
            [](const QJsonObject& command) {
                return command.value(QStringLiteral("type")).toString();
            });
    };
    const auto priorChatRefreshes =
        commandCount(QStringLiteral("room.chat.list"));
    QVERIFY(actions.checkChat());
    QCOMPARE(
        actions.chatOutcome(),
        kodosi::MissionActions::Outcome::Reconciling);
    QCOMPARE(
        commandCount(QStringLiteral("room.chat.list")),
        priorChatRefreshes + 1);

    const auto priorTaskRefreshes =
        commandCount(QStringLiteral("room.tasks.list"));
    QVERIFY(actions.checkTaskCreate());
    QCOMPARE(
        actions.taskCreateOutcome(),
        kodosi::MissionActions::Outcome::Reconciling);
    QCOMPARE(
        commandCount(QStringLiteral("room.tasks.list")),
        priorTaskRefreshes + 1);

    const auto priorDirectoryRefreshes =
        commandCount(QStringLiteral("room.refresh"));
    QVERIFY(actions.checkCreateMission());
    QCOMPARE(
        actions.createMissionOutcome(),
        kodosi::MissionActions::Outcome::Reconciling);
    QCOMPARE(
        commandCount(QStringLiteral("room.refresh")),
        priorDirectoryRefreshes + 1);

    const auto priorRecoveries =
        commandCount(QStringLiteral("room.mutations.recover"));
    QVERIFY(actions.checkLedger());
    QCOMPARE(
        actions.ledgerOutcome(),
        kodosi::MissionActions::Outcome::Reconciling);
    QCOMPARE(
        commandCount(QStringLiteral("room.mutations.recover")),
        priorRecoveries + 1);

    const QSet<QString> syntheticSafeCommands {
        QStringLiteral("room.refresh"),
        QStringLiteral("room.refreshInvitations"),
        QStringLiteral("room.refreshMembers"),
        QStringLiteral("room.chat.list"),
        QStringLiteral("room.tasks.list"),
        QStringLiteral("room.mutations.recover"),
    };
    for (const auto& command : dispatcher.commands) {
        QVERIFY(syntheticSafeCommands.contains(
            command.value(QStringLiteral("type")).toString()));
    }
}

void MissionActionsTest::validatesUtf16CodeUnitLimits()
{
    FakeMissionMutationDispatcher dispatcher;
    kodosi::MissionDirectoryModel directory(dispatcher);
    seed(directory);
    kodosi::MissionDetailModel detail(dispatcher, directory);
    detail.ingestAuthEvent(auth());
    seedMembers(dispatcher, detail);
    kodosi::PeopleModel people;
    people.ingestAuthEvent(auth());
    kodosi::SessionCatalogModel sessions;
    kodosi::MissionActions actions(
        dispatcher, directory, detail, people, sessions);
    actions.ingestAuthEvent(auth());

    const auto emoji = QStringLiteral("🚀");
    actions.setCreateMissionName(emoji.repeated(64));
    actions.setCreateMissionSlug(QStringLiteral("utf16-room"));
    QVERIFY(actions.createMission());
    const auto createRequest =
        dispatcher.commands.back().value(QStringLiteral("requestId")).toString();
    actions.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.action.result")},
        {QStringLiteral("requestId"), createRequest},
        {QStringLiteral("operation"), QStringLiteral("create")},
        {QStringLiteral("roomId"), createRequest},
        {QStringLiteral("status"), QStringLiteral("failed")},
    }));
    QVERIFY(actions.discardCreateMission());
    actions.setCreateMissionName(emoji.repeated(64) + QStringLiteral("x"));
    QVERIFY(!actions.createMission());

    actions.setChatDraftBody(emoji.repeated(2'000));
    QVERIFY(actions.sendChat());
    const auto chatRequest =
        dispatcher.commands.back().value(QStringLiteral("requestId")).toString();
    actions.ingestRoomEvent(actionEvent(
        QStringLiteral("room.action.result"),
        chatRequest,
        QStringLiteral("chat.post"),
        {},
        QStringLiteral("failed")));
    QVERIFY(actions.discardChat());
    actions.setChatDraftBody(
        emoji.repeated(2'000) + QStringLiteral("x"));
    QVERIFY(!actions.sendChat());

    actions.setTaskDraftTitle(emoji.repeated(100));
    actions.setTaskDraftDescription(emoji.repeated(2'000));
    QVERIFY(actions.submitTaskCreate());
    const auto taskRequest =
        dispatcher.commands.back().value(QStringLiteral("requestId")).toString();
    actions.ingestRoomEvent(actionEvent(
        QStringLiteral("room.action.result"),
        taskRequest,
        QStringLiteral("tasks.create"),
        {},
        QStringLiteral("failed")));
    QVERIFY(actions.discardTaskCreate());
    actions.setTaskDraftTitle(
        emoji.repeated(100) + QStringLiteral("x"));
    QVERIFY(!actions.submitTaskCreate());
}

void MissionActionsTest::handlesEveryActionResultStatus()
{
    const QList<QPair<QString, kodosi::MissionActions::Outcome>> cases {
        {QStringLiteral("failed"), kodosi::MissionActions::Outcome::Failed},
        {QStringLiteral("conflict"), kodosi::MissionActions::Outcome::Conflict},
        {QStringLiteral("unknown"), kodosi::MissionActions::Outcome::Unknown},
        {QStringLiteral("succeeded"),
         kodosi::MissionActions::Outcome::AcceptedAwaitingProjection},
    };
    for (const auto& [status, expected] : cases) {
        FakeMissionMutationDispatcher dispatcher;
        kodosi::MissionDirectoryModel directory(dispatcher);
        seed(directory);
        kodosi::MissionDetailModel detail(dispatcher, directory);
        detail.ingestAuthEvent(auth());
        QVERIFY(detail.openMission(QStringLiteral("mission")));
        kodosi::PeopleModel people;
        people.ingestAuthEvent(auth());
        kodosi::SessionCatalogModel sessions;
        kodosi::MissionActions actions(
            dispatcher, directory, detail, people, sessions);
        actions.ingestAuthEvent(auth());

        QVERIFY(actions.postBroadcast(
            QStringLiteral("mission"),
            QStringLiteral("status")));
        const auto requestId = dispatcher.commands.back()
                                   .value(QStringLiteral("requestId"))
                                   .toString();
        actions.ingestRoomEvent(actionEvent(
            QStringLiteral("room.action.result"),
            requestId,
            QStringLiteral("chat.post"),
            {},
            status));
        QCOMPARE(actions.chatOutcome(), expected);
        if (status == QStringLiteral("succeeded")) {
            actions.ingestRoomEvent(envelope({
                {QStringLiteral("type"), QStringLiteral("room.chat.snapshot")},
                {QStringLiteral("room_id"), QStringLiteral("mission")},
                {QStringLiteral("messages"),
                 QJsonArray {
                     messageEntity(
                         requestId,
                         QStringLiteral("status")),
                 }},
            }));
            QCOMPARE(
                actions.chatOutcome(),
                kodosi::MissionActions::Outcome::Succeeded);
        }
    }
}

void MissionActionsTest::validatesMissionInputs()
{
    FakeMissionMutationDispatcher dispatcher;
    kodosi::MissionDirectoryModel directory(dispatcher);
    seed(directory);
    kodosi::MissionDetailModel detail(dispatcher, directory);
    detail.ingestAuthEvent(auth());
    kodosi::PeopleModel people;
    people.ingestAuthEvent(auth());
    kodosi::SessionCatalogModel sessions;
    kodosi::MissionActions actions(
        dispatcher, directory, detail, people, sessions);
    actions.ingestAuthEvent(auth());

    QVERIFY(!actions.postBroadcast(QStringLiteral("missing"), QStringLiteral("hi")));
    QVERIFY(!actions.postBroadcast(QStringLiteral("mission"), QString {}));
    QVERIFY(!actions.createTask(
        QStringLiteral("mission"),
        QString {},
        QString {}));
}

void MissionActionsTest::preservesUnavailableRecipientsAndUsesTypedCandidates()
{
    FakeMissionMutationDispatcher dispatcher;
    kodosi::MissionDirectoryModel directory(dispatcher);
    seed(directory);
    kodosi::PeopleModel people;
    seedPeople(people);
    kodosi::SessionCatalogModel sessions;
    seedSessions(sessions);
    kodosi::MissionDetailModel detail(
        dispatcher,
        directory,
        {
            .people = &people,
            .sessions = &sessions,
        });
    detail.ingestAuthEvent(auth());
    seedMembers(dispatcher, detail);
    kodosi::MissionActions actions(
        dispatcher, directory, detail, people, sessions);
    actions.ingestAuthEvent(auth());

    const auto candidates = actions.inviteCandidates();
    QCOMPARE(candidates.size(), 1);
    const auto candidate = candidates.constFirst().toMap();
    QVERIFY(candidate.value(QStringLiteral("candidateId"))
        .toString()
        .startsWith(QStringLiteral("invite-candidate-")));
    QCOMPARE(
        candidate.value(QStringLiteral("handle")).toString(),
        QStringLiteral("alice"));
    QVERIFY(actions.inviteFriendCandidate(
        candidate.value(QStringLiteral("candidateId")).toString()));
    QCOMPARE(
        dispatcher.commands.back().value(QStringLiteral("type")).toString(),
        QStringLiteral("room.invite"));
    QCOMPARE(
        dispatcher.commands.back()
            .value(QStringLiteral("invitee_user_id"))
            .toString(),
        QStringLiteral("friend"));

    const auto agent = crewPresentation(
        detail,
        kodosi::MissionCrewModel::Kind::Agent);
    QVERIFY(!agent.isEmpty());
    QVERIFY(actions.toggleChatRecipient(agent));
    actions.setChatDraftBody(QStringLiteral("Keep this target"));
    QVERIFY(actions.chatCanSubmit());

    sessions.ingestSessionEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("session.list")},
        {QStringLiteral("sessions"), QJsonArray {}},
    }));

    QCOMPARE(actions.chatRecipientPresentationIds(), QStringList {agent});
    QCOMPARE(actions.chatUnavailableRecipients().size(), 1);
    QVERIFY(actions.chatHasUnavailableRecipients());
    QVERIFY(!actions.chatCanSubmit());
    QVERIFY(!actions.sendChat());
    QVERIFY(!actions.chatCanRetry());
    QCOMPARE(actions.chatRecipientPresentationIds(), QStringList {agent});
    QVERIFY(actions.removeChatRecipient(agent));
    QVERIFY(!actions.chatHasUnavailableRecipients());
    QVERIFY(actions.chatCanRetry());
    QVERIFY(actions.chatCanSubmit());
}

QTEST_GUILESS_MAIN(MissionActionsTest)

#include "tst_mission_actions.moc"
