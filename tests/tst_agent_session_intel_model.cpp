#include "models/AgentSessionIntelModel.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest/QTest>

#include <utility>

class FakeAgentIntelDispatcher final : public kodosi::CommandDispatcher {
public:
    QVector<QJsonObject> commands;
    bool rejectNext = false;

    Result send(const kodosi::CommandLane lane, const QByteArrayView json) override
    {
        if (lane != kodosi::CommandLane::System) {
            return std::unexpected(kodosi::RuntimeFailure {
                .code = kodosi::RuntimeFailure::Code::InvalidArgument,
                .ffiResult = -1,
                .message = QStringLiteral("Wrong lane"),
            });
        }
        const auto document = QJsonDocument::fromJson(json.toByteArray());
        if (!document.isObject()) {
            return std::unexpected(kodosi::RuntimeFailure {
                .code = kodosi::RuntimeFailure::Code::InvalidArgument,
                .ffiResult = -1,
                .message = QStringLiteral("Invalid JSON"),
            });
        }
        commands.push_back(document.object());
        if (std::exchange(rejectNext, false)) {
            return std::unexpected(kodosi::RuntimeFailure {
                .code = kodosi::RuntimeFailure::Code::FfiRejected,
                .ffiResult = -1,
                .message = QStringLiteral("Rejected"),
            });
        }
        return {};
    }
};

class AgentSessionIntelModelTest final : public QObject {
    Q_OBJECT

private slots:
    void hydratesCompleteCurrentProjection();
    void stagesNewerPushUntilCorrelatedBaseline();
    void rejectsStaleIncarnationsAndReappliesWhenCatalogArrives();
    void equalRevisionConflictFailsClosedAndRequeries();
    void stagedEqualRevisionConflictFailsClosed();
    void rejectsDuplicateAndMalformedEntriesAtomically();
    void rejectsMalformedAccountIdentities();
    void acceptsIdentitylessReadyContext();
    void acceptsRustBoundedSupplementaryUnicode();
    void preservesExactRevisionAboveJsonSafeInteger();
    void retriesTimedOutBaseline();
    void presentationRolesHideAuthorityIdentity();
};

namespace {

constexpr auto accountEpoch = 7;
const auto sessionIncarnation =
    QStringLiteral("01900000-0000-7000-8000-000000000011");
const auto authorityIncarnation =
    QStringLiteral("01900000-0000-7000-8000-000000000010");

QByteArray auth(const QString& userId = QStringLiteral("account"))
{
    return QJsonDocument(QJsonObject {
        {QStringLiteral("type"), QStringLiteral("auth.ready")},
        {QStringLiteral("userId"), userId},
        {QStringLiteral("accountEpoch"), accountEpoch},
    }).toJson(QJsonDocument::Compact);
}

QByteArray sessions(
    const QString& incarnation = sessionIncarnation,
    const QString& userId = QStringLiteral("account"))
{
    return QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), userId},
        {QStringLiteral("accountEpoch"), accountEpoch},
        {QStringLiteral("type"), QStringLiteral("session.list")},
        {QStringLiteral("sessions"),
         QJsonArray {
             QJsonObject {
                 {QStringLiteral("kind"), QStringLiteral("local")},
                 {QStringLiteral("id"), QStringLiteral("session-1")},
                 {QStringLiteral("incarnationId"), incarnation},
                 {QStringLiteral("name"), QStringLiteral("Session")},
                 {QStringLiteral("project"), QStringLiteral("/repo")},
                 {QStringLiteral("mode"), QStringLiteral("normal")},
                 {QStringLiteral("status"), QStringLiteral("active")},
                 {QStringLiteral("recovery"), QStringLiteral("live")},
                 {QStringLiteral("scope"), QStringLiteral("justMe")},
                 {QStringLiteral("access"), QStringLiteral("approve")},
             },
         }},
    }).toJson(QJsonDocument::Compact);
}

QJsonObject snapshot(const QString& activity)
{
    return {
        {QStringLiteral("identity"),
         QJsonObject {
             {QStringLiteral("agentType"), QStringLiteral("claude")},
             {QStringLiteral("version"), QStringLiteral("2.1.88")},
             {QStringLiteral("model"), QStringLiteral("opus")},
             {QStringLiteral("title"), QStringLiteral("Review runtime")},
             {QStringLiteral("cwd"), QStringLiteral("/repo")},
             {QStringLiteral("vendorSessionId"), QJsonValue::Null},
             {QStringLiteral("processId"), 42},
         }},
        {QStringLiteral("lifecycle"), QStringLiteral("waiting")},
        {QStringLiteral("attention"),
         QJsonObject {
             {QStringLiteral("kind"), QStringLiteral("question")},
             {QStringLiteral("summary"), QStringLiteral("Choose a target")},
             {QStringLiteral("actionable"), true},
         }},
        {QStringLiteral("pendingInteraction"),
         QJsonObject {
             {QStringLiteral("kind"), QStringLiteral("focus")},
             {QStringLiteral("summary"), QStringLiteral("Respond in terminal")},
             {QStringLiteral("toolName"), QJsonValue::Null},
             {QStringLiteral("canApprove"), false},
             {QStringLiteral("canDeny"), false},
             {QStringLiteral("canAnswer"), false},
             {QStringLiteral("canFocus"), true},
         }},
        {QStringLiteral("currentActivity"),
         QJsonObject {
             {QStringLiteral("summary"), activity},
             {QStringLiteral("lastProgressAt"),
              QStringLiteral("2026-08-31T21:00:00Z")},
         }},
        {QStringLiteral("workers"),
         QJsonObject {
             {QStringLiteral("active"), 2},
             {QStringLiteral("blocked"), 1},
             {QStringLiteral("failed"), 0},
             {QStringLiteral("completed"), 3},
         }},
        {QStringLiteral("outcome"), QJsonValue::Null},
        {QStringLiteral("exceptionalState"),
         QJsonObject {
             {QStringLiteral("kind"), QStringLiteral("rateLimit")},
             {QStringLiteral("summary"), QStringLiteral("Waiting for quota")},
             {QStringLiteral("retryable"), true},
         }},
        {QStringLiteral("source"),
         QJsonObject {
             {QStringLiteral("kind"), QStringLiteral("extension")},
             {QStringLiteral("degraded"), true},
             {QStringLiteral("detail"), QStringLiteral("Extension delayed")},
         }},
    };
}

QJsonObject entry(
    const QString& activity,
    const QString& incarnation = sessionIncarnation)
{
    return {
        {QStringLiteral("sessionId"), QStringLiteral("session-1")},
        {QStringLiteral("sessionIncarnationId"), incarnation},
        {QStringLiteral("snapshot"), snapshot(activity)},
    };
}

QByteArray liveSet(
    const QString& requestId,
    const quint64 revision,
    const QJsonArray& entries,
    const QString& authority = authorityIncarnation,
    const QString& userId = QStringLiteral("account"))
{
    return QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), userId},
        {QStringLiteral("accountEpoch"), accountEpoch},
        {QStringLiteral("type"), QStringLiteral("agent.intel.liveSet")},
        {QStringLiteral("requestId"),
         requestId.isEmpty() ? QJsonValue(QJsonValue::Null)
                             : QJsonValue(requestId)},
        {QStringLiteral("authorityIncarnationId"), authority},
        {QStringLiteral("revision"), static_cast<qint64>(revision)},
        {QStringLiteral("entries"), entries},
    }).toJson(QJsonDocument::Compact);
}

QString pendingRequestId(const FakeAgentIntelDispatcher& dispatcher)
{
    return dispatcher.commands.constLast()
        .value(QStringLiteral("requestId"))
        .toString();
}

void activate(
    kodosi::SessionCatalogModel& sessionsModel,
    kodosi::AgentSessionIntelModel& model,
    const bool withSession = true)
{
    const auto authEvent = auth();
    sessionsModel.ingestAuthEvent(authEvent);
    if (withSession) {
        sessionsModel.ingestSessionEvent(sessions());
    }
    model.ingestAuthEvent(authEvent);
}

} // namespace

void AgentSessionIntelModelTest::hydratesCompleteCurrentProjection()
{
    FakeAgentIntelDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::AgentSessionIntelModel model(dispatcher, sessionsModel);
    activate(sessionsModel, model);
    QCOMPARE(
        dispatcher.commands.constLast().value(QStringLiteral("type")).toString(),
        QStringLiteral("agent.intel.queryLiveSet"));

    model.ingestAgentIntelEvent(liveSet(
        pendingRequestId(dispatcher),
        1,
        QJsonArray {entry(QStringLiteral("Running tests"))}));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(
        model.hydrationState(),
        kodosi::AgentSessionIntelModel::HydrationState::Current);
    const auto projection =
        model.presentationForSession(QStringLiteral("session-1"));
    QCOMPARE(
        projection.value(QStringLiteral("currentActivity")).toString(),
        QStringLiteral("Running tests"));
    QCOMPARE(
        projection.value(QStringLiteral("activeWorkers")).toUInt(),
        2U);
    QVERIFY(projection.value(QStringLiteral("canFocus")).toBool());
    QVERIFY(!projection.value(QStringLiteral("canApprove")).toBool());
    QVERIFY(!projection.value(QStringLiteral("canDeny")).toBool());
    QVERIFY(!projection.value(QStringLiteral("canAnswer")).toBool());
    QVERIFY(
        projection.value(QStringLiteral("hasExceptionalState")).toBool());
}

void AgentSessionIntelModelTest::stagesNewerPushUntilCorrelatedBaseline()
{
    FakeAgentIntelDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::AgentSessionIntelModel model(dispatcher, sessionsModel);
    activate(sessionsModel, model);
    const auto requestId = pendingRequestId(dispatcher);

    model.ingestAgentIntelEvent(liveSet(
        {},
        2,
        QJsonArray {entry(QStringLiteral("Newer"))}));
    QCOMPARE(model.rowCount(), 0);
    model.ingestAgentIntelEvent(liveSet(
        requestId,
        1,
        QJsonArray {entry(QStringLiteral("Baseline"))}));
    QCOMPARE(
        model.presentationForSession(QStringLiteral("session-1"))
            .value(QStringLiteral("currentActivity"))
            .toString(),
        QStringLiteral("Newer"));
}

void AgentSessionIntelModelTest::
    rejectsStaleIncarnationsAndReappliesWhenCatalogArrives()
{
    FakeAgentIntelDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::AgentSessionIntelModel model(dispatcher, sessionsModel);
    activate(sessionsModel, model, false);
    model.ingestAgentIntelEvent(liveSet(
        pendingRequestId(dispatcher),
        1,
        QJsonArray {entry(QStringLiteral("Waiting"))}));
    QCOMPARE(model.rowCount(), 0);

    sessionsModel.ingestSessionEvent(sessions());
    QCOMPARE(model.rowCount(), 1);
    sessionsModel.ingestSessionEvent(sessions(
        QStringLiteral("01900000-0000-7000-8000-000000000012")));
    QCOMPARE(model.rowCount(), 0);
}

void AgentSessionIntelModelTest::equalRevisionConflictFailsClosedAndRequeries()
{
    FakeAgentIntelDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::AgentSessionIntelModel model(dispatcher, sessionsModel);
    QSignalSpy errors(&model, &kodosi::AgentSessionIntelModel::decodeError);
    activate(sessionsModel, model);
    model.ingestAgentIntelEvent(liveSet(
        pendingRequestId(dispatcher),
        1,
        QJsonArray {entry(QStringLiteral("First"))}));
    const auto commandCount = dispatcher.commands.size();

    model.ingestAgentIntelEvent(liveSet(
        {},
        1,
        QJsonArray {entry(QStringLiteral("Conflict"))}));
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(errors.size(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(
        dispatcher.commands.size(),
        commandCount + 1,
        1'500);
}

void AgentSessionIntelModelTest::stagedEqualRevisionConflictFailsClosed()
{
    FakeAgentIntelDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::AgentSessionIntelModel model(dispatcher, sessionsModel);
    QSignalSpy errors(&model, &kodosi::AgentSessionIntelModel::decodeError);
    activate(sessionsModel, model);
    model.ingestAgentIntelEvent(liveSet(
        {},
        1,
        QJsonArray {entry(QStringLiteral("First staged"))}));
    model.ingestAgentIntelEvent(liveSet(
        {},
        1,
        QJsonArray {entry(QStringLiteral("Conflicting staged"))}));

    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(errors.size(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(dispatcher.commands.size(), 2, 1'500);
}

void AgentSessionIntelModelTest::rejectsDuplicateAndMalformedEntriesAtomically()
{
    FakeAgentIntelDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::AgentSessionIntelModel model(dispatcher, sessionsModel);
    QSignalSpy errors(&model, &kodosi::AgentSessionIntelModel::decodeError);
    activate(sessionsModel, model);

    const auto value = entry(QStringLiteral("Duplicate"));
    model.ingestAgentIntelEvent(liveSet(
        pendingRequestId(dispatcher),
        1,
        QJsonArray {value, value}));
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(errors.size(), 1);

    QVERIFY(model.refresh());
    auto malformed = entry(QStringLiteral("Malformed"));
    auto payload = malformed.value(QStringLiteral("snapshot")).toObject();
    payload.remove(QStringLiteral("outcome"));
    malformed.insert(QStringLiteral("snapshot"), payload);
    model.ingestAgentIntelEvent(liveSet(
        pendingRequestId(dispatcher),
        2,
        QJsonArray {malformed}));
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(errors.size(), 2);
}

void AgentSessionIntelModelTest::rejectsMalformedAccountIdentities()
{
    FakeAgentIntelDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::AgentSessionIntelModel model(dispatcher, sessionsModel);
    QSignalSpy errors(&model, &kodosi::AgentSessionIntelModel::decodeError);

    model.ingestAuthEvent(QByteArrayLiteral(
        R"({"type":"auth.ready","userId":7,"accountEpoch":1})"));
    QVERIFY(dispatcher.commands.isEmpty());
    QCOMPARE(errors.size(), 1);

    activate(sessionsModel, model);
    auto malformed = liveSet(
        pendingRequestId(dispatcher),
        1,
        QJsonArray {entry(QStringLiteral("Ignored"))});
    malformed.replace(
        QByteArrayLiteral("\"accountUserId\":\"account\""),
        QByteArrayLiteral("\"accountUserId\":{}"));
    model.ingestAgentIntelEvent(std::move(malformed));
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(errors.size(), 2);
}

void AgentSessionIntelModelTest::acceptsIdentitylessReadyContext()
{
    FakeAgentIntelDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::AgentSessionIntelModel model(dispatcher, sessionsModel);
    model.ingestAuthEvent(QByteArrayLiteral(
        R"({"type":"auth.ready","userId":null,"accountEpoch":1})"));
    QCOMPARE(dispatcher.commands.size(), 1);
}

void AgentSessionIntelModelTest::acceptsRustBoundedSupplementaryUnicode()
{
    FakeAgentIntelDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::AgentSessionIntelModel model(dispatcher, sessionsModel);
    activate(sessionsModel, model);
    QString activity;
    constexpr char32_t emoji = 0x1F680;
    const auto scalar = QString::fromUcs4(&emoji, 1);
    for (auto index = 0; index < 240; ++index) {
        activity += scalar;
    }
    model.ingestAgentIntelEvent(liveSet(
        pendingRequestId(dispatcher),
        1,
        QJsonArray {entry(activity)}));
    QCOMPARE(
        model.presentationForSession(QStringLiteral("session-1"))
            .value(QStringLiteral("currentActivity"))
            .toString(),
        activity);
}

void AgentSessionIntelModelTest::preservesExactRevisionAboveJsonSafeInteger()
{
    FakeAgentIntelDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::AgentSessionIntelModel model(dispatcher, sessionsModel);
    activate(sessionsModel, model);
    constexpr quint64 currentRevision = 9'007'199'254'740'993ULL;
    model.ingestAgentIntelEvent(liveSet(
        pendingRequestId(dispatcher),
        currentRevision,
        QJsonArray {entry(QStringLiteral("Current"))}));
    model.ingestAgentIntelEvent(liveSet(
        {},
        currentRevision - 1,
        QJsonArray {entry(QStringLiteral("Stale"))}));
    QCOMPARE(
        model.presentationForSession(QStringLiteral("session-1"))
            .value(QStringLiteral("currentActivity"))
            .toString(),
        QStringLiteral("Current"));
}

void AgentSessionIntelModelTest::retriesTimedOutBaseline()
{
    FakeAgentIntelDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::AgentSessionIntelModel model(
        dispatcher,
        sessionsModel,
        0);
    activate(sessionsModel, model);
    QCOMPARE(dispatcher.commands.size(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(dispatcher.commands.size(), 2, 1'500);
}

void AgentSessionIntelModelTest::presentationRolesHideAuthorityIdentity()
{
    FakeAgentIntelDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::AgentSessionIntelModel model(dispatcher, sessionsModel);
    const auto roles = model.roleNames().values();
    QVERIFY(!roles.contains(QByteArrayLiteral("sessionIncarnationId")));
    QVERIFY(!roles.contains(QByteArrayLiteral("authorityIncarnationId")));
    QVERIFY(!roles.contains(QByteArrayLiteral("revision")));
    QVERIFY(!roles.contains(QByteArrayLiteral("requestId")));
    QVERIFY(!roles.contains(QByteArrayLiteral("vendorSessionId")));
    QVERIFY(!roles.contains(QByteArrayLiteral("processId")));
}

QTEST_GUILESS_MAIN(AgentSessionIntelModelTest)

#include "tst_agent_session_intel_model.moc"
