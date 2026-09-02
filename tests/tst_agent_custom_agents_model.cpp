#include "models/AgentCustomAgentsModel.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QUuid>
#include <QtTest/QTest>

#include <utility>

class FakeCustomAgentsDispatcher final : public kodosi::CommandDispatcher {
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

class AgentCustomAgentsModelTest final : public QObject {
    Q_OBJECT

private slots:
    void exactBoundCommandAndEligibility();
    void summarySelectionLoadsExactDetailOnce();
    void failedReadRelistsAndRetriesOnlyUniqueSummary();
    void malformedBoundsAndFencesFailClosed();
    void populatedGeometryModelAndPrivacy();
};

namespace {

constexpr auto accountEpoch = 7;
const auto firstIncarnation =
    QStringLiteral("01900000-0000-7000-8000-000000000011");
const auto secondIncarnation =
    QStringLiteral("01900000-0000-7000-8000-000000000012");
const auto authorityIncarnation =
    QStringLiteral("01900000-0000-7000-8000-000000000010");

QString uuidV7()
{
    return QUuid::createUuidV7().toString(QUuid::WithoutBraces);
}

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

QJsonObject summary(
    const QString& selectionToken,
    const QString& name,
    const QString& description = QStringLiteral("Description"),
    const QJsonValue& model = QStringLiteral("sonnet"),
    const QJsonArray& tools = QJsonArray {
        QStringLiteral("Read"),
        QStringLiteral("Grep"),
    },
    const int errorCount = 0)
{
    return {
        {QStringLiteral("selectionToken"), selectionToken},
        {QStringLiteral("target"), QStringLiteral("claude")},
        {QStringLiteral("name"), name},
        {QStringLiteral("description"), description},
        {QStringLiteral("model"), model},
        {QStringLiteral("tools"), tools},
        {QStringLiteral("errorCount"), errorCount},
    };
}

QJsonObject detail(
    const QString& selectionToken,
    const QString& name,
    const QString& description = QStringLiteral("Description"),
    const QString& frontmatter = QStringLiteral("name: agent\n"),
    const QString& prompt = QStringLiteral("System prompt"),
    const QJsonValue& model = QStringLiteral("sonnet"),
    const QJsonArray& tools = QJsonArray {QStringLiteral("Read")},
    const QJsonArray& errors = {})
{
    return {
        {QStringLiteral("selectionToken"), selectionToken},
        {QStringLiteral("target"), QStringLiteral("claude")},
        {QStringLiteral("name"), name},
        {QStringLiteral("description"), description},
        {QStringLiteral("model"), model},
        {QStringLiteral("tools"), tools},
        {QStringLiteral("disallowedTools"),
         QJsonArray {QStringLiteral("Bash")}},
        {QStringLiteral("frontmatter"), frontmatter},
        {QStringLiteral("prompt"), prompt},
        {QStringLiteral("errors"), errors},
    };
}

QByteArray reply(
    const QString& requestId,
    const QJsonValue& payload,
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

QByteArray runtimeError(const QString& requestId)
{
    return QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), QStringLiteral("account")},
        {QStringLiteral("accountEpoch"), accountEpoch},
        {QStringLiteral("type"), QStringLiteral("agent.intel.error")},
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("message"),
         QStringLiteral("sensitive runtime detail")},
    }).toJson(QJsonDocument::Compact);
}

QString requestId(const QJsonObject& command)
{
    return command.value(QStringLiteral("requestId")).toString();
}

QJsonObject lastAgentIntelCommand(
    const FakeCustomAgentsDispatcher& dispatcher)
{
    return dispatcher.commands(kodosi::CommandLane::AgentIntel).constLast();
}

void activateSession(
    FakeCustomAgentsDispatcher& dispatcher,
    kodosi::SessionCatalogModel& sessionsModel,
    kodosi::AgentSessionIntelModel& intelModel,
    kodosi::AgentCustomAgentsModel& customAgentsModel,
    const bool hydrateIntel = true)
{
    const auto authEvent = auth();
    sessionsModel.ingestAuthEvent(authEvent);
    sessionsModel.ingestSessionEvent(sessions(QJsonArray {
        localSession(QStringLiteral("session-1"), firstIncarnation),
    }));
    intelModel.ingestAuthEvent(authEvent);
    customAgentsModel.ingestAuthEvent(authEvent);
    if (!hydrateIntel) {
        return;
    }
    const auto systemCommands =
        dispatcher.commands(kodosi::CommandLane::System);
    intelModel.ingestAgentIntelEvent(liveSet(
        requestId(systemCommands.constLast()),
        QJsonArray {
            intelEntry(QStringLiteral("session-1"), firstIncarnation),
        }));
}

void supplyList(
    FakeCustomAgentsDispatcher& dispatcher,
    kodosi::AgentCustomAgentsModel& model,
    const QJsonArray& items,
    const quint64 epoch = accountEpoch)
{
    const auto command = lastAgentIntelCommand(dispatcher);
    model.ingestAgentIntelEvent(reply(
        requestId(command),
        QJsonObject {{QStringLiteral("items"), items}},
        epoch));
}

void supplyDetail(
    FakeCustomAgentsDispatcher& dispatcher,
    kodosi::AgentCustomAgentsModel& model,
    const QJsonObject& payload)
{
    const auto command = lastAgentIntelCommand(dispatcher);
    model.ingestAgentIntelEvent(reply(requestId(command), payload));
}

QString roleText(
    const kodosi::AgentCustomAgentsModel& model,
    const int row,
    const int role)
{
    return model.data(model.index(row), role).toString();
}

} // namespace

void AgentCustomAgentsModelTest::exactBoundCommandAndEligibility()
{
    FakeCustomAgentsDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::AgentSessionIntelModel intelModel(dispatcher, sessionsModel);
    kodosi::AgentCustomAgentsModel model(
        dispatcher,
        sessionsModel,
        intelModel);
    activateSession(
        dispatcher,
        sessionsModel,
        intelModel,
        model,
        false);

    QVERIFY(model.inspect(QStringLiteral("session-1")));
    QVERIFY(dispatcher.commands(kodosi::CommandLane::AgentIntel).isEmpty());
    QCOMPARE(model.state(), kodosi::AgentCustomAgentsModel::State::Waiting);

    const auto query =
        dispatcher.commands(kodosi::CommandLane::System).constLast();
    intelModel.ingestAgentIntelEvent(liveSet(
        requestId(query),
        QJsonArray {
            intelEntry(QStringLiteral("session-1"), firstIncarnation),
        }));
    const auto command = lastAgentIntelCommand(dispatcher);
    QCOMPARE(
        command,
        QJsonObject({
            {QStringLiteral("type"),
             QStringLiteral("agent.intel.listCustomAgentsBound")},
            {QStringLiteral("requestId"), requestId(command)},
            {QStringLiteral("directory"),
             QStringLiteral("/repo/.claude/agents")},
        }));
    supplyList(dispatcher, model, {});
    QCOMPARE(model.state(), kodosi::AgentCustomAgentsModel::State::Ready);

    intelModel.ingestAgentIntelEvent(liveSet(
        {},
        QJsonArray {
            intelEntry(
                QStringLiteral("session-1"),
                firstIncarnation,
                QStringLiteral("copilot")),
        },
        2));
    QCOMPARE(
        model.state(),
        kodosi::AgentCustomAgentsModel::State::Ineligible);
}

void AgentCustomAgentsModelTest::summarySelectionLoadsExactDetailOnce()
{
    FakeCustomAgentsDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::AgentSessionIntelModel intelModel(dispatcher, sessionsModel);
    kodosi::AgentCustomAgentsModel model(
        dispatcher,
        sessionsModel,
        intelModel);
    activateSession(dispatcher, sessionsModel, intelModel, model);
    QVERIFY(model.inspect(QStringLiteral("session-1")));
    const auto alphaSelection = uuidV7();
    const auto zetaSelection = uuidV7();
    supplyList(
        dispatcher,
        model,
        QJsonArray {
            summary(zetaSelection, QStringLiteral("Zeta")),
            summary(
                alphaSelection,
                QStringLiteral("Alpha"),
                QStringLiteral("Reads carefully"),
                QStringLiteral("opus"),
                QJsonArray {QStringLiteral("Read")},
                1),
        });

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(
        roleText(model, 0, kodosi::AgentCustomAgentsModel::NameRole),
        QStringLiteral("Alpha"));
    const auto presentationToken =
        roleText(model, 0, kodosi::AgentCustomAgentsModel::ItemTokenRole);
    const auto accessibleId =
        roleText(model, 0, kodosi::AgentCustomAgentsModel::AccessibleIdRole);
    QVERIFY(presentationToken != alphaSelection);
    QVERIFY(model.select(presentationToken));
    QVERIFY(model.detailLoading());
    auto command = lastAgentIntelCommand(dispatcher);
    QCOMPARE(
        command.value(QStringLiteral("type")).toString(),
        QStringLiteral("agent.intel.readCustomAgentBound"));
    QCOMPARE(
        command.value(QStringLiteral("selectionToken")).toString(),
        alphaSelection);
    QVERIFY(!command.contains(QStringLiteral("path")));
    QVERIFY(!command.contains(QStringLiteral("directory")));

    supplyDetail(
        dispatcher,
        model,
        detail(
            alphaSelection,
            QStringLiteral("Alpha"),
            QStringLiteral("Exact description"),
            QStringLiteral("name: Alpha\ntools: [Read]\n"),
            QStringLiteral("Investigate exactly."),
            QStringLiteral("opus"),
            QJsonArray {QStringLiteral("Read")},
            QJsonArray {QStringLiteral("missing field")}));
    QVERIFY(model.detailLoaded());
    QVERIFY(!model.detailLoading());
    QCOMPARE(model.selectedDescription(), QStringLiteral("Exact description"));
    QCOMPARE(model.systemPrompt(), QStringLiteral("Investigate exactly."));
    QCOMPARE(model.detailErrors(), QStringLiteral("missing field"));

    const auto commandCount =
        dispatcher.commands(kodosi::CommandLane::AgentIntel).size();
    QVERIFY(model.select(presentationToken));
    QCOMPARE(
        dispatcher.commands(kodosi::CommandLane::AgentIntel).size(),
        commandCount);

    QVERIFY(model.retry());
    const auto refreshedSelection = uuidV7();
    supplyList(
        dispatcher,
        model,
        QJsonArray {
            summary(
                refreshedSelection,
                QStringLiteral("Alpha"),
                QStringLiteral("Reads carefully"),
                QStringLiteral("opus"),
                QJsonArray {QStringLiteral("Read")},
                1),
        });
    QCOMPARE(model.selectedItemToken(), presentationToken);
    QCOMPARE(
        roleText(
            model,
            0,
            kodosi::AgentCustomAgentsModel::AccessibleIdRole),
        accessibleId);
    command = lastAgentIntelCommand(dispatcher);
    QCOMPARE(
        command.value(QStringLiteral("selectionToken")).toString(),
        refreshedSelection);

    model.close();
    QVERIFY(model.inspect(QStringLiteral("session-1")));
    supplyList(
        dispatcher,
        model,
        QJsonArray {
            summary(
                uuidV7(),
                QStringLiteral("Alpha"),
                QStringLiteral("Changed metadata"),
                QStringLiteral("sonnet"),
                QJsonArray {QStringLiteral("Read"), QStringLiteral("Grep")},
                0),
            summary(
                uuidV7(),
                QStringLiteral("Alpha"),
                QStringLiteral("Second file with the same agent name")),
        });
    QSet<QString> accessibleIds;
    for (auto row = 0; row < model.rowCount(); ++row) {
        const auto id = roleText(
            model,
            row,
            kodosi::AgentCustomAgentsModel::AccessibleIdRole);
        QVERIFY(!id.contains(QStringLiteral("Alpha")));
        QVERIFY(!id.contains(QStringLiteral("/")));
        QCOMPARE(id.size(), 64);
        accessibleIds.insert(id);
    }
    QCOMPARE(accessibleIds.size(), 2);
}

void AgentCustomAgentsModelTest::
    failedReadRelistsAndRetriesOnlyUniqueSummary()
{
    FakeCustomAgentsDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::AgentSessionIntelModel intelModel(dispatcher, sessionsModel);
    kodosi::AgentCustomAgentsModel model(
        dispatcher,
        sessionsModel,
        intelModel);
    activateSession(dispatcher, sessionsModel, intelModel, model);
    QVERIFY(model.inspect(QStringLiteral("session-1")));
    supplyList(
        dispatcher,
        model,
        QJsonArray {
            summary(uuidV7(), QStringLiteral("Reviewer")),
        });
    const auto presentationToken =
        roleText(model, 0, kodosi::AgentCustomAgentsModel::ItemTokenRole);
    QVERIFY(model.select(presentationToken));
    auto command = lastAgentIntelCommand(dispatcher);
    model.ingestAgentIntelEvent(runtimeError(requestId(command)));
    QVERIFY(!model.detailError().isEmpty());
    QVERIFY(!model.detailError().contains(QStringLiteral("sensitive")));

    QVERIFY(model.retry());
    const auto retrySelection = uuidV7();
    supplyList(
        dispatcher,
        model,
        QJsonArray {
            summary(retrySelection, QStringLiteral("Reviewer")),
        });
    QCOMPARE(model.selectedItemToken(), presentationToken);
    command = lastAgentIntelCommand(dispatcher);
    QCOMPARE(
        command.value(QStringLiteral("selectionToken")).toString(),
        retrySelection);

    supplyDetail(
        dispatcher,
        model,
        detail(retrySelection, QStringLiteral("Reviewer")));
    QVERIFY(model.retry());
    supplyList(
        dispatcher,
        model,
        QJsonArray {
            summary(uuidV7(), QStringLiteral("Reviewer")),
            summary(uuidV7(), QStringLiteral("Reviewer")),
        });
    QVERIFY(model.selectedItemToken().isEmpty());
    QCOMPARE(model.rowCount(), 2);
}

void AgentCustomAgentsModelTest::malformedBoundsAndFencesFailClosed()
{
    FakeCustomAgentsDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::AgentSessionIntelModel intelModel(dispatcher, sessionsModel);
    kodosi::AgentCustomAgentsModel model(
        dispatcher,
        sessionsModel,
        intelModel);
    QSignalSpy decodeErrors(
        &model,
        &kodosi::AgentCustomAgentsModel::decodeError);
    activateSession(dispatcher, sessionsModel, intelModel, model);
    QVERIFY(model.inspect(QStringLiteral("session-1")));
    supplyList(
        dispatcher,
        model,
        QJsonArray {
            summary(uuidV7(), QStringLiteral("Kept")),
        });
    QCOMPARE(model.rowCount(), 1);

    const auto rejectList = [&](QJsonArray items) {
        QVERIFY(model.retry());
        supplyList(dispatcher, model, items);
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(
            model.state(),
            kodosi::AgentCustomAgentsModel::State::Failed);
    };

    auto withPath = summary(uuidV7(), QStringLiteral("Unsafe"));
    withPath.insert(
        QStringLiteral("path"),
        QStringLiteral("/repo/.claude/agents/unsafe.md"));
    rejectList({withPath});

    auto missingModel = summary(uuidV7(), QStringLiteral("Missing"));
    missingModel.remove(QStringLiteral("model"));
    rejectList({missingModel});

    const auto duplicateToken = uuidV7();
    rejectList({
        summary(duplicateToken, QStringLiteral("One")),
        summary(duplicateToken, QStringLiteral("Two")),
    });

    QJsonArray tooMany;
    for (int index = 0; index < 129; ++index) {
        tooMany.push_back(summary(
            uuidV7(),
            QStringLiteral("Agent %1").arg(index),
            {}));
    }
    rejectList(std::move(tooMany));

    QJsonArray aggregate;
    for (int index = 0; index < 128; ++index) {
        aggregate.push_back(summary(
            uuidV7(),
            QStringLiteral("Agent %1").arg(index),
            QString(2 * 1024, QLatin1Char('d'))));
    }
    rejectList(std::move(aggregate));

    QVERIFY(model.retry());
    supplyList(
        dispatcher,
        model,
        QJsonArray {
            summary(uuidV7(), QStringLiteral("Current")),
        },
        accountEpoch - 1);
    QVERIFY(model.loading());
    supplyList(
        dispatcher,
        model,
        QJsonArray {
            summary(uuidV7(), QStringLiteral("Current")),
        });
    const auto token =
        roleText(model, 0, kodosi::AgentCustomAgentsModel::ItemTokenRole);
    QVERIFY(model.select(token));
    auto command = lastAgentIntelCommand(dispatcher);
    auto wrongDetail = detail(uuidV7(), QStringLiteral("Current"));
    model.ingestAgentIntelEvent(reply(requestId(command), wrongDetail));
    QVERIFY(!model.detailError().isEmpty());

    QVERIFY(model.retry());
    supplyList(
        dispatcher,
        model,
        QJsonArray {
            summary(uuidV7(), QStringLiteral("Current")),
        });
    command = lastAgentIntelCommand(dispatcher);
    sessionsModel.ingestSessionEvent(upsertSession(
        localSession(QStringLiteral("session-1"), secondIncarnation)));
    model.ingestAgentIntelEvent(reply(
        requestId(command),
        detail(
            command.value(QStringLiteral("selectionToken")).toString(),
            QStringLiteral("Current"))));
    QVERIFY(model.selectedItemToken().isEmpty());
    QCOMPARE(
        model.state(),
        kodosi::AgentCustomAgentsModel::State::Ineligible);
    QVERIFY(decodeErrors.size() >= 6);
}

void AgentCustomAgentsModelTest::populatedGeometryModelAndPrivacy()
{
    FakeCustomAgentsDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::AgentSessionIntelModel intelModel(dispatcher, sessionsModel);
    kodosi::AgentCustomAgentsModel model(
        dispatcher,
        sessionsModel,
        intelModel);
    activateSession(dispatcher, sessionsModel, intelModel, model);
    QVERIFY(model.inspect(QStringLiteral("session-1")));
    const auto runtimeToken = uuidV7();
    supplyList(
        dispatcher,
        model,
        QJsonArray {
            summary(
                runtimeToken,
                QStringLiteral("Populated"),
                QString(2 * 1024, QLatin1Char('d')),
                QStringLiteral("sonnet"),
                QJsonArray {
                    QStringLiteral("Read"),
                    QStringLiteral("Grep"),
                }),
        });
    const auto presentationToken =
        roleText(model, 0, kodosi::AgentCustomAgentsModel::ItemTokenRole);
    QVERIFY(presentationToken != runtimeToken);
    QVERIFY(model.select(presentationToken));
    supplyDetail(
        dispatcher,
        model,
        detail(
            runtimeToken,
            QStringLiteral("Populated"),
            QString(32 * 1024, QLatin1Char('d')),
            {},
            QString(1024 * 1024, QLatin1Char('\\'))));
    QVERIFY(model.detailLoaded());
    QVERIFY(model.frontmatter().isEmpty());
    QCOMPARE(model.systemPrompt().size(), 1024 * 1024);
    QCOMPARE(
        model.selectedDisallowedTools(),
        QStringList {QStringLiteral("Bash")});

    const auto roles = model.roleNames().values();
    QCOMPARE(roles.size(), 7);
    QVERIFY(roles.contains(QByteArrayLiteral("accessibleId")));
    const auto accessibleId = roleText(
        model,
        0,
        kodosi::AgentCustomAgentsModel::AccessibleIdRole);
    QCOMPARE(accessibleId.size(), 64);
    for (const auto forbidden : {
             QByteArrayLiteral("selectionToken"),
             QByteArrayLiteral("path"),
             QByteArrayLiteral("sourcePath"),
             QByteArrayLiteral("directory"),
             QByteArrayLiteral("target"),
             QByteArrayLiteral("wireJson"),
         }) {
        QVERIFY(!roles.contains(forbidden));
        QVERIFY(model.metaObject()->indexOfProperty(forbidden.constData()) < 0);
    }

    QFile surface(QStringLiteral(KODOSI_SOURCE_DIR)
        + QStringLiteral(
            "/src/qml/AgentIntel/AgentCustomAgentsSurface.qml"));
    QVERIFY(surface.open(QIODevice::ReadOnly));
    const auto qml = surface.readAll();
    for (const auto forbidden : {
             QByteArrayLiteral("selectionToken"),
             QByteArrayLiteral("sourcePath"),
             QByteArrayLiteral("wireJson"),
         }) {
        QVERIFY2(!qml.contains(forbidden), forbidden.constData());
    }
    const auto scroll =
        qml.indexOf("objectName: \"panel.agentIntel.agents.detail.scroll\"");
    const auto description =
        qml.indexOf("objectName: \"panel.agentIntel.agents.detail.description\"");
    const auto metadata =
        qml.indexOf("objectName: \"panel.agentIntel.agents.detail.metadata\"");
    const auto frontmatter =
        qml.indexOf("objectName: \"panel.agentIntel.agents.detail.frontmatter\"");
    QVERIFY(scroll >= 0);
    QVERIFY(description > scroll);
    QVERIFY(metadata > scroll);
    QVERIFY(frontmatter > scroll);
    QVERIFY(qml.contains(QByteArrayLiteral("Layout.fillHeight: true")));
    QVERIFY(qml.contains(
        QByteArrayLiteral("visible: Models.AgentCustomAgents.detailLoaded")));
    const auto delegateStart = qml.indexOf("delegate: KButton {");
    const auto delegateEnd = qml.indexOf("background: Rectangle {", delegateStart);
    QVERIFY(delegateStart >= 0);
    QVERIFY(delegateEnd > delegateStart);
    const auto delegate = qml.mid(delegateStart, delegateEnd - delegateStart);
    QVERIFY(delegate.contains(
        QByteArrayLiteral("&& !Models.AgentCustomAgents.detailLoading")));
    QVERIFY(qml.contains(
        QByteArrayLiteral(
            "objectName: \"panel.agentIntel.agents.detail.retry\"")));
}

QTEST_GUILESS_MAIN(AgentCustomAgentsModelTest)

#include "tst_agent_custom_agents_model.moc"
