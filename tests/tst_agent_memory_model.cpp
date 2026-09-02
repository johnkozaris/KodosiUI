#include "models/AgentMemoryModel.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest/QTest>

#include <utility>

class FakeMemoryDispatcher final : public kodosi::CommandDispatcher {
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

class AgentMemoryModelTest final : public QObject {
    Q_OBJECT

private slots:
    void listSelectAndReadUseExactBoundShapes();
    void rejectsVerifiedIdentityMismatch();
    void consumedTokenRetryRelistsBeforeReading();
    void preHydrationDemandRecoversWhenClaudeArrives();
    void fencesAccountRuntimeSessionAndSelectionReplies();
    void malformedAndDuplicateListsAreRejectedAtomically();
    void enforcesTypeDescriptionAndContentBounds();
    void timeoutAndRuntimeErrorPreserveInventory();
    void presentationAndQmlHidePrivateAuthority();
};

namespace {

constexpr auto accountEpoch = 7;
const auto firstIncarnation =
    QStringLiteral("01900000-0000-7000-8000-000000000011");
const auto secondIncarnation =
    QStringLiteral("01900000-0000-7000-8000-000000000012");
const auto authorityIncarnation =
    QStringLiteral("01900000-0000-7000-8000-000000000010");
const auto firstToken =
    QStringLiteral("01900000-0000-7000-8000-000000000021");
const auto secondToken =
    QStringLiteral("01900000-0000-7000-8000-000000000022");

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
    const QString& incarnation,
    const QString& project = QStringLiteral("/repo"))
{
    return {
        {QStringLiteral("kind"), QStringLiteral("local")},
        {QStringLiteral("id"), id},
        {QStringLiteral("incarnationId"), incarnation},
        {QStringLiteral("name"), id},
        {QStringLiteral("project"), project},
        {QStringLiteral("mode"), QStringLiteral("normal")},
        {QStringLiteral("status"), QStringLiteral("active")},
        {QStringLiteral("recovery"), QStringLiteral("live")},
        {QStringLiteral("scope"), QStringLiteral("justMe")},
        {QStringLiteral("access"), QStringLiteral("approve")},
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

QJsonObject snapshot(
    const QString& agent = QStringLiteral("claude"),
    const QString& cwd = QStringLiteral("/repo"))
{
    return {
        {QStringLiteral("identity"),
         QJsonObject {
             {QStringLiteral("agentType"), agent},
             {QStringLiteral("version"), QJsonValue::Null},
             {QStringLiteral("model"), QJsonValue::Null},
             {QStringLiteral("title"), QJsonValue::Null},
             {QStringLiteral("cwd"), cwd},
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
    const QString& agent = QStringLiteral("claude"),
    const QString& cwd = QStringLiteral("/repo"))
{
    return {
        {QStringLiteral("sessionId"), id},
        {QStringLiteral("sessionIncarnationId"), incarnation},
        {QStringLiteral("snapshot"), snapshot(agent, cwd)},
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

QJsonObject memoryItem(
    const QString& filename,
    const QString& token,
    const QJsonValue& memoryType = QJsonValue(QJsonValue::Null))
{
    return {
        {QStringLiteral("selectionToken"), token},
        {QStringLiteral("filename"), filename},
        {QStringLiteral("memoryType"), memoryType},
    };
}

QJsonObject listPayload(const QJsonArray& items)
{
    return {
        {QStringLiteral("canonicalCwd"), QStringLiteral("/repo")},
        {QStringLiteral("projectSlug"), QStringLiteral("-repo")},
        {QStringLiteral("items"), items},
    };
}

QJsonObject readPayload(
    const QString& filename,
    const QString& token,
    const QString& content = QStringLiteral("body"),
    const QString& canonicalCwd = QStringLiteral("/repo"),
    const QJsonValue& memoryType = QJsonValue(QJsonValue::Null))
{
    return {
        {QStringLiteral("content"), content},
        {QStringLiteral("selectionToken"), token},
        {QStringLiteral("canonicalCwd"), canonicalCwd},
        {QStringLiteral("projectSlug"), QStringLiteral("-repo")},
        {QStringLiteral("filename"), filename},
        {QStringLiteral("memoryType"), memoryType},
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
    const QString& message = QStringLiteral("selection consumed"))
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

QString requestId(const QJsonObject& command)
{
    return command.value(QStringLiteral("requestId")).toString();
}

QJsonObject lastAgentIntelCommand(
    const FakeMemoryDispatcher& dispatcher)
{
    return dispatcher.commands(kodosi::CommandLane::AgentIntel).constLast();
}

void activateSession(
    FakeMemoryDispatcher& dispatcher,
    kodosi::SessionCatalogModel& sessionsModel,
    kodosi::AgentSessionIntelModel& intelModel,
    kodosi::AgentMemoryModel& memoryModel,
    const QJsonArray& sessionEntries,
    const QJsonArray& intelEntries,
    const bool hydrateIntel = true)
{
    const auto authEvent = auth();
    sessionsModel.ingestAuthEvent(authEvent);
    sessionsModel.ingestSessionEvent(sessions(sessionEntries));
    intelModel.ingestAuthEvent(authEvent);
    memoryModel.ingestAuthEvent(authEvent);
    if (!hydrateIntel) {
        return;
    }
    const auto systemCommands =
        dispatcher.commands(kodosi::CommandLane::System);
    intelModel.ingestAgentIntelEvent(liveSet(
        requestId(systemCommands.constLast()),
        intelEntries));
}

void supplyList(
    FakeMemoryDispatcher& dispatcher,
    kodosi::AgentMemoryModel& model,
    const QJsonArray& items)
{
    const auto command = lastAgentIntelCommand(dispatcher);
    model.ingestAgentIntelEvent(reply(
        requestId(command),
        listPayload(items)));
}

} // namespace

void AgentMemoryModelTest::listSelectAndReadUseExactBoundShapes()
{
    FakeMemoryDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::AgentSessionIntelModel intelModel(dispatcher, sessionsModel);
    kodosi::AgentMemoryModel model(dispatcher, sessionsModel, intelModel);
    activateSession(
        dispatcher,
        sessionsModel,
        intelModel,
        model,
        QJsonArray {localSession(QStringLiteral("session-1"), firstIncarnation)},
        QJsonArray {intelEntry(QStringLiteral("session-1"), firstIncarnation)});

    QVERIFY(model.inspect(QStringLiteral("session-1")));
    const auto listCommand = lastAgentIntelCommand(dispatcher);
    QCOMPARE(
        listCommand,
        QJsonObject({
            {QStringLiteral("type"),
             QStringLiteral("agent.intel.listClaudeMemoryBound")},
            {QStringLiteral("requestId"), requestId(listCommand)},
            {QStringLiteral("cwd"), QStringLiteral("/repo")},
        }));
    supplyList(
        dispatcher,
        model,
        QJsonArray {
            memoryItem(
                QStringLiteral("MEMORY.md"),
                firstToken,
                QStringLiteral("project")),
        });
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.state(), kodosi::AgentMemoryModel::State::Ready);
    QCOMPARE(
        model.data(model.index(0), kodosi::AgentMemoryModel::FilenameRole)
            .toString(),
        QStringLiteral("MEMORY.md"));
    QCOMPARE(
        model.data(model.index(0), kodosi::AgentMemoryModel::DescriptionRole)
            .toString(),
        QStringLiteral("Project memory"));

    QVERIFY(model.select(QStringLiteral("MEMORY.md")));
    const auto readCommand = lastAgentIntelCommand(dispatcher);
    QCOMPARE(
        readCommand,
        QJsonObject({
            {QStringLiteral("type"),
             QStringLiteral("agent.intel.readClaudeMemoryBound")},
            {QStringLiteral("requestId"), requestId(readCommand)},
            {QStringLiteral("selectionToken"), firstToken},
        }));
    model.ingestAgentIntelEvent(reply(
        requestId(readCommand),
        readPayload(
            QStringLiteral("MEMORY.md"),
            firstToken,
            QStringLiteral("remember this"),
            QStringLiteral("/repo"),
            QStringLiteral("project"))));
    QCOMPARE(model.content(), QStringLiteral("remember this"));
    QVERIFY(!model.contentLoading());
    QVERIFY(model.contentError().isEmpty());
}

void AgentMemoryModelTest::rejectsVerifiedIdentityMismatch()
{
    FakeMemoryDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::AgentSessionIntelModel intelModel(dispatcher, sessionsModel);
    kodosi::AgentMemoryModel model(dispatcher, sessionsModel, intelModel);
    QSignalSpy errors(&model, &kodosi::AgentMemoryModel::decodeError);
    activateSession(
        dispatcher,
        sessionsModel,
        intelModel,
        model,
        QJsonArray {localSession(QStringLiteral("session-1"), firstIncarnation)},
        QJsonArray {intelEntry(QStringLiteral("session-1"), firstIncarnation)});
    QVERIFY(model.inspect(QStringLiteral("session-1")));
    supplyList(
        dispatcher,
        model,
        QJsonArray {memoryItem(QStringLiteral("fact.md"), firstToken)});
    QVERIFY(model.select(QStringLiteral("fact.md")));
    const auto readCommand = lastAgentIntelCommand(dispatcher);
    model.ingestAgentIntelEvent(reply(
        requestId(readCommand),
        readPayload(
            QStringLiteral("fact.md"),
            firstToken,
            QStringLiteral("wrong"),
            QStringLiteral("/other"))));

    QCOMPARE(model.rowCount(), 1);
    QVERIFY(model.content().isEmpty());
    QVERIFY(model.contentError().contains(QStringLiteral("different selection")));
    QCOMPARE(errors.size(), 1);
}

void AgentMemoryModelTest::consumedTokenRetryRelistsBeforeReading()
{
    FakeMemoryDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::AgentSessionIntelModel intelModel(dispatcher, sessionsModel);
    kodosi::AgentMemoryModel model(dispatcher, sessionsModel, intelModel);
    activateSession(
        dispatcher,
        sessionsModel,
        intelModel,
        model,
        QJsonArray {localSession(QStringLiteral("session-1"), firstIncarnation)},
        QJsonArray {intelEntry(QStringLiteral("session-1"), firstIncarnation)});
    QVERIFY(model.inspect(QStringLiteral("session-1")));
    supplyList(
        dispatcher,
        model,
        QJsonArray {memoryItem(QStringLiteral("fact.md"), firstToken)});
    QVERIFY(model.select(QStringLiteral("fact.md")));
    auto command = lastAgentIntelCommand(dispatcher);
    model.ingestAgentIntelEvent(runtimeError(requestId(command)));
    QCOMPARE(model.rowCount(), 1);

    QVERIFY(model.retry());
    command = lastAgentIntelCommand(dispatcher);
    QCOMPARE(
        command.value(QStringLiteral("type")).toString(),
        QStringLiteral("agent.intel.listClaudeMemoryBound"));
    QVERIFY(!command.contains(QStringLiteral("selectionToken")));
    model.ingestAgentIntelEvent(reply(
        requestId(command),
        listPayload(QJsonArray {
            memoryItem(QStringLiteral("fact.md"), secondToken),
        })));
    command = lastAgentIntelCommand(dispatcher);
    QCOMPARE(
        command.value(QStringLiteral("type")).toString(),
        QStringLiteral("agent.intel.readClaudeMemoryBound"));
    QCOMPARE(
        command.value(QStringLiteral("selectionToken")).toString(),
        secondToken);
    QVERIFY(
        command.value(QStringLiteral("selectionToken")).toString()
        != firstToken);
    model.ingestAgentIntelEvent(reply(
        requestId(command),
        readPayload(QStringLiteral("fact.md"), secondToken)));
    QCOMPARE(model.content(), QStringLiteral("body"));
}

void AgentMemoryModelTest::preHydrationDemandRecoversWhenClaudeArrives()
{
    FakeMemoryDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::AgentSessionIntelModel intelModel(dispatcher, sessionsModel);
    kodosi::AgentMemoryModel model(dispatcher, sessionsModel, intelModel);
    activateSession(
        dispatcher,
        sessionsModel,
        intelModel,
        model,
        QJsonArray {localSession(QStringLiteral("session-1"), firstIncarnation)},
        {},
        false);

    QVERIFY(model.inspect(QStringLiteral("session-1")));
    QVERIFY(dispatcher.commands(kodosi::CommandLane::AgentIntel).isEmpty());
    QCOMPARE(model.state(), kodosi::AgentMemoryModel::State::Waiting);

    const auto query =
        dispatcher.commands(kodosi::CommandLane::System).constLast();
    intelModel.ingestAgentIntelEvent(liveSet(
        requestId(query),
        QJsonArray {
            intelEntry(QStringLiteral("session-1"), firstIncarnation),
        }));
    QCOMPARE(
        lastAgentIntelCommand(dispatcher)
            .value(QStringLiteral("type"))
            .toString(),
        QStringLiteral("agent.intel.listClaudeMemoryBound"));
}

void AgentMemoryModelTest::
    fencesAccountRuntimeSessionAndSelectionReplies()
{
    FakeMemoryDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::AgentSessionIntelModel intelModel(dispatcher, sessionsModel);
    kodosi::AgentMemoryModel model(dispatcher, sessionsModel, intelModel);
    activateSession(
        dispatcher,
        sessionsModel,
        intelModel,
        model,
        QJsonArray {localSession(QStringLiteral("session-1"), firstIncarnation)},
        QJsonArray {intelEntry(QStringLiteral("session-1"), firstIncarnation)});
    QVERIFY(model.inspect(QStringLiteral("session-1")));
    auto command = lastAgentIntelCommand(dispatcher);
    model.ingestAgentIntelEvent(reply(
        requestId(command),
        listPayload(QJsonArray {
            memoryItem(QStringLiteral("a.md"), firstToken),
        }),
        accountEpoch - 1));
    QVERIFY(model.loading());
    QCOMPARE(model.rowCount(), 0);

    supplyList(
        dispatcher,
        model,
        QJsonArray {
            memoryItem(QStringLiteral("a.md"), firstToken),
            memoryItem(QStringLiteral("b.md"), secondToken),
        });
    QVERIFY(model.select(QStringLiteral("a.md")));
    const auto firstRead = lastAgentIntelCommand(dispatcher);
    QVERIFY(model.select(QStringLiteral("b.md")));
    const auto secondRead = lastAgentIntelCommand(dispatcher);
    model.ingestAgentIntelEvent(reply(
        requestId(firstRead),
        readPayload(QStringLiteral("a.md"), firstToken, QStringLiteral("late"))));
    QVERIFY(model.content().isEmpty());
    model.ingestAgentIntelEvent(reply(
        requestId(secondRead),
        readPayload(QStringLiteral("b.md"), secondToken, QStringLiteral("current"))));
    QCOMPARE(model.content(), QStringLiteral("current"));

    QVERIFY(model.retry());
    command = lastAgentIntelCommand(dispatcher);
    intelModel.ingestAgentIntelEvent(liveSet(
        {},
        QJsonArray {
            intelEntry(
                QStringLiteral("session-1"),
                firstIncarnation,
                QStringLiteral("copilot")),
        },
        2));
    model.ingestAgentIntelEvent(reply(
        requestId(command),
        listPayload(QJsonArray {
            memoryItem(QStringLiteral("identity-stale.md"), firstToken),
        })));
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.state(), kodosi::AgentMemoryModel::State::Ineligible);

    intelModel.ingestAgentIntelEvent(liveSet(
        {},
        QJsonArray {
            intelEntry(QStringLiteral("session-1"), firstIncarnation),
        },
        3));
    supplyList(
        dispatcher,
        model,
        QJsonArray {
            memoryItem(QStringLiteral("restored.md"), firstToken),
        });
    QCOMPARE(model.rowCount(), 1);

    QVERIFY(model.retry());
    command = lastAgentIntelCommand(dispatcher);
    sessionsModel.ingestSessionEvent(upsertSession(
        localSession(QStringLiteral("session-1"), secondIncarnation)));
    model.ingestAgentIntelEvent(reply(
        requestId(command),
        listPayload(QJsonArray {
            memoryItem(QStringLiteral("stale.md"), firstToken),
        })));
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.state(), kodosi::AgentMemoryModel::State::Ineligible);

    QVERIFY(model.inspect(QStringLiteral("session-1")));
    intelModel.ingestAgentIntelEvent(liveSet(
        {},
        QJsonArray {
            intelEntry(QStringLiteral("session-1"), secondIncarnation),
        },
        4));
    command = lastAgentIntelCommand(dispatcher);
    model.resetRuntimeAuthority();
    model.ingestAgentIntelEvent(reply(
        requestId(command),
        listPayload(QJsonArray {
            memoryItem(QStringLiteral("runtime-stale.md"), firstToken),
        })));
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.state(), kodosi::AgentMemoryModel::State::Dormant);

    model.ingestAuthEvent(auth(accountEpoch + 1, QStringLiteral("other")));
    model.ingestAgentIntelEvent(reply(
        requestId(command),
        listPayload(QJsonArray {
            memoryItem(QStringLiteral("account-stale.md"), firstToken),
        }),
        accountEpoch,
        QStringLiteral("account")));
    QCOMPARE(model.rowCount(), 0);
}

void AgentMemoryModelTest::
    malformedAndDuplicateListsAreRejectedAtomically()
{
    FakeMemoryDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::AgentSessionIntelModel intelModel(dispatcher, sessionsModel);
    kodosi::AgentMemoryModel model(dispatcher, sessionsModel, intelModel);
    QSignalSpy errors(&model, &kodosi::AgentMemoryModel::decodeError);
    activateSession(
        dispatcher,
        sessionsModel,
        intelModel,
        model,
        QJsonArray {localSession(QStringLiteral("session-1"), firstIncarnation)},
        QJsonArray {intelEntry(QStringLiteral("session-1"), firstIncarnation)});
    QVERIFY(model.inspect(QStringLiteral("session-1")));
    supplyList(
        dispatcher,
        model,
        QJsonArray {memoryItem(QStringLiteral("kept.md"), firstToken)});
    QCOMPARE(model.rowCount(), 1);

    QVERIFY(model.retry());
    auto command = lastAgentIntelCommand(dispatcher);
    model.ingestAgentIntelEvent(reply(
        requestId(command),
        listPayload(QJsonArray {
            memoryItem(QStringLiteral("a.md"), secondToken),
            memoryItem(QStringLiteral("b.md"), secondToken),
        })));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(
        model.data(model.index(0), kodosi::AgentMemoryModel::FilenameRole)
            .toString(),
        QStringLiteral("kept.md"));

    QVERIFY(model.retry());
    command = lastAgentIntelCommand(dispatcher);
    auto malformed =
        memoryItem(QStringLiteral("bad.md"), secondToken);
    malformed.remove(QStringLiteral("memoryType"));
    model.ingestAgentIntelEvent(reply(
        requestId(command),
        listPayload(QJsonArray {malformed})));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(errors.size(), 2);

    QVERIFY(model.retry());
    command = lastAgentIntelCommand(dispatcher);
    model.ingestAgentIntelEvent(reply(
        requestId(command),
        listPayload(QJsonArray {
            memoryItem(QStringLiteral("same.md"), firstToken),
            memoryItem(QStringLiteral("same.md"), secondToken),
        })));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(errors.size(), 3);
}

void AgentMemoryModelTest::enforcesTypeDescriptionAndContentBounds()
{
    FakeMemoryDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::AgentSessionIntelModel intelModel(dispatcher, sessionsModel);
    kodosi::AgentMemoryModel model(dispatcher, sessionsModel, intelModel);
    activateSession(
        dispatcher,
        sessionsModel,
        intelModel,
        model,
        QJsonArray {localSession(QStringLiteral("session-1"), firstIncarnation)},
        QJsonArray {intelEntry(QStringLiteral("session-1"), firstIncarnation)});
    QVERIFY(model.inspect(QStringLiteral("session-1")));
    supplyList(
        dispatcher,
        model,
        QJsonArray {
            memoryItem(
                QStringLiteral("kept.md"),
                firstToken,
                QStringLiteral("future-kind")),
        });
    QCOMPARE(
        model.data(model.index(0), kodosi::AgentMemoryModel::KindRole)
            .toString(),
        QStringLiteral("future-kind"));
    QCOMPARE(
        model.data(model.index(0), kodosi::AgentMemoryModel::DescriptionRole)
            .toString(),
        QStringLiteral("Claude memory"));

    QVERIFY(model.retry());
    auto command = lastAgentIntelCommand(dispatcher);
    model.ingestAgentIntelEvent(reply(
        requestId(command),
        listPayload(QJsonArray {
            memoryItem(
                QStringLiteral("too-long.md"),
                secondToken,
                QString(129, QLatin1Char('x'))),
        })));
    QCOMPARE(model.rowCount(), 1);

    QVERIFY(model.retry());
    supplyList(
        dispatcher,
        model,
        QJsonArray {
            memoryItem(QStringLiteral("large.md"), secondToken),
        });
    QVERIFY(model.select(QStringLiteral("large.md")));
    command = lastAgentIntelCommand(dispatcher);
    model.ingestAgentIntelEvent(reply(
        requestId(command),
        readPayload(
            QStringLiteral("large.md"),
            secondToken,
            QString(4 * 1024 * 1024 + 1, QLatin1Char('x')))));
    QCOMPARE(model.rowCount(), 1);
    QVERIFY(model.content().isEmpty());
    QVERIFY(model.contentError().contains(QStringLiteral("different selection")));
}

void AgentMemoryModelTest::timeoutAndRuntimeErrorPreserveInventory()
{
    FakeMemoryDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::AgentSessionIntelModel intelModel(dispatcher, sessionsModel);
    kodosi::AgentMemoryModel model(
        dispatcher,
        sessionsModel,
        intelModel,
        0);
    activateSession(
        dispatcher,
        sessionsModel,
        intelModel,
        model,
        QJsonArray {localSession(QStringLiteral("session-1"), firstIncarnation)},
        QJsonArray {intelEntry(QStringLiteral("session-1"), firstIncarnation)});
    QVERIFY(model.inspect(QStringLiteral("session-1")));
    supplyList(
        dispatcher,
        model,
        QJsonArray {memoryItem(QStringLiteral("fact.md"), firstToken)});
    QCOMPARE(model.rowCount(), 1);
    QVERIFY(model.select(QStringLiteral("fact.md")));
    QTRY_VERIFY_WITH_TIMEOUT(!model.contentLoading(), 500);
    QCOMPARE(model.rowCount(), 1);
    QVERIFY(model.contentError().contains(QStringLiteral("did not reply")));

    QVERIFY(model.retry());
    auto command = lastAgentIntelCommand(dispatcher);
    model.ingestAgentIntelEvent(runtimeError(
        requestId(command),
        QStringLiteral("cannot relist")));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.state(), kodosi::AgentMemoryModel::State::Failed);
    QVERIFY(!model.error().contains(QStringLiteral("cannot relist")));
}

void AgentMemoryModelTest::presentationAndQmlHidePrivateAuthority()
{
    FakeMemoryDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::AgentSessionIntelModel intelModel(dispatcher, sessionsModel);
    kodosi::AgentMemoryModel model(dispatcher, sessionsModel, intelModel);
    const auto roles = model.roleNames().values();
    QCOMPARE(roles.size(), 3);
    QVERIFY(roles.contains(QByteArrayLiteral("filename")));
    QVERIFY(roles.contains(QByteArrayLiteral("kind")));
    QVERIFY(roles.contains(QByteArrayLiteral("description")));
    for (const auto forbidden : {
             QByteArrayLiteral("selectionToken"),
             QByteArrayLiteral("canonicalCwd"),
             QByteArrayLiteral("projectSlug"),
             QByteArrayLiteral("requestId"),
             QByteArrayLiteral("accountEpoch"),
             QByteArrayLiteral("incarnationId"),
             QByteArrayLiteral("path"),
             QByteArrayLiteral("sizeBytes"),
         }) {
        QVERIFY(!roles.contains(forbidden));
        QVERIFY(model.metaObject()->indexOfProperty(forbidden.constData()) < 0);
    }

    QFile file(QStringLiteral(KODOSI_SOURCE_DIR)
        + QStringLiteral("/src/qml/AgentIntel/AgentIntelDrawer.qml"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto qml = file.readAll();
    for (const auto forbidden : {
             QByteArrayLiteral("selectionToken"),
             QByteArrayLiteral("canonicalCwd"),
             QByteArrayLiteral("projectSlug"),
             QByteArrayLiteral("requestId"),
             QByteArrayLiteral("accountEpoch"),
             QByteArrayLiteral("runtimeIncarnation"),
             QByteArrayLiteral("sourceFileBytes"),
             QByteArrayLiteral("sizeBytes"),
             QByteArrayLiteral("wireJson"),
         }) {
        QVERIFY2(!qml.contains(forbidden), forbidden.constData());
    }
    QVERIFY(qml.contains(
        QByteArrayLiteral("objectName: \"panel.agentIntel.tab.memory\"")));
    QVERIFY(qml.contains(
        QByteArrayLiteral(
            "objectName: \"panel.agentIntel.memory.preview.content\"")));
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
    QVERIFY(!qml.contains(QByteArrayLiteral("Text.AutoText")));
}

QTEST_GUILESS_MAIN(AgentMemoryModelTest)

#include "tst_agent_memory_model.moc"
