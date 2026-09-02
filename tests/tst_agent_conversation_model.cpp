#include "models/AgentConversationModel.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest/QTest>

#include <optional>
#include <utility>

class FakeConversationDispatcher final : public kodosi::CommandDispatcher {
public:
    struct Sent {
        kodosi::CommandLane lane;
        QJsonObject object;
    };

    QVector<Sent> sent;
    bool rejectNext = false;

    Result send(
        const kodosi::CommandLane lane,
        const QByteArrayView json) override
    {
        const auto document = QJsonDocument::fromJson(json.toByteArray());
        if (!document.isObject()) {
            return std::unexpected(kodosi::RuntimeFailure {
                .code = kodosi::RuntimeFailure::Code::InvalidArgument,
                .ffiResult = -1,
                .message = QStringLiteral("Invalid JSON"),
            });
        }
        sent.push_back({lane, document.object()});
        if (std::exchange(rejectNext, false)) {
            return std::unexpected(kodosi::RuntimeFailure {
                .code = kodosi::RuntimeFailure::Code::FfiRejected,
                .ffiResult = -1,
                .message = QStringLiteral("Rejected"),
            });
        }
        return {};
    }

    [[nodiscard]] QVector<QJsonObject> commands(
        const kodosi::CommandLane lane) const
    {
        QVector<QJsonObject> result;
        for (const auto& command : sent) {
            if (command.lane == lane) {
                result.push_back(command.object);
            }
        }
        return result;
    }
};

class AgentConversationModelTest final : public QObject {
    Q_OBJECT

private slots:
    void resolveThenReadUsesExactCommandShapes();
    void fencesStaleAccountSelectionIncarnationAndRuntimeReplies();
    void timesOutBoundedly();
    void rejectsMismatchedNativeTranscriptIdentity();
    void retainsIdenticalRowsWithinPageAndRejectsMalformedAtomically();
    void prependsDisjointEarlierPagesInOrderRetainingIdenticalRows();
    void acceptsRedactionExpansionWithinPageLimit();
    void paginationFailurePreservesRowsAndRetriesCursor();
    void rejectsRemoteSessionsExplicitly();
    void usesCanonicalCopilotProvider();
    void presentationRolesHidePrivateAuthority();
    void qmlNeverReceivesPrivateTranscriptAuthority();
};

namespace {

constexpr auto accountEpoch = 7;
const auto firstIncarnation =
    QStringLiteral("01900000-0000-7000-8000-000000000011");
const auto secondIncarnation =
    QStringLiteral("01900000-0000-7000-8000-000000000012");
const auto authorityIncarnation =
    QStringLiteral("01900000-0000-7000-8000-000000000010");
const auto nativeSessionId =
    QStringLiteral("7cbb3967-052f-43e8-bda4-2aec4dbef9e1");

QByteArray auth(
    const quint64 epoch = accountEpoch,
    const QString& userId = QStringLiteral("account"))
{
    return QJsonDocument(QJsonObject {
        {QStringLiteral("type"), QStringLiteral("auth.ready")},
        {QStringLiteral("userId"), userId},
        {QStringLiteral("accountEpoch"), static_cast<qint64>(epoch)},
    }).toJson(QJsonDocument::Compact);
}

QJsonObject localSession(
    const QString& id,
    const QString& incarnation)
{
    return {
        {QStringLiteral("kind"), QStringLiteral("local")},
        {QStringLiteral("id"), id},
        {QStringLiteral("incarnationId"), incarnation},
        {QStringLiteral("name"), id},
        {QStringLiteral("project"), QStringLiteral("/repo")},
        {QStringLiteral("mode"), QStringLiteral("normal")},
        {QStringLiteral("status"), QStringLiteral("active")},
        {QStringLiteral("recovery"), QStringLiteral("live")},
        {QStringLiteral("scope"), QStringLiteral("justMe")},
        {QStringLiteral("access"), QStringLiteral("approve")},
    };
}

QJsonObject remoteSession()
{
    return {
        {QStringLiteral("kind"), QStringLiteral("remote")},
        {QStringLiteral("id"), QStringLiteral("remote")},
        {QStringLiteral("incarnationId"), firstIncarnation},
        {QStringLiteral("name"), QStringLiteral("Remote")},
        {QStringLiteral("project"), QStringLiteral("/repo")},
        {QStringLiteral("mode"), QStringLiteral("normal")},
        {QStringLiteral("status"), QStringLiteral("active")},
        {QStringLiteral("scope"), QStringLiteral("room")},
        {QStringLiteral("access"), QStringLiteral("view")},
        {QStringLiteral("owner"), QStringLiteral("Owner")},
        {QStringLiteral("permissions"), 1},
        {QStringLiteral("connectionState"), QStringLiteral("connected")},
        {QStringLiteral("accessState"), QStringLiteral("ready")},
    };
}

QByteArray sessions(const QJsonArray& entries)
{
    return QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), QStringLiteral("account")},
        {QStringLiteral("accountEpoch"), accountEpoch},
        {QStringLiteral("type"), QStringLiteral("session.list")},
        {QStringLiteral("sessions"), entries},
    }).toJson(QJsonDocument::Compact);
}

QByteArray upsertSession(const QJsonObject& session)
{
    return QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), QStringLiteral("account")},
        {QStringLiteral("accountEpoch"), accountEpoch},
        {QStringLiteral("type"), QStringLiteral("session.upsert")},
        {QStringLiteral("session"), session},
    }).toJson(QJsonDocument::Compact);
}

QJsonObject snapshot(const QString& agent = QStringLiteral("claude"))
{
    return {
        {QStringLiteral("identity"),
         QJsonObject {
             {QStringLiteral("agentType"), agent},
             {QStringLiteral("version"), QJsonValue::Null},
             {QStringLiteral("model"), QJsonValue::Null},
             {QStringLiteral("title"), QJsonValue::Null},
             {QStringLiteral("cwd"), QStringLiteral("/repo")},
             {QStringLiteral("vendorSessionId"), QJsonValue::Null},
             {QStringLiteral("processId"), QJsonValue::Null},
         }},
        {QStringLiteral("lifecycle"), QStringLiteral("working")},
        {QStringLiteral("attention"), QJsonValue::Null},
        {QStringLiteral("pendingInteraction"), QJsonValue::Null},
        {QStringLiteral("currentActivity"), QJsonValue::Null},
        {QStringLiteral("workers"),
         QJsonObject {
             {QStringLiteral("active"), 0},
             {QStringLiteral("blocked"), 0},
             {QStringLiteral("failed"), 0},
             {QStringLiteral("completed"), 0},
         }},
        {QStringLiteral("outcome"), QJsonValue::Null},
        {QStringLiteral("exceptionalState"), QJsonValue::Null},
        {QStringLiteral("source"),
         QJsonObject {
             {QStringLiteral("kind"), QStringLiteral("runtime")},
             {QStringLiteral("degraded"), false},
             {QStringLiteral("detail"), QJsonValue::Null},
         }},
    };
}

QJsonObject intelEntry(
    const QString& id,
    const QString& incarnation,
    const QString& agent = QStringLiteral("claude"))
{
    return {
        {QStringLiteral("sessionId"), id},
        {QStringLiteral("sessionIncarnationId"), incarnation},
        {QStringLiteral("snapshot"), snapshot(agent)},
    };
}

QByteArray liveSet(
    const QString& requestId,
    const QJsonArray& entries,
    const quint64 revision = 1)
{
    return QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), QStringLiteral("account")},
        {QStringLiteral("accountEpoch"), accountEpoch},
        {QStringLiteral("type"), QStringLiteral("agent.intel.liveSet")},
        {QStringLiteral("requestId"),
         requestId.isEmpty() ? QJsonValue(QJsonValue::Null)
                             : QJsonValue(requestId)},
        {QStringLiteral("authorityIncarnationId"), authorityIncarnation},
        {QStringLiteral("revision"), static_cast<qint64>(revision)},
        {QStringLiteral("entries"), entries},
    }).toJson(QJsonDocument::Compact);
}

QJsonObject row(
    const QString& role,
    const QString& content,
    const QString& toolName = {},
    const QString& timestamp = {})
{
    return {
        {QStringLiteral("role"), role},
        {QStringLiteral("content"), content},
        {QStringLiteral("toolName"),
         toolName.isEmpty() ? QJsonValue(QJsonValue::Null)
                            : QJsonValue(toolName)},
        {QStringLiteral("timestamp"),
         timestamp.isEmpty() ? QJsonValue(QJsonValue::Null)
                             : QJsonValue(timestamp)},
    };
}

QJsonObject page(
    const QJsonArray& entries,
    const std::optional<quint64> cursor,
    const quint64 sourceFileBytes,
    const quint64 readBytes,
    const quint64 sourceRecords,
    const QString& degraded = {})
{
    return {
        {QStringLiteral("entries"), entries},
        {QStringLiteral("nextBeforeByte"),
         cursor ? QJsonValue(static_cast<double>(*cursor))
                : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("sourceFileBytes"),
         static_cast<double>(sourceFileBytes)},
        {QStringLiteral("readBytes"), static_cast<double>(readBytes)},
        {QStringLiteral("sourceRecords"),
         static_cast<double>(sourceRecords)},
        {QStringLiteral("degradedReason"),
         degraded.isEmpty() ? QJsonValue(QJsonValue::Null)
                            : QJsonValue(degraded)},
    };
}

QByteArray reply(
    const QString& requestId,
    const QJsonObject& payload,
    const quint64 epoch = accountEpoch,
    const QString& userId = QStringLiteral("account"))
{
    return QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), userId},
        {QStringLiteral("accountEpoch"), static_cast<qint64>(epoch)},
        {QStringLiteral("type"), QStringLiteral("agent.intel.reply")},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("payload"), payload},
    }).toJson(QJsonDocument::Compact);
}

QByteArray runtimeError(
    const QString& requestId,
    const QString& message)
{
    return QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), QStringLiteral("account")},
        {QStringLiteral("accountEpoch"), accountEpoch},
        {QStringLiteral("type"), QStringLiteral("agent.intel.error")},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("message"), message},
    }).toJson(QJsonDocument::Compact);
}

QJsonObject resolution(
    const QString& runtimeId,
    const QString& incarnation = firstIncarnation)
{
    return {
        {QStringLiteral("runtimeSessionId"), runtimeId},
        {QStringLiteral("runtimeIncarnationId"), incarnation},
        {QStringLiteral("nativeSessionId"), nativeSessionId},
        {QStringLiteral("activeJsonl"),
         QStringLiteral("/home/user/.claude/projects/repo/")
             + nativeSessionId + QStringLiteral(".jsonl")},
    };
}

QString requestId(const QJsonObject& command)
{
    return command.value(QStringLiteral("requestId")).toString();
}

void activateLocal(
    FakeConversationDispatcher& dispatcher,
    kodosi::SessionCatalogModel& sessionsModel,
    kodosi::AgentSessionIntelModel& intelModel,
    kodosi::AgentConversationModel& conversation,
    const QJsonArray& sessionEntries,
    const QJsonArray& intelEntries)
{
    const auto authEvent = auth();
    sessionsModel.ingestAuthEvent(authEvent);
    sessionsModel.ingestSessionEvent(sessions(sessionEntries));
    intelModel.ingestAuthEvent(authEvent);
    const auto systemCommands =
        dispatcher.commands(kodosi::CommandLane::System);
    intelModel.ingestAgentIntelEvent(liveSet(
        requestId(systemCommands.constLast()),
        intelEntries));
    conversation.ingestAuthEvent(authEvent);
}

QJsonObject lastAgentIntelCommand(
    const FakeConversationDispatcher& dispatcher)
{
    return dispatcher.commands(kodosi::CommandLane::AgentIntel).constLast();
}

void resolveSelected(
    FakeConversationDispatcher& dispatcher,
    kodosi::AgentConversationModel& model,
    const QString& runtimeId)
{
    const auto resolveCommand = lastAgentIntelCommand(dispatcher);
    model.ingestAgentIntelEvent(reply(
        requestId(resolveCommand),
        resolution(runtimeId)));
}

} // namespace

void AgentConversationModelTest::resolveThenReadUsesExactCommandShapes()
{
    FakeConversationDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::AgentSessionIntelModel intelModel(dispatcher, sessionsModel);
    kodosi::AgentConversationModel model(
        dispatcher,
        sessionsModel,
        intelModel);
    activateLocal(
        dispatcher,
        sessionsModel,
        intelModel,
        model,
        QJsonArray {localSession(QStringLiteral("session-1"), firstIncarnation)},
        QJsonArray {intelEntry(QStringLiteral("session-1"), firstIncarnation)});

    QVERIFY(model.inspect(QStringLiteral("session-1")));
    const auto resolveCommand = lastAgentIntelCommand(dispatcher);
    const QJsonObject expectedResolve {
        {QStringLiteral("type"),
         QStringLiteral("agent.intel.resolveActiveSession")},
        {QStringLiteral("requestId"), requestId(resolveCommand)},
        {QStringLiteral("cwd"), QStringLiteral("/repo")},
        {QStringLiteral("sessionId"), QStringLiteral("session-1")},
        {QStringLiteral("expectedRuntimeIncarnationId"), firstIncarnation},
    };
    QCOMPARE(resolveCommand, expectedResolve);

    resolveSelected(dispatcher, model, QStringLiteral("session-1"));
    const auto readCommand = lastAgentIntelCommand(dispatcher);
    const QJsonObject expectedRead {
        {QStringLiteral("type"),
         QStringLiteral("agent.intel.readSessionConversation")},
        {QStringLiteral("requestId"), requestId(readCommand)},
        {QStringLiteral("agent"), QStringLiteral("claude")},
        {QStringLiteral("cwd"), QStringLiteral("/repo")},
        {QStringLiteral("sessionId"), nativeSessionId},
        {QStringLiteral("maxRecords"), 100},
        {QStringLiteral("maxBytes"), 262144},
    };
    QCOMPARE(readCommand, expectedRead);
    model.ingestAgentIntelEvent(reply(
        requestId(readCommand),
        page(
            QJsonArray {
                row(
                    QStringLiteral("user"),
                    QStringLiteral("Run the tests"),
                    {},
                    QStringLiteral("2026-09-01T04:00:00Z")),
                row(
                    QStringLiteral("tool_use"),
                    QStringLiteral("just test"),
                    QStringLiteral("Bash")),
            },
            std::nullopt,
            200,
            200,
            2)));

    QCOMPARE(model.rowCount(), 2);
    QVERIFY(!model.loading());
    QVERIFY(model.error().isEmpty());
    QCOMPARE(
        model.data(model.index(0), kodosi::AgentConversationModel::ContentRole)
            .toString(),
        QStringLiteral("Run the tests"));
    model.close();
    QCOMPARE(model.rowCount(), 0);
    QVERIFY(!model.loading());
    QVERIFY(model.error().isEmpty());
}

void AgentConversationModelTest::
    fencesStaleAccountSelectionIncarnationAndRuntimeReplies()
{
    FakeConversationDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::AgentSessionIntelModel intelModel(dispatcher, sessionsModel);
    kodosi::AgentConversationModel model(
        dispatcher,
        sessionsModel,
        intelModel);
    activateLocal(
        dispatcher,
        sessionsModel,
        intelModel,
        model,
        QJsonArray {
            localSession(QStringLiteral("session-1"), firstIncarnation),
            localSession(QStringLiteral("session-2"), firstIncarnation),
        },
        QJsonArray {
            intelEntry(QStringLiteral("session-1"), firstIncarnation),
            intelEntry(QStringLiteral("session-2"), firstIncarnation),
        });

    QVERIFY(model.inspect(QStringLiteral("session-1")));
    const auto firstResolve = lastAgentIntelCommand(dispatcher);
    model.ingestAgentIntelEvent(reply(
        requestId(firstResolve),
        resolution(QStringLiteral("session-1")),
        accountEpoch - 1));
    QCOMPARE(
        dispatcher.commands(kodosi::CommandLane::AgentIntel).size(),
        1);
    QVERIFY(model.loading());

    QVERIFY(model.inspect(QStringLiteral("session-2")));
    const auto secondResolve = lastAgentIntelCommand(dispatcher);
    model.ingestAgentIntelEvent(reply(
        requestId(firstResolve),
        resolution(QStringLiteral("session-1"))));
    QCOMPARE(
        requestId(lastAgentIntelCommand(dispatcher)),
        requestId(secondResolve));

    sessionsModel.ingestSessionEvent(upsertSession(
        localSession(QStringLiteral("session-2"), secondIncarnation)));
    QVERIFY(!model.loading());
    QCOMPARE(model.rowCount(), 0);
    model.ingestAgentIntelEvent(reply(
        requestId(secondResolve),
        resolution(QStringLiteral("session-2"))));
    QCOMPARE(
        dispatcher.commands(kodosi::CommandLane::AgentIntel).size(),
        2);

    intelModel.ingestAgentIntelEvent(liveSet(
        {},
        QJsonArray {
            intelEntry(QStringLiteral("session-1"), firstIncarnation),
            intelEntry(QStringLiteral("session-2"), secondIncarnation),
        },
        2));
    QVERIFY(model.inspect(QStringLiteral("session-2")));
    const auto runtimeResolve = lastAgentIntelCommand(dispatcher);
    model.resetRuntimeAuthority();
    model.ingestAgentIntelEvent(reply(
        requestId(runtimeResolve),
        resolution(QStringLiteral("session-2"), secondIncarnation)));
    QCOMPARE(model.rowCount(), 0);
    QVERIFY(!model.loading());
}

void AgentConversationModelTest::timesOutBoundedly()
{
    FakeConversationDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::AgentSessionIntelModel intelModel(dispatcher, sessionsModel);
    kodosi::AgentConversationModel model(
        dispatcher,
        sessionsModel,
        intelModel,
        0);
    activateLocal(
        dispatcher,
        sessionsModel,
        intelModel,
        model,
        QJsonArray {localSession(QStringLiteral("session-1"), firstIncarnation)},
        QJsonArray {intelEntry(QStringLiteral("session-1"), firstIncarnation)});

    QVERIFY(model.inspect(QStringLiteral("session-1")));
    QTRY_VERIFY_WITH_TIMEOUT(!model.loading(), 500);
    QVERIFY(model.error().contains(QStringLiteral("did not reply")));
}

void AgentConversationModelTest::rejectsMismatchedNativeTranscriptIdentity()
{
    FakeConversationDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::AgentSessionIntelModel intelModel(dispatcher, sessionsModel);
    kodosi::AgentConversationModel model(
        dispatcher,
        sessionsModel,
        intelModel);
    activateLocal(
        dispatcher,
        sessionsModel,
        intelModel,
        model,
        QJsonArray {localSession(QStringLiteral("session-1"), firstIncarnation)},
        QJsonArray {intelEntry(QStringLiteral("session-1"), firstIncarnation)});
    QVERIFY(model.inspect(QStringLiteral("session-1")));

    auto resolveCommand = lastAgentIntelCommand(dispatcher);
    auto mismatchedPath = resolution(QStringLiteral("session-1"));
    mismatchedPath.insert(
        QStringLiteral("activeJsonl"),
        QStringLiteral("/home/user/.claude/projects/repo/other.jsonl"));
    model.ingestAgentIntelEvent(reply(
        requestId(resolveCommand),
        mismatchedPath));
    QVERIFY(model.error().contains(QStringLiteral("invalid transcript identity")));
    QCOMPARE(
        dispatcher.commands(kodosi::CommandLane::AgentIntel).size(),
        1);

    QVERIFY(model.retry());
    resolveCommand = lastAgentIntelCommand(dispatcher);
    model.ingestAgentIntelEvent(reply(
        requestId(resolveCommand),
        resolution(QStringLiteral("session-1"), secondIncarnation)));
    QVERIFY(model.error().contains(QStringLiteral("invalid transcript identity")));
    QCOMPARE(
        dispatcher.commands(kodosi::CommandLane::AgentIntel).size(),
        2);
}

void AgentConversationModelTest::
    retainsIdenticalRowsWithinPageAndRejectsMalformedAtomically()
{
    FakeConversationDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::AgentSessionIntelModel intelModel(dispatcher, sessionsModel);
    kodosi::AgentConversationModel model(
        dispatcher,
        sessionsModel,
        intelModel);
    QSignalSpy errors(&model, &kodosi::AgentConversationModel::decodeError);
    activateLocal(
        dispatcher,
        sessionsModel,
        intelModel,
        model,
        QJsonArray {localSession(QStringLiteral("session-1"), firstIncarnation)},
        QJsonArray {intelEntry(QStringLiteral("session-1"), firstIncarnation)});

    QVERIFY(model.inspect(QStringLiteral("session-1")));
    resolveSelected(dispatcher, model, QStringLiteral("session-1"));
    const auto readCommand = lastAgentIntelCommand(dispatcher);
    const auto duplicate =
        row(QStringLiteral("assistant"), QStringLiteral("Done"));
    model.ingestAgentIntelEvent(reply(
        requestId(readCommand),
        page(
            QJsonArray {duplicate, duplicate},
            std::nullopt,
            100,
            100,
            2)));
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(errors.size(), 0);
    QCOMPARE(
        model.data(model.index(0), kodosi::AgentConversationModel::ContentRole)
            .toString(),
        QStringLiteral("Done"));
    QCOMPARE(
        model.data(model.index(1), kodosi::AgentConversationModel::ContentRole)
            .toString(),
        QStringLiteral("Done"));

    model.close();
    QVERIFY(model.inspect(QStringLiteral("session-1")));
    resolveSelected(dispatcher, model, QStringLiteral("session-1"));
    const auto malformedRead = lastAgentIntelCommand(dispatcher);
    auto malformed =
        row(QStringLiteral("tool_use"), QStringLiteral("output"));
    malformed.insert(QStringLiteral("extra"), true);
    model.ingestAgentIntelEvent(reply(
        requestId(malformedRead),
        page(
            QJsonArray {malformed},
            std::nullopt,
            100,
            100,
            1)));
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(errors.size(), 1);
}

void AgentConversationModelTest::
    prependsDisjointEarlierPagesInOrderRetainingIdenticalRows()
{
    FakeConversationDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::AgentSessionIntelModel intelModel(dispatcher, sessionsModel);
    kodosi::AgentConversationModel model(
        dispatcher,
        sessionsModel,
        intelModel);
    activateLocal(
        dispatcher,
        sessionsModel,
        intelModel,
        model,
        QJsonArray {localSession(QStringLiteral("session-1"), firstIncarnation)},
        QJsonArray {intelEntry(QStringLiteral("session-1"), firstIncarnation)});
    QVERIFY(model.inspect(QStringLiteral("session-1")));
    resolveSelected(dispatcher, model, QStringLiteral("session-1"));
    auto readCommand = lastAgentIntelCommand(dispatcher);
    model.ingestAgentIntelEvent(reply(
        requestId(readCommand),
        page(
            QJsonArray {
                row(QStringLiteral("assistant"), QStringLiteral("middle")),
                row(QStringLiteral("assistant"), QStringLiteral("middle")),
                row(QStringLiteral("assistant"), QStringLiteral("newest")),
            },
            100,
            300,
            200,
            3,
            QStringLiteral("A long record was skipped."))));
    QCOMPARE(model.rowCount(), 3);
    QVERIFY(model.hasEarlier());
    QCOMPARE(
        model.degradedWarning(),
        QStringLiteral("A long record was skipped."));

    QVERIFY(model.loadEarlier());
    readCommand = lastAgentIntelCommand(dispatcher);
    QCOMPARE(
        readCommand.value(QStringLiteral("beforeByte")).toInteger(),
        100);
    model.ingestAgentIntelEvent(reply(
        requestId(readCommand),
        page(
            QJsonArray {
                row(QStringLiteral("user"), QStringLiteral("oldest")),
                row(QStringLiteral("assistant"), QStringLiteral("middle")),
            },
            std::nullopt,
            300,
            100,
            2)));
    QCOMPARE(model.rowCount(), 5);
    const QStringList expected {
        QStringLiteral("oldest"),
        QStringLiteral("middle"),
        QStringLiteral("middle"),
        QStringLiteral("middle"),
        QStringLiteral("newest"),
    };
    for (qsizetype index = 0; index < expected.size(); ++index) {
        QCOMPARE(
            model.data(
                     model.index(index),
                     kodosi::AgentConversationModel::ContentRole)
                .toString(),
            expected.at(index));
    }
    QVERIFY(!model.hasEarlier());
    QVERIFY(model.degradedWarning().isEmpty());
}

void AgentConversationModelTest::acceptsRedactionExpansionWithinPageLimit()
{
    FakeConversationDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::AgentSessionIntelModel intelModel(dispatcher, sessionsModel);
    kodosi::AgentConversationModel model(
        dispatcher,
        sessionsModel,
        intelModel);
    QSignalSpy errors(&model, &kodosi::AgentConversationModel::decodeError);
    activateLocal(
        dispatcher,
        sessionsModel,
        intelModel,
        model,
        QJsonArray {localSession(QStringLiteral("session-1"), firstIncarnation)},
        QJsonArray {intelEntry(QStringLiteral("session-1"), firstIncarnation)});

    QVERIFY(model.inspect(QStringLiteral("session-1")));
    resolveSelected(dispatcher, model, QStringLiteral("session-1"));
    const auto readCommand = lastAgentIntelCommand(dispatcher);
    const auto expandedRedaction =
        QStringLiteral("[REDACTED:")
        + QString(4'096, QLatin1Char('x'))
        + QStringLiteral("]");
    model.ingestAgentIntelEvent(reply(
        requestId(readCommand),
        page(
            QJsonArray {
                row(QStringLiteral("assistant"), expandedRedaction),
            },
            std::nullopt,
            64,
            16,
            1)));

    QCOMPARE(errors.size(), 0);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(
        model.data(model.index(0), kodosi::AgentConversationModel::ContentRole)
            .toString(),
        expandedRedaction);
}

void AgentConversationModelTest::
    paginationFailurePreservesRowsAndRetriesCursor()
{
    FakeConversationDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::AgentSessionIntelModel intelModel(dispatcher, sessionsModel);
    kodosi::AgentConversationModel model(
        dispatcher,
        sessionsModel,
        intelModel);
    activateLocal(
        dispatcher,
        sessionsModel,
        intelModel,
        model,
        QJsonArray {localSession(QStringLiteral("session-1"), firstIncarnation)},
        QJsonArray {intelEntry(QStringLiteral("session-1"), firstIncarnation)});
    QVERIFY(model.inspect(QStringLiteral("session-1")));
    resolveSelected(dispatcher, model, QStringLiteral("session-1"));
    auto readCommand = lastAgentIntelCommand(dispatcher);
    model.ingestAgentIntelEvent(reply(
        requestId(readCommand),
        page(
            QJsonArray {
                row(QStringLiteral("assistant"), QStringLiteral("current")),
            },
            90,
            200,
            110,
            1)));
    QCOMPARE(model.rowCount(), 1);

    QVERIFY(model.loadEarlier());
    readCommand = lastAgentIntelCommand(dispatcher);
    model.ingestAgentIntelEvent(runtimeError(
        requestId(readCommand),
        QStringLiteral("transcript changed")));
    QCOMPARE(model.rowCount(), 1);
    QVERIFY(model.hasEarlier());
    QCOMPARE(
        model.error(),
        QStringLiteral(
            "Couldn't load earlier conversation entries. Try again."));
    QVERIFY(!model.error().contains(QStringLiteral("transcript changed")));

    QVERIFY(model.retry());
    const auto retryCommand = lastAgentIntelCommand(dispatcher);
    QCOMPARE(
        retryCommand.value(QStringLiteral("beforeByte")).toInteger(),
        90);
    QCOMPARE(model.rowCount(), 1);
}

void AgentConversationModelTest::rejectsRemoteSessionsExplicitly()
{
    FakeConversationDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::AgentSessionIntelModel intelModel(dispatcher, sessionsModel);
    kodosi::AgentConversationModel model(
        dispatcher,
        sessionsModel,
        intelModel);
    const auto authEvent = auth();
    sessionsModel.ingestAuthEvent(authEvent);
    sessionsModel.ingestSessionEvent(sessions(QJsonArray {remoteSession()}));
    model.ingestAuthEvent(authEvent);

    QVERIFY(!model.inspect(QStringLiteral("remote")));
    QVERIFY(model.error().contains(QStringLiteral("only for local")));
    QVERIFY(dispatcher.commands(kodosi::CommandLane::AgentIntel).isEmpty());
}

void AgentConversationModelTest::usesCanonicalCopilotProvider()
{
    FakeConversationDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::AgentSessionIntelModel intelModel(dispatcher, sessionsModel);
    kodosi::AgentConversationModel model(
        dispatcher,
        sessionsModel,
        intelModel);
    activateLocal(
        dispatcher,
        sessionsModel,
        intelModel,
        model,
        QJsonArray {localSession(QStringLiteral("session-1"), firstIncarnation)},
        QJsonArray {intelEntry(
            QStringLiteral("session-1"),
            firstIncarnation,
            QStringLiteral("copilot"))});
    QVERIFY(model.inspect(QStringLiteral("session-1")));
    resolveSelected(dispatcher, model, QStringLiteral("session-1"));
    const auto readCommand = lastAgentIntelCommand(dispatcher);
    QCOMPARE(
        readCommand.value(QStringLiteral("agent")).toString(),
        QStringLiteral("copilot"));
    QCOMPARE(
        readCommand.value(QStringLiteral("sessionId")).toString(),
        nativeSessionId);
}

void AgentConversationModelTest::presentationRolesHidePrivateAuthority()
{
    FakeConversationDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::AgentSessionIntelModel intelModel(dispatcher, sessionsModel);
    kodosi::AgentConversationModel model(
        dispatcher,
        sessionsModel,
        intelModel);
    const auto roles = model.roleNames().values();
    QCOMPARE(roles.size(), 4);
    QVERIFY(roles.contains(QByteArrayLiteral("role")));
    QVERIFY(roles.contains(QByteArrayLiteral("content")));
    QVERIFY(roles.contains(QByteArrayLiteral("toolName")));
    QVERIFY(roles.contains(QByteArrayLiteral("timestamp")));
    QVERIFY(!roles.contains(QByteArrayLiteral("sessionId")));
    QVERIFY(!roles.contains(QByteArrayLiteral("incarnationId")));
    QVERIFY(!roles.contains(QByteArrayLiteral("nativeSessionId")));
    QVERIFY(!roles.contains(QByteArrayLiteral("transcriptPath")));
    QVERIFY(!roles.contains(QByteArrayLiteral("beforeByte")));
}

void AgentConversationModelTest::qmlNeverReceivesPrivateTranscriptAuthority()
{
    QFile file(QStringLiteral(KODOSI_SOURCE_DIR)
        + QStringLiteral("/src/qml/AgentIntel/AgentIntelDrawer.qml"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto qml = file.readAll();
    for (const auto forbidden : {
             QByteArrayLiteral("nativeSessionId"),
             QByteArrayLiteral("activeJsonl"),
             QByteArrayLiteral("transcriptPath"),
             QByteArrayLiteral("beforeByte"),
             QByteArrayLiteral("accountEpoch"),
             QByteArrayLiteral("runtimeIncarnation"),
             QByteArrayLiteral("vendorSessionId"),
         }) {
        QVERIFY2(!qml.contains(forbidden), forbidden.constData());
    }
    QVERIFY(qml.contains(QByteArrayLiteral("KReadOnlyText")));
    QFile brandedText(QStringLiteral(KODOSI_SOURCE_DIR)
        + QStringLiteral("/src/qml/Controls/KReadOnlyText.qml"));
    QVERIFY(brandedText.open(QIODevice::ReadOnly));
    const auto brandedQml = brandedText.readAll();
    QVERIFY(brandedQml.contains(
        QByteArrayLiteral("textFormat: Text.PlainText")));
    QVERIFY(brandedQml.contains(QByteArrayLiteral("selectByMouse: true")));
    QVERIFY(brandedQml.contains(
        QByteArrayLiteral("selectionColor: KodosiTheme.accent")));
    QVERIFY(brandedQml.contains(
        QByteArrayLiteral(
            "selectedTextColor: KodosiTheme.accentForeground")));
}

QTEST_GUILESS_MAIN(AgentConversationModelTest)

#include "tst_agent_conversation_model.moc"
