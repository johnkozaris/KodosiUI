#include "models/SessionCatalogModel.hpp"
#include "models/SessionActions.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest/QTest>

class FakeCatalogDispatcher final : public kodosi::CommandDispatcher {
public:
    QVector<QJsonObject> commands;
    bool rejectSessionList = false;

    Result send(
        const kodosi::CommandLane lane,
        const QByteArrayView json) override
    {
        const auto document = QJsonDocument::fromJson(json.toByteArray());
        if (lane != kodosi::CommandLane::Sessions || !document.isObject()
            || rejectSessionList) {
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

class SessionCatalogTest final : public QObject {
    Q_OBJECT

private slots:
    void completeSnapshotUpsertAndRemoval();
    void partialSnapshotRetainsPriorRows();
    void accountEpochFencesAndReplaysFutureEvents();
    void runtimeResetAcceptsFreshEpochSequence();
    void resolvesRoomAssignmentIdentityNatively();
    void retainsPresentationSafeSemanticActions();
    void omissionDefaultsSemanticActionsToFalse();
    void malformedSemanticActionsAreRejected();
    void projectsProtocol36TerminalCapabilities();
    void separatesDisplayOwnerFromOwnerIdentity();
    void typedPresentationQueryTracksLiveUpserts();
    void refreshImmediateFailureAndDeduplication();
    void refreshTimeoutAndEventFailure();
    void authorityRecoversAndResets();
};

namespace {

constexpr auto ready =
    R"({"type":"auth.ready","userId":"user","accountEpoch":1})";
constexpr auto snapshot =
    R"({"authority":"accountContext","accountUserId":"user","accountEpoch":1,"type":"session.list","sessions":[]})";

} // namespace

void SessionCatalogTest::completeSnapshotUpsertAndRemoval()
{
    kodosi::SessionCatalogModel model;
    QSignalSpy inactive(
        &model,
        &kodosi::SessionCatalogModel::inactiveLocalObserved);
    model.ingestAuthEvent(
        QByteArrayLiteral(R"({"type":"auth.ready","userId":"user","accountEpoch":1})"));
    model.ingestSessionEvent(QByteArrayLiteral(
        R"({"authority":"accountContext","accountUserId":"user","accountEpoch":1,"type":"session.list","sessions":[{"kind":"local","id":"one","incarnationId":"inc-1","name":"One","project":"/repo","mode":"normal","status":"active","recovery":"live","scope":"justMe","access":"inject"}]})"));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0), kodosi::SessionCatalogModel::SessionIdRole).toString(),
             QStringLiteral("one"));
    QVERIFY(model.data(model.index(0), kodosi::SessionCatalogModel::CommandableRole).toBool());

    model.ingestSessionEvent(QByteArrayLiteral(
        R"({"authority":"accountContext","accountUserId":"user","accountEpoch":1,"type":"session.upsert","session":{"kind":"local","id":"one","incarnationId":"inc-1","name":"Renamed","project":"/repo","mode":"normal","status":"stopped","recovery":"resumable","scope":"justMe","access":"inject"}})"));
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(inactive.count(), 1);
    QCOMPARE(inactive.constFirst().at(0).toString(), QStringLiteral("one"));
    QCOMPARE(inactive.constFirst().at(1).toString(), QStringLiteral("inc-1"));

    model.ingestSessionEvent(
        QByteArrayLiteral(R"({"authority":"accountContext","accountUserId":"user","accountEpoch":1,"type":"session.removed","sessionId":"one"})"));
    QCOMPARE(model.rowCount(), 0);
}

void SessionCatalogTest::partialSnapshotRetainsPriorRows()
{
    kodosi::SessionCatalogModel model;
    model.ingestAuthEvent(
        QByteArrayLiteral(R"({"type":"auth.ready","userId":"user","accountEpoch":1})"));
    model.ingestSessionEvent(QByteArrayLiteral(
        R"({"authority":"accountContext","accountUserId":"user","accountEpoch":1,"type":"session.list","sessions":[{"kind":"local","id":"one","incarnationId":"inc-1","name":"One","project":"/repo","mode":"normal","status":"active","recovery":"live","scope":"justMe","access":"inject"}]})"));
    model.ingestSessionEvent(QByteArrayLiteral(
        R"({"authority":"accountContext","accountUserId":"user","accountEpoch":1,"type":"session.list","sessions":[{"kind":"future","id":"bad"},{"kind":"local","id":"two","incarnationId":"inc-2","name":"Two","project":"/repo","mode":"normal","status":"active","recovery":"live","scope":"justMe","access":"inject"}]})"));

    QCOMPARE(model.rowCount(), 2);
}

void SessionCatalogTest::accountEpochFencesAndReplaysFutureEvents()
{
    kodosi::SessionCatalogModel model;
    model.ingestAuthEvent(
        QByteArrayLiteral(R"({"type":"auth.ready","userId":"first","accountEpoch":1})"));
    model.ingestSessionEvent(QByteArrayLiteral(
        R"({"authority":"accountContext","accountUserId":"second","accountEpoch":2,"type":"session.list","sessions":[{"kind":"local","id":"future","incarnationId":"inc","name":"Future","project":"/repo","mode":"normal","status":"active","recovery":"live","scope":"myDevices","access":"approve"}]})"));
    QCOMPARE(model.rowCount(), 0);

    model.ingestAuthEvent(
        QByteArrayLiteral(R"({"type":"auth.ready","userId":"second","accountEpoch":2})"));
    QCOMPARE(model.rowCount(), 1);

    model.ingestSessionEvent(QByteArrayLiteral(
        R"({"authority":"accountContext","accountUserId":"first","accountEpoch":1,"type":"session.removed","sessionId":"future"})"));
    QCOMPARE(model.rowCount(), 1);
}

void SessionCatalogTest::runtimeResetAcceptsFreshEpochSequence()
{
    kodosi::SessionCatalogModel model;
    model.ingestAuthEvent(
        QByteArrayLiteral(R"({"type":"auth.ready","userId":"old","accountEpoch":9})"));
    model.resetRuntimeAuthority();
    model.ingestAuthEvent(
        QByteArrayLiteral(R"({"type":"auth.ready","userId":"new","accountEpoch":1})"));
    model.ingestSessionEvent(QByteArrayLiteral(
        R"({"authority":"accountContext","accountUserId":"new","accountEpoch":1,"type":"session.list","sessions":[{"kind":"local","id":"fresh","incarnationId":"inc","name":"Fresh","project":"/repo","mode":"normal","status":"active","recovery":"live","scope":"justMe","access":"inject"}]})"));

    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(
        model.data(model.index(0), kodosi::SessionCatalogModel::SessionIdRole).toString(),
        QStringLiteral("fresh"));
}

void SessionCatalogTest::resolvesRoomAssignmentIdentityNatively()
{
    kodosi::SessionCatalogModel model;
    model.ingestAuthEvent(
        QByteArrayLiteral(R"({"type":"auth.ready","userId":"user","accountEpoch":1})"));
    model.ingestSessionEvent(QByteArrayLiteral(
        R"({"authority":"accountContext","accountUserId":"user","accountEpoch":1,"type":"session.list","sessions":[{"kind":"local","id":"local","incarnationId":"runtime-incarnation","name":"Local","project":"/repo","mode":"normal","status":"active","recovery":"live","scope":"room","access":"inject","roomId":"mission","backendSessionId":"backend","backendIncarnationId":"backend-incarnation"},{"kind":"remote","id":"remote","incarnationId":"remote-incarnation","name":"Remote","project":"/repo","mode":"normal","status":"active","scope":"room","access":"inject","ownerUserId":"owner","permissions":1,"roomId":"mission","connectionState":"connected","accessState":"ready"}]})"));

    const auto local = model.actionContext(QStringLiteral("local"));
    QVERIFY(local);
    QCOMPARE(local->roomId, QStringLiteral("mission"));
    QCOMPARE(local->assignmentSessionId, QStringLiteral("backend"));
    QCOMPARE(
        local->assignmentIncarnationId,
        QStringLiteral("backend-incarnation"));
    const auto remote = model.actionContext(QStringLiteral("remote"));
    QVERIFY(remote);
    QCOMPARE(remote->assignmentSessionId, QStringLiteral("remote"));
    QCOMPARE(
        remote->assignmentIncarnationId,
        QStringLiteral("remote-incarnation"));
    QVERIFY(!model.roleNames().values().contains(
        QByteArrayLiteral("incarnationId")));
    QVERIFY(!model.roleNames().values().contains(
        QByteArrayLiteral("backendSessionId")));
}

void SessionCatalogTest::retainsPresentationSafeSemanticActions()
{
    kodosi::SessionCatalogModel model;
    model.ingestAuthEvent(
        QByteArrayLiteral(R"({"type":"auth.ready","userId":"user","accountEpoch":1})"));
    model.ingestSessionEvent(QByteArrayLiteral(
        R"({"authority":"accountContext","accountUserId":"user","accountEpoch":1,"type":"session.list","sessions":[{"kind":"local","id":"one","incarnationId":"inc-1","name":"One","project":"/repo","mode":"normal","status":"active","recovery":"live","scope":"justMe","access":"inject","semanticActions":{"queue":true,"steer":false,"stopAndSend":true}}]})"));

    const auto context = model.actionContext(QStringLiteral("one"));
    QVERIFY(context);
    QVERIFY(context->canQueue);
    QVERIFY(!context->canSteer);
    QVERIFY(context->canStopAndSend);
    QVERIFY(model.data(model.index(0), kodosi::SessionCatalogModel::CanQueueRole).toBool());
    QVERIFY(!model.data(model.index(0), kodosi::SessionCatalogModel::CanSteerRole).toBool());
    QVERIFY(model.data(
        model.index(0),
        kodosi::SessionCatalogModel::CanStopAndSendRole).toBool());
    QVERIFY(!model.roleNames().values().contains(
        QByteArrayLiteral("semanticActions")));
}

void SessionCatalogTest::omissionDefaultsSemanticActionsToFalse()
{
    kodosi::SessionCatalogModel model;
    model.ingestAuthEvent(
        QByteArrayLiteral(R"({"type":"auth.ready","userId":"user","accountEpoch":1})"));
    model.ingestSessionEvent(QByteArrayLiteral(
        R"({"authority":"accountContext","accountUserId":"user","accountEpoch":1,"type":"session.list","sessions":[{"kind":"local","id":"one","incarnationId":"inc-1","name":"One","project":"/repo","mode":"normal","status":"active","recovery":"live","scope":"justMe","access":"inject"}]})"));

    const auto context = model.actionContext(QStringLiteral("one"));
    QVERIFY(context);
    QVERIFY(!context->canQueue);
    QVERIFY(!context->canSteer);
    QVERIFY(!context->canStopAndSend);
}

void SessionCatalogTest::malformedSemanticActionsAreRejected()
{
    kodosi::SessionCatalogModel model;
    model.ingestAuthEvent(
        QByteArrayLiteral(R"({"type":"auth.ready","userId":"user","accountEpoch":1})"));
    model.ingestSessionEvent(QByteArrayLiteral(
        R"({"authority":"accountContext","accountUserId":"user","accountEpoch":1,"type":"session.list","sessions":[{"kind":"local","id":"one","incarnationId":"inc-1","name":"One","project":"/repo","mode":"normal","status":"active","recovery":"live","scope":"justMe","access":"inject","semanticActions":{"queue":true,"steer":"yes","stopAndSend":false}}]})"));
    QCOMPARE(model.rowCount(), 0);

    model.ingestSessionEvent(QByteArrayLiteral(
        R"({"authority":"accountContext","accountUserId":"user","accountEpoch":1,"type":"session.upsert","session":{"kind":"local","id":"one","incarnationId":"inc-1","name":"One","project":"/repo","mode":"normal","status":"active","recovery":"live","scope":"justMe","access":"inject","semanticActions":true}})"));
    QCOMPARE(model.rowCount(), 0);
}

void SessionCatalogTest::projectsProtocol36TerminalCapabilities()
{
    kodosi::SessionCatalogModel model;
    model.ingestAuthEvent(QByteArray(ready));
    model.ingestSessionEvent(QByteArrayLiteral(
        R"({"authority":"accountContext","accountUserId":"user","accountEpoch":1,"type":"session.list","sessions":[{"kind":"local","id":"local","incarnationId":"local-inc","name":"Local","project":"/repo","mode":"normal","status":"active","recovery":"live","scope":"justMe","access":"inject"},{"kind":"remote","id":"view-only","incarnationId":"view-inc","name":"View only","project":"/repo","mode":"normal","status":"active","scope":"room","access":"view","ownerUserId":"owner","permissions":1,"connectionState":"connected","accessState":"ready"},{"kind":"remote","id":"interactive","incarnationId":"interactive-inc","name":"Interactive","project":"/repo","mode":"normal","status":"active","scope":"room","access":"inject","permissions":11,"connectionState":"connected","accessState":"ready"},{"kind":"remote","id":"resizable","incarnationId":"resizable-inc","name":"Resizable","project":"/repo","mode":"normal","status":"active","scope":"room","access":"inject","permissions":15,"connectionState":"connected","accessState":"ready"}]})"));

    const auto local = model.presentationSession(QStringLiteral("local"));
    QVERIFY(local);
    QVERIFY(local->canRetainPresentation);
    QVERIFY(local->isStageReady);
    QVERIFY(local->canSendInput);
    QVERIFY(local->canRetainFocus);
    QVERIFY(local->canSendFocus);
    QVERIFY(local->canResize);

    const auto readOnly =
        model.presentationSession(QStringLiteral("view-only"));
    QVERIFY(readOnly);
    QVERIFY(readOnly->canRetainPresentation);
    QVERIFY(readOnly->isStageReady);
    QVERIFY(readOnly->isRemoteConnectable);
    QVERIFY(!readOnly->canSendInput);
    QVERIFY(!readOnly->canRetainFocus);
    QVERIFY(!readOnly->canSendFocus);
    QVERIFY(!readOnly->canResize);

    const auto interactive =
        model.presentationSession(QStringLiteral("interactive"));
    QVERIFY(interactive);
    QVERIFY(interactive->canSendInput);
    QVERIFY(interactive->canRetainFocus);
    QVERIFY(interactive->canSendFocus);
    QVERIFY(!interactive->canResize);

    const auto resizable =
        model.presentationSession(QStringLiteral("resizable"));
    QVERIFY(resizable);
    QVERIFY(resizable->canResize);
}

void SessionCatalogTest::separatesDisplayOwnerFromOwnerIdentity()
{
    kodosi::SessionCatalogModel model;
    model.ingestAuthEvent(QByteArray(ready));
    model.ingestSessionEvent(QByteArrayLiteral(
        R"({"authority":"accountContext","accountUserId":"user","accountEpoch":1,"type":"session.list","sessions":[{"kind":"remote","id":"owned","incarnationId":"owned-inc","name":"Owned","project":"/repo","mode":"normal","status":"active","scope":"room","access":"inject","ownerUserId":"user","permissions":511,"connectionState":"connected","accessState":"ready"},{"kind":"remote","id":"participant","incarnationId":"participant-inc","name":"Participant","project":"/repo","mode":"normal","status":"active","scope":"room","access":"inject","owner":"Alice","ownerUserId":"alice-id","permissions":511,"connectionState":"connected","accessState":"ready"}]})"));

    QCOMPARE(
        model.data(model.index(0), kodosi::SessionCatalogModel::OwnerRole).toString(),
        QString {});
    QCOMPARE(
        model.data(model.index(1), kodosi::SessionCatalogModel::OwnerRole).toString(),
        QStringLiteral("Alice"));

    FakeCatalogDispatcher dispatcher;
    kodosi::SessionActions actions(dispatcher, model);
    QVERIFY(actions.canInterrupt(QStringLiteral("owned")));
    QVERIFY(actions.canSetMode(QStringLiteral("owned")));
    QVERIFY(model.presentationSession(QStringLiteral("owned"))->canResize);
    QVERIFY(!actions.canInterrupt(QStringLiteral("participant")));
    QVERIFY(!actions.canSetMode(QStringLiteral("participant")));
    QVERIFY(!model.presentationSession(QStringLiteral("participant"))->canResize);

    const auto owned = model.actionContext(QStringLiteral("owned"));
    const auto participant = model.actionContext(QStringLiteral("participant"));
    QVERIFY(owned);
    QVERIFY(participant);
    QCOMPARE(owned->owner, QString {});
    QCOMPARE(owned->ownerUserId, QStringLiteral("user"));
    QCOMPARE(participant->owner, QStringLiteral("Alice"));
    QCOMPARE(participant->ownerUserId, QStringLiteral("alice-id"));
}

void SessionCatalogTest::typedPresentationQueryTracksLiveUpserts()
{
    kodosi::SessionCatalogModel model;
    model.ingestAuthEvent(QByteArray(ready));
    model.ingestSessionEvent(QByteArrayLiteral(
        R"({"authority":"accountContext","accountUserId":"user","accountEpoch":1,"type":"session.list","sessions":[{"kind":"local","id":"changing","incarnationId":"inc","name":"Changing","project":"/repo","mode":"normal","status":"active","recovery":"live","scope":"justMe","access":"inject"}]})"));
    QVERIFY(model.hasAuthoritativeSnapshot());
    auto presentation =
        model.presentationSession(QStringLiteral("changing"));
    QVERIFY(presentation);
    QCOMPARE(presentation->kind, QStringLiteral("local"));
    QVERIFY(presentation->canSendInput);

    model.ingestSessionEvent(QByteArrayLiteral(
        R"({"authority":"accountContext","accountUserId":"user","accountEpoch":1,"type":"session.upsert","session":{"kind":"remote","id":"changing","incarnationId":"inc","name":"Changing","project":"/repo","mode":"normal","status":"active","scope":"room","access":"view","ownerUserId":"owner","permissions":1,"connectionState":"connected","accessState":"ready"}})"));
    presentation =
        model.presentationSession(QStringLiteral("changing"));
    QVERIFY(presentation);
    QCOMPARE(presentation->kind, QStringLiteral("remote"));
    QVERIFY(presentation->canRetainPresentation);
    QVERIFY(!presentation->canSendInput);
    QVERIFY(!presentation->canResize);

    const auto qmlPresentation =
        model.presentationForSession(QStringLiteral("changing"));
    QCOMPARE(qmlPresentation.value(QStringLiteral("kind")).toString(),
             QStringLiteral("remote"));
    QVERIFY(qmlPresentation
                .value(QStringLiteral("canRetainPresentation"))
                .toBool());
    QVERIFY(!qmlPresentation.value(QStringLiteral("canSendInput")).toBool());
}

void SessionCatalogTest::refreshImmediateFailureAndDeduplication()
{
    FakeCatalogDispatcher dispatcher;
    kodosi::SessionCatalogModel model(100);
    kodosi::SessionActions actions(dispatcher, model);
    model.ingestAuthEvent(QByteArray(ready));

    dispatcher.rejectSessionList = true;
    actions.ingestAuthEvent(QByteArray(ready));
    QCOMPARE(
        model.authorityState(),
        kodosi::SessionCatalogModel::AuthorityState::Failed);
    QVERIFY(!model.authorityError().isEmpty());

    dispatcher.rejectSessionList = false;
    QVERIFY(actions.refresh());
    QVERIFY(actions.refresh());
    QCOMPARE(dispatcher.commands.size(), 1);
    QCOMPARE(
        model.authorityState(),
        kodosi::SessionCatalogModel::AuthorityState::Loading);
}

void SessionCatalogTest::refreshTimeoutAndEventFailure()
{
    FakeCatalogDispatcher dispatcher;
    kodosi::SessionCatalogModel model(10);
    kodosi::SessionActions actions(dispatcher, model);
    model.ingestAuthEvent(QByteArray(ready));
    actions.ingestAuthEvent(QByteArray(ready));

    QTRY_COMPARE_WITH_TIMEOUT(
        model.authorityState(),
        kodosi::SessionCatalogModel::AuthorityState::Failed,
        250);
    QVERIFY(model.authorityError().contains(
        QStringLiteral("did not respond")));

    QVERIFY(actions.refresh());
    model.ingestSessionEvent(QByteArrayLiteral(
        R"({"authority":"accountContext","accountUserId":"user","accountEpoch":1,"type":"session.error","operation":"session.list","message":"backend unavailable"})"));
    QCOMPARE(
        model.authorityState(),
        kodosi::SessionCatalogModel::AuthorityState::Failed);
    QVERIFY(model.authorityError().contains(
        QStringLiteral("backend unavailable")));

    model.ingestSessionEvent(QByteArrayLiteral(
        R"({"authority":"accountContext","accountUserId":"user","accountEpoch":1,"type":"session.error","operation":"session.list","sessionId":null,"requestId":null,"message":"nullable failure"})"));
    QCOMPARE(
        model.authorityState(),
        kodosi::SessionCatalogModel::AuthorityState::Failed);
    QVERIFY(model.authorityError().contains(
        QStringLiteral("nullable failure")));
}

void SessionCatalogTest::authorityRecoversAndResets()
{
    FakeCatalogDispatcher dispatcher;
    kodosi::SessionCatalogModel model(10);
    kodosi::SessionActions actions(dispatcher, model);
    model.ingestAuthEvent(QByteArray(ready));
    actions.ingestAuthEvent(QByteArray(ready));
    QTRY_COMPARE_WITH_TIMEOUT(
        model.authorityState(),
        kodosi::SessionCatalogModel::AuthorityState::Failed,
        250);

    model.ingestSessionEvent(QByteArray(snapshot));
    QCOMPARE(
        model.authorityState(),
        kodosi::SessionCatalogModel::AuthorityState::Loaded);
    QVERIFY(model.authorityError().isEmpty());

    dispatcher.rejectSessionList = true;
    QVERIFY(!actions.refresh());
    QCOMPARE(
        model.authorityState(),
        kodosi::SessionCatalogModel::AuthorityState::Failed);
    QVERIFY(model.authorityError().contains(QStringLiteral("Try again")));
    dispatcher.rejectSessionList = false;
    model.ingestSessionEvent(QByteArray(snapshot));
    QCOMPARE(
        model.authorityState(),
        kodosi::SessionCatalogModel::AuthorityState::Loaded);

    QVERIFY(actions.refresh());
    QTRY_COMPARE_WITH_TIMEOUT(
        model.authorityState(),
        kodosi::SessionCatalogModel::AuthorityState::Failed,
        250);
    QVERIFY(model.authorityError().contains(
        QStringLiteral("did not respond")));
    model.ingestSessionEvent(QByteArray(snapshot));
    QCOMPARE(
        model.authorityState(),
        kodosi::SessionCatalogModel::AuthorityState::Loaded);

    QVERIFY(actions.refresh());
    model.ingestSessionEvent(QByteArrayLiteral(
        R"({"authority":"accountContext","accountUserId":"user","accountEpoch":1,"type":"session.error","operation":"session.list","sessionId":null,"requestId":null,"message":"refresh failed"})"));
    QCOMPARE(
        model.authorityState(),
        kodosi::SessionCatalogModel::AuthorityState::Failed);
    QVERIFY(model.authorityError().contains(
        QStringLiteral("refresh failed")));
    model.ingestSessionEvent(QByteArray(snapshot));

    model.ingestAuthEvent(QByteArrayLiteral(
        R"({"type":"auth.ready","userId":"other","accountEpoch":2})"));
    actions.ingestAuthEvent(QByteArrayLiteral(
        R"({"type":"auth.ready","userId":"other","accountEpoch":2})"));
    QCOMPARE(
        model.authorityState(),
        kodosi::SessionCatalogModel::AuthorityState::Loading);
    QVERIFY(model.authorityError().isEmpty());

    model.ingestSessionEvent(QByteArrayLiteral(
        R"({"authority":"accountContext","accountUserId":"other","accountEpoch":2,"type":"session.list","sessions":[]})"));
    QCOMPARE(
        model.authorityState(),
        kodosi::SessionCatalogModel::AuthorityState::Loaded);

    model.resetRuntimeAuthority();
    QCOMPARE(
        model.authorityState(),
        kodosi::SessionCatalogModel::AuthorityState::Loading);
    QVERIFY(model.authorityError().isEmpty());
}

QTEST_GUILESS_MAIN(SessionCatalogTest)

#include "tst_session_catalog.moc"
