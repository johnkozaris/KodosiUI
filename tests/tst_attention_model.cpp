#include "models/AgentSessionIntelModel.hpp"
#include "models/AttentionModel.hpp"
#include "models/PendingPermissionsModel.hpp"
#include "models/SessionActions.hpp"
#include "models/SessionCatalogModel.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QSignalSpy>
#include <QtTest/QTest>

class FakeAttentionDispatcher final : public kodosi::CommandDispatcher {
public:
    QVector<QJsonObject> commands;
    QSet<QString> rejectedToolUseIds;
    QSet<QString> rejectedTypes;

    Result send(
        const kodosi::CommandLane lane,
        const QByteArrayView json) override
    {
        if (lane != kodosi::CommandLane::System
            && lane != kodosi::CommandLane::Sessions) {
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
        const auto command = document.object();
        commands.push_back(command);
        if (rejectedTypes.contains(
                command.value(QStringLiteral("type")).toString())
            || rejectedToolUseIds.contains(
                command.value(QStringLiteral("toolUseId")).toString())) {
            return std::unexpected(kodosi::RuntimeFailure {
                .code = kodosi::RuntimeFailure::Code::FfiRejected,
                .ffiResult = -1,
                .message = QStringLiteral("Rejected"),
            });
        }
        return {};
    }
};

class AttentionModelTest final : public QObject {
    Q_OBJECT

private slots:
    void ordersApprovalsAndOtherAttention();
    void deduplicatesPermissionsAndExcludesRemoteSessions();
    void bulkApprovesOnlySafeReadsAndPreservesPartialFailures();
    void rejectsStaleTokensAndIncarnationReplacement();
    void fencesAgentTokensByExactRevision();
    void followsSourceResetsAndDataChanges();
    void exposesOnlyPresentationRoles();
    void enforcesOneTapApprovalPolicy();
    void usesAttentionThemeControls();
    void sessionListFailureSurfacesInAttention();
    void combinesAgentIntelAuthority();
    void deduplicatesRefreshAndClearsResetErrors();
    void reportsAuthorityAndExactNavigation();
};

namespace {

constexpr auto accountEpoch = 9;
const auto authorityIncarnation =
    QStringLiteral("01900000-0000-7000-8000-000000000090");
const auto incarnation1 =
    QStringLiteral("01900000-0000-7000-8000-000000000091");
const auto incarnation2 =
    QStringLiteral("01900000-0000-7000-8000-000000000092");
const auto incarnation3 =
    QStringLiteral("01900000-0000-7000-8000-000000000093");
const auto incarnation4 =
    QStringLiteral("01900000-0000-7000-8000-000000000094");
const auto incarnation5 =
    QStringLiteral("01900000-0000-7000-8000-000000000095");

struct Fixture {
    FakeAttentionDispatcher dispatcher;
    kodosi::SessionCatalogModel sessions;
    kodosi::SessionActions sessionActions {dispatcher, sessions};
    kodosi::AgentSessionIntelModel intel {dispatcher, sessions};
    kodosi::PendingPermissionsModel permissions {dispatcher, sessions};
    kodosi::AttentionModel attention {
        permissions,
        intel,
        sessions,
        sessionActions,
    };
};

QString lastRequestId(
    const FakeAttentionDispatcher& dispatcher,
    const QString& type);

QByteArray auth(
    const QString& userId = QStringLiteral("account"),
    const bool authenticated = true)
{
    QJsonObject object {
        {QStringLiteral("type"),
         authenticated ? QStringLiteral("auth.ready")
                       : QStringLiteral("auth.required")},
        {QStringLiteral("accountEpoch"), accountEpoch},
    };
    if (authenticated) {
        object.insert(QStringLiteral("userId"), userId);
    }
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QJsonObject session(
    const QString& id,
    const QString& incarnation,
    const QString& status = QStringLiteral("active"),
    const QString& name = {},
    const QString& kind = QStringLiteral("local"),
    const QString& connectionState = {},
    const QString& accessState = {})
{
    QJsonObject object {
        {QStringLiteral("kind"), kind},
        {QStringLiteral("id"), id},
        {QStringLiteral("incarnationId"), incarnation},
        {QStringLiteral("name"), name.isEmpty() ? id : name},
        {QStringLiteral("project"), QStringLiteral("/repo/") + id},
        {QStringLiteral("mode"), QStringLiteral("normal")},
        {QStringLiteral("status"), status},
        {QStringLiteral("recovery"), QStringLiteral("live")},
        {QStringLiteral("scope"), QStringLiteral("justMe")},
        {QStringLiteral("access"), QStringLiteral("approve")},
    };
    if (kind == QStringLiteral("remote")) {
        object.insert(QStringLiteral("connectionState"), connectionState);
        object.insert(QStringLiteral("accessState"), accessState);
        object.insert(QStringLiteral("permissions"), 0x1ff);
    }
    return object;
}

QByteArray sessionList(
    const QJsonArray& values,
    const QString& userId = QStringLiteral("account"))
{
    return QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), userId},
        {QStringLiteral("accountEpoch"), accountEpoch},
        {QStringLiteral("type"), QStringLiteral("session.list")},
        {QStringLiteral("sessions"), values},
    }).toJson(QJsonDocument::Compact);
}

void activate(
    Fixture& fixture,
    const QJsonArray& sessions,
    const bool authenticated = true)
{
    const auto authEvent = auth(QStringLiteral("account"), authenticated);
    fixture.sessions.ingestAuthEvent(authEvent);
    fixture.sessionActions.ingestAuthEvent(authEvent);
    fixture.sessions.ingestSessionEvent(sessionList(
        sessions,
        authenticated ? QStringLiteral("account") : QString {}));
    fixture.intel.ingestAuthEvent(authEvent);
    fixture.intel.ingestAgentIntelEvent(
        QJsonDocument(QJsonObject {
            {QStringLiteral("authority"),
             QStringLiteral("accountContext")},
            {QStringLiteral("accountUserId"),
             authenticated
                 ? QJsonValue(QStringLiteral("account"))
                 : QJsonValue(QJsonValue::Null)},
            {QStringLiteral("accountEpoch"), accountEpoch},
            {QStringLiteral("type"),
             QStringLiteral("agent.intel.liveSet")},
            {QStringLiteral("requestId"),
             lastRequestId(
                 fixture.dispatcher,
                 QStringLiteral("agent.intel.queryLiveSet"))},
            {QStringLiteral("authorityIncarnationId"),
             authorityIncarnation},
            {QStringLiteral("revision"), 0},
            {QStringLiteral("entries"), QJsonArray {}},
        }).toJson(QJsonDocument::Compact));
    fixture.permissions.ingestAuthEvent(authEvent);
}

QJsonObject request(
    const QString& sessionId,
    const QString& incarnation,
    const QString& toolUseId,
    const QString& risk,
    const qint64 createdAtMs,
    const quint64 generation = 1,
    const QString& phase = QStringLiteral("actionable"))
{
    return {
        {QStringLiteral("sessionId"), sessionId},
        {QStringLiteral("sessionIncarnationId"), incarnation},
        {QStringLiteral("requestGeneration"),
         static_cast<qint64>(generation)},
        {QStringLiteral("toolUseId"), toolUseId},
        {QStringLiteral("toolName"), QStringLiteral("Bash")},
        {QStringLiteral("toolInput"),
         QJsonObject {
             {QStringLiteral("command"),
              QStringLiteral("echo ") + toolUseId},
         }},
        {QStringLiteral("createdAtMs"), static_cast<double>(createdAtMs)},
        {QStringLiteral("deadlineAtMs"), 1'900'000'000'000.0},
        {QStringLiteral("risk"), risk},
        {QStringLiteral("decisionPhase"), phase},
    };
}

QByteArray permissionsSnapshot(
    const quint64 generation,
    const QJsonArray& requests)
{
    return QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), QStringLiteral("account")},
        {QStringLiteral("accountEpoch"), accountEpoch},
        {QStringLiteral("type"),
         QStringLiteral("agent.intel.pendingPermissionsSnapshot")},
        {QStringLiteral("generation"), static_cast<qint64>(generation)},
        {QStringLiteral("requests"), requests},
    }).toJson(QJsonDocument::Compact);
}

QJsonObject intelEntry(
    const QString& sessionId,
    const QString& incarnation,
    const QString& kind,
    const QString& summary,
    const QString& lastProgressAt = QStringLiteral("2026-09-01T20:00:00Z"))
{
    return {
        {QStringLiteral("sessionId"), sessionId},
        {QStringLiteral("sessionIncarnationId"), incarnation},
        {QStringLiteral("snapshot"),
         QJsonObject {
             {QStringLiteral("identity"),
              QJsonObject {
                  {QStringLiteral("agentType"), QStringLiteral("claude")},
                  {QStringLiteral("version"), QJsonValue::Null},
                  {QStringLiteral("model"), QJsonValue::Null},
                  {QStringLiteral("title"), QJsonValue::Null},
                  {QStringLiteral("cwd"), QStringLiteral("/repo")},
                  {QStringLiteral("vendorSessionId"), QJsonValue::Null},
                  {QStringLiteral("processId"), QJsonValue::Null},
              }},
             {QStringLiteral("lifecycle"), QStringLiteral("waiting")},
             {QStringLiteral("attention"),
              QJsonObject {
                  {QStringLiteral("kind"), kind},
                  {QStringLiteral("summary"), summary},
                  {QStringLiteral("actionable"), true},
              }},
             {QStringLiteral("pendingInteraction"), QJsonValue::Null},
             {QStringLiteral("currentActivity"),
              QJsonObject {
                  {QStringLiteral("summary"), QStringLiteral("Waiting")},
                  {QStringLiteral("lastProgressAt"), lastProgressAt},
              }},
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
         }},
    };
}

QString lastRequestId(
    const FakeAttentionDispatcher& dispatcher,
    const QString& type)
{
    for (auto iterator = dispatcher.commands.crbegin();
         iterator != dispatcher.commands.crend();
         ++iterator) {
        if (iterator->value(QStringLiteral("type")).toString() == type) {
            return iterator->value(QStringLiteral("requestId")).toString();
        }
    }
    return {};
}

QByteArray liveSet(
    const quint64 revision,
    const QJsonArray& entries,
    const QString& requestId = {})
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

kodosi::AttentionModel::Category categoryAt(
    const kodosi::AttentionModel& model,
    const int row)
{
    return model.data(
        model.index(row),
        kodosi::AttentionModel::CategoryRole)
        .value<kodosi::AttentionModel::Category>();
}

kodosi::AttentionModel::Risk riskAt(
    const kodosi::AttentionModel& model,
    const int row)
{
    return model.data(
        model.index(row),
        kodosi::AttentionModel::RiskRole)
        .value<kodosi::AttentionModel::Risk>();
}

QString stableIdAt(const kodosi::AttentionModel& model, const int row)
{
    return model.data(
        model.index(row),
        kodosi::AttentionModel::StableIdRole)
        .toString();
}

QString tokenAt(const kodosi::AttentionModel& model, const int row)
{
    return model.data(
        model.index(row),
        kodosi::AttentionModel::AttentionTokenRole)
        .toString();
}

int rowFor(
    const kodosi::AttentionModel& model,
    const kodosi::AttentionModel::Category category,
    const QString& sessionId = {})
{
    for (auto row = 0; row < model.rowCount(); ++row) {
        if (categoryAt(model, row) != category) {
            continue;
        }
        if (sessionId.isEmpty()
            || model.data(
                    model.index(row),
                    kodosi::AttentionModel::SessionIdRole)
                    .toString()
                == sessionId) {
            return row;
        }
    }
    return -1;
}

QVector<QJsonObject> commandsOfType(
    const FakeAttentionDispatcher& dispatcher,
    const QString& type)
{
    QVector<QJsonObject> result;
    for (const auto& command : dispatcher.commands) {
        if (command.value(QStringLiteral("type")).toString() == type) {
            result.push_back(command);
        }
    }
    return result;
}

} // namespace

void AttentionModelTest::ordersApprovalsAndOtherAttention()
{
    Fixture fixture;
    activate(
        fixture,
        QJsonArray {
            session(QStringLiteral("s1"), incarnation1),
        });
    fixture.permissions.ingestAgentIntelEvent(permissionsSnapshot(
        1,
        QJsonArray {
            request(
                QStringLiteral("s1"),
                incarnation1,
                QStringLiteral("network-new"),
                QStringLiteral("network"),
                1'700'000'000'200),
            request(
                QStringLiteral("s1"),
                incarnation1,
                QStringLiteral("safe-a"),
                QStringLiteral("safe"),
                1'700'000'000'100),
            request(
                QStringLiteral("s1"),
                incarnation1,
                QStringLiteral("destructive"),
                QStringLiteral("destructive"),
                1'700'000'000'400),
            request(
                QStringLiteral("s1"),
                incarnation1,
                QStringLiteral("credential"),
                QStringLiteral("credential"),
                1'700'000'000'300),
            request(
                QStringLiteral("s1"),
                incarnation1,
                QStringLiteral("network-old"),
                QStringLiteral("network"),
                1'700'000'000'000),
            request(
                QStringLiteral("s1"),
                incarnation1,
                QStringLiteral("unknown"),
                QStringLiteral("unknown"),
                1'700'000'000'500),
            request(
                QStringLiteral("s1"),
                incarnation1,
                QStringLiteral("safe-b"),
                QStringLiteral("safe"),
                1'700'000'000'600),
        }));

    QCOMPARE(fixture.attention.rowCount(), 6);
    QCOMPARE(
        riskAt(fixture.attention, 0),
        kodosi::AttentionModel::Risk::Destructive);
    QCOMPARE(
        riskAt(fixture.attention, 1),
        kodosi::AttentionModel::Risk::Credential);
    QCOMPARE(
        fixture.attention.data(
            fixture.attention.index(2),
            kodosi::AttentionModel::SummaryRole).toString(),
        QStringLiteral("echo network-old"));
    QCOMPARE(
        fixture.attention.data(
            fixture.attention.index(3),
            kodosi::AttentionModel::SummaryRole).toString(),
        QStringLiteral("echo network-new"));
    QCOMPARE(
        riskAt(fixture.attention, 4),
        kodosi::AttentionModel::Risk::Unknown);
    QCOMPARE(
        categoryAt(fixture.attention, 5),
        kodosi::AttentionModel::Category::BulkSafe);

    activate(
        fixture,
        QJsonArray {
            session(
                QStringLiteral("blocked"),
                incarnation1,
                QStringLiteral("blocked")),
            session(
                QStringLiteral("question-old"),
                incarnation2),
            session(
                QStringLiteral("question-new"),
                incarnation3),
            session(
                QStringLiteral("review"),
                incarnation4),
            session(
                QStringLiteral("waiting"),
                incarnation5,
                QStringLiteral("waiting")),
        });
    fixture.permissions.ingestAgentIntelEvent(
        permissionsSnapshot(2, {}));
    const auto requestId = lastRequestId(
        fixture.dispatcher,
        QStringLiteral("agent.intel.queryLiveSet"));
    fixture.intel.ingestAgentIntelEvent(liveSet(
        1,
        QJsonArray {
            intelEntry(
                QStringLiteral("question-old"),
                incarnation2,
                QStringLiteral("question"),
                QStringLiteral("Older"),
                QStringLiteral("2026-09-01T19:00:00Z")),
            intelEntry(
                QStringLiteral("question-new"),
                incarnation3,
                QStringLiteral("question"),
                QStringLiteral("Newer"),
                QStringLiteral("2026-09-01T21:00:00Z")),
            intelEntry(
                QStringLiteral("review"),
                incarnation4,
                QStringLiteral("review"),
                QStringLiteral("Review")),
        },
        requestId));

    QCOMPARE(fixture.attention.rowCount(), 5);
    QCOMPARE(
        fixture.attention.data(
            fixture.attention.index(0),
            kodosi::AttentionModel::SessionIdRole).toString(),
        QStringLiteral("question-new"));
    QCOMPARE(
        fixture.attention.data(
            fixture.attention.index(1),
            kodosi::AttentionModel::SessionIdRole).toString(),
        QStringLiteral("question-old"));
    QCOMPARE(
        categoryAt(fixture.attention, 2),
        kodosi::AttentionModel::Category::Blocked);
    QCOMPARE(
        categoryAt(fixture.attention, 3),
        kodosi::AttentionModel::Category::Waiting);
    QCOMPARE(
        fixture.attention.data(
            fixture.attention.index(4),
            kodosi::AttentionModel::SessionIdRole).toString(),
        QStringLiteral("review"));
}

void AttentionModelTest::deduplicatesPermissionsAndExcludesRemoteSessions()
{
    Fixture fixture;
    activate(
        fixture,
        QJsonArray {
            session(QStringLiteral("approval"), incarnation1),
            session(QStringLiteral("failure"), incarnation2),
            session(
                QStringLiteral("offline"),
                incarnation3,
                QStringLiteral("blocked"),
                {},
                QStringLiteral("remote"),
                QStringLiteral("offline"),
                QStringLiteral("ready")),
            session(
                QStringLiteral("denied"),
                incarnation4,
                QStringLiteral("active"),
                {},
                QStringLiteral("remote"),
                QStringLiteral("connected"),
                QStringLiteral("accessDenied")),
            session(
                QStringLiteral("connected"),
                incarnation5,
                QStringLiteral("active"),
                {},
                QStringLiteral("remote"),
                QStringLiteral("connected"),
                QStringLiteral("ready")),
        });
    fixture.permissions.ingestAgentIntelEvent(permissionsSnapshot(
        1,
        QJsonArray {
            request(
                QStringLiteral("approval"),
                incarnation1,
                QStringLiteral("tool"),
                QStringLiteral("network"),
                1'700'000'000'000),
        }));
    fixture.intel.ingestAgentIntelEvent(liveSet(
        1,
        QJsonArray {
            intelEntry(
                QStringLiteral("approval"),
                incarnation1,
                QStringLiteral("permission"),
                QStringLiteral("Duplicate permission")),
            intelEntry(
                QStringLiteral("failure"),
                incarnation2,
                QStringLiteral("failure"),
                QStringLiteral("Failed")),
            intelEntry(
                QStringLiteral("offline"),
                incarnation3,
                QStringLiteral("failure"),
                QStringLiteral("Offline")),
            intelEntry(
                QStringLiteral("denied"),
                incarnation4,
                QStringLiteral("failure"),
                QStringLiteral("Denied")),
            intelEntry(
                QStringLiteral("connected"),
                incarnation5,
                QStringLiteral("question"),
                QStringLiteral("Connected")),
        },
        lastRequestId(
            fixture.dispatcher,
            QStringLiteral("agent.intel.queryLiveSet"))));

    QCOMPARE(fixture.attention.rowCount(), 3);
    QVERIFY(
        rowFor(
            fixture.attention,
            kodosi::AttentionModel::Category::Approval,
            QStringLiteral("approval"))
        >= 0);
    QVERIFY(
        rowFor(
            fixture.attention,
            kodosi::AttentionModel::Category::Agent,
            QStringLiteral("approval"))
        < 0);
    QVERIFY(
        rowFor(
            fixture.attention,
            kodosi::AttentionModel::Category::Agent,
            QStringLiteral("failure"))
        >= 0);
    QVERIFY(
        rowFor(
            fixture.attention,
            kodosi::AttentionModel::Category::Agent,
            QStringLiteral("connected"))
        >= 0);
    QVERIFY(
        rowFor(
            fixture.attention,
            kodosi::AttentionModel::Category::Agent,
            QStringLiteral("offline"))
        < 0);
    QVERIFY(
        rowFor(
            fixture.attention,
            kodosi::AttentionModel::Category::Agent,
            QStringLiteral("denied"))
        < 0);
}

void AttentionModelTest::bulkApprovesOnlySafeReadsAndPreservesPartialFailures()
{
    Fixture fixture;
    activate(
        fixture,
        QJsonArray {
            session(QStringLiteral("s1"), incarnation1),
        });
    fixture.permissions.ingestAgentIntelEvent(permissionsSnapshot(
        1,
        QJsonArray {
            request(
                QStringLiteral("s1"),
                incarnation1,
                QStringLiteral("safe-a"),
                QStringLiteral("safe"),
                1'700'000'000'000),
            request(
                QStringLiteral("s1"),
                incarnation1,
                QStringLiteral("safe-b"),
                QStringLiteral("safe"),
                1'700'000'000'001),
            request(
                QStringLiteral("s1"),
                incarnation1,
                QStringLiteral("network"),
                QStringLiteral("network"),
                1'700'000'000'002),
            request(
                QStringLiteral("s1"),
                incarnation1,
                QStringLiteral("destructive"),
                QStringLiteral("destructive"),
                1'700'000'000'003),
            request(
                QStringLiteral("s1"),
                incarnation1,
                QStringLiteral("credential"),
                QStringLiteral("credential"),
                1'700'000'000'004),
            request(
                QStringLiteral("s1"),
                incarnation1,
                QStringLiteral("unknown"),
                QStringLiteral("unknown"),
                1'700'000'000'005),
        }));
    const auto bulkRow = rowFor(
        fixture.attention,
        kodosi::AttentionModel::Category::BulkSafe);
    QVERIFY(bulkRow >= 0);
    const auto bulkToken = tokenAt(fixture.attention, bulkRow);
    fixture.dispatcher.commands.clear();
    fixture.dispatcher.rejectedToolUseIds.insert(QStringLiteral("safe-b"));

    QVERIFY(!fixture.attention.approveAll(bulkToken));
    const auto decisions = commandsOfType(
        fixture.dispatcher,
        QStringLiteral("agent.intel.allowPendingPermissionRequest"));
    QCOMPARE(decisions.size(), 2);
    QSet<QString> decidedTools;
    for (const auto& decision : decisions) {
        decidedTools.insert(
            decision.value(QStringLiteral("toolUseId")).toString());
    }
    const QSet<QString> expectedTools {
        QStringLiteral("safe-a"),
        QStringLiteral("safe-b"),
    };
    QCOMPARE(decidedTools, expectedTools);
    QVERIFY(!fixture.attention.operationError().isEmpty());

    bool safeAActionable = true;
    bool safeBActionable = false;
    for (auto row = 0; row < fixture.permissions.rowCount(); ++row) {
        const auto tool = fixture.permissions.data(
            fixture.permissions.index(row),
            kodosi::PendingPermissionsModel::ToolUseIdRole).toString();
        const auto isActionable = fixture.permissions.data(
            fixture.permissions.index(row),
            kodosi::PendingPermissionsModel::ActionableRole).toBool();
        if (tool == QStringLiteral("safe-a")) {
            safeAActionable = isActionable;
        } else if (tool == QStringLiteral("safe-b")) {
            safeBActionable = isActionable;
        }
    }
    QVERIFY(!safeAActionable);
    QVERIFY(safeBActionable);
    QVERIFY(
        rowFor(
            fixture.attention,
            kodosi::AttentionModel::Category::BulkSafe)
        >= 0);
}

void AttentionModelTest::rejectsStaleTokensAndIncarnationReplacement()
{
    Fixture fixture;
    activate(
        fixture,
        QJsonArray {
            session(QStringLiteral("s1"), incarnation1),
        });
    fixture.permissions.ingestAgentIntelEvent(permissionsSnapshot(
        1,
        QJsonArray {
            request(
                QStringLiteral("s1"),
                incarnation1,
                QStringLiteral("tool"),
                QStringLiteral("network"),
                1'700'000'000'000),
        }));
    const auto oldToken = tokenAt(fixture.attention, 0);

    fixture.sessions.ingestSessionEvent(sessionList(QJsonArray {
        session(QStringLiteral("s1"), incarnation2),
    }));
    QCOMPARE(fixture.attention.rowCount(), 0);
    fixture.dispatcher.commands.clear();
    QVERIFY(!fixture.attention.approve(oldToken));
    QVERIFY(!fixture.attention.operationError().isEmpty());
    QVERIFY(commandsOfType(
        fixture.dispatcher,
        QStringLiteral("session.list")).size() >= 1);
    QCOMPARE(
        commandsOfType(
            fixture.dispatcher,
            QStringLiteral(
                "agent.intel.allowPendingPermissionRequest")).size(),
        0);

    fixture.permissions.ingestAgentIntelEvent(permissionsSnapshot(
        2,
        QJsonArray {
            request(
                QStringLiteral("s1"),
                incarnation2,
                QStringLiteral("tool"),
                QStringLiteral("network"),
                1'700'000'000'000),
        }));
    QCOMPARE(fixture.attention.rowCount(), 1);
    const auto newToken = tokenAt(fixture.attention, 0);
    QVERIFY(newToken != oldToken);

    QSignalSpy navigation(
        &fixture.attention,
        &kodosi::AttentionModel::navigationRequested);
    QVERIFY(fixture.attention.jump(newToken));
    QCOMPARE(navigation.count(), 1);
    QCOMPARE(
        navigation.constFirst().at(0).toString(),
        QStringLiteral("s1"));
    QCOMPARE(navigation.constFirst().at(1).toString(), newToken);
}

void AttentionModelTest::fencesAgentTokensByExactRevision()
{
    Fixture fixture;
    activate(
        fixture,
        QJsonArray {
            session(QStringLiteral("s1"), incarnation1),
        });
    fixture.permissions.ingestAgentIntelEvent(
        permissionsSnapshot(1, {}));
    fixture.intel.ingestAgentIntelEvent(liveSet(
        1,
        QJsonArray {
            intelEntry(
                QStringLiteral("s1"),
                incarnation1,
                QStringLiteral("question"),
                QStringLiteral("First")),
        },
        lastRequestId(
            fixture.dispatcher,
            QStringLiteral("agent.intel.queryLiveSet"))));
    QCOMPARE(fixture.attention.rowCount(), 1);
    const auto oldToken = tokenAt(fixture.attention, 0);
    const auto stableId = stableIdAt(fixture.attention, 0);
    QVERIFY(!oldToken.contains(QStringLiteral("s1")));
    QVERIFY(!oldToken.contains(incarnation1));

    fixture.intel.ingestAgentIntelEvent(liveSet(
        2,
        QJsonArray {
            intelEntry(
                QStringLiteral("s1"),
                incarnation1,
                QStringLiteral("question"),
                QStringLiteral("First")),
        }));
    const auto newToken = tokenAt(fixture.attention, 0);
    QVERIFY(newToken != oldToken);
    QCOMPARE(stableIdAt(fixture.attention, 0), stableId);
    QSignalSpy navigation(
        &fixture.attention,
        &kodosi::AttentionModel::navigationRequested);
    QVERIFY(!fixture.attention.jump(oldToken));
    QCOMPARE(navigation.count(), 0);
    QVERIFY(fixture.attention.jump(newToken));
    QCOMPARE(navigation.count(), 1);
}

void AttentionModelTest::followsSourceResetsAndDataChanges()
{
    Fixture fixture;
    activate(
        fixture,
        QJsonArray {
            session(QStringLiteral("s1"), incarnation1),
        });
    QSignalSpy resets(
        &fixture.attention,
        &QAbstractItemModel::modelReset);
    fixture.permissions.ingestAgentIntelEvent(permissionsSnapshot(
        1,
        QJsonArray {
            request(
                QStringLiteral("s1"),
                incarnation1,
                QStringLiteral("safe"),
                QStringLiteral("safe"),
                1'700'000'000'000),
        }));
    QVERIFY(resets.count() >= 1);
    resets.clear();

    const auto bulkRow = rowFor(
        fixture.attention,
        kodosi::AttentionModel::Category::BulkSafe);
    QVERIFY(fixture.attention.approveAll(tokenAt(fixture.attention, bulkRow)));
    QVERIFY(resets.count() >= 1);
    QCOMPARE(
        categoryAt(fixture.attention, 0),
        kodosi::AttentionModel::Category::Approval);
    QVERIFY(!fixture.attention.data(
        fixture.attention.index(0),
        kodosi::AttentionModel::CanApproveRole).toBool());

    resets.clear();
    fixture.permissions.ingestAgentIntelEvent(
        permissionsSnapshot(2, {}));
    fixture.sessions.ingestSessionEvent(sessionList(QJsonArray {
        session(
            QStringLiteral("s1"),
            incarnation1,
            QStringLiteral("waiting")),
    }));
    QVERIFY(resets.count() >= 1);
    QCOMPARE(
        categoryAt(fixture.attention, 0),
        kodosi::AttentionModel::Category::Waiting);
}

void AttentionModelTest::exposesOnlyPresentationRoles()
{
    Fixture fixture;
    activate(
        fixture,
        QJsonArray {
            session(QStringLiteral("s1"), incarnation1),
        });
    fixture.permissions.ingestAgentIntelEvent(
        permissionsSnapshot(1, {}));
    fixture.intel.ingestAgentIntelEvent(liveSet(
        1,
        QJsonArray {
            intelEntry(
                QStringLiteral("s1"),
                incarnation1,
                QStringLiteral("question"),
                QStringLiteral("Choose")),
        },
        lastRequestId(
            fixture.dispatcher,
            QStringLiteral("agent.intel.queryLiveSet"))));

    const auto roles = fixture.attention.roleNames();
    QCOMPARE(roles.size(), 13);
    const QSet<QByteArray> expected {
        QByteArrayLiteral("category"),
        QByteArrayLiteral("sessionId"),
        QByteArrayLiteral("sessionName"),
        QByteArrayLiteral("title"),
        QByteArrayLiteral("summary"),
        QByteArrayLiteral("risk"),
        QByteArrayLiteral("tone"),
        QByteArrayLiteral("actionKind"),
        QByteArrayLiteral("canApprove"),
        QByteArrayLiteral("canDeny"),
        QByteArrayLiteral("canJump"),
        QByteArrayLiteral("attentionToken"),
        QByteArrayLiteral("stableId"),
    };
    QSet<QByteArray> actual;
    for (const auto& role : roles) {
        actual.insert(role);
    }
    QCOMPARE(actual, expected);
    const auto joined = QByteArrayList(actual.cbegin(), actual.cend()).join(',');
    QVERIFY(!joined.contains("incarnation"));
    QVERIFY(!joined.contains("revision"));
    QVERIFY(!joined.contains("generation"));
    QVERIFY(!joined.contains("tool"));
    QVERIFY(!joined.contains("json"));
}

void AttentionModelTest::enforcesOneTapApprovalPolicy()
{
    Fixture fixture;
    activate(
        fixture,
        QJsonArray {
            session(QStringLiteral("s1"), incarnation1),
        });
    fixture.permissions.ingestAgentIntelEvent(permissionsSnapshot(
        1,
        QJsonArray {
            request(
                QStringLiteral("s1"),
                incarnation1,
                QStringLiteral("network"),
                QStringLiteral("network"),
                1'700'000'000'000),
            request(
                QStringLiteral("s1"),
                incarnation1,
                QStringLiteral("destructive"),
                QStringLiteral("destructive"),
                1'700'000'000'001),
            request(
                QStringLiteral("s1"),
                incarnation1,
                QStringLiteral("credential"),
                QStringLiteral("credential"),
                1'700'000'000'002),
            request(
                QStringLiteral("s1"),
                incarnation1,
                QStringLiteral("unknown"),
                QStringLiteral("unknown"),
                1'700'000'000'003),
        }));

    QSignalSpy navigation(
        &fixture.attention,
        &kodosi::AttentionModel::navigationRequested);
    for (auto row = 0; row < fixture.attention.rowCount(); ++row) {
        const auto risk = riskAt(fixture.attention, row);
        const auto action = fixture.attention.data(
            fixture.attention.index(row),
            kodosi::AttentionModel::ActionKindRole)
                                .value<kodosi::AttentionModel::ActionKind>();
        const auto token = tokenAt(fixture.attention, row);
        if (risk == kodosi::AttentionModel::Risk::Network) {
            QCOMPARE(action, kodosi::AttentionModel::ActionKind::Approve);
            continue;
        }

        QCOMPARE(action, kodosi::AttentionModel::ActionKind::Review);
        fixture.dispatcher.commands.clear();
        QVERIFY(!fixture.attention.approve(token));
        QCOMPARE(
            commandsOfType(
                fixture.dispatcher,
                QStringLiteral(
                    "agent.intel.allowPendingPermissionRequest")).size(),
            0);
        QVERIFY(fixture.attention.review(token));
        QCOMPARE(navigation.count(), 1);
        QCOMPARE(
            navigation.constFirst().at(1).toString(),
            token);
        navigation.clear();
    }

    QFile qml(
        QStringLiteral(KODOSI_SOURCE_DIR)
        + QStringLiteral("/src/qml/Attention/AttentionCard.qml"));
    QVERIFY(qml.open(QIODevice::ReadOnly));
    const auto source = qml.readAll();
    QVERIFY(source.contains(
        "visible: root.actionKind === Models.Attention.Review"));
    QVERIFY(source.contains(
        "visible: root.actionKind === Models.Attention.Approve"));

    QFile panel(
        QStringLiteral(KODOSI_SOURCE_DIR)
        + QStringLiteral("/src/qml/Attention/AttentionPanel.qml"));
    QVERIFY(panel.open(QIODevice::ReadOnly));
    const auto panelSource = panel.readAll();
    QVERIFY(panelSource.contains(
        "objectName: \"panel.attention.authorityError\""));
    QVERIFY(panelSource.contains(
        "visible: Models.Attention.count > 0"));
}

void AttentionModelTest::usesAttentionThemeControls()
{
    const QStringList files {
        QStringLiteral("AttentionButton.qml"),
        QStringLiteral("AttentionCard.qml"),
        QStringLiteral("AttentionPanel.qml"),
        QStringLiteral("AttentionRail.qml"),
        QStringLiteral("AttentionSpinner.qml"),
    };
    const QRegularExpression rawColor(
        QStringLiteral(R"(["']#[0-9a-fA-F]{3,8}["'])"));
    const QRegularExpression defaultButton(
        QStringLiteral(R"((^|\n)\s*Button\s*\{)"));
    const QRegularExpression numericRadius(
        QStringLiteral(R"(\bradius\s*:\s*([0-9]+(?:\.[0-9]+)?))"));

    for (const auto& fileName : files) {
        QFile file(
            QStringLiteral(KODOSI_SOURCE_DIR)
            + QStringLiteral("/src/qml/Attention/") + fileName);
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(fileName));
        const auto source = QString::fromUtf8(file.readAll());
        QVERIFY2(
            !rawColor.match(source).hasMatch(),
            qPrintable(fileName));
        QVERIFY2(
            !source.contains(QStringLiteral("\"transparent\"")),
            qPrintable(fileName));
        if (fileName != QStringLiteral("AttentionButton.qml")) {
            QVERIFY2(
                !defaultButton.match(source).hasMatch(),
                qPrintable(fileName));
        }
        auto radii = numericRadius.globalMatch(source);
        while (radii.hasNext()) {
            const auto radius = radii.next().captured(1).toDouble();
            QVERIFY2(radius <= 7.0, qPrintable(fileName));
        }
        if (fileName == QStringLiteral("AttentionCard.qml")) {
            QVERIFY(source.contains(QStringLiteral("approval.deny.")));
            QVERIFY(source.contains(QStringLiteral("approval.allow.")));
        } else if (fileName == QStringLiteral("AttentionRail.qml")) {
            QVERIFY(source.contains(QStringLiteral(
                "objectName: \"stage.approvals\"")));
        }
    }

    QFile mainQml(
        QStringLiteral(KODOSI_SOURCE_DIR)
        + QStringLiteral("/src/qml/Main.qml"));
    QVERIFY(mainQml.open(QIODevice::ReadOnly));
    const auto mainSource = QString::fromUtf8(mainQml.readAll());
    const auto attentionStart =
        mainSource.indexOf(QStringLiteral("id: attentionButton"));
    const auto attentionEnd =
        mainSource.indexOf(QStringLiteral("id: settingsButton"));
    QVERIFY(attentionStart >= 0);
    QVERIFY(attentionEnd > attentionStart);
    const auto attentionHeader =
        mainSource.mid(attentionStart, attentionEnd - attentionStart);
    QVERIFY(!rawColor.match(attentionHeader).hasMatch());
    QVERIFY(!attentionHeader.contains(QStringLiteral("\"transparent\"")));
    QVERIFY(attentionHeader.contains(QStringLiteral("KIconButton")));
    QVERIFY(attentionHeader.contains(QStringLiteral("glyph: \"bell\"")));
}

void AttentionModelTest::sessionListFailureSurfacesInAttention()
{
    Fixture fixture;
    const auto authEvent = auth();
    fixture.sessions.ingestAuthEvent(authEvent);
    fixture.dispatcher.rejectedTypes.insert(
        QStringLiteral("session.list"));
    fixture.sessionActions.ingestAuthEvent(authEvent);
    fixture.intel.ingestAuthEvent(authEvent);
    fixture.permissions.ingestAuthEvent(authEvent);

    QVERIFY(fixture.attention.refresh());
    QCOMPARE(
        fixture.attention.authorityState(),
        kodosi::AttentionModel::AuthorityState::Failed);
    QVERIFY(fixture.attention.authorityError().contains(
        QStringLiteral("sessions")));
}

void AttentionModelTest::combinesAgentIntelAuthority()
{
    Fixture fixture;
    activate(fixture, {});
    fixture.permissions.ingestAgentIntelEvent(
        permissionsSnapshot(1, {}));
    QCOMPARE(
        fixture.attention.authorityState(),
        kodosi::AttentionModel::AuthorityState::Loaded);

    fixture.dispatcher.rejectedTypes.insert(
        QStringLiteral("agent.intel.queryLiveSet"));
    QVERIFY(!fixture.intel.refresh());
    QCOMPARE(
        fixture.attention.authorityState(),
        kodosi::AttentionModel::AuthorityState::Failed);
    QVERIFY(fixture.attention.authorityError().contains(
        QStringLiteral("agent intelligence")));

    fixture.dispatcher.rejectedTypes.clear();
    QVERIFY(fixture.intel.refresh());
    fixture.intel.ingestAgentIntelEvent(liveSet(
        1,
        {},
        lastRequestId(
            fixture.dispatcher,
            QStringLiteral("agent.intel.queryLiveSet"))));
    QCOMPARE(
        fixture.attention.authorityState(),
        kodosi::AttentionModel::AuthorityState::Loaded);
}

void AttentionModelTest::deduplicatesRefreshAndClearsResetErrors()
{
    Fixture fixture;
    activate(
        fixture,
        QJsonArray {
            session(QStringLiteral("s1"), incarnation1),
        });
    fixture.permissions.ingestAgentIntelEvent(permissionsSnapshot(
        1,
        QJsonArray {
            request(
                QStringLiteral("s1"),
                incarnation1,
                QStringLiteral("destructive"),
                QStringLiteral("destructive"),
                1'700'000'000'000),
        }));
    const auto token = tokenAt(fixture.attention, 0);
    QVERIFY(!fixture.attention.approve(token));
    QVERIFY(!fixture.attention.operationError().isEmpty());

    const auto switchedAuth = auth(QStringLiteral("account-2"));
    fixture.sessions.ingestAuthEvent(switchedAuth);
    QVERIFY(fixture.attention.operationError().isEmpty());
    fixture.permissions.ingestAuthEvent(switchedAuth);
    const auto pendingBefore = commandsOfType(
        fixture.dispatcher,
        QStringLiteral("agent.intel.queryPendingPermissions")).size();
    QVERIFY(fixture.attention.refresh());
    QVERIFY(fixture.attention.refresh());
    QCOMPARE(
        commandsOfType(
            fixture.dispatcher,
            QStringLiteral(
                "agent.intel.queryPendingPermissions")).size(),
        pendingBefore);

    Fixture runtimeReset;
    activate(
        runtimeReset,
        QJsonArray {
            session(QStringLiteral("s1"), incarnation1),
        });
    runtimeReset.permissions.ingestAgentIntelEvent(permissionsSnapshot(
        1,
        QJsonArray {
            request(
                QStringLiteral("s1"),
                incarnation1,
                QStringLiteral("destructive"),
                QStringLiteral("destructive"),
                1'700'000'000'000),
        }));
    QVERIFY(!runtimeReset.attention.approve(
        tokenAt(runtimeReset.attention, 0)));
    QVERIFY(!runtimeReset.attention.operationError().isEmpty());
    runtimeReset.sessions.resetRuntimeAuthority();
    QVERIFY(runtimeReset.attention.operationError().isEmpty());
}

void AttentionModelTest::reportsAuthorityAndExactNavigation()
{
    Fixture unauthenticated;
    QCOMPARE(
        unauthenticated.attention.authorityState(),
        kodosi::AttentionModel::AuthorityState::Loading);

    activate(unauthenticated, {}, false);
    QCOMPARE(
        unauthenticated.sessions.authorityState(),
        kodosi::SessionCatalogModel::AuthorityState::Loaded);
    QCOMPARE(
        unauthenticated.intel.hydrationState(),
        kodosi::AgentSessionIntelModel::HydrationState::Current);
    QCOMPARE(
        unauthenticated.attention.authorityState(),
        kodosi::AttentionModel::AuthorityState::Loaded);
    QCOMPARE(unauthenticated.attention.rowCount(), 0);

    Fixture failed;
    activate(failed, {});
    const auto pendingQuery = lastRequestId(
        failed.dispatcher,
        QStringLiteral("agent.intel.queryPendingPermissions"));
    QVERIFY(!pendingQuery.isEmpty());
    failed.permissions.ingestAgentIntelEvent(
        QJsonDocument(QJsonObject {
            {QStringLiteral("authority"),
             QStringLiteral("accountContext")},
            {QStringLiteral("accountUserId"),
             QStringLiteral("account")},
            {QStringLiteral("accountEpoch"), accountEpoch},
            {QStringLiteral("type"), QStringLiteral("agent.intel.error")},
            {QStringLiteral("requestId"), pendingQuery},
            {QStringLiteral("message"), QStringLiteral("Try again")},
        }).toJson(QJsonDocument::Compact));
    QCOMPARE(
        failed.attention.authorityState(),
        kodosi::AttentionModel::AuthorityState::Failed);
    QVERIFY(failed.attention.authorityError().contains(
        QStringLiteral("Try again")));
    failed.dispatcher.commands.clear();
    QVERIFY(failed.attention.refresh());
    QVERIFY(commandsOfType(
        failed.dispatcher,
        QStringLiteral("session.list")).size() >= 1);

    Fixture fixture;
    activate(
        fixture,
        QJsonArray {
            session(QStringLiteral("s1"), incarnation1),
        });
    fixture.permissions.ingestAgentIntelEvent(permissionsSnapshot(
        1,
        QJsonArray {
            request(
                QStringLiteral("s1"),
                incarnation1,
                QStringLiteral("tool"),
                QStringLiteral("destructive"),
                1'700'000'000'000),
        }));
    QCOMPARE(fixture.attention.runningCount(), 1);
    QCOMPARE(
        fixture.attention.authorityState(),
        kodosi::AttentionModel::AuthorityState::Loaded);

    const auto token = tokenAt(fixture.attention, 0);
    const auto pendingToken = fixture.permissions.data(
        fixture.permissions.index(0),
        kodosi::PendingPermissionsModel::IdentityTokenRole).toString();
    QCOMPARE(token, pendingToken);
    QSignalSpy navigation(
        &fixture.attention,
        &kodosi::AttentionModel::navigationRequested);
    QVERIFY(fixture.attention.review(token));
    QCOMPARE(navigation.count(), 1);
    QCOMPARE(navigation.constFirst().at(0).toString(), QStringLiteral("s1"));
    QCOMPARE(navigation.constFirst().at(1).toString(), pendingToken);
}

QTEST_GUILESS_MAIN(AttentionModelTest)

#include "tst_attention_model.moc"
