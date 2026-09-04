#include "models/PendingPermissionsModel.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest/QTest>

#include <utility>

class FakePermissionDispatcher final : public kodosi::CommandDispatcher {
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

class PendingPermissionsModelTest final : public QObject {
    Q_OBJECT

private slots:
    void appliesCurrentIncarnationSnapshot();
    void replacesByGenerationAndFencesAccounts();
    void reappliesSnapshotWhenIncarnationAppears();
    void dispatchesOnlyExactActionableIdentity();
    void acceptsCorrelatedRecoveryReply();
    void rejectsMalformedSnapshotAtomically();
    void boundsMissingAndMalformedQueryReplies();
    void pendingFeedbackPreservesDeliveryWatchdog();
    void queryFailurePreservesSnapshotAuthority();
    void stalePushDoesNotCancelRefreshReply();
    void remoteFailureRestoresActionability();
    void rejectsDuplicateRequestIdentities();
};

namespace {

QByteArray auth(const QString& userId, const quint64 epoch)
{
    return QJsonDocument(QJsonObject {
        {QStringLiteral("type"), QStringLiteral("auth.ready")},
        {QStringLiteral("userId"), userId},
        {QStringLiteral("accountEpoch"), static_cast<qint64>(epoch)},
    }).toJson(QJsonDocument::Compact);
}

QByteArray sessions(
    const QString& userId,
    const quint64 epoch,
    const QString& incarnation)
{
    return QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), userId},
        {QStringLiteral("accountEpoch"), static_cast<qint64>(epoch)},
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

QJsonObject request(
    const QString& incarnation,
    const quint64 generation = 7,
    const QString& decisionPhase = QStringLiteral("actionable"))
{
    return {
        {QStringLiteral("sessionId"), QStringLiteral("session-1")},
        {QStringLiteral("sessionIncarnationId"), incarnation},
        {QStringLiteral("requestGeneration"), static_cast<qint64>(generation)},
        {QStringLiteral("toolUseId"), QStringLiteral("tool-1")},
        {QStringLiteral("toolName"), QStringLiteral("Bash")},
        {QStringLiteral("toolInput"),
         QJsonObject {{QStringLiteral("command"), QStringLiteral("ls -la")}}},
        {QStringLiteral("createdAtMs"), 1'700'000'000'000.0},
        {QStringLiteral("deadlineAtMs"), 1'800'000'000'000.0},
        {QStringLiteral("risk"), QStringLiteral("destructive")},
        {QStringLiteral("decisionPhase"), decisionPhase},
    };
}

QByteArray snapshot(
    const QString& userId,
    const quint64 epoch,
    const quint64 generation,
    const QJsonArray& requests)
{
    return QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), userId},
        {QStringLiteral("accountEpoch"), static_cast<qint64>(epoch)},
        {QStringLiteral("type"),
         QStringLiteral("agent.intel.pendingPermissionsSnapshot")},
        {QStringLiteral("generation"), static_cast<qint64>(generation)},
        {QStringLiteral("requests"), requests},
    }).toJson(QJsonDocument::Compact);
}

void activate(
    kodosi::SessionCatalogModel& sessionsModel,
    kodosi::PendingPermissionsModel& permissions,
    const QString& userId,
    const quint64 epoch,
    const QString& incarnation)
{
    const auto authEvent = auth(userId, epoch);
    sessionsModel.ingestAuthEvent(authEvent);
    sessionsModel.ingestSessionEvent(sessions(userId, epoch, incarnation));
    permissions.ingestAuthEvent(authEvent);
}

} // namespace

void PendingPermissionsModelTest::appliesCurrentIncarnationSnapshot()
{
    FakePermissionDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::PendingPermissionsModel permissions(dispatcher, sessionsModel);
    activate(
        sessionsModel,
        permissions,
        QStringLiteral("account"),
        1,
        QStringLiteral("inc-1"));

    auto secondRequest = request(
        QStringLiteral("inc-1"),
        8);
    secondRequest.insert(
        QStringLiteral("toolUseId"),
        QStringLiteral("tool-2"));
    secondRequest.insert(
        QStringLiteral("createdAtMs"),
        1'700'000'000'001.0);
    permissions.ingestAgentIntelEvent(snapshot(
        QStringLiteral("account"),
        1,
        1,
        QJsonArray {
            request(QStringLiteral("inc-1")),
            secondRequest,
        }));
    QCOMPARE(permissions.rowCount(), 2);
    QCOMPARE(
        permissions.data(
            permissions.index(0),
            kodosi::PendingPermissionsModel::ToolInputSummaryRole)
            .toString(),
        QStringLiteral("ls -la"));
    QVERIFY(permissions.data(
            permissions.index(0),
            kodosi::PendingPermissionsModel::ActionableRole)
            .toBool());
    const auto sessionPresentation =
        permissions.presentationForSession(QStringLiteral("session-1"));
    QCOMPARE(
        sessionPresentation.value(QStringLiteral("queuedCount")).toInt(),
        1);
    QVERIFY(
        sessionPresentation.value(QStringLiteral("deadline")).toDateTime().isValid());
    QCOMPARE(
        permissions.topPresentation().value(QStringLiteral("identityToken")),
        sessionPresentation.value(QStringLiteral("identityToken")));
    QCOMPARE(
        permissions.authorityState(),
        kodosi::PendingPermissionsModel::AuthorityState::Loaded);
}

void PendingPermissionsModelTest::replacesByGenerationAndFencesAccounts()
{
    FakePermissionDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::PendingPermissionsModel permissions(dispatcher, sessionsModel);
    activate(
        sessionsModel,
        permissions,
        QStringLiteral("account-a"),
        1,
        QStringLiteral("inc-1"));
    permissions.ingestAgentIntelEvent(snapshot(
        QStringLiteral("account-a"),
        1,
        2,
        QJsonArray {}));
    permissions.ingestAgentIntelEvent(snapshot(
        QStringLiteral("account-a"),
        1,
        1,
        QJsonArray {request(QStringLiteral("inc-1"))}));
    QCOMPARE(permissions.rowCount(), 0);

    permissions.ingestAgentIntelEvent(snapshot(
        QStringLiteral("account-b"),
        2,
        1,
        QJsonArray {request(QStringLiteral("inc-2"))}));
    QCOMPARE(permissions.rowCount(), 0);
    activate(
        sessionsModel,
        permissions,
        QStringLiteral("account-b"),
        2,
        QStringLiteral("inc-2"));
    QCOMPARE(permissions.rowCount(), 1);
}

void PendingPermissionsModelTest::reappliesSnapshotWhenIncarnationAppears()
{
    FakePermissionDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::PendingPermissionsModel permissions(dispatcher, sessionsModel);
    const auto authEvent = auth(QStringLiteral("account"), 1);
    sessionsModel.ingestAuthEvent(authEvent);
    permissions.ingestAuthEvent(authEvent);
    permissions.ingestAgentIntelEvent(snapshot(
        QStringLiteral("account"),
        1,
        1,
        QJsonArray {request(QStringLiteral("inc-new"))}));
    QCOMPARE(permissions.rowCount(), 0);
    QCOMPARE(
        permissions.authorityState(),
        kodosi::PendingPermissionsModel::AuthorityState::Loading);

    sessionsModel.ingestSessionEvent(sessions(
        QStringLiteral("account"),
        1,
        QStringLiteral("inc-new")));
    QCOMPARE(permissions.rowCount(), 1);
    QCOMPARE(
        permissions.authorityState(),
        kodosi::PendingPermissionsModel::AuthorityState::Loaded);
}

void PendingPermissionsModelTest::dispatchesOnlyExactActionableIdentity()
{
    FakePermissionDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::PendingPermissionsModel permissions(dispatcher, sessionsModel);
    activate(
        sessionsModel,
        permissions,
        QStringLiteral("account"),
        1,
        QStringLiteral("inc-1"));
    permissions.ingestAgentIntelEvent(snapshot(
        QStringLiteral("account"),
        1,
        1,
        QJsonArray {request(QStringLiteral("inc-1"))}));
    const auto token = permissions
                           .data(
                               permissions.index(0),
                               kodosi::PendingPermissionsModel::IdentityTokenRole)
                           .toString();

    QVERIFY(!permissions.approve(QStringLiteral("forged")));
    QVERIFY(permissions.deny(token, QStringLiteral(" no ")));
    QCOMPARE(
        dispatcher.commands.back().value(QStringLiteral("type")).toString(),
        QStringLiteral("agent.intel.denyPendingPermissionRequest"));
    QCOMPARE(
        dispatcher.commands.back().value(QStringLiteral("reason")).toString(),
        QStringLiteral("no"));
    QVERIFY(!permissions.approve(token));
}

void PendingPermissionsModelTest::acceptsCorrelatedRecoveryReply()
{
    FakePermissionDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::PendingPermissionsModel permissions(dispatcher, sessionsModel);
    activate(
        sessionsModel,
        permissions,
        QStringLiteral("account"),
        1,
        QStringLiteral("inc-1"));
    const auto queryId =
        dispatcher.commands.front().value(QStringLiteral("requestId")).toString();
    permissions.ingestAgentIntelEvent(QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), QStringLiteral("account")},
        {QStringLiteral("accountEpoch"), 1},
        {QStringLiteral("type"), QStringLiteral("agent.intel.reply")},
        {QStringLiteral("requestId"), queryId},
        {QStringLiteral("payload"),
         QJsonObject {
             {QStringLiteral("generation"), 3},
             {QStringLiteral("requests"),
              QJsonArray {request(QStringLiteral("inc-1"))}},
         }},
    }).toJson(QJsonDocument::Compact));

    QCOMPARE(permissions.rowCount(), 1);
    QCOMPARE(
        permissions.authorityState(),
        kodosi::PendingPermissionsModel::AuthorityState::Loaded);
}

void PendingPermissionsModelTest::rejectsMalformedSnapshotAtomically()
{
    FakePermissionDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::PendingPermissionsModel permissions(dispatcher, sessionsModel);
    QSignalSpy errors(&permissions, &kodosi::PendingPermissionsModel::decodeError);
    activate(
        sessionsModel,
        permissions,
        QStringLiteral("account"),
        1,
        QStringLiteral("inc-1"));
    permissions.ingestAgentIntelEvent(snapshot(
        QStringLiteral("account"),
        1,
        1,
        QJsonArray {request(QStringLiteral("inc-1"))}));

    auto bad = request(QStringLiteral("inc-1"));
    bad.insert(QStringLiteral("requestGeneration"), -1);
    permissions.ingestAgentIntelEvent(snapshot(
        QStringLiteral("account"),
        1,
        2,
        QJsonArray {bad}));
    QCOMPARE(errors.count(), 1);
    QCOMPARE(permissions.rowCount(), 1);

    bad = request(QStringLiteral("inc-1"));
    bad.insert(QStringLiteral("requestGeneration"), 0);
    permissions.ingestAgentIntelEvent(snapshot(
        QStringLiteral("account"),
        1,
        3,
        QJsonArray {bad}));
    QCOMPARE(errors.count(), 2);
    QCOMPARE(permissions.rowCount(), 1);
}

void PendingPermissionsModelTest::rejectsDuplicateRequestIdentities()
{
    FakePermissionDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::PendingPermissionsModel permissions(dispatcher, sessionsModel);
    QSignalSpy errors(&permissions, &kodosi::PendingPermissionsModel::decodeError);
    activate(
        sessionsModel,
        permissions,
        QStringLiteral("account"),
        1,
        QStringLiteral("inc-1"));

    const auto duplicate = request(QStringLiteral("inc-1"));
    permissions.ingestAgentIntelEvent(snapshot(
        QStringLiteral("account"),
        1,
        1,
        QJsonArray {duplicate, duplicate}));

    QCOMPARE(permissions.rowCount(), 0);
    QCOMPARE(errors.size(), 1);
}

void PendingPermissionsModelTest::boundsMissingAndMalformedQueryReplies()
{
    FakePermissionDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::PendingPermissionsModel permissions(
        dispatcher,
        sessionsModel,
        15'000,
        1);
    activate(
        sessionsModel,
        permissions,
        QStringLiteral("account"),
        1,
        QStringLiteral("inc-1"));
    QTest::qWait(100);
    QCOMPARE(
        permissions.authorityState(),
        kodosi::PendingPermissionsModel::AuthorityState::AuthorityFailed);

    QVERIFY(permissions.refresh());
    const auto queryId =
        dispatcher.commands.back().value(QStringLiteral("requestId")).toString();
    permissions.ingestAgentIntelEvent(QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), QStringLiteral("account")},
        {QStringLiteral("accountEpoch"), 1},
        {QStringLiteral("type"), QStringLiteral("agent.intel.reply")},
        {QStringLiteral("requestId"), queryId},
        {QStringLiteral("payload"), QStringLiteral("not-a-snapshot")},
    }).toJson(QJsonDocument::Compact));
    QCOMPARE(
        permissions.authorityState(),
        kodosi::PendingPermissionsModel::AuthorityState::AuthorityFailed);
}

void PendingPermissionsModelTest::pendingFeedbackPreservesDeliveryWatchdog()
{
    FakePermissionDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::PendingPermissionsModel permissions(
        dispatcher,
        sessionsModel,
        1,
        1'000);
    activate(
        sessionsModel,
        permissions,
        QStringLiteral("account"),
        1,
        QStringLiteral("inc-1"));
    permissions.ingestAgentIntelEvent(snapshot(
        QStringLiteral("account"),
        1,
        1,
        QJsonArray {request(QStringLiteral("inc-1"))}));
    const auto token = permissions
                           .data(
                               permissions.index(0),
                               kodosi::PendingPermissionsModel::IdentityTokenRole)
                           .toString();
    QVERIFY(permissions.approve(token));
    permissions.ingestAgentIntelEvent(snapshot(
        QStringLiteral("account"),
        1,
        2,
        QJsonArray {
            request(
                QStringLiteral("inc-1"),
                7,
                QStringLiteral("sending")),
        }));
    permissions.ingestAgentIntelEvent(QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), QStringLiteral("account")},
        {QStringLiteral("accountEpoch"), 1},
        {QStringLiteral("type"),
         QStringLiteral("agent.intel.remotePermissionDecisionState")},
        {QStringLiteral("sessionId"), QStringLiteral("session-1")},
        {QStringLiteral("sessionIncarnationId"), QStringLiteral("inc-1")},
        {QStringLiteral("toolUseId"), QStringLiteral("tool-1")},
        {QStringLiteral("requestGeneration"), 7},
        {QStringLiteral("phase"), QStringLiteral("pending")},
    }).toJson(QJsonDocument::Compact));
    QTest::qWait(100);

    QCOMPARE(
        permissions
            .data(
                permissions.index(0),
                kodosi::PendingPermissionsModel::DecisionStateRole)
            .value<kodosi::PendingPermissionsModel::DecisionState>(),
        kodosi::PendingPermissionsModel::DecisionState::DeliveryUnknown);
    QCOMPARE(
        dispatcher.commands.back().value(QStringLiteral("type")).toString(),
        QStringLiteral("agent.intel.queryPendingPermissions"));
}

void PendingPermissionsModelTest::queryFailurePreservesSnapshotAuthority()
{
    FakePermissionDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::PendingPermissionsModel permissions(
        dispatcher,
        sessionsModel,
        15'000,
        1);
    activate(
        sessionsModel,
        permissions,
        QStringLiteral("account"),
        1,
        QStringLiteral("inc-1"));
    permissions.ingestAgentIntelEvent(snapshot(
        QStringLiteral("account"),
        1,
        1,
        QJsonArray {request(QStringLiteral("inc-1"))}));
    QVERIFY(permissions.refresh());
    QTest::qWait(100);
    QCOMPARE(permissions.rowCount(), 1);
    QCOMPARE(
        permissions.authorityState(),
        kodosi::PendingPermissionsModel::AuthorityState::Loaded);

    QVERIFY(permissions.refresh());
    const auto queryId =
        dispatcher.commands.back().value(QStringLiteral("requestId")).toString();
    permissions.ingestAgentIntelEvent(QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), QStringLiteral("account")},
        {QStringLiteral("accountEpoch"), 1},
        {QStringLiteral("type"), QStringLiteral("agent.intel.reply")},
        {QStringLiteral("requestId"), queryId},
        {QStringLiteral("payload"),
         QJsonObject {
             {QStringLiteral("generation"), 2},
             {QStringLiteral("requests"), QStringLiteral("invalid")},
         }},
    }).toJson(QJsonDocument::Compact));
    QCOMPARE(permissions.rowCount(), 1);
    QCOMPARE(
        permissions.authorityState(),
        kodosi::PendingPermissionsModel::AuthorityState::Loaded);

    const auto authEvent = auth(QStringLiteral("account-2"), 2);
    sessionsModel.ingestAuthEvent(authEvent);
    permissions.ingestAuthEvent(authEvent);
    permissions.ingestAgentIntelEvent(snapshot(
        QStringLiteral("account-2"),
        2,
        1,
        QJsonArray {request(QStringLiteral("missing-incarnation"))}));
    QTest::qWait(100);
    QCOMPARE(permissions.rowCount(), 0);
    QCOMPARE(
        permissions.authorityState(),
        kodosi::PendingPermissionsModel::AuthorityState::Loading);
}

void PendingPermissionsModelTest::stalePushDoesNotCancelRefreshReply()
{
    FakePermissionDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::PendingPermissionsModel permissions(dispatcher, sessionsModel);
    activate(
        sessionsModel,
        permissions,
        QStringLiteral("account"),
        1,
        QStringLiteral("inc-1"));
    permissions.ingestAgentIntelEvent(snapshot(
        QStringLiteral("account"),
        1,
        5,
        QJsonArray {request(QStringLiteral("inc-1"))}));
    QCOMPARE(permissions.rowCount(), 1);

    QVERIFY(permissions.refresh());
    const auto queryId =
        dispatcher.commands.back().value(QStringLiteral("requestId")).toString();
    permissions.ingestAgentIntelEvent(snapshot(
        QStringLiteral("account"),
        1,
        4,
        {}));
    permissions.ingestAgentIntelEvent(QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), QStringLiteral("account")},
        {QStringLiteral("accountEpoch"), 1},
        {QStringLiteral("type"), QStringLiteral("agent.intel.reply")},
        {QStringLiteral("requestId"), queryId},
        {QStringLiteral("payload"),
         QJsonObject {
             {QStringLiteral("generation"), 6},
             {QStringLiteral("requests"), QJsonArray {}},
         }},
    }).toJson(QJsonDocument::Compact));

    QCOMPARE(permissions.rowCount(), 0);
    QCOMPARE(
        permissions.authorityState(),
        kodosi::PendingPermissionsModel::AuthorityState::Loaded);
}

void PendingPermissionsModelTest::remoteFailureRestoresActionability()
{
    FakePermissionDispatcher dispatcher;
    kodosi::SessionCatalogModel sessionsModel;
    kodosi::PendingPermissionsModel permissions(dispatcher, sessionsModel);
    activate(
        sessionsModel,
        permissions,
        QStringLiteral("account"),
        1,
        QStringLiteral("inc-1"));
    permissions.ingestAgentIntelEvent(snapshot(
        QStringLiteral("account"),
        1,
        1,
        QJsonArray {request(QStringLiteral("inc-1"))}));
    const auto token = permissions
                           .data(
                               permissions.index(0),
                               kodosi::PendingPermissionsModel::IdentityTokenRole)
                           .toString();
    QVERIFY(permissions.approve(token));
    permissions.ingestAgentIntelEvent(snapshot(
        QStringLiteral("account"),
        1,
        2,
        QJsonArray {
            request(
                QStringLiteral("inc-1"),
                7,
                QStringLiteral("sending")),
        }));
    permissions.ingestAgentIntelEvent(QJsonDocument(QJsonObject {
        {QStringLiteral("authority"), QStringLiteral("accountContext")},
        {QStringLiteral("accountUserId"), QStringLiteral("account")},
        {QStringLiteral("accountEpoch"), 1},
        {QStringLiteral("type"),
         QStringLiteral("agent.intel.remotePermissionDecisionState")},
        {QStringLiteral("sessionId"), QStringLiteral("session-1")},
        {QStringLiteral("sessionIncarnationId"), QStringLiteral("inc-1")},
        {QStringLiteral("toolUseId"), QStringLiteral("tool-1")},
        {QStringLiteral("requestGeneration"), 7},
        {QStringLiteral("phase"), QStringLiteral("failed")},
        {QStringLiteral("status"), QStringLiteral("rejected")},
    }).toJson(QJsonDocument::Compact));

    QCOMPARE(
        permissions
            .data(
                permissions.index(0),
                kodosi::PendingPermissionsModel::DecisionStateRole)
            .value<kodosi::PendingPermissionsModel::DecisionState>(),
        kodosi::PendingPermissionsModel::DecisionState::DeliveryFailed);
    QVERIFY(permissions.data(
            permissions.index(0),
            kodosi::PendingPermissionsModel::ActionableRole)
            .toBool());
    permissions.reapplyLatestSnapshot();
    QCOMPARE(
        permissions
            .data(
                permissions.index(0),
                kodosi::PendingPermissionsModel::DecisionStateRole)
            .value<kodosi::PendingPermissionsModel::DecisionState>(),
        kodosi::PendingPermissionsModel::DecisionState::DeliveryFailed);
    QVERIFY(permissions.data(
            permissions.index(0),
            kodosi::PendingPermissionsModel::ActionableRole)
            .toBool());
}

QTEST_GUILESS_MAIN(PendingPermissionsModelTest)

#include "tst_pending_permissions_model.moc"
