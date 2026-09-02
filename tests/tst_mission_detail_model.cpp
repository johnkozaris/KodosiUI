#include "models/MissionDetailModel.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest/QTest>

class FakeMissionDetailDispatcher final : public kodosi::CommandDispatcher {
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

class MissionDetailModelTest final : public QObject {
    Q_OBJECT

private slots:
    void hydratesSelectedMissionWithExactTokens();
    void rejectsLateMissionAndHydrationResponses();
    void paginatesTasksWithBoundedOffsets();
    void failedOpenRollsBackSelection();
    void hydrationPreservesLiveUpdates();
    void uncorrelatedErrorsCannotCancelHydration();
};

namespace {

QByteArray auth()
{
    return QByteArrayLiteral(
        R"({"type":"auth.ready","userId":"me","accountEpoch":1})");
}

QByteArray envelope(QJsonObject event)
{
    event.insert(QStringLiteral("authority"), QStringLiteral("accountContext"));
    event.insert(QStringLiteral("accountUserId"), QStringLiteral("me"));
    event.insert(QStringLiteral("accountEpoch"), 1);
    return QJsonDocument(event).toJson(QJsonDocument::Compact);
}

void seedDirectory(kodosi::MissionDirectoryModel& directory)
{
    directory.ingestAuthEvent(auth());
    directory.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.snapshot")},
        {QStringLiteral("rooms"),
         QJsonArray {
             QJsonObject {
                 {QStringLiteral("id"), QStringLiteral("mission-1")},
                 {QStringLiteral("name"), QStringLiteral("Launch")},
                 {QStringLiteral("slug"), QStringLiteral("launch")},
                 {QStringLiteral("ownerUserId"), QStringLiteral("me")},
                 {QStringLiteral("rosterGeneration"), 1},
             },
             QJsonObject {
                 {QStringLiteral("id"), QStringLiteral("mission-2")},
                 {QStringLiteral("name"), QStringLiteral("Other")},
                 {QStringLiteral("slug"), QStringLiteral("other")},
                 {QStringLiteral("ownerUserId"), QStringLiteral("me")},
                 {QStringLiteral("rosterGeneration"), 1},
             },
         }},
    }));
}

QJsonObject task(const QString& id, const qint64 revision = 1)
{
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("roomId"), QStringLiteral("mission-1")},
        {QStringLiteral("createdByUserId"), QStringLiteral("me")},
        {QStringLiteral("title"), QStringLiteral("Ship Linux")},
        {QStringLiteral("status"), QStringLiteral("InProgress")},
        {QStringLiteral("revision"), revision},
        {QStringLiteral("createdAt"), QStringLiteral("2026-08-31T08:00:00Z")},
        {QStringLiteral("updatedAt"), QStringLiteral("2026-08-31T09:00:00Z")},
    };
}

QJsonObject command(
    const FakeMissionDetailDispatcher& dispatcher,
    const QString& type)
{
    const auto found = std::ranges::find(
        dispatcher.commands,
        type,
        [](const QJsonObject& value) {
            return value.value(QStringLiteral("type")).toString();
        });
    return found == dispatcher.commands.end() ? QJsonObject {} : *found;
}

} // namespace

void MissionDetailModelTest::hydratesSelectedMissionWithExactTokens()
{
    FakeMissionDetailDispatcher dispatcher;
    kodosi::MissionDirectoryModel directory(dispatcher);
    kodosi::MissionDetailModel detail(dispatcher, directory);
    seedDirectory(directory);
    detail.ingestAuthEvent(auth());

    QVERIFY(detail.openMission(QStringLiteral("mission-1")));
    const auto membersCommand =
        command(dispatcher, QStringLiteral("room.refreshMembers"));
    const auto chatCommand =
        command(dispatcher, QStringLiteral("room.chat.list"));
    const auto taskCommand =
        command(dispatcher, QStringLiteral("room.tasks.list"));
    QVERIFY(!membersCommand.isEmpty());
    QVERIFY(!chatCommand.isEmpty());
    QVERIFY(!taskCommand.isEmpty());

    detail.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.members")},
        {QStringLiteral("room_id"), QStringLiteral("mission-1")},
        {QStringLiteral("hydration_id"),
         membersCommand.value(QStringLiteral("hydration_id"))},
        {QStringLiteral("members"),
         QJsonArray {
             QJsonObject {
                 {QStringLiteral("roomId"), QStringLiteral("mission-1")},
                 {QStringLiteral("userId"), QStringLiteral("me")},
                 {QStringLiteral("role"), QStringLiteral("owner")},
                 {QStringLiteral("displayName"), QStringLiteral("John")},
             },
         }},
    }));
    detail.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.chat.snapshot")},
        {QStringLiteral("room_id"), QStringLiteral("mission-1")},
        {QStringLiteral("hydration_id"),
         chatCommand.value(QStringLiteral("hydration_id"))},
        {QStringLiteral("messages"),
         QJsonArray {
             QJsonObject {
                 {QStringLiteral("id"), QStringLiteral("message-1")},
                 {QStringLiteral("roomId"), QStringLiteral("mission-1")},
                 {QStringLiteral("authorUserId"), QStringLiteral("me")},
                 {QStringLiteral("authorKind"), QStringLiteral("Human")},
                 {QStringLiteral("body"), QStringLiteral("Ready")},
                 {QStringLiteral("seq"), 1},
                 {QStringLiteral("postedAt"), QStringLiteral("2026-08-31T09:00:00Z")},
             },
         }},
    }));
    detail.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.tasks.page")},
        {QStringLiteral("room_id"), QStringLiteral("mission-1")},
        {QStringLiteral("hydration_id"),
         taskCommand.value(QStringLiteral("hydration_id"))},
        {QStringLiteral("request_offset"), 0},
        {QStringLiteral("has_more"), false},
        {QStringLiteral("tasks"), QJsonArray {task(QStringLiteral("task-1"))}},
    }));

    QCOMPARE(detail.members()->rowCount(), 1);
    QCOMPARE(detail.messages()->rowCount(), 1);
    QCOMPARE(detail.tasks()->rowCount(), 1);
    QVERIFY(!detail.loading());
}

void MissionDetailModelTest::rejectsLateMissionAndHydrationResponses()
{
    FakeMissionDetailDispatcher dispatcher;
    kodosi::MissionDirectoryModel directory(dispatcher);
    kodosi::MissionDetailModel detail(dispatcher, directory);
    seedDirectory(directory);
    detail.ingestAuthEvent(auth());
    QVERIFY(detail.openMission(QStringLiteral("mission-1")));

    detail.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.members")},
        {QStringLiteral("room_id"), QStringLiteral("mission-2")},
        {QStringLiteral("hydration_id"), QStringLiteral("late")},
        {QStringLiteral("members"), QJsonArray {}},
    }));
    detail.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.members")},
        {QStringLiteral("room_id"), QStringLiteral("mission-1")},
        {QStringLiteral("hydration_id"), QStringLiteral("wrong")},
        {QStringLiteral("members"), QJsonArray {}},
    }));
    QCOMPARE(detail.members()->rowCount(), 0);
    QVERIFY(detail.loading());

    const auto firstTasks = command(
        dispatcher,
        QStringLiteral("room.tasks.list"));
    QVERIFY(detail.openMission(QStringLiteral("mission-1")));
    detail.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.tasks.page")},
        {QStringLiteral("room_id"), QStringLiteral("mission-1")},
        {QStringLiteral("hydration_id"),
         firstTasks.value(QStringLiteral("hydration_id"))},
        {QStringLiteral("request_offset"), 0},
        {QStringLiteral("has_more"), false},
        {QStringLiteral("tasks"), QJsonArray {task(QStringLiteral("late"))}},
    }));
    QCOMPARE(detail.tasks()->rowCount(), 0);
    QVERIFY(detail.loading());
}

void MissionDetailModelTest::paginatesTasksWithBoundedOffsets()
{
    FakeMissionDetailDispatcher dispatcher;
    kodosi::MissionDirectoryModel directory(dispatcher);
    kodosi::MissionDetailModel detail(dispatcher, directory);
    seedDirectory(directory);
    detail.ingestAuthEvent(auth());
    QVERIFY(detail.openMission(QStringLiteral("mission-1")));
    const auto first = command(dispatcher, QStringLiteral("room.tasks.list"));
    const auto token = first.value(QStringLiteral("hydration_id"));

    detail.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.tasks.page")},
        {QStringLiteral("room_id"), QStringLiteral("mission-1")},
        {QStringLiteral("hydration_id"), token},
        {QStringLiteral("request_offset"), 0},
        {QStringLiteral("has_more"), true},
        {QStringLiteral("next_offset"), 1},
        {QStringLiteral("tasks"), QJsonArray {task(QStringLiteral("task-1"))}},
    }));
    QCOMPARE(
        dispatcher.commands.back().value(QStringLiteral("offset")).toInteger(),
        1);
    detail.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.tasks.page")},
        {QStringLiteral("room_id"), QStringLiteral("mission-1")},
        {QStringLiteral("hydration_id"), token},
        {QStringLiteral("request_offset"), 1},
        {QStringLiteral("has_more"), false},
        {QStringLiteral("tasks"), QJsonArray {task(QStringLiteral("task-2"))}},
    }));
    QCOMPARE(detail.tasks()->rowCount(), 2);
}

void MissionDetailModelTest::failedOpenRollsBackSelection()
{
    FakeMissionDetailDispatcher dispatcher;
    kodosi::MissionDirectoryModel directory(dispatcher);
    kodosi::MissionDetailModel detail(dispatcher, directory);
    seedDirectory(directory);
    detail.ingestAuthEvent(auth());
    dispatcher.rejectType = QStringLiteral("room.chat.list");

    QVERIFY(!detail.openMission(QStringLiteral("mission-1")));
    QVERIFY(detail.missionId().isEmpty());
    QVERIFY(!detail.loading());
    QCOMPARE(detail.members()->rowCount(), 0);
}

void MissionDetailModelTest::hydrationPreservesLiveUpdates()
{
    FakeMissionDetailDispatcher dispatcher;
    kodosi::MissionDirectoryModel directory(dispatcher);
    kodosi::MissionDetailModel detail(dispatcher, directory);
    seedDirectory(directory);
    detail.ingestAuthEvent(auth());
    QVERIFY(detail.openMission(QStringLiteral("mission-1")));
    const auto chat = command(dispatcher, QStringLiteral("room.chat.list"));
    const auto tasks = command(dispatcher, QStringLiteral("room.tasks.list"));

    const QJsonObject liveMessage {
        {QStringLiteral("id"), QStringLiteral("live")},
        {QStringLiteral("roomId"), QStringLiteral("mission-1")},
        {QStringLiteral("authorUserId"), QStringLiteral("agent")},
        {QStringLiteral("authorKind"), QStringLiteral("Agent")},
        {QStringLiteral("body"), QStringLiteral("Live update")},
        {QStringLiteral("seq"), 2},
        {QStringLiteral("postedAt"), QStringLiteral("2026-08-31T09:01:00Z")},
    };
    detail.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.chat.posted")},
        {QStringLiteral("room_id"), QStringLiteral("mission-1")},
        {QStringLiteral("message"), liveMessage},
    }));
    detail.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.chat.snapshot")},
        {QStringLiteral("room_id"), QStringLiteral("mission-1")},
        {QStringLiteral("hydration_id"),
         chat.value(QStringLiteral("hydration_id"))},
        {QStringLiteral("messages"),
         QJsonArray {
             QJsonObject {
                 {QStringLiteral("id"), QStringLiteral("old")},
                 {QStringLiteral("roomId"), QStringLiteral("mission-1")},
                 {QStringLiteral("authorUserId"), QStringLiteral("me")},
                 {QStringLiteral("authorKind"), QStringLiteral("Human")},
                 {QStringLiteral("body"), QStringLiteral("Earlier")},
                 {QStringLiteral("seq"), 1},
                 {QStringLiteral("postedAt"), QStringLiteral("2026-08-31T09:00:00Z")},
             },
         }},
    }));
    QCOMPARE(detail.messages()->rowCount(), 2);

    detail.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.tasks.upserted")},
        {QStringLiteral("room_id"), QStringLiteral("mission-1")},
        {QStringLiteral("task"), task(QStringLiteral("task-1"), 4)},
    }));
    detail.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.tasks.page")},
        {QStringLiteral("room_id"), QStringLiteral("mission-1")},
        {QStringLiteral("hydration_id"),
         tasks.value(QStringLiteral("hydration_id"))},
        {QStringLiteral("request_offset"), 0},
        {QStringLiteral("has_more"), false},
        {QStringLiteral("tasks"), QJsonArray {task(QStringLiteral("task-1"), 1)}},
    }));
    QCOMPARE(detail.tasks()->rowCount(), 1);
    QCOMPARE(
        detail.tasks()->actionContext(QStringLiteral("task-1"))->revision,
        4);
    QVERIFY(!detail.tasks()->roleNames().values().contains(
        QByteArrayLiteral("revision")));

    detail.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.chat.snapshot")},
        {QStringLiteral("room_id"), QStringLiteral("mission-1")},
        {QStringLiteral("messages"), QJsonArray {liveMessage}},
    }));
    QCOMPARE(detail.messages()->rowCount(), 2);
}

void MissionDetailModelTest::uncorrelatedErrorsCannotCancelHydration()
{
    FakeMissionDetailDispatcher dispatcher;
    kodosi::MissionDirectoryModel directory(dispatcher);
    kodosi::MissionDetailModel detail(dispatcher, directory, 1);
    seedDirectory(directory);
    detail.ingestAuthEvent(auth());
    QVERIFY(detail.openMission(QStringLiteral("mission-1")));
    detail.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.error")},
        {QStringLiteral("room_id"), QStringLiteral("mission-1")},
        {QStringLiteral("operation"), QStringLiteral("chat.list")},
        {QStringLiteral("message"), QStringLiteral("old failure")},
    }));
    QVERIFY(detail.loading());
    QTest::qWait(100);
    QVERIFY(!detail.loading());
    QVERIFY(detail.lastError().contains(QStringLiteral("timed out")));
}

QTEST_GUILESS_MAIN(MissionDetailModelTest)

#include "tst_mission_detail_model.moc"
