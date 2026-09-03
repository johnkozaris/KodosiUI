#include "models/AgentSessionIntelModel.hpp"
#include "models/AttentionModel.hpp"
#include "models/MissionDetailModel.hpp"
#include "models/DesktopStateModel.hpp"
#include "models/PendingPermissionsModel.hpp"
#include "models/PeopleModel.hpp"
#include "models/SessionActions.hpp"
#include "models/SessionCatalogModel.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>
#include <QUuid>
#include <QtTest/QTest>

class FakeMissionDetailDispatcher final : public kodosi::CommandDispatcher {
public:
    QVector<QJsonObject> commands;
    QString rejectType;

    Result send(const kodosi::CommandLane lane, const QByteArrayView json) override
    {
        const auto document = QJsonDocument::fromJson(json.toByteArray());
        if ((lane != kodosi::CommandLane::Rooms
                && lane != kodosi::CommandLane::Sessions
                && lane != kodosi::CommandLane::AgentIntel
                && lane != kodosi::CommandLane::System)
            || !document.isObject()
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
    void failedOpenRetainsRecoverableSelection();
    void fullRetryRecoversEachDispatchFailure();
    void fullRetryPreservesStaleProjectionsAcrossTimeout();
    void hydrationPreservesLiveUpdates();
    void uncorrelatedErrorsCannotCancelHydration();
    void projectsCrewMessagesDeliveryAndFocusWithIncarnationFencing();
    void preservesUnknownArchivedDueAndResultTaskPresentation();
    void pagesMissionAttentionBeyondBoundedWindow();
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

QJsonObject lastCommand(
    const FakeMissionDetailDispatcher& dispatcher,
    const QString& type)
{
    const auto found = std::ranges::find(
        dispatcher.commands.crbegin(),
        dispatcher.commands.crend(),
        type,
        [](const QJsonObject& value) {
            return value.value(QStringLiteral("type")).toString();
        });
    return found == dispatcher.commands.crend() ? QJsonObject {} : *found;
}

int commandCount(
    const FakeMissionDetailDispatcher& dispatcher,
    const QString& type)
{
    return static_cast<int>(std::ranges::count(
        dispatcher.commands,
        type,
        [](const QJsonObject& value) {
            return value.value(QStringLiteral("type")).toString();
        }));
}

void completeHydration(
    kodosi::MissionDetailModel& detail,
    const QJsonObject& membersCommand,
    const QJsonObject& chatCommand,
    const QJsonObject& taskCommand,
    const QString& suffix)
{
    detail.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.members")},
        {QStringLiteral("room_id"), QStringLiteral("mission-1")},
        {QStringLiteral("hydration_id"),
         membersCommand.value(QStringLiteral("hydration_id"))},
        {QStringLiteral("members"),
         QJsonArray {
             QJsonObject {
                 {QStringLiteral("roomId"), QStringLiteral("mission-1")},
                 {QStringLiteral("userId"),
                  QStringLiteral("member-") + suffix},
                 {QStringLiteral("role"), QStringLiteral("member")},
                 {QStringLiteral("displayName"),
                  QStringLiteral("Member ") + suffix},
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
                 {QStringLiteral("id"),
                  QStringLiteral("message-") + suffix},
                 {QStringLiteral("roomId"), QStringLiteral("mission-1")},
                 {QStringLiteral("authorUserId"), QStringLiteral("me")},
                 {QStringLiteral("authorSessionId"), QJsonValue::Null},
                 {QStringLiteral("authorKind"), QStringLiteral("Human")},
                 {QStringLiteral("body"),
                  QStringLiteral("Message ") + suffix},
                 {QStringLiteral("recipientSessionIds"), QJsonArray {}},
                 {QStringLiteral("recipientUserIds"), QJsonArray {}},
                 {QStringLiteral("seq"), 1},
                 {QStringLiteral("postedAt"),
                  QStringLiteral("2026-08-31T09:00:00Z")},
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
        {QStringLiteral("tasks"),
         QJsonArray {
             task(QStringLiteral("task-") + suffix),
         }},
    }));
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

void MissionDetailModelTest::failedOpenRetainsRecoverableSelection()
{
    FakeMissionDetailDispatcher dispatcher;
    kodosi::MissionDirectoryModel directory(dispatcher);
    kodosi::MissionDetailModel detail(dispatcher, directory);
    seedDirectory(directory);
    detail.ingestAuthEvent(auth());
    dispatcher.rejectType = QStringLiteral("room.chat.list");

    QVERIFY(detail.openMission(QStringLiteral("mission-1")));
    QCOMPARE(detail.missionId(), QStringLiteral("mission-1"));
    QVERIFY(detail.loading());
    QVERIFY(!detail.lastError().isEmpty());
    QCOMPARE(detail.members()->rowCount(), 0);
}

void MissionDetailModelTest::fullRetryRecoversEachDispatchFailure()
{
    const QStringList rejectedTypes {
        QStringLiteral("room.refreshMembers"),
        QStringLiteral("room.chat.list"),
        QStringLiteral("room.tasks.list"),
    };
    for (const auto& rejectedType : rejectedTypes) {
        FakeMissionDetailDispatcher dispatcher;
        kodosi::MissionDirectoryModel directory(dispatcher);
        kodosi::MissionDetailModel detail(dispatcher, directory);
        seedDirectory(directory);
        detail.ingestAuthEvent(auth());
        dispatcher.rejectType = rejectedType;

        QVERIFY(detail.openMission(QStringLiteral("mission-1")));
        QCOMPARE(detail.missionId(), QStringLiteral("mission-1"));
        QVERIFY(!detail.lastError().isEmpty());

        dispatcher.rejectType.clear();
        const auto membersBefore =
            commandCount(dispatcher, QStringLiteral("room.refreshMembers"));
        const auto chatBefore =
            commandCount(dispatcher, QStringLiteral("room.chat.list"));
        const auto tasksBefore =
            commandCount(dispatcher, QStringLiteral("room.tasks.list"));
        QVERIFY(detail.retry());
        QCOMPARE(
            commandCount(
                dispatcher,
                QStringLiteral("room.refreshMembers")),
            membersBefore + 1);
        QCOMPARE(
            commandCount(dispatcher, QStringLiteral("room.chat.list")),
            chatBefore + 1);
        QCOMPARE(
            commandCount(dispatcher, QStringLiteral("room.tasks.list")),
            tasksBefore + 1);

        completeHydration(
            detail,
            lastCommand(
                dispatcher,
                QStringLiteral("room.refreshMembers")),
            lastCommand(dispatcher, QStringLiteral("room.chat.list")),
            lastCommand(dispatcher, QStringLiteral("room.tasks.list")),
            QStringLiteral("recovered"));
        QVERIFY(!detail.loading());
        QVERIFY(detail.lastError().isEmpty());
        QVERIFY(detail.membersReady());
        QVERIFY(detail.tasksReady());
        QCOMPARE(detail.members()->rowCount(), 1);
        QCOMPARE(detail.messages()->rowCount(), 1);
        QCOMPARE(detail.tasks()->rowCount(), 1);
    }
}

void MissionDetailModelTest::fullRetryPreservesStaleProjectionsAcrossTimeout()
{
    FakeMissionDetailDispatcher dispatcher;
    kodosi::MissionDirectoryModel directory(dispatcher);
    kodosi::MissionDetailModel detail(dispatcher, directory, 5);
    seedDirectory(directory);
    detail.ingestAuthEvent(auth());
    QVERIFY(detail.openMission(QStringLiteral("mission-1")));
    completeHydration(
        detail,
        lastCommand(
            dispatcher,
            QStringLiteral("room.refreshMembers")),
        lastCommand(dispatcher, QStringLiteral("room.chat.list")),
        lastCommand(dispatcher, QStringLiteral("room.tasks.list")),
        QStringLiteral("stale"));
    QVERIFY(!detail.loading());
    QVERIFY(detail.lastError().isEmpty());

    QVERIFY(detail.retry());
    const auto timedOutMembers = lastCommand(
        dispatcher,
        QStringLiteral("room.refreshMembers"));
    const auto timedOutChat =
        lastCommand(dispatcher, QStringLiteral("room.chat.list"));
    const auto timedOutTasks =
        lastCommand(dispatcher, QStringLiteral("room.tasks.list"));
    QVERIFY(detail.loading());
    QVERIFY(!detail.membersReady());
    QVERIFY(!detail.tasksReady());
    QCOMPARE(detail.members()->rowCount(), 1);
    QCOMPARE(detail.messages()->rowCount(), 1);
    QCOMPARE(detail.tasks()->rowCount(), 1);

    QTest::qWait(50);
    QVERIFY(!detail.loading());
    QVERIFY(detail.lastError().contains(QStringLiteral("timed out")));
    QCOMPARE(detail.members()->rowCount(), 1);
    QCOMPARE(detail.messages()->rowCount(), 1);
    QCOMPARE(detail.tasks()->rowCount(), 1);

    QVERIFY(detail.retry());
    const auto recoveredMembers = lastCommand(
        dispatcher,
        QStringLiteral("room.refreshMembers"));
    const auto recoveredChat =
        lastCommand(dispatcher, QStringLiteral("room.chat.list"));
    const auto recoveredTasks =
        lastCommand(dispatcher, QStringLiteral("room.tasks.list"));
    QVERIFY(
        recoveredMembers.value(QStringLiteral("hydration_id"))
            != timedOutMembers.value(QStringLiteral("hydration_id")));
    QVERIFY(
        recoveredChat.value(QStringLiteral("hydration_id"))
            != timedOutChat.value(QStringLiteral("hydration_id")));
    QVERIFY(
        recoveredTasks.value(QStringLiteral("hydration_id"))
            != timedOutTasks.value(QStringLiteral("hydration_id")));

    detail.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.members")},
        {QStringLiteral("room_id"), QStringLiteral("mission-1")},
        {QStringLiteral("hydration_id"),
         recoveredMembers.value(QStringLiteral("hydration_id"))},
        {QStringLiteral("members"), QJsonArray {}},
    }));
    QVERIFY(!detail.lastError().isEmpty());
    detail.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.chat.snapshot")},
        {QStringLiteral("room_id"), QStringLiteral("mission-1")},
        {QStringLiteral("hydration_id"),
         recoveredChat.value(QStringLiteral("hydration_id"))},
        {QStringLiteral("messages"), QJsonArray {}},
    }));
    QVERIFY(!detail.lastError().isEmpty());
    detail.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.tasks.page")},
        {QStringLiteral("room_id"), QStringLiteral("mission-1")},
        {QStringLiteral("hydration_id"),
         recoveredTasks.value(QStringLiteral("hydration_id"))},
        {QStringLiteral("request_offset"), 0},
        {QStringLiteral("has_more"), false},
        {QStringLiteral("tasks"), QJsonArray {}},
    }));

    QVERIFY(detail.lastError().isEmpty());
    QVERIFY(detail.membersReady());
    QVERIFY(detail.tasksReady());
    QCOMPARE(detail.members()->rowCount(), 0);
    QCOMPARE(detail.messages()->rowCount(), 0);
    QCOMPARE(detail.tasks()->rowCount(), 0);
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

void MissionDetailModelTest::projectsCrewMessagesDeliveryAndFocusWithIncarnationFencing()
{
    FakeMissionDetailDispatcher dispatcher;
    kodosi::MissionDirectoryModel directory(dispatcher);
    seedDirectory(directory);
    kodosi::PeopleModel people;
    kodosi::SessionCatalogModel sessions;
    kodosi::SessionActions sessionActions(dispatcher, sessions);
    const auto settingsPath =
        QCoreApplication::applicationDirPath()
        + QStringLiteral("/mission-detail-focus-settings.ini");
    QFile::remove(settingsPath);
    auto settings = std::make_unique<QSettings>(
        settingsPath,
        QSettings::IniFormat);
    kodosi::DesktopStateModel desktopState(
        std::move(settings),
        QList<QRect> {QRect(0, 0, 1280, 800)},
        0,
        false);
    desktopState.attachSessionCatalog(&sessions);

    kodosi::MissionDetailModel::Dependencies dependencies;
    dependencies.people = &people;
    dependencies.sessions = &sessions;
    dependencies.desktopState = &desktopState;
    dependencies.sessionActions = &sessionActions;
    kodosi::MissionDetailModel detail(
        dispatcher,
        directory,
        dependencies);

    people.ingestAuthEvent(auth());
    sessions.ingestAuthEvent(auth());
    sessionActions.ingestAuthEvent(auth());
    detail.ingestAuthEvent(auth());
    sessions.ingestSessionEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("session.list")},
        {QStringLiteral("sessions"),
         QJsonArray {
             QJsonObject {
                 {QStringLiteral("kind"), QStringLiteral("local")},
                 {QStringLiteral("id"), QStringLiteral("agent-local")},
                 {QStringLiteral("incarnationId"),
                  QStringLiteral("agent-incarnation-a")},
                 {QStringLiteral("name"), QStringLiteral("Builder")},
                 {QStringLiteral("project"), QStringLiteral("/repo")},
                 {QStringLiteral("mode"), QStringLiteral("normal")},
                 {QStringLiteral("status"), QStringLiteral("active")},
                 {QStringLiteral("recovery"), QStringLiteral("live")},
                 {QStringLiteral("scope"), QStringLiteral("room")},
                 {QStringLiteral("access"), QStringLiteral("inject")},
                 {QStringLiteral("roomId"), QStringLiteral("mission-1")},
                 {QStringLiteral("backendSessionId"),
                  QStringLiteral("agent-backend")},
                 {QStringLiteral("backendIncarnationId"),
                  QStringLiteral("agent-backend-incarnation")},
             },
         }},
    }));

    QVERIFY(detail.openMission(QStringLiteral("mission-1")));
    const auto members =
        command(dispatcher, QStringLiteral("room.refreshMembers"));
    const auto chat =
        command(dispatcher, QStringLiteral("room.chat.list"));
    detail.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.members")},
        {QStringLiteral("room_id"), QStringLiteral("mission-1")},
        {QStringLiteral("hydration_id"),
         members.value(QStringLiteral("hydration_id"))},
        {QStringLiteral("members"),
         QJsonArray {
             QJsonObject {
                 {QStringLiteral("roomId"), QStringLiteral("mission-1")},
                 {QStringLiteral("userId"), QStringLiteral("me")},
                 {QStringLiteral("role"), QStringLiteral("owner")},
                 {QStringLiteral("displayName"), QStringLiteral("Me")},
             },
             QJsonObject {
                 {QStringLiteral("roomId"), QStringLiteral("mission-1")},
                 {QStringLiteral("userId"), QStringLiteral("member")},
                 {QStringLiteral("role"), QStringLiteral("member")},
                 {QStringLiteral("displayName"), QStringLiteral("Reviewer")},
             },
         }},
    }));
    detail.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.chat.snapshot")},
        {QStringLiteral("room_id"), QStringLiteral("mission-1")},
        {QStringLiteral("hydration_id"),
         chat.value(QStringLiteral("hydration_id"))},
        {QStringLiteral("messages"),
         QJsonArray {
             QJsonObject {
                 {QStringLiteral("id"), QStringLiteral("directed")},
                 {QStringLiteral("roomId"), QStringLiteral("mission-1")},
                 {QStringLiteral("authorUserId"), QStringLiteral("me")},
                 {QStringLiteral("authorSessionId"),
                  QStringLiteral("agent-local")},
                 {QStringLiteral("authorKind"), QStringLiteral("Agent")},
                 {QStringLiteral("recipientSessionIds"),
                  QJsonArray {QStringLiteral("agent-local")}},
                 {QStringLiteral("recipientUserIds"),
                  QJsonArray {QStringLiteral("member")}},
                 {QStringLiteral("body"), QStringLiteral("Review")},
                 {QStringLiteral("seq"), 1},
                 {QStringLiteral("postedAt"),
                  QStringLiteral("2026-09-03T12:00:00Z")},
             },
         }},
    }));

    QCOMPARE(detail.crew()->rowCount(), 3);
    QString agentPresentation;
    for (auto row = 0; row < detail.crew()->rowCount(); ++row) {
        const auto index = detail.crew()->index(row);
        if (detail.crew()
                ->data(index, kodosi::MissionCrewModel::KindRole)
                .toInt()
            == static_cast<int>(kodosi::MissionCrewModel::Kind::Agent)) {
            agentPresentation = detail.crew()
                                    ->data(
                                        index,
                                        kodosi::MissionCrewModel::PresentationIdRole)
                                    .toString();
        }
    }
    QVERIFY(!agentPresentation.isEmpty());
    QVERIFY(detail.selectCrew(agentPresentation));
    QCOMPARE(detail.crew()->dispatchableAgentCount(), 1);
    QCOMPARE(
        detail.selectedTerminalSessionId(),
        QStringLiteral("agent-local"));
    QVERIFY(detail.selectedCanOpenFullTerminal());
    QVERIFY(detail.selectedCanInterrupt());
    desktopState.setActiveView(1);
    QVERIFY(detail.openFocusedFullTerminal());
    QCOMPARE(desktopState.activeView(), 0);
    QCOMPARE(
        desktopState.selectedSessionId(),
        QStringLiteral("agent-local"));
    QVERIFY(detail.interruptFocused());
    QCOMPARE(
        dispatcher.commands.back().value(QStringLiteral("type")).toString(),
        QStringLiteral("session.interrupt"));
    QCOMPARE(
        dispatcher.commands.back()
            .value(QStringLiteral("expectedRuntimeIncarnationId"))
            .toString(),
        QStringLiteral("agent-incarnation-a"));

    QCOMPARE(detail.messages()->rowCount(), 1);
    const auto messageIndex = detail.messages()->index(0);
    QCOMPARE(
        detail.messages()
            ->data(
                messageIndex,
                kodosi::MissionMessagesModel::AuthorDisplayRole)
            .toString(),
        QStringLiteral("Builder"));
    QVERIFY(detail.messages()
                ->data(
                    messageIndex,
                    kodosi::MissionMessagesModel::AudienceSummaryRole)
                .toString()
                .contains(QStringLiteral("Reviewer")));
    const auto recipientPresentations = detail.messages()
                                            ->data(
                                                messageIndex,
                                                kodosi::MissionMessagesModel::
                                                    RecipientPresentationIdsRole)
                                            .toStringList();
    QCOMPARE(recipientPresentations.size(), 2);
    QVERIFY(!recipientPresentations.contains(QStringLiteral("agent-local")));
    QVERIFY(!recipientPresentations.contains(QStringLiteral("member")));

    const QStringList deliveryStates {
        QStringLiteral("waitingForAdapter"),
        QStringLiteral("ready"),
        QStringLiteral("offered"),
        QStringLiteral("acceptedByTransport"),
        QStringLiteral("actedOn"),
        QStringLiteral("failed"),
    };
    for (const auto& deliveryState : deliveryStates) {
        detail.ingestRoomEvent(envelope({
            {QStringLiteral("type"), QStringLiteral("room.agent.delivery")},
            {QStringLiteral("session_id"), QStringLiteral("agent-local")},
            {QStringLiteral("session_incarnation_id"),
             QStringLiteral("agent-incarnation-a")},
            {QStringLiteral("state"), deliveryState},
            {QStringLiteral("detail"), QStringLiteral("delivery detail")},
        }));
        bool sawDelivery = false;
        for (auto row = 0; row < detail.crew()->rowCount(); ++row) {
            const auto index = detail.crew()->index(row);
            if (detail.crew()
                    ->data(index, kodosi::MissionCrewModel::DeliveryStateRole)
                    .toString()
                == deliveryState) {
                sawDelivery = true;
            }
        }
        QVERIFY(sawDelivery);
    }

    sessions.ingestSessionEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("session.upsert")},
        {QStringLiteral("session"),
         QJsonObject {
             {QStringLiteral("kind"), QStringLiteral("local")},
             {QStringLiteral("id"), QStringLiteral("agent-local")},
             {QStringLiteral("incarnationId"),
              QStringLiteral("agent-incarnation-b")},
             {QStringLiteral("name"), QStringLiteral("Builder")},
             {QStringLiteral("project"), QStringLiteral("/repo")},
             {QStringLiteral("mode"), QStringLiteral("normal")},
             {QStringLiteral("status"), QStringLiteral("active")},
             {QStringLiteral("recovery"), QStringLiteral("live")},
             {QStringLiteral("scope"), QStringLiteral("room")},
             {QStringLiteral("access"), QStringLiteral("inject")},
             {QStringLiteral("roomId"), QStringLiteral("mission-1")},
             {QStringLiteral("backendSessionId"),
              QStringLiteral("agent-backend")},
             {QStringLiteral("backendIncarnationId"),
              QStringLiteral("agent-backend-incarnation-b")},
         }},
    }));
    QVERIFY(detail.selectedCrewPresentationId().isEmpty());
    QVERIFY(detail.selectedTerminalSessionId().isEmpty());
    QVERIFY(!detail.crew()->containsPresentationId(agentPresentation));

    detail.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.agent.delivery")},
        {QStringLiteral("session_id"), QStringLiteral("agent-local")},
        {QStringLiteral("session_incarnation_id"),
         QStringLiteral("agent-incarnation-a")},
        {QStringLiteral("state"), QStringLiteral("failed")},
    }));
    for (auto row = 0; row < detail.crew()->rowCount(); ++row) {
        QCOMPARE(
            detail.crew()
                ->data(
                    detail.crew()->index(row),
                    kodosi::MissionCrewModel::DeliveryStateRole)
                .toString(),
            QString {});
    }
    QFile::remove(settingsPath);
}

void MissionDetailModelTest::preservesUnknownArchivedDueAndResultTaskPresentation()
{
    FakeMissionDetailDispatcher dispatcher;
    kodosi::MissionDirectoryModel directory(dispatcher);
    kodosi::MissionDetailModel detail(dispatcher, directory);
    seedDirectory(directory);
    detail.ingestAuthEvent(auth());
    QVERIFY(detail.openMission(QStringLiteral("mission-1")));
    const auto members =
        command(dispatcher, QStringLiteral("room.refreshMembers"));
    const auto tasks =
        command(dispatcher, QStringLiteral("room.tasks.list"));
    QCOMPARE(tasks.value(QStringLiteral("limit")).toInteger(), 500);
    detail.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.members")},
        {QStringLiteral("room_id"), QStringLiteral("mission-1")},
        {QStringLiteral("hydration_id"),
         members.value(QStringLiteral("hydration_id"))},
        {QStringLiteral("members"),
         QJsonArray {
             QJsonObject {
                 {QStringLiteral("roomId"), QStringLiteral("mission-1")},
                 {QStringLiteral("userId"), QStringLiteral("me")},
                 {QStringLiteral("role"), QStringLiteral("owner")},
                 {QStringLiteral("displayName"), QStringLiteral("Me")},
             },
         }},
    }));

    auto unknown = task(QStringLiteral("unknown"));
    unknown.insert(QStringLiteral("status"), QStringLiteral("PausedByRuntime"));
    unknown.insert(
        QStringLiteral("dueAt"),
        QStringLiteral("2026-09-05T10:00:00Z"));
    unknown.insert(
        QStringLiteral("completedAt"),
        QStringLiteral("2026-09-05T11:00:00Z"));
    unknown.insert(QStringLiteral("result"), QStringLiteral("Evidence text"));
    unknown.insert(
        QStringLiteral("resultAuthorUserId"),
        QStringLiteral("me"));
    auto archived = task(QStringLiteral("archived"));
    archived.insert(QStringLiteral("status"), QStringLiteral("Archived"));
    detail.ingestRoomEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("room.tasks.page")},
        {QStringLiteral("room_id"), QStringLiteral("mission-1")},
        {QStringLiteral("hydration_id"),
         tasks.value(QStringLiteral("hydration_id"))},
        {QStringLiteral("request_offset"), 0},
        {QStringLiteral("has_more"), false},
        {QStringLiteral("tasks"), QJsonArray {unknown, archived}},
    }));

    QCOMPARE(detail.tasks()->rowCount(), 2);
    QModelIndex unknownIndex;
    QModelIndex archivedIndex;
    for (auto row = 0; row < detail.tasks()->rowCount(); ++row) {
        const auto index = detail.tasks()->index(row);
        if (detail.tasks()
                ->data(index, kodosi::MissionTasksModel::TaskIdRole)
                .toString()
            == QStringLiteral("unknown")) {
            unknownIndex = index;
        } else {
            archivedIndex = index;
        }

    }
    QVERIFY(unknownIndex.isValid());
    QVERIFY(!detail.tasks()
                 ->data(
                     unknownIndex,
                     kodosi::MissionTasksModel::KnownStatusRole)
                 .toBool());
    QVERIFY(detail.tasks()
                ->data(
                    unknownIndex,
                    kodosi::MissionTasksModel::StatusLabelRole)
                .toString()
                .contains(QStringLiteral("PausedByRuntime")));
    QVERIFY(!detail.tasks()
                 ->data(
                     unknownIndex,
                     kodosi::MissionTasksModel::UnknownStatusMessageRole)
                 .toString()
                 .isEmpty());
    QCOMPARE(
        detail.tasks()
            ->data(
                unknownIndex,
                kodosi::MissionTasksModel::ResultEvidenceRole)
            .toString(),
        QStringLiteral("Evidence text"));
    QCOMPARE(
        detail.tasks()
            ->data(
                unknownIndex,
                kodosi::MissionTasksModel::ResultAuthorDisplayRole)
            .toString(),
        QStringLiteral("You"));
    QVERIFY(detail.tasks()
                ->data(
                    unknownIndex,
                    kodosi::MissionTasksModel::DueAtRole)
                .toDateTime()
                .isValid());
    QVERIFY(detail.tasks()
                ->data(
                    unknownIndex,
                    kodosi::MissionTasksModel::CompletedAtRole)
                .toDateTime()
                .isValid());
    QVERIFY(archivedIndex.isValid());
    QVERIFY(detail.tasks()
                ->data(
                    archivedIndex,
                    kodosi::MissionTasksModel::KnownStatusRole)
                .toBool());
    QCOMPARE(
        detail.tasks()
            ->data(
                archivedIndex,
                kodosi::MissionTasksModel::StatusLabelRole)
            .toString(),
        QStringLiteral("Archived"));
}

void MissionDetailModelTest::pagesMissionAttentionBeyondBoundedWindow()
{
    FakeMissionDetailDispatcher dispatcher;
    kodosi::MissionDirectoryModel directory(dispatcher);
    seedDirectory(directory);
    kodosi::SessionCatalogModel sessions;
    kodosi::SessionActions sessionActions(dispatcher, sessions);
    kodosi::AgentSessionIntelModel intel(dispatcher, sessions);
    kodosi::PendingPermissionsModel permissions(dispatcher, sessions);
    kodosi::AttentionModel attention(
        permissions,
        intel,
        sessions,
        sessionActions);
    kodosi::MissionDetailModel detail(
        dispatcher,
        directory,
        {
            .sessions = &sessions,
            .attention = &attention,
            .sessionActions = &sessionActions,
        });

    const auto authEvent = auth();
    sessions.ingestAuthEvent(authEvent);
    sessionActions.ingestAuthEvent(authEvent);
    intel.ingestAuthEvent(authEvent);
    permissions.ingestAuthEvent(authEvent);
    detail.ingestAuthEvent(authEvent);
    QVERIFY(detail.openMission(QStringLiteral("mission-1")));

    QJsonArray sessionValues;
    QJsonArray requests;
    for (auto index = 0; index < 65; ++index) {
        const auto number = index + 1;
        const auto sessionId =
            QStringLiteral("mission-attention-%1").arg(number);
        const auto incarnation =
            QUuid::createUuidV7().toString(QUuid::WithoutBraces);
        sessionValues.append(QJsonObject {
            {QStringLiteral("kind"), QStringLiteral("local")},
            {QStringLiteral("id"), sessionId},
            {QStringLiteral("incarnationId"), incarnation},
            {QStringLiteral("name"),
             QStringLiteral("Attention agent %1").arg(number)},
            {QStringLiteral("project"), QStringLiteral("/repo")},
            {QStringLiteral("mode"), QStringLiteral("normal")},
            {QStringLiteral("status"), QStringLiteral("active")},
            {QStringLiteral("recovery"), QStringLiteral("live")},
            {QStringLiteral("scope"), QStringLiteral("room")},
            {QStringLiteral("access"), QStringLiteral("approve")},
            {QStringLiteral("roomId"), QStringLiteral("mission-1")},
            {QStringLiteral("backendSessionId"),
             QStringLiteral("backend-%1").arg(number)},
            {QStringLiteral("backendIncarnationId"),
             QUuid::createUuidV7().toString(QUuid::WithoutBraces)},
        });
        requests.append(QJsonObject {
            {QStringLiteral("sessionId"), sessionId},
            {QStringLiteral("sessionIncarnationId"), incarnation},
            {QStringLiteral("requestGeneration"), 1},
            {QStringLiteral("toolUseId"),
             QStringLiteral("tool-%1").arg(number)},
            {QStringLiteral("toolName"),
             QStringLiteral("Tool %1").arg(number)},
            {QStringLiteral("toolInput"), QJsonObject {}},
            {QStringLiteral("createdAtMs"),
             1'780'000'000'000.0 + index},
            {QStringLiteral("deadlineAtMs"),
             1'900'000'000'000.0},
            {QStringLiteral("risk"), QStringLiteral("destructive")},
            {QStringLiteral("decisionPhase"),
             QStringLiteral("actionable")},
        });
    }
    sessions.ingestSessionEvent(envelope({
        {QStringLiteral("type"), QStringLiteral("session.list")},
        {QStringLiteral("sessions"), sessionValues},
    }));
    permissions.ingestAgentIntelEvent(envelope({
        {QStringLiteral("type"),
         QStringLiteral(
             "agent.intel.pendingPermissionsSnapshot")},
        {QStringLiteral("generation"), 1},
        {QStringLiteral("requests"), requests},
    }));

    auto* scoped = detail.attention();
    QCOMPARE(scoped->totalCount(), 65);
    QCOMPARE(scoped->rowCount(), 64);
    QCOMPARE(scoped->pageOffset(), 0);
    QVERIFY(scoped->truncated());
    QVERIFY(scoped->canLoadMore());
    QVERIFY(!scoped->canLoadPrevious());
    auto* crew = detail.crew();
    const auto pageTwoAgent = [&] {
        for (auto row = 0; row < crew->rowCount(); ++row) {
            const auto index = crew->index(row);
            if (crew
                    ->data(index, kodosi::MissionCrewModel::DisplayNameRole)
                    .toString()
                == QStringLiteral("Attention agent 65")) {
                return index;
            }
        }
        return QModelIndex {};
    }();
    QVERIFY(pageTwoAgent.isValid());
    QCOMPARE(
        crew
            ->data(
                pageTwoAgent,
                kodosi::MissionCrewModel::AttentionCountRole)
            .toInt(),
        1);
    QVERIFY(scoped->loadMore());
    QCOMPARE(scoped->totalCount(), 65);
    QCOMPARE(scoped->rowCount(), 1);
    QCOMPARE(scoped->pageOffset(), 64);
    QCOMPARE(
        scoped->data(
            scoped->index(0),
            kodosi::MissionScopedAttentionModel::TitleRole)
            .toString(),
        QStringLiteral("Tool 65 approval"));
    QVERIFY(!scoped->canLoadMore());
    QVERIFY(scoped->canLoadPrevious());
    QVERIFY(scoped->loadPrevious());
    QCOMPARE(scoped->rowCount(), 64);
    QCOMPARE(scoped->pageOffset(), 0);
}

QTEST_GUILESS_MAIN(MissionDetailModelTest)

#include "tst_mission_detail_model.moc"
