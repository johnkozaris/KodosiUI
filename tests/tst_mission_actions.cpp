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
    void projectionRefreshPrecedesReceiptAck();
    void rejectedReceiptAckRetainsMutation();
    void recoveredReceiptMutationsReconcile();
    void durableUnknownCannotBeDiscarded();
    void taskActionsUseNativeRevisionsAndIncarnations();
    void reconcilesTaskAfterMissionSwitch();
    void accountTransitionDropsLateResults();
    void reconcilesMissingReceipts();
    void validatesMissionInputs();
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
        QStringLiteral("backend-incarnation"))
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
                  QStringLiteral("runtime-incarnation")},
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
    const QString& status = {})
{
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
    return envelope(event);
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
             QJsonObject {
                 {QStringLiteral("id"), chatId},
                 {QStringLiteral("body"), QStringLiteral("hello")},
                 {QStringLiteral("recipientSessionIds"), QJsonArray {}},
                 {QStringLiteral("recipientUserIds"), QJsonArray {}},
             },
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
             QJsonObject {
                 {QStringLiteral("id"), taskId},
                 {QStringLiteral("title"), QStringLiteral("Ship")},
                 {QStringLiteral("description"), QStringLiteral("Linux")},
             },
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
    actions.ingestRoomEvent(actionEvent(
        QStringLiteral("room.action.result"),
        exactRequest,
        QStringLiteral("removeMember"),
        fingerprint,
        QStringLiteral("succeeded")));
    QVERIFY(!actions.busy());
    QCOMPARE(
        dispatcher.commands.back().value(QStringLiteral("type")).toString(),
        QStringLiteral("room.mutations.reconcile"));
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
    dispatcher.rejectType = QStringLiteral("room.chat.list");
    actions.ingestRoomEvent(actionEvent(
        QStringLiteral("room.action.result"),
        requestId,
        QStringLiteral("removeMember"),
        fingerprint,
        QStringLiteral("conflict")));
    QVERIFY(actions.busy());
    QVERIFY(!detail.membersReady());
    QVERIFY(!directory.invitationsReady());
    QVERIFY(actions.lastError().contains(QStringLiteral("could not be refreshed")));

    dispatcher.rejectType.clear();
    QTRY_VERIFY_WITH_TIMEOUT(!actions.busy(), 500);
    QCOMPARE(detail.missionId(), QStringLiteral("mission"));
    QCOMPARE(
        dispatcher.commands.back().value(QStringLiteral("type")).toString(),
        QStringLiteral("room.mutations.reconcile"));
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
    QVERIFY(actions.busy());
    QVERIFY(actions.lastError().contains(QStringLiteral("could not be acknowledged")));

    dispatcher.rejectType.clear();
    QTRY_VERIFY_WITH_TIMEOUT(!actions.busy(), 500);
    QCOMPARE(
        dispatcher.commands.back().value(QStringLiteral("type")).toString(),
        QStringLiteral("room.mutations.reconcile"));
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
    QVERIFY(!actions.busy());
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

    QVERIFY(actions.canActOnSelectedTasks());
    QCOMPARE(actions.assignmentOptions().size(), 2);
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
        QString(64, u'b'),
        QStringLiteral("succeeded")));
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
        QString(64, u'd'),
        QStringLiteral("succeeded")));
    QVERIFY(!actions.busy());

    const auto assignmentRevision = actions.assignmentRevision();
    seedSessions(sessions, QStringLiteral("backend-incarnation-2"));
    QVERIFY(actions.assignmentRevision() > assignmentRevision);
    QVERIFY(actions.assignmentForTask(QStringLiteral("task")).isEmpty());

    seedMembers(dispatcher, detail);
    seedTask(dispatcher, detail, 8, false);
    QVERIFY(actions.assignTask(
        QStringLiteral("mission"),
        QStringLiteral("task"),
        QStringLiteral("local-agent")));
    command = dispatcher.commands.back();
    QCOMPARE(
        command.value(QStringLiteral("expected_task_revision")).toInteger(),
        8);
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
    QVERIFY(!actions.busy());
    QCOMPARE(detail.tasks()->rowCount(), 1);
    QVERIFY(detail.membersReady());
    QVERIFY(!detail.tasksReady());
    QVERIFY(!actions.canActOnSelectedTasks());

    seedTask(
        dispatcher,
        detail,
        9,
        true,
        QStringLiteral("backend-incarnation-2"));
    QVERIFY(detail.tasksReady());
    QVERIFY(actions.canActOnSelectedTasks());
    QCOMPARE(
        actions.assignmentForTask(QStringLiteral("task")),
        QStringLiteral("local-agent"));
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
             QJsonObject {
                 {QStringLiteral("id"), requestId},
                 {QStringLiteral("title"), QStringLiteral("Ship")},
                 {QStringLiteral("description"), QStringLiteral("Linux")},
             },
         }},
    }));
    QCOMPARE(completed.count(), 1);
    QVERIFY(!actions.busy());
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

QTEST_GUILESS_MAIN(MissionActionsTest)

#include "tst_mission_actions.moc"
