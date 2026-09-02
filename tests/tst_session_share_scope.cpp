#include "models/SessionShareScope.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QUuid>
#include <QtTest/QTest>

class FakeShareDispatcher final : public kodosi::CommandDispatcher {
public:
    struct Sent {
        kodosi::CommandLane lane;
        QJsonObject command;
    };

    QVector<Sent> sent;
    bool rejectScope = false;

    Result send(
        const kodosi::CommandLane lane,
        const QByteArrayView json) override
    {
        const auto document = QJsonDocument::fromJson(json.toByteArray());
        if (!document.isObject()
            || (rejectScope && lane == kodosi::CommandLane::Sessions
                && document.object().value(QStringLiteral("type")).toString()
                    == QStringLiteral("session.scope"))) {
            return std::unexpected(kodosi::RuntimeFailure {
                .code = kodosi::RuntimeFailure::Code::FfiRejected,
                .ffiResult = -1,
                .message = QStringLiteral("Rejected"),
            });
        }
        sent.push_back({.lane = lane, .command = document.object()});
        return {};
    }

    [[nodiscard]] QVector<QJsonObject> scopeCommands() const
    {
        QVector<QJsonObject> result;
        for (const auto& entry : sent) {
            if (entry.lane == kodosi::CommandLane::Sessions
                && entry.command.value(QStringLiteral("type")).toString()
                    == QStringLiteral("session.scope")) {
                result.push_back(entry.command);
            }
        }
        return result;
    }
};

class SessionShareScopeTest final : public QObject {
    Q_OBJECT

private slots:
    void sendsExactCommandsAndRejectsInvalidTargets();
    void rejectsNoopUnavailableAndMissingMission();
    void replacementIncarnationFailsAndReleases();
    void correlatesOnlyExactReceiptsAndErrors();
    void boundsAcceptedBudget();
    void recoversOnlyFromAuthoritativeCatalog();
    void fencesAccountsAndResetsRuntime();
    void timeoutRetriesWithSameRequestIdentity();
};

namespace {

QByteArray authEvent(const QString& userId, const quint64 epoch)
{
    return QJsonDocument(QJsonObject {
        {QStringLiteral("type"),
         userId.isEmpty()
             ? QStringLiteral("auth.required")
             : QStringLiteral("auth.ready")},
        {QStringLiteral("userId"), userId},
        {QStringLiteral("accountEpoch"), static_cast<qint64>(epoch)},
    }).toJson(QJsonDocument::Compact);
}

QJsonObject sessionObject(
    const QString& incarnation,
    const QString& scope = QStringLiteral("justMe"),
    const QString& roomId = {},
    const QString& kind = QStringLiteral("local"),
    const QString& status = QStringLiteral("active"),
    const QString& recovery = QStringLiteral("live"))
{
    QJsonObject session {
        {QStringLiteral("kind"), kind},
        {QStringLiteral("id"), QStringLiteral("session-1")},
        {QStringLiteral("incarnationId"), incarnation},
        {QStringLiteral("name"), QStringLiteral("Agent")},
        {QStringLiteral("project"), QStringLiteral("/repo")},
        {QStringLiteral("mode"), QStringLiteral("normal")},
        {QStringLiteral("status"), status},
        {QStringLiteral("recovery"), recovery},
        {QStringLiteral("scope"), scope},
        {QStringLiteral("access"), QStringLiteral("approve")},
    };
    if (!roomId.isEmpty()) {
        session.insert(QStringLiteral("roomId"), roomId);
        session.insert(QStringLiteral("roomName"), QStringLiteral("Launch"));
    }
    if (kind == QStringLiteral("remote")) {
        session.insert(QStringLiteral("permissions"), 0x1ff);
    }
    return session;
}

QByteArray sessionEvent(
    QJsonObject event,
    const QString& userId = QStringLiteral("me"),
    const quint64 epoch = 1)
{
    event.insert(QStringLiteral("authority"), QStringLiteral("accountContext"));
    event.insert(QStringLiteral("accountUserId"), userId);
    event.insert(
        QStringLiteral("accountEpoch"),
        static_cast<qint64>(epoch));
    return QJsonDocument(event).toJson(QJsonDocument::Compact);
}

void authenticate(
    kodosi::SessionCatalogModel& sessions,
    kodosi::MissionDirectoryModel& missions,
    kodosi::SessionShareScope& sharing,
    const QString& userId = QStringLiteral("me"),
    const quint64 epoch = 1)
{
    const auto event = authEvent(userId, epoch);
    sessions.ingestAuthEvent(event);
    missions.ingestAuthEvent(event);
    sharing.ingestAuthEvent(event);
}

void upsert(
    kodosi::SessionCatalogModel& sessions,
    QJsonObject session,
    const QString& userId = QStringLiteral("me"),
    const quint64 epoch = 1)
{
    sessions.ingestSessionEvent(sessionEvent(
        {
            {QStringLiteral("type"), QStringLiteral("session.upsert")},
            {QStringLiteral("session"), std::move(session)},
        },
        userId,
        epoch));
}

void snapshot(
    kodosi::SessionCatalogModel& sessions,
    QJsonObject session,
    const QString& userId = QStringLiteral("me"),
    const quint64 epoch = 1)
{
    sessions.ingestSessionEvent(sessionEvent(
        {
            {QStringLiteral("type"), QStringLiteral("session.list")},
            {QStringLiteral("sessions"), QJsonArray {std::move(session)}},
        },
        userId,
        epoch));
}

void addMission(
    kodosi::MissionDirectoryModel& missions,
    const QString& userId = QStringLiteral("me"),
    const quint64 epoch = 1)
{
    missions.ingestRoomEvent(sessionEvent(
        {
            {QStringLiteral("type"), QStringLiteral("room.snapshot")},
            {QStringLiteral("rooms"),
             QJsonArray {
                 QJsonObject {
                     {QStringLiteral("id"), QStringLiteral("mission-1")},
                     {QStringLiteral("name"), QStringLiteral("Launch")},
                     {QStringLiteral("slug"), QStringLiteral("launch")},
                     {QStringLiteral("ownerUserId"), userId},
                     {QStringLiteral("rosterGeneration"), 1},
                 },
             }},
        },
        userId,
        epoch));
}

QByteArray receipt(
    const QString& type,
    const QString& requestId,
    const QString& incarnation,
    const QString& scope,
    const QString& roomId = {},
    const std::optional<quint64> budget = std::nullopt,
    const QString& sessionId = QStringLiteral("session-1"),
    const QString& userId = QStringLiteral("me"),
    const quint64 epoch = 1)
{
    QJsonObject event {
        {QStringLiteral("type"), type},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("sessionId"), sessionId},
        {QStringLiteral("expectedRuntimeIncarnationId"), incarnation},
        {QStringLiteral("scope"), scope},
    };
    if (!roomId.isEmpty()) {
        event.insert(QStringLiteral("roomId"), roomId);
    }
    if (budget) {
        event.insert(
            QStringLiteral("budgetMs"),
            static_cast<qint64>(*budget));
    }
    return sessionEvent(std::move(event), userId, epoch);
}

} // namespace

void SessionShareScopeTest::sendsExactCommandsAndRejectsInvalidTargets()
{
    FakeShareDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    kodosi::MissionDirectoryModel missions(dispatcher);
    kodosi::SessionShareScope sharing(dispatcher, sessions, missions);
    authenticate(sessions, missions, sharing);
    addMission(missions);
    upsert(sessions, sessionObject(QStringLiteral("inc-1")));

    QVERIFY(sharing.setScope(
        QStringLiteral("session-1"),
        QStringLiteral("myDevices"),
        {}));
    auto commands = dispatcher.scopeCommands();
    QCOMPARE(commands.size(), 1);
    const auto command = commands.back();
    QCOMPARE(command.size(), 5);
    QCOMPARE(
        command.value(QStringLiteral("type")).toString(),
        QStringLiteral("session.scope"));
    QCOMPARE(
        command.value(QStringLiteral("sessionId")).toString(),
        QStringLiteral("session-1"));
    QCOMPARE(
        command.value(QStringLiteral("expectedRuntimeIncarnationId")).toString(),
        QStringLiteral("inc-1"));
    QCOMPARE(
        command.value(QStringLiteral("scope")).toString(),
        QStringLiteral("myDevices"));
    QVERIFY(!command.contains(QStringLiteral("roomId")));
    const QUuid request(
        command.value(QStringLiteral("requestId")).toString());
    QVERIFY(!request.isNull());
    QCOMPARE(request.version(), QUuid::UnixEpoch);

    sharing.resetRuntimeAuthority();
    authenticate(sessions, missions, sharing);
    QVERIFY(sharing.setScope(
        QStringLiteral("session-1"),
        QStringLiteral("room"),
        QStringLiteral("mission-1")));
    commands = dispatcher.scopeCommands();
    QCOMPARE(commands.size(), 2);
    QCOMPARE(commands.back().size(), 6);
    QCOMPARE(
        commands.back().value(QStringLiteral("roomId")).toString(),
        QStringLiteral("mission-1"));

    sharing.resetRuntimeAuthority();
    authenticate(sessions, missions, sharing);
    QVERIFY(!sharing.setScope(
        QStringLiteral("session-1"),
        QStringLiteral("friends"),
        {}));
    QCOMPARE(sharing.phase(QStringLiteral("session-1")), QStringLiteral("error"));
    QCOMPARE(dispatcher.scopeCommands().size(), 2);
}

void SessionShareScopeTest::rejectsNoopUnavailableAndMissingMission()
{
    FakeShareDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    kodosi::MissionDirectoryModel missions(dispatcher);
    kodosi::SessionShareScope sharing(dispatcher, sessions, missions);
    authenticate(sessions, missions, sharing);

    upsert(sessions, sessionObject(QStringLiteral("inc-1")));
    QVERIFY(!sharing.setScope(
        QStringLiteral("session-1"),
        QStringLiteral("justMe"),
        {}));
    QVERIFY(!sharing.setScope(
        QStringLiteral("session-1"),
        QStringLiteral("room"),
        QStringLiteral("missing")));
    QVERIFY(!sharing.setScope(
        QStringLiteral("session-1"),
        QStringLiteral("invalid"),
        {}));
    QCOMPARE(dispatcher.scopeCommands().size(), 0);

    upsert(sessions, sessionObject(
        QStringLiteral("inc-1"),
        QStringLiteral("justMe"),
        {},
        QStringLiteral("remote")));
    QVERIFY(!sharing.setScope(
        QStringLiteral("session-1"),
        QStringLiteral("myDevices"),
        {}));

    upsert(sessions, sessionObject(
        QStringLiteral("inc-1"),
        QStringLiteral("justMe"),
        {},
        QStringLiteral("local"),
        QStringLiteral("stopping")));
    QVERIFY(!sharing.setScope(
        QStringLiteral("session-1"),
        QStringLiteral("myDevices"),
        {}));
    QCOMPARE(dispatcher.scopeCommands().size(), 0);
}

void SessionShareScopeTest::replacementIncarnationFailsAndReleases()
{
    FakeShareDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    kodosi::MissionDirectoryModel missions(dispatcher);
    kodosi::SessionShareScope sharing(dispatcher, sessions, missions);
    authenticate(sessions, missions, sharing);
    upsert(sessions, sessionObject(QStringLiteral("inc-1")));

    QVERIFY(sharing.setScope(
        QStringLiteral("session-1"),
        QStringLiteral("myDevices"),
        {}));
    const auto original = sharing.requestId(QStringLiteral("session-1"));
    upsert(sessions, sessionObject(QStringLiteral("inc-2")));
    QCOMPARE(sharing.phase(QStringLiteral("session-1")), QStringLiteral("error"));
    QVERIFY(sharing.message(QStringLiteral("session-1")).contains(
        QStringLiteral("restarted")));

    QVERIFY(sharing.retry(QStringLiteral("session-1")));
    const auto retried = sharing.requestId(QStringLiteral("session-1"));
    QVERIFY(retried != original);
    const auto commands = dispatcher.scopeCommands();
    QCOMPARE(commands.size(), 2);
    QCOMPARE(
        commands.back()
            .value(QStringLiteral("expectedRuntimeIncarnationId"))
            .toString(),
        QStringLiteral("inc-2"));
}

void SessionShareScopeTest::correlatesOnlyExactReceiptsAndErrors()
{
    FakeShareDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    kodosi::MissionDirectoryModel missions(dispatcher);
    kodosi::SessionShareScope sharing(dispatcher, sessions, missions);
    authenticate(sessions, missions, sharing);
    addMission(missions);
    upsert(sessions, sessionObject(QStringLiteral("inc-1")));
    QVERIFY(sharing.setScope(
        QStringLiteral("session-1"),
        QStringLiteral("room"),
        QStringLiteral("mission-1")));
    const auto request = sharing.requestId(QStringLiteral("session-1"));

    sharing.ingestSessionEvent(receipt(
        QStringLiteral("session.scopeAccepted"),
        QStringLiteral("wrong"),
        QStringLiteral("inc-1"),
        QStringLiteral("room"),
        QStringLiteral("mission-1"),
        100));
    QCOMPARE(
        sharing.phase(QStringLiteral("session-1")),
        QStringLiteral("pending"));
    sharing.ingestSessionEvent(receipt(
        QStringLiteral("session.scopeAccepted"),
        request,
        QStringLiteral("inc-1"),
        QStringLiteral("room"),
        QStringLiteral("wrong"),
        100));
    QCOMPARE(
        sharing.phase(QStringLiteral("session-1")),
        QStringLiteral("pending"));
    sharing.ingestSessionEvent(receipt(
        QStringLiteral("session.scopeAccepted"),
        request,
        QStringLiteral("inc-1"),
        QStringLiteral("room"),
        QStringLiteral("mission-1"),
        100));
    QCOMPARE(
        sharing.phase(QStringLiteral("session-1")),
        QStringLiteral("accepted"));

    sharing.ingestSessionEvent(receipt(
        QStringLiteral("session.scopeChanged"),
        request,
        QStringLiteral("wrong-inc"),
        QStringLiteral("room"),
        QStringLiteral("mission-1")));
    QCOMPARE(
        sharing.phase(QStringLiteral("session-1")),
        QStringLiteral("accepted"));
    sharing.ingestSessionEvent(receipt(
        QStringLiteral("session.scopeChanged"),
        request,
        QStringLiteral("inc-1"),
        QStringLiteral("room"),
        QStringLiteral("mission-1")));
    QCOMPARE(sharing.phase(QStringLiteral("session-1")), QStringLiteral("idle"));

    QVERIFY(sharing.setScope(
        QStringLiteral("session-1"),
        QStringLiteral("myDevices"),
        {}));
    const auto errorRequest = sharing.requestId(QStringLiteral("session-1"));
    sharing.ingestSessionEvent(sessionEvent({
        {QStringLiteral("type"), QStringLiteral("session.error")},
        {QStringLiteral("operation"), QStringLiteral("session.mode")},
        {QStringLiteral("requestId"), errorRequest},
        {QStringLiteral("sessionId"), QStringLiteral("session-1")},
        {QStringLiteral("message"), QStringLiteral("Wrong operation")},
    }));
    QCOMPARE(
        sharing.phase(QStringLiteral("session-1")),
        QStringLiteral("pending"));
    sharing.ingestSessionEvent(sessionEvent({
        {QStringLiteral("type"), QStringLiteral("session.error")},
        {QStringLiteral("operation"), QStringLiteral("session.scope")},
        {QStringLiteral("requestId"), errorRequest},
        {QStringLiteral("sessionId"), QStringLiteral("session-1")},
        {QStringLiteral("message"), QStringLiteral("Denied")},
    }));
    QCOMPARE(sharing.phase(QStringLiteral("session-1")), QStringLiteral("error"));
    QCOMPARE(
        sharing.message(QStringLiteral("session-1")),
        QStringLiteral("Denied"));
}

void SessionShareScopeTest::boundsAcceptedBudget()
{
    FakeShareDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    kodosi::MissionDirectoryModel missions(dispatcher);
    kodosi::SessionShareScope sharing(
        dispatcher,
        sessions,
        missions,
        {.receiptTimeoutMs = 100, .maximumAcceptedBudgetMs = 20});
    authenticate(sessions, missions, sharing);
    upsert(sessions, sessionObject(QStringLiteral("inc-1")));
    QVERIFY(sharing.setScope(
        QStringLiteral("session-1"),
        QStringLiteral("myDevices"),
        {}));
    const auto request = sharing.requestId(QStringLiteral("session-1"));
    sharing.ingestSessionEvent(receipt(
        QStringLiteral("session.scopeAccepted"),
        request,
        QStringLiteral("inc-1"),
        QStringLiteral("myDevices"),
        {},
        1'000'000));
    QCOMPARE(
        sharing.phase(QStringLiteral("session-1")),
        QStringLiteral("accepted"));
    QTest::qWait(50);
    QCOMPARE(
        sharing.phase(QStringLiteral("session-1")),
        QStringLiteral("unknown"));
}

void SessionShareScopeTest::recoversOnlyFromAuthoritativeCatalog()
{
    FakeShareDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    kodosi::MissionDirectoryModel missions(dispatcher);
    kodosi::SessionShareScope sharing(dispatcher, sessions, missions);
    authenticate(sessions, missions, sharing);
    addMission(missions);
    upsert(sessions, sessionObject(QStringLiteral("inc-1")));
    QSignalSpy succeeded(
        &sharing,
        &kodosi::SessionShareScope::transitionSucceeded);
    QVERIFY(sharing.setScope(
        QStringLiteral("session-1"),
        QStringLiteral("room"),
        QStringLiteral("mission-1")));

    upsert(sessions, sessionObject(
        QStringLiteral("inc-1"),
        QStringLiteral("room"),
        QStringLiteral("mission-1")));
    QCOMPARE(
        sharing.phase(QStringLiteral("session-1")),
        QStringLiteral("pending"));
    snapshot(sessions, sessionObject(
        QStringLiteral("inc-1"),
        QStringLiteral("room"),
        QStringLiteral("mission-1")));
    QCOMPARE(sharing.phase(QStringLiteral("session-1")), QStringLiteral("idle"));
    QCOMPARE(succeeded.size(), 1);

    QVERIFY(sharing.setScope(
        QStringLiteral("session-1"),
        QStringLiteral("justMe"),
        {}));
    snapshot(sessions, sessionObject(
        QStringLiteral("inc-2"),
        QStringLiteral("room"),
        QStringLiteral("mission-1")));
    QCOMPARE(sharing.phase(QStringLiteral("session-1")), QStringLiteral("error"));
    QVERIFY(sharing.message(QStringLiteral("session-1")).contains(
        QStringLiteral("restarted")));
}

void SessionShareScopeTest::fencesAccountsAndResetsRuntime()
{
    FakeShareDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    kodosi::MissionDirectoryModel missions(dispatcher);
    kodosi::SessionShareScope sharing(dispatcher, sessions, missions);
    authenticate(sessions, missions, sharing);
    upsert(sessions, sessionObject(QStringLiteral("inc-1")));
    QVERIFY(sharing.setScope(
        QStringLiteral("session-1"),
        QStringLiteral("myDevices"),
        {}));
    const auto request = sharing.requestId(QStringLiteral("session-1"));

    sharing.ingestSessionEvent(receipt(
        QStringLiteral("session.scopeChanged"),
        request,
        QStringLiteral("inc-1"),
        QStringLiteral("myDevices"),
        {},
        std::nullopt,
        QStringLiteral("session-1"),
        QStringLiteral("other"),
        1));
    QCOMPARE(
        sharing.phase(QStringLiteral("session-1")),
        QStringLiteral("pending"));

    sharing.ingestAuthEvent(authEvent({}, 2));
    QCOMPARE(sharing.phase(QStringLiteral("session-1")), QStringLiteral("idle"));
    QVERIFY(!sharing.canChange(QStringLiteral("session-1")));

    authenticate(sessions, missions, sharing, QStringLiteral("me"), 3);
    upsert(
        sessions,
        sessionObject(QStringLiteral("inc-3")),
        QStringLiteral("me"),
        3);
    QVERIFY(sharing.setScope(
        QStringLiteral("session-1"),
        QStringLiteral("myDevices"),
        {}));
    sharing.resetRuntimeAuthority();
    QCOMPARE(sharing.phase(QStringLiteral("session-1")), QStringLiteral("idle"));
    QVERIFY(!sharing.canChange(QStringLiteral("session-1")));
}

void SessionShareScopeTest::timeoutRetriesWithSameRequestIdentity()
{
    FakeShareDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    kodosi::MissionDirectoryModel missions(dispatcher);
    kodosi::SessionShareScope sharing(
        dispatcher,
        sessions,
        missions,
        {.receiptTimeoutMs = 20, .maximumAcceptedBudgetMs = 40});
    authenticate(sessions, missions, sharing);
    upsert(sessions, sessionObject(QStringLiteral("inc-1")));
    QVERIFY(sharing.setScope(
        QStringLiteral("session-1"),
        QStringLiteral("myDevices"),
        {}));
    const auto request = sharing.requestId(QStringLiteral("session-1"));
    QTest::qWait(50);
    QCOMPARE(
        sharing.phase(QStringLiteral("session-1")),
        QStringLiteral("unknown"));
    QVERIFY(!sharing.setScope(
        QStringLiteral("session-1"),
        QStringLiteral("friends"),
        {}));
    QCOMPARE(
        sharing.phase(QStringLiteral("session-1")),
        QStringLiteral("unknown"));
    QCOMPARE(sharing.requestId(QStringLiteral("session-1")), request);
    upsert(sessions, sessionObject(
        QStringLiteral("inc-1"),
        QStringLiteral("justMe"),
        {},
        QStringLiteral("local"),
        QStringLiteral("stopping")));
    QVERIFY(sharing.retry(QStringLiteral("session-1")));
    QCOMPARE(sharing.requestId(QStringLiteral("session-1")), request);
    const auto commands = dispatcher.scopeCommands();
    QCOMPARE(commands.size(), 2);
    QCOMPARE(
        commands.front().value(QStringLiteral("requestId")),
        commands.back().value(QStringLiteral("requestId")));
}

QTEST_GUILESS_MAIN(SessionShareScopeTest)

#include "tst_session_share_scope.moc"
