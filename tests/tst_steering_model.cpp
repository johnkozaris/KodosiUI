#include "models/SteeringModel.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest/QTest>

#include <utility>

class FakeSteeringDispatcher final : public kodosi::CommandDispatcher {
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

class SteeringModelTest final : public QObject {
    Q_OBJECT

private slots:
    void opaqueSteerIdAndChangedCompletedReceiptReconcile();
    void explicitSendingTransitionBlocksUntilTerminalTransition();
    void injectedTransitionOverridesRawDeliveryUnknown();
    void reconciliationFailureStaysBlockingUntilExactEmpty();
    void concurrentRuntimeRequestsRemainRetainedAndBlock();
    void lateOlderReplyCannotReplaceActiveNewerRequest();
    void cancellationReplyMustMatchExactSelectedTarget();
    void hydrationPrefersActiveAndNeverResurrectsDraft();
    void hydrationKeepsBlockingUnknownSelectedOverTerminalHistory();
    void editedTextAndModeSurviveAnotherRequestsInjection();
    void oversizedWhitespaceCannotClaimCapturedDraft();
    void staleAccountAndIncarnationEventsAreIgnored();
    void duplicateAndMalformedQueriesAreAtomic();
    void timeoutRecoveryAcceptsLateExactTransition();
    void runtimePendingSnapshotHonorsPerSessionBound();
    void terminalPresentationHistoryIsBounded();
    void sendRequiresSuccessfulFullHydration();
    void failedFullHydrationRemainsRetryable();
    void incarnationReplacementRequiresFreshHydration();
    void fullHydrationDiscardsOldIncarnationReceiptsAfterRestart();
    void fullQueryAcceptsRustMaximumAtomically();
    void repeatedExactRetryTimeoutsDoNotLeakOperationCapacity();
    void draftSurvivesUnavailableAuthorityAndReinspection();
    void signedOutLocalSteeringUsesRuntimeLocalScope();
    void signedOutStopAndSendOnlyLocalUsesAvailableModeFallback();
};

namespace {

constexpr auto accountEpoch = 7;
const auto incarnation =
    QStringLiteral("01900000-0000-7000-8000-000000000011");

QByteArray auth(const QString& account = QStringLiteral("account"))
{
    return QJsonDocument(QJsonObject {
        {QStringLiteral("type"), QStringLiteral("auth.ready")},
        {QStringLiteral("userId"), account},
        {QStringLiteral("accountEpoch"), accountEpoch},
    }).toJson(QJsonDocument::Compact);
}

QByteArray signedOutAuth()
{
    return QJsonDocument(QJsonObject {
        {QStringLiteral("type"), QStringLiteral("auth.required")},
        {QStringLiteral("reason"), QStringLiteral("signedOut")},
        {QStringLiteral("accountEpoch"), accountEpoch},
    }).toJson(QJsonDocument::Compact);
}

QByteArray signedOutSessions(
    const bool canSteer = true,
    const bool canStopAndSend = false)
{
    return QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), QJsonValue::Null},
        {QStringLiteral("accountEpoch"), accountEpoch},
        {QStringLiteral("type"), QStringLiteral("session.list")},
        {QStringLiteral("sessions"),
         QJsonArray {
             QJsonObject {
                 {QStringLiteral("kind"), QStringLiteral("local")},
                 {QStringLiteral("id"), QStringLiteral("session-1")},
                 {QStringLiteral("incarnationId"), incarnation},
                 {QStringLiteral("name"), QStringLiteral("Local Session")},
                 {QStringLiteral("project"), QStringLiteral("/repo")},
                 {QStringLiteral("mode"), QStringLiteral("normal")},
                 {QStringLiteral("status"), QStringLiteral("active")},
                 {QStringLiteral("recovery"), QStringLiteral("live")},
                 {QStringLiteral("scope"), QStringLiteral("justMe")},
                 {QStringLiteral("access"), QStringLiteral("approve")},
                 {QStringLiteral("semanticActions"),
                  QJsonObject {
                      {QStringLiteral("queue"), false},
                      {QStringLiteral("steer"), canSteer},
                      {QStringLiteral("stopAndSend"), canStopAndSend},
                  }},
             },
             QJsonObject {
                 {QStringLiteral("kind"), QStringLiteral("remote")},
                 {QStringLiteral("id"), QStringLiteral("remote-1")},
                 {QStringLiteral("incarnationId"),
                  QStringLiteral("01900000-0000-7000-8000-000000000012")},
                 {QStringLiteral("name"), QStringLiteral("Remote Session")},
                 {QStringLiteral("project"), QStringLiteral("/repo")},
                 {QStringLiteral("mode"), QStringLiteral("normal")},
                 {QStringLiteral("status"), QStringLiteral("active")},
                 {QStringLiteral("scope"), QStringLiteral("room")},
                 {QStringLiteral("access"), QStringLiteral("approve")},
                 {QStringLiteral("ownerUserId"), QStringLiteral("owner")},
                 {QStringLiteral("permissions"), 0x1ff},
                 {QStringLiteral("connectionState"), QStringLiteral("connected")},
                 {QStringLiteral("accessState"), QStringLiteral("ready")},
                 {QStringLiteral("semanticActions"),
                  QJsonObject {
                      {QStringLiteral("queue"), true},
                      {QStringLiteral("steer"), true},
                      {QStringLiteral("stopAndSend"), true},
                  }},
             },
         }},
    }).toJson(QJsonDocument::Compact);
}

QByteArray sessions(
    const QString& currentIncarnation = incarnation,
    const QString& account = QStringLiteral("account"),
    const bool canQueue = true,
    const bool canSteer = true,
    const bool canStopAndSend = true)
{
    return QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), account},
        {QStringLiteral("accountEpoch"), accountEpoch},
        {QStringLiteral("type"), QStringLiteral("session.list")},
        {QStringLiteral("sessions"),
         QJsonArray {
             QJsonObject {
                 {QStringLiteral("kind"), QStringLiteral("local")},
                 {QStringLiteral("id"), QStringLiteral("session-1")},
                 {QStringLiteral("incarnationId"), currentIncarnation},
                 {QStringLiteral("name"), QStringLiteral("Session")},
                 {QStringLiteral("project"), QStringLiteral("/repo")},
                 {QStringLiteral("mode"), QStringLiteral("normal")},
                 {QStringLiteral("status"), QStringLiteral("active")},
                 {QStringLiteral("recovery"), QStringLiteral("live")},
                 {QStringLiteral("scope"), QStringLiteral("justMe")},
                 {QStringLiteral("access"), QStringLiteral("approve")},
                 {QStringLiteral("semanticActions"),
                  QJsonObject {
                      {QStringLiteral("queue"), canQueue},
                      {QStringLiteral("steer"), canSteer},
                      {QStringLiteral("stopAndSend"), canStopAndSend},
                  }},
             },
         }},
    }).toJson(QJsonDocument::Compact);
}

QJsonObject entry(
    const QString& requestId,
    const QString& steerId,
    const QString& state,
    const QString& text = QStringLiteral("ship it"),
    const QString& mode = QStringLiteral("queue"),
    const quint64 queuedAt = 1,
    const QString& sessionIncarnation = incarnation,
    const QString& account = QStringLiteral("account"))
{
    return {
        {QStringLiteral("steerId"), steerId},
        {QStringLiteral("accountUserId"), account},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("sessionIncarnationId"), sessionIncarnation},
        {QStringLiteral("mode"), mode},
        {QStringLiteral("sessionId"), QStringLiteral("session-1")},
        {QStringLiteral("text"), text},
        {QStringLiteral("queuedAtMs"), static_cast<qint64>(queuedAt)},
        {QStringLiteral("deliveryState"), state},
        {QStringLiteral("atToolUseId"), QJsonValue::Null},
    };
}

QByteArray eventEnvelope(QJsonObject object)
{
    object.insert(QStringLiteral("authority"), QStringLiteral("accountContext"));
    object.insert(QStringLiteral("accountUserId"), QStringLiteral("account"));
    object.insert(QStringLiteral("accountEpoch"), accountEpoch);
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QByteArray reply(const QString& requestId, const QJsonValue& payload)
{
    return eventEnvelope({
        {QStringLiteral("type"), QStringLiteral("agent.intel.reply")},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("payload"), payload},
    });
}

QByteArray signedOutReply(const QString& requestId, const QJsonValue& payload)
{
    auto object = QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), QJsonValue::Null},
        {QStringLiteral("accountEpoch"), accountEpoch},
        {QStringLiteral("type"), QStringLiteral("agent.intel.reply")},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("payload"), payload},
    };
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QByteArray runtimeState(
    const QJsonObject& value,
    const QString& transition,
    const QString& message = {})
{
    QJsonObject object {
        {QStringLiteral("type"), QStringLiteral("agent.intel.steerState")},
        {QStringLiteral("entry"), value},
        {QStringLiteral("transition"), transition},
    };
    if (!message.isEmpty()) {
        object.insert(QStringLiteral("message"), message);
    }
    return eventEnvelope(std::move(object));
}

QByteArray error(const QString& requestId, const QString& message)
{
    return eventEnvelope({
        {QStringLiteral("type"), QStringLiteral("agent.intel.error")},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("message"), message},
    });
}

QString commandRequestId(
    const FakeSteeringDispatcher& dispatcher,
    const qsizetype index = -1)
{
    const auto resolved =
        index < 0 ? dispatcher.commands.size() + index : index;
    return dispatcher.commands.at(resolved)
        .value(QStringLiteral("requestId"))
        .toString();
}

void activate(
    kodosi::SessionCatalogModel& sessionsModel,
    kodosi::SteeringModel& model)
{
    const auto authEvent = auth();
    sessionsModel.ingestAuthEvent(authEvent);
    sessionsModel.ingestSessionEvent(sessions());
    model.ingestAuthEvent(authEvent);
    QVERIFY(model.inspect(QStringLiteral("session-1")));
}

void finishEmptyHydration(
    FakeSteeringDispatcher& dispatcher,
    kodosi::SteeringModel& model)
{
    model.ingestAgentIntelEvent(
        reply(commandRequestId(dispatcher), QJsonArray {}));
}

} // namespace

void SteeringModelTest::opaqueSteerIdAndChangedCompletedReceiptReconcile()
{
    FakeSteeringDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::SteeringModel model(dispatcher, sessionsModel);
    activate(sessionsModel, model);
    finishEmptyHydration(dispatcher, model);
    QVERIFY(model.saveDraft(
        QStringLiteral("session-1"),
        QStringLiteral("ship it"),
        QStringLiteral("steer")));
    QVERIFY(model.send(QStringLiteral("session-1")));
    const auto semanticId = commandRequestId(dispatcher);
    const auto semanticCommand = dispatcher.commands.constLast();
    QCOMPARE(
        semanticCommand.value(QStringLiteral("type")).toString(),
        QStringLiteral("agent.intel.semanticSend"));
    QCOMPARE(
        semanticCommand.value(QStringLiteral("sessionId")).toString(),
        QStringLiteral("session-1"));
    QCOMPARE(
        semanticCommand.value(QStringLiteral("incarnationId")).toString(),
        incarnation);
    QCOMPARE(
        semanticCommand.value(QStringLiteral("mode")).toString(),
        QStringLiteral("steer"));
    QCOMPARE(
        semanticCommand.value(QStringLiteral("text")).toString(),
        QStringLiteral("ship it"));
    QVERIFY(!semanticCommand.contains(QStringLiteral("accountUserId")));
    model.ingestAgentIntelEvent(reply(
        semanticId,
        entry(
            semanticId,
            QStringLiteral("opaque:runtime/steer"),
            QStringLiteral("queued"),
            QStringLiteral("ship it"),
            QStringLiteral("steer"))));
    QVERIFY(model.canCancel());
    QVERIFY(model.busy());

    model.ingestAgentIntelEvent(runtimeState(
        entry(
            semanticId,
            semanticId,
            QStringLiteral("injected"),
            QStringLiteral("ship it"),
            QStringLiteral("steer")),
        QStringLiteral("injected")));
    QVERIFY(!model.busy());
    QCOMPARE(model.draftText(), QString {});
}

void SteeringModelTest::explicitSendingTransitionBlocksUntilTerminalTransition()
{
    FakeSteeringDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::SteeringModel model(dispatcher, sessionsModel);
    activate(sessionsModel, model);
    const auto requestId =
        QStringLiteral("01900000-0000-7000-8000-000000000021");
    model.ingestAgentIntelEvent(runtimeState(
        entry(
            requestId,
            QStringLiteral("opaque-a"),
            QStringLiteral("deliveryUnknown")),
        QStringLiteral("sending")));
    QVERIFY(model.busy());
    QVERIFY(model.statusText().contains(QStringLiteral("Waiting")));

    model.ingestAgentIntelEvent(runtimeState(
        entry(
            requestId,
            QStringLiteral("opaque-a"),
            QStringLiteral("injected")),
        QStringLiteral("injected")));
    QVERIFY(!model.busy());

    const auto cancelled =
        QStringLiteral("01900000-0000-7000-8000-000000000022");
    model.ingestAgentIntelEvent(runtimeState(
        entry(
            cancelled,
            QStringLiteral("opaque-b"),
            QStringLiteral("deliveryUnknown")),
        QStringLiteral("sending")));
    QVERIFY(model.busy());
    model.ingestAgentIntelEvent(runtimeState(
        entry(
            cancelled,
            QStringLiteral("opaque-b"),
            QStringLiteral("cancelled")),
        QStringLiteral("cancelled")));
    QVERIFY(!model.busy());
}

void SteeringModelTest::injectedTransitionOverridesRawDeliveryUnknown()
{
    FakeSteeringDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::SteeringModel model(dispatcher, sessionsModel);
    activate(sessionsModel, model);
    finishEmptyHydration(dispatcher, model);
    QVERIFY(model.saveDraft(
        QStringLiteral("session-1"),
        QStringLiteral("ship it"),
        QStringLiteral("steer")));
    QVERIFY(model.send(QStringLiteral("session-1")));
    const auto requestId = commandRequestId(dispatcher);
    model.ingestAgentIntelEvent(reply(
        requestId,
        entry(
            requestId,
            QStringLiteral("opaque-runtime-steer"),
            QStringLiteral("queued"),
            QStringLiteral("ship it"),
            QStringLiteral("steer"))));

    model.ingestAgentIntelEvent(runtimeState(
        entry(
            requestId,
            QStringLiteral("opaque-runtime-steer"),
            QStringLiteral("deliveryUnknown"),
            QStringLiteral("ship it"),
            QStringLiteral("steer")),
        QStringLiteral("sending")));
    QVERIFY(model.busy());

    model.ingestAgentIntelEvent(runtimeState(
        entry(
            requestId,
            QStringLiteral("opaque-runtime-steer"),
            QStringLiteral("deliveryUnknown"),
            QStringLiteral("ship it"),
            QStringLiteral("steer")),
        QStringLiteral("injected")));
    QVERIFY(!model.busy());
    QCOMPARE(model.blockingRequestCount(QStringLiteral("session-1")), 0);
    QCOMPARE(model.draftText(), QString {});
    const auto terminalStatus = model.statusText();
    QVERIFY(terminalStatus.contains(QStringLiteral("Injected")));

    model.ingestAgentIntelEvent(runtimeState(
        entry(
            requestId,
            QStringLiteral("opaque-runtime-steer"),
            QStringLiteral("deliveryUnknown"),
            QStringLiteral("ship it"),
            QStringLiteral("steer")),
        QStringLiteral("sending")));
    QVERIFY(!model.busy());
    QCOMPARE(model.blockingRequestCount(QStringLiteral("session-1")), 0);
    QCOMPARE(model.draftText(), QString {});
    QCOMPARE(model.statusText(), terminalStatus);
}

void SteeringModelTest::reconciliationFailureStaysBlockingUntilExactEmpty()
{
    FakeSteeringDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::SteeringModel model(dispatcher, sessionsModel);
    activate(sessionsModel, model);
    finishEmptyHydration(dispatcher, model);
    QVERIFY(model.saveDraft(
        QStringLiteral("session-1"),
        QStringLiteral("ship it"),
        QStringLiteral("queue")));
    QVERIFY(model.send(QStringLiteral("session-1")));
    const auto semanticId = commandRequestId(dispatcher);

    model.ingestAgentIntelEvent(error(
        semanticId,
        QStringLiteral("client delivery uncertain")));
    QCOMPARE(
        dispatcher.commands.constLast()
            .value(QStringLiteral("semanticRequestId"))
            .toString(),
        semanticId);
    const auto firstQuery = commandRequestId(dispatcher);
    model.ingestAgentIntelEvent(error(
        firstQuery,
        QStringLiteral("query unavailable")));
    QVERIFY(model.busy());
    QVERIFY(model.canRetry());
    QVERIFY(!model.canSend());

    QVERIFY(model.retry(QStringLiteral("session-1")));
    model.ingestAgentIntelEvent(
        reply(commandRequestId(dispatcher), QJsonArray {}));
    QVERIFY(!model.busy());
    QVERIFY(!model.canRetry());
    QCOMPARE(model.draftText(), QStringLiteral("ship it"));
}

void SteeringModelTest::concurrentRuntimeRequestsRemainRetainedAndBlock()
{
    FakeSteeringDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::SteeringModel model(dispatcher, sessionsModel);
    activate(sessionsModel, model);
    const auto requestA =
        QStringLiteral("01900000-0000-7000-8000-000000000031");
    const auto requestB =
        QStringLiteral("01900000-0000-7000-8000-000000000032");
    model.ingestAgentIntelEvent(reply(
        commandRequestId(dispatcher),
        QJsonArray {
            entry(
                requestA,
                QStringLiteral("opaque-a"),
                QStringLiteral("queued"),
                QStringLiteral("A"),
                QStringLiteral("queue"),
                1),
            entry(
                requestB,
                QStringLiteral("opaque-b"),
                QStringLiteral("queued"),
                QStringLiteral("B"),
                QStringLiteral("steer"),
                2),
        }));
    QCOMPARE(model.retainedRequestCount(QStringLiteral("session-1")), 2);
    QCOMPARE(model.blockingRequestCount(QStringLiteral("session-1")), 2);
    QVERIFY(!model.canSend());

    model.ingestAgentIntelEvent(runtimeState(
        entry(
            requestA,
            requestA,
            QStringLiteral("injected"),
            QStringLiteral("A")),
        QStringLiteral("injected")));
    QCOMPARE(model.retainedRequestCount(QStringLiteral("session-1")), 2);
    QCOMPARE(model.blockingRequestCount(QStringLiteral("session-1")), 1);
    QVERIFY(model.statusText().contains(QStringLiteral("Queued")));
    QVERIFY(model.cancel(QStringLiteral("session-1")));
    QCOMPARE(
        dispatcher.commands.constLast()
            .value(QStringLiteral("steerId"))
            .toString(),
        QStringLiteral("opaque-b"));
}

void SteeringModelTest::lateOlderReplyCannotReplaceActiveNewerRequest()
{
    FakeSteeringDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::SteeringModel model(dispatcher, sessionsModel);
    activate(sessionsModel, model);
    finishEmptyHydration(dispatcher, model);
    QVERIFY(model.saveDraft(
        QStringLiteral("session-1"),
        QStringLiteral("A"),
        QStringLiteral("queue")));
    QVERIFY(model.send(QStringLiteral("session-1")));
    const auto requestA = commandRequestId(dispatcher);
    model.ingestAgentIntelEvent(error(requestA, QStringLiteral("uncertain")));
    model.ingestAgentIntelEvent(
        reply(commandRequestId(dispatcher), QJsonArray {}));
    QVERIFY(!model.busy());

    QVERIFY(model.saveDraft(
        QStringLiteral("session-1"),
        QStringLiteral("B"),
        QStringLiteral("steer")));
    QVERIFY(model.send(QStringLiteral("session-1")));
    const auto requestB = commandRequestId(dispatcher);
    model.ingestAgentIntelEvent(runtimeState(
        entry(
            requestA,
            QStringLiteral("late-opaque-a"),
            QStringLiteral("queued"),
            QStringLiteral("A")),
        QStringLiteral("queued")));

    QCOMPARE(model.blockingRequestCount(QStringLiteral("session-1")), 1);
    QVERIFY(model.statusText().contains(QStringLiteral("Waiting")));
    model.ingestAgentIntelEvent(reply(
        requestB,
        entry(
            requestB,
            QStringLiteral("opaque-b"),
            QStringLiteral("queued"),
            QStringLiteral("B"),
            QStringLiteral("steer"))));
    QVERIFY(model.statusText().contains(QStringLiteral("Queued")));
}

void SteeringModelTest::cancellationReplyMustMatchExactSelectedTarget()
{
    FakeSteeringDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::SteeringModel model(dispatcher, sessionsModel);
    activate(sessionsModel, model);
    const auto requestA =
        QStringLiteral("01900000-0000-7000-8000-000000000041");
    const auto requestB =
        QStringLiteral("01900000-0000-7000-8000-000000000042");
    model.ingestAgentIntelEvent(reply(
        commandRequestId(dispatcher),
        QJsonArray {
            entry(requestA, QStringLiteral("opaque-a"), QStringLiteral("queued")),
            entry(requestB, QStringLiteral("opaque-b"), QStringLiteral("queued")),
        }));
    QVERIFY(model.cancel(QStringLiteral("session-1")));
    const auto cancelRequest = commandRequestId(dispatcher);
    model.ingestAgentIntelEvent(reply(
        cancelRequest,
        entry(
            requestA,
            requestA,
            QStringLiteral("cancelled"))));
    QCOMPARE(model.blockingRequestCount(QStringLiteral("session-1")), 2);
    QCOMPARE(
        dispatcher.commands.constLast()
            .value(QStringLiteral("semanticRequestId"))
            .toString(),
        requestB);
    QVERIFY(!model.canRetry());
    model.ingestAgentIntelEvent(reply(
        commandRequestId(dispatcher),
        QJsonArray {entry(
            requestB,
            requestB,
            QStringLiteral("cancelled"))}));
    QCOMPARE(model.blockingRequestCount(QStringLiteral("session-1")), 1);
}

void SteeringModelTest::hydrationPrefersActiveAndNeverResurrectsDraft()
{
    FakeSteeringDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::SteeringModel model(dispatcher, sessionsModel);
    activate(sessionsModel, model);
    const auto injected =
        QStringLiteral("01900000-0000-7000-8000-000000000051");
    const auto unknown =
        QStringLiteral("01900000-0000-7000-8000-000000000052");
    const auto active =
        QStringLiteral("01900000-0000-7000-8000-000000000053");
    model.ingestAgentIntelEvent(reply(
        commandRequestId(dispatcher),
        QJsonArray {
            entry(
                injected,
                injected,
                QStringLiteral("injected"),
                QStringLiteral("old injected")),
            entry(
                unknown,
                unknown,
                QStringLiteral("deliveryUnknown"),
                QStringLiteral("old unknown")),
            entry(
                active,
                QStringLiteral("opaque-active"),
                QStringLiteral("preparing"),
                QStringLiteral("active")),
        }));
    QCOMPARE(model.draftText(), QString {});
    QVERIFY(model.busy());
    QVERIFY(model.statusText().contains(QStringLiteral("preparing")));
}

void SteeringModelTest::hydrationKeepsBlockingUnknownSelectedOverTerminalHistory()
{
    FakeSteeringDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::SteeringModel model(dispatcher, sessionsModel);
    activate(sessionsModel, model);
    const auto unknown =
        QStringLiteral("01900000-0000-7000-8000-000000000054");
    const auto injected =
        QStringLiteral("01900000-0000-7000-8000-000000000055");
    const auto cancelled =
        QStringLiteral("01900000-0000-7000-8000-000000000056");
    model.ingestAgentIntelEvent(reply(
        commandRequestId(dispatcher),
        QJsonArray {
            entry(
                unknown,
                QStringLiteral("opaque-unknown"),
                QStringLiteral("deliveryUnknown"),
                QStringLiteral("old unknown"),
                QStringLiteral("queue"),
                1),
            entry(
                injected,
                injected,
                QStringLiteral("injected"),
                QStringLiteral("new injected"),
                QStringLiteral("queue"),
                2),
            entry(
                cancelled,
                cancelled,
                QStringLiteral("cancelled"),
                QStringLiteral("new cancelled"),
                QStringLiteral("queue"),
                3),
        }));

    QCOMPARE(model.blockingRequestCount(QStringLiteral("session-1")), 1);
    QVERIFY(model.busy());
    QVERIFY(!model.canSend());
    QVERIFY(model.statusText().contains(QStringLiteral("cannot prove")));
    QVERIFY(model.canRetry());
    QVERIFY(model.retry(QStringLiteral("session-1")));
    QCOMPARE(
        dispatcher.commands.constLast()
            .value(QStringLiteral("semanticRequestId"))
            .toString(),
        unknown);
}

void SteeringModelTest::editedTextAndModeSurviveAnotherRequestsInjection()
{
    FakeSteeringDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::SteeringModel model(dispatcher, sessionsModel);
    activate(sessionsModel, model);
    finishEmptyHydration(dispatcher, model);
    QVERIFY(model.saveDraft(
        QStringLiteral("session-1"),
        QStringLiteral("first"),
        QStringLiteral("queue")));
    QVERIFY(model.send(QStringLiteral("session-1")));
    const auto requestId = commandRequestId(dispatcher);
    model.ingestAgentIntelEvent(reply(
        requestId,
        entry(
            requestId,
            QStringLiteral("opaque-first"),
            QStringLiteral("queued"),
            QStringLiteral("first"))));

    QVERIFY(model.saveDraft(
        QStringLiteral("session-1"),
        QStringLiteral("second"),
        QStringLiteral("stopAndSend")));
    model.ingestAgentIntelEvent(runtimeState(
        entry(
            requestId,
            requestId,
            QStringLiteral("injected"),
            QStringLiteral("first")),
        QStringLiteral("injected")));
    QCOMPARE(model.draftText(), QStringLiteral("second"));
    QCOMPARE(model.draftMode(), QStringLiteral("stopAndSend"));
}

void SteeringModelTest::oversizedWhitespaceCannotClaimCapturedDraft()
{
    FakeSteeringDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::SteeringModel model(dispatcher, sessionsModel);
    activate(sessionsModel, model);
    finishEmptyHydration(dispatcher, model);
    QVERIFY(model.saveDraft(
        QStringLiteral("session-1"),
        QStringLiteral("keep"),
        QStringLiteral("queue")));
    QVERIFY(model.send(QStringLiteral("session-1")));
    const auto requestId = commandRequestId(dispatcher);
    model.ingestAgentIntelEvent(reply(
        requestId,
        entry(
            requestId,
            QStringLiteral("opaque-keep"),
            QStringLiteral("queued"),
            QStringLiteral("keep"))));

    const QString whitespace(20'000, QLatin1Char(' '));
    QVERIFY(model.saveDraft(
        QStringLiteral("session-1"),
        whitespace,
        QStringLiteral("queue")));
    QVERIFY(!model.send(QStringLiteral("session-1")));
    model.ingestAgentIntelEvent(runtimeState(
        entry(
            requestId,
            requestId,
            QStringLiteral("injected"),
            QStringLiteral("keep")),
        QStringLiteral("injected")));
    QCOMPARE(model.draftText(), whitespace);
}

void SteeringModelTest::staleAccountAndIncarnationEventsAreIgnored()
{
    FakeSteeringDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::SteeringModel model(dispatcher, sessionsModel);
    activate(sessionsModel, model);
    finishEmptyHydration(dispatcher, model);
    const auto requestId =
        QStringLiteral("01900000-0000-7000-8000-000000000061");
    auto staleAccount = runtimeState(
        entry(
            requestId,
            QStringLiteral("opaque"),
            QStringLiteral("queued")),
        QStringLiteral("queued"));
    staleAccount.replace(
        QByteArrayLiteral("\"accountUserId\":\"account\""),
        QByteArrayLiteral("\"accountUserId\":\"other\""));
    model.ingestAgentIntelEvent(staleAccount);
    QCOMPARE(model.retainedRequestCount(QStringLiteral("session-1")), 0);

    model.ingestAgentIntelEvent(runtimeState(
        entry(
            requestId,
            QStringLiteral("opaque"),
            QStringLiteral("queued"),
            QStringLiteral("ship it"),
            QStringLiteral("queue"),
            1,
            QStringLiteral("01900000-0000-7000-8000-000000000099")),
        QStringLiteral("queued")));
    QCOMPARE(model.retainedRequestCount(QStringLiteral("session-1")), 0);
}

void SteeringModelTest::duplicateAndMalformedQueriesAreAtomic()
{
    FakeSteeringDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::SteeringModel model(dispatcher, sessionsModel);
    QSignalSpy errors(&model, &kodosi::SteeringModel::decodeError);
    activate(sessionsModel, model);
    const auto requestId =
        QStringLiteral("01900000-0000-7000-8000-000000000071");
    const auto value =
        entry(requestId, QStringLiteral("opaque"), QStringLiteral("queued"));
    model.ingestAgentIntelEvent(reply(
        commandRequestId(dispatcher),
        QJsonArray {value, value}));
    QCOMPARE(model.retainedRequestCount(QStringLiteral("session-1")), 0);
    QCOMPARE(errors.size(), 1);

    QVERIFY(model.rehydrate(QStringLiteral("session-1")));
    auto malformed = value;
    malformed.insert(QStringLiteral("steerId"), 7);
    model.ingestAgentIntelEvent(reply(
        commandRequestId(dispatcher),
        QJsonArray {malformed}));
    QCOMPARE(model.retainedRequestCount(QStringLiteral("session-1")), 0);
    QCOMPARE(errors.size(), 2);
}

void SteeringModelTest::timeoutRecoveryAcceptsLateExactTransition()
{
    FakeSteeringDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::SteeringModel model(
        dispatcher,
        sessionsModel,
        {.replyTimeoutMs = 15});
    activate(sessionsModel, model);
    finishEmptyHydration(dispatcher, model);
    QVERIFY(model.saveDraft(
        QStringLiteral("session-1"),
        QStringLiteral("timeout"),
        QStringLiteral("queue")));
    QVERIFY(model.send(QStringLiteral("session-1")));
    const auto semanticId = commandRequestId(dispatcher);
    QTRY_VERIFY_WITH_TIMEOUT(dispatcher.commands.size() >= 3, 100);
    QTRY_VERIFY_WITH_TIMEOUT(model.canRetry(), 100);
    QVERIFY(model.busy());

    model.ingestAgentIntelEvent(runtimeState(
        entry(
            semanticId,
            semanticId,
            QStringLiteral("injected"),
            QStringLiteral("timeout")),
        QStringLiteral("injected")));
    QVERIFY(!model.busy());
    QVERIFY(!model.canRetry());
    QCOMPARE(model.draftText(), QString {});
}

void SteeringModelTest::runtimePendingSnapshotHonorsPerSessionBound()
{
    FakeSteeringDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::SteeringModel model(dispatcher, sessionsModel);
    QSignalSpy errors(&model, &kodosi::SteeringModel::decodeError);
    activate(sessionsModel, model);
    QJsonArray pending;
    for (auto index = 0; index < 33; ++index) {
        pending.push_back(entry(
            QStringLiteral("01900000-0000-7000-8000-%1")
                .arg(index + 1, 12, 10, QLatin1Char('0')),
            QStringLiteral("opaque-%1").arg(index),
            QStringLiteral("queued"),
            QStringLiteral("pending"),
            QStringLiteral("queue"),
            static_cast<quint64>(index + 1)));
    }
    model.ingestAgentIntelEvent(reply(
        commandRequestId(dispatcher),
        pending));
    QCOMPARE(model.retainedRequestCount(QStringLiteral("session-1")), 0);
    QCOMPARE(errors.size(), 1);
}

void SteeringModelTest::terminalPresentationHistoryIsBounded()
{
    FakeSteeringDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::SteeringModel model(dispatcher, sessionsModel);
    activate(sessionsModel, model);
    QJsonArray terminal;
    for (auto index = 0; index < 40; ++index) {
        terminal.push_back(entry(
            QStringLiteral("01900000-0000-7000-8000-%1")
                .arg(index + 1, 12, 10, QLatin1Char('0')),
            QStringLiteral("receipt-%1").arg(index),
            QStringLiteral("injected"),
            QStringLiteral("history"),
            QStringLiteral("queue"),
            static_cast<quint64>(index + 1)));
    }
    model.ingestAgentIntelEvent(reply(
        commandRequestId(dispatcher),
        terminal));
    QCOMPARE(model.retainedRequestCount(QStringLiteral("session-1")), 32);
    QVERIFY(!model.busy());
}

void SteeringModelTest::sendRequiresSuccessfulFullHydration()
{
    FakeSteeringDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::SteeringModel model(dispatcher, sessionsModel);
    activate(sessionsModel, model);
    QVERIFY(model.saveDraft(
        QStringLiteral("session-1"),
        QStringLiteral("wait for authority"),
        QStringLiteral("queue")));

    QCOMPARE(dispatcher.commands.size(), 1);
    QVERIFY(!model.canSend());
    QVERIFY(!model.send(QStringLiteral("session-1")));
    QCOMPARE(dispatcher.commands.size(), 1);

    finishEmptyHydration(dispatcher, model);
    QVERIFY(model.canSend());
    QVERIFY(model.send(QStringLiteral("session-1")));
    QCOMPARE(
        dispatcher.commands.constLast().value(QStringLiteral("type")).toString(),
        QStringLiteral("agent.intel.semanticSend"));
}

void SteeringModelTest::failedFullHydrationRemainsRetryable()
{
    FakeSteeringDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::SteeringModel model(dispatcher, sessionsModel);
    activate(sessionsModel, model);
    QVERIFY(model.saveDraft(
        QStringLiteral("session-1"),
        QStringLiteral("retry hydration"),
        QStringLiteral("queue")));

    model.ingestAgentIntelEvent(error(
        commandRequestId(dispatcher),
        QStringLiteral("query unavailable")));
    QVERIFY(model.canRetry());
    QVERIFY(!model.canSend());
    model.clearError(QStringLiteral("session-1"));
    QVERIFY(model.error().isEmpty());
    QVERIFY(model.canRetry());
    QVERIFY(!model.send(QStringLiteral("session-1")));
    QCOMPARE(dispatcher.commands.size(), 1);

    QVERIFY(model.retry(QStringLiteral("session-1")));
    finishEmptyHydration(dispatcher, model);
    QVERIFY(!model.canRetry());
    QVERIFY(model.canSend());
}

void SteeringModelTest::incarnationReplacementRequiresFreshHydration()
{
    FakeSteeringDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::SteeringModel model(dispatcher, sessionsModel);
    activate(sessionsModel, model);
    finishEmptyHydration(dispatcher, model);
    QVERIFY(model.saveDraft(
        QStringLiteral("session-1"),
        QStringLiteral("new incarnation"),
        QStringLiteral("queue")));
    QVERIFY(model.canSend());

    const auto secondIncarnation =
        QStringLiteral("01900000-0000-7000-8000-000000000012");
    sessionsModel.ingestSessionEvent(sessions(secondIncarnation));
    const auto staleQuery = commandRequestId(dispatcher);
    QVERIFY(!model.canSend());

    const auto thirdIncarnation =
        QStringLiteral("01900000-0000-7000-8000-000000000013");
    sessionsModel.ingestSessionEvent(sessions(thirdIncarnation));
    const auto currentQuery = commandRequestId(dispatcher);
    QVERIFY(staleQuery != currentQuery);
    model.ingestAgentIntelEvent(reply(staleQuery, QJsonArray {}));
    QVERIFY(!model.canSend());
    QVERIFY(!model.send(QStringLiteral("session-1")));

    model.ingestAgentIntelEvent(reply(currentQuery, QJsonArray {}));
    QVERIFY(model.canSend());
}

void SteeringModelTest::fullHydrationDiscardsOldIncarnationReceiptsAfterRestart()
{
    FakeSteeringDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::SteeringModel model(dispatcher, sessionsModel);
    activate(sessionsModel, model);

    const auto replacementIncarnation =
        QStringLiteral("01900000-0000-7000-8000-000000000012");
    sessionsModel.ingestSessionEvent(sessions(replacementIncarnation));
    const auto currentQuery = commandRequestId(dispatcher);
    QVERIFY(model.saveDraft(
        QStringLiteral("session-1"),
        QStringLiteral("after restart"),
        QStringLiteral("queue")));

    model.ingestAgentIntelEvent(reply(
        currentQuery,
        QJsonArray {
            entry(
                QStringLiteral("01900000-0000-7000-8000-000000000081"),
                QStringLiteral("old-receipt"),
                QStringLiteral("injected"),
                QStringLiteral("old"),
                QStringLiteral("queue"),
                1,
                incarnation),
            entry(
                QStringLiteral("01900000-0000-7000-8000-000000000082"),
                QStringLiteral("current-receipt"),
                QStringLiteral("injected"),
                QStringLiteral("current"),
                QStringLiteral("queue"),
                2,
                replacementIncarnation),
        }));

    QCOMPARE(model.retainedRequestCount(QStringLiteral("session-1")), 1);
    QVERIFY(model.canSend());
    QVERIFY(!model.canRetry());
}

void SteeringModelTest::fullQueryAcceptsRustMaximumAtomically()
{
    FakeSteeringDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::SteeringModel model(dispatcher, sessionsModel);
    QSignalSpy errors(&model, &kodosi::SteeringModel::decodeError);
    activate(sessionsModel, model);

    const auto makeEntries = [](const int count) {
        QJsonArray entries;
        for (auto index = 0; index < count; ++index) {
            entries.push_back(entry(
                QStringLiteral("01900000-0000-7000-8000-%1")
                    .arg(index + 1, 12, 10, QLatin1Char('0')),
                QStringLiteral("bounded-%1").arg(index),
                index < 32 ? QStringLiteral("queued")
                           : QStringLiteral("injected"),
                QStringLiteral("bounded"),
                QStringLiteral("queue"),
                static_cast<quint64>(index + 1)));
        }
        return entries;
    };

    model.ingestAgentIntelEvent(reply(
        commandRequestId(dispatcher),
        makeEntries(545)));
    QCOMPARE(model.retainedRequestCount(QStringLiteral("session-1")), 0);
    QCOMPARE(errors.size(), 1);
    QVERIFY(model.canRetry());

    QVERIFY(model.retry(QStringLiteral("session-1")));
    model.ingestAgentIntelEvent(reply(
        commandRequestId(dispatcher),
        makeEntries(544)));
    QCOMPARE(model.retainedRequestCount(QStringLiteral("session-1")), 64);
    QCOMPARE(model.blockingRequestCount(QStringLiteral("session-1")), 32);
    QCOMPARE(errors.size(), 1);
}

void SteeringModelTest::repeatedExactRetryTimeoutsDoNotLeakOperationCapacity()
{
    FakeSteeringDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::SteeringModel model(
        dispatcher,
        sessionsModel,
        {.replyTimeoutMs = 2});
    activate(sessionsModel, model);
    finishEmptyHydration(dispatcher, model);
    QVERIFY(model.saveDraft(
        QStringLiteral("session-1"),
        QStringLiteral("retry forever"),
        QStringLiteral("queue")));
    QVERIFY(model.send(QStringLiteral("session-1")));

    QTRY_VERIFY_WITH_TIMEOUT(model.canRetry(), 100);
    for (auto attempt = 0; attempt < 140; ++attempt) {
        const auto commandsBeforeRetry = dispatcher.commands.size();
        QVERIFY2(model.retry(QStringLiteral("session-1")),
                 qPrintable(QStringLiteral("retry %1 was rejected").arg(attempt)));
        QCOMPARE(dispatcher.commands.size(), commandsBeforeRetry + 1);
        QVERIFY(model.retry(QStringLiteral("session-1")));
        QCOMPARE(dispatcher.commands.size(), commandsBeforeRetry + 1);
        QTRY_VERIFY_WITH_TIMEOUT(model.canRetry(), 100);
    }
    QVERIFY(model.busy());
}

void SteeringModelTest::draftSurvivesUnavailableAuthorityAndReinspection()
{
    FakeSteeringDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::SteeringModel model(dispatcher, sessionsModel);
    activate(sessionsModel, model);
    finishEmptyHydration(dispatcher, model);

    sessionsModel.ingestSessionEvent(sessions(
        incarnation,
        QStringLiteral("account"),
        false,
        false,
        false));
    QVERIFY(model.saveDraft(
        QStringLiteral("session-1"),
        QStringLiteral("keep this draft"),
        QStringLiteral("stopAndSend")));
    QCOMPARE(model.draftText(), QStringLiteral("keep this draft"));
    QCOMPARE(model.draftMode(), QStringLiteral("stopAndSend"));
    QVERIFY(!model.send(QStringLiteral("session-1")));

    const auto commandsBeforeReconnect = dispatcher.commands.size();
    sessionsModel.ingestSessionEvent(sessions());
    QCOMPARE(dispatcher.commands.size(), commandsBeforeReconnect + 1);
    finishEmptyHydration(dispatcher, model);

    model.clearInspection();
    const auto presentation = model.presentationForSession(QStringLiteral("session-1"));
    QCOMPARE(presentation.value(QStringLiteral("draftText")).toString(), QStringLiteral("keep this draft"));
    QVERIFY(presentation.value(QStringLiteral("canSend")).toBool());
    const auto commandsBeforeReopen = dispatcher.commands.size();
    QVERIFY(model.inspect(QStringLiteral("session-1")));
    QCOMPARE(dispatcher.commands.size(), commandsBeforeReopen);
    QCOMPARE(model.draftText(), QStringLiteral("keep this draft"));
    QCOMPARE(model.draftMode(), QStringLiteral("stopAndSend"));
}

void SteeringModelTest::signedOutLocalSteeringUsesRuntimeLocalScope()
{
    FakeSteeringDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::SteeringModel model(dispatcher, sessionsModel);
    const auto authEvent = signedOutAuth();
    sessionsModel.ingestAuthEvent(authEvent);
    sessionsModel.ingestSessionEvent(signedOutSessions());

    QVERIFY(!model.inspect(QStringLiteral("session-1")));
    QCOMPARE(dispatcher.commands.size(), 0);

    model.ingestAuthEvent(authEvent);
    QCOMPARE(dispatcher.commands.size(), 1);
    QCOMPARE(
        dispatcher.commands.constLast().value(QStringLiteral("type")).toString(),
        QStringLiteral("agent.intel.querySteer"));
    model.ingestAgentIntelEvent(
        signedOutReply(commandRequestId(dispatcher), QJsonArray {}));

    QCOMPARE(
        model.availableModes(),
        QStringList {QStringLiteral("steer")});
    QVERIFY(model.saveDraft(
        QStringLiteral("session-1"),
        QStringLiteral("local message"),
        QStringLiteral("steer")));
    QVERIFY(model.canSend());
    QVERIFY(model.send(QStringLiteral("session-1")));
    const auto semanticId = commandRequestId(dispatcher);
    model.ingestAgentIntelEvent(signedOutReply(
        semanticId,
        entry(
            semanticId,
            QStringLiteral("runtime-local-steer"),
            QStringLiteral("queued"),
            QStringLiteral("local message"),
            QStringLiteral("steer"),
            1,
            incarnation,
            QStringLiteral("local"))));

    QCOMPARE(model.retainedRequestCount(QStringLiteral("session-1")), 1);
    QVERIFY(model.canCancel());
    QVERIFY(model.error().isEmpty());

    model.clearInspection();
    const auto commandCount = dispatcher.commands.size();
    QVERIFY(!model.inspect(QStringLiteral("remote-1")));
    QCOMPARE(dispatcher.commands.size(), commandCount);
    QVERIFY(model.availableModes().isEmpty());
}

void SteeringModelTest::signedOutStopAndSendOnlyLocalUsesAvailableModeFallback()
{
    FakeSteeringDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::SteeringModel model(dispatcher, sessionsModel);
    const auto authEvent = signedOutAuth();
    sessionsModel.ingestAuthEvent(authEvent);
    sessionsModel.ingestSessionEvent(signedOutSessions(false, true));

    QVERIFY(!model.inspect(QStringLiteral("session-1")));
    model.ingestAuthEvent(authEvent);
    model.ingestAgentIntelEvent(
        signedOutReply(commandRequestId(dispatcher), QJsonArray {}));

    QCOMPARE(
        model.availableModes(),
        QStringList {QStringLiteral("stopAndSend")});
    QCOMPARE(model.draftMode(), QStringLiteral("stopAndSend"));
    QVERIFY(model.saveDraft(
        QStringLiteral("session-1"),
        QStringLiteral("interrupt locally"),
        QStringLiteral("queue")));
    QCOMPARE(model.draftMode(), QStringLiteral("stopAndSend"));
    QVERIFY(model.canSend());
    QVERIFY(model.send(QStringLiteral("session-1")));
    const auto semanticId = commandRequestId(dispatcher);
    QCOMPARE(
        dispatcher.commands.constLast().value(QStringLiteral("mode")).toString(),
        QStringLiteral("stopAndSend"));
    model.ingestAgentIntelEvent(signedOutReply(
        semanticId,
        entry(
            semanticId,
            QStringLiteral("runtime-local-stop"),
            QStringLiteral("injected"),
            QStringLiteral("interrupt locally"),
            QStringLiteral("stopAndSend"),
            1,
            incarnation,
            QStringLiteral("local"))));
    QCOMPARE(model.draftText(), QString {});
}

QTEST_GUILESS_MAIN(SteeringModelTest)

#include "tst_steering_model.moc"
