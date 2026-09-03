#include "models/RuntimeDiagnosticsModel.hpp"

#include <QFile>
#include <QSignalSpy>
#include <QtTest/QTest>

class RuntimeDiagnosticsModelTest final : public QObject {
    Q_OBJECT

private slots:
    void tracksHeartbeatErrorsAndHealth();
    void healthyStateRetainsRoutinePendingCleanup();
    void rejectsMalformedHealthAtomically();
    void clearsRuntimeAuthority();
    void notifiesRuntimeStopWithoutPriorHealth();
    void diagnosticsSurfaceExposesSwiftParityContracts();
};

void RuntimeDiagnosticsModelTest::tracksHeartbeatErrorsAndHealth()
{
    kodosi::RuntimeBridge runtime;
    kodosi::RuntimeDiagnosticsModel model(runtime);
    QCOMPARE(model.protocolVersion(), 37U);
    QCOMPARE(model.runtimeContract(), QStringLiteral("desktop-runtime"));
    QVERIFY(!model.systemReady());
    QVERIFY(!model.hasRuntimeHealth());

    model.ingestSystemEvent(QByteArrayLiteral("{\"type\":\"heartbeat\"}"));
    QVERIFY(model.systemReady());

    model.ingestSystemEvent(QByteArrayLiteral(
        "{\"type\":\"error\",\"message\":\"remote operation failed\"}"));
    QCOMPARE(
        model.runtimeError(),
        QStringLiteral("remote operation failed"));

    model.ingestSystemEvent(QByteArrayLiteral(
        "{\"type\":\"runtime.health\",\"collaborationCleanup\":{"
        "\"state\":\"quarantined\",\"pendingCount\":3,"
        "\"quarantinedCount\":2,\"message\":\"operator action required\"}}"));
    QVERIFY(model.hasRuntimeHealth());
    QCOMPARE(
        model.cleanupState(),
        kodosi::RuntimeDiagnosticsModel::CleanupState::Quarantined);
    QCOMPARE(model.cleanupPendingCount(), 3U);
    QCOMPARE(model.cleanupQuarantinedCount(), 2U);
    QCOMPARE(
        model.cleanupMessage(),
        QStringLiteral("operator action required"));

    model.clearRuntimeError();
    QVERIFY(model.runtimeError().isEmpty());
}

void RuntimeDiagnosticsModelTest::healthyStateRetainsRoutinePendingCleanup()
{
    kodosi::RuntimeBridge runtime;
    kodosi::RuntimeDiagnosticsModel model(runtime);
    model.ingestSystemEvent(QByteArrayLiteral(
        "{\"type\":\"runtime.health\",\"collaborationCleanup\":{"
        "\"state\":\"healthy\",\"pendingCount\":1,"
        "\"quarantinedCount\":0,\"message\":null}}"));

    QVERIFY(model.hasRuntimeHealth());
    QCOMPARE(
        model.cleanupState(),
        kodosi::RuntimeDiagnosticsModel::CleanupState::Healthy);
    QCOMPARE(model.cleanupPendingCount(), 1U);
    QCOMPARE(model.cleanupQuarantinedCount(), 0U);
}

void RuntimeDiagnosticsModelTest::rejectsMalformedHealthAtomically()
{
    kodosi::RuntimeBridge runtime;
    kodosi::RuntimeDiagnosticsModel model(runtime);
    QSignalSpy errors(
        &model,
        &kodosi::RuntimeDiagnosticsModel::decodeError);
    model.ingestSystemEvent(QByteArrayLiteral(
        "{\"type\":\"runtime.health\",\"collaborationCleanup\":{"
        "\"state\":\"healthy\",\"pendingCount\":0,"
        "\"quarantinedCount\":0,\"message\":null}}"));

    for (const auto& malformed : {
             QByteArrayLiteral("{"),
             QByteArrayLiteral("{\"type\":\"runtime.health\"}"),
             QByteArrayLiteral(
                 "{\"type\":\"runtime.health\",\"collaborationCleanup\":{"
                 "\"state\":\"healthy\",\"pendingCount\":-1,"
                 "\"quarantinedCount\":0}}"),
             QByteArrayLiteral(
                 "{\"type\":\"runtime.health\",\"collaborationCleanup\":{"
                 "\"state\":\"healthy\",\"pendingCount\":0.5,"
                 "\"quarantinedCount\":0}}"),
             QByteArrayLiteral(
                 "{\"type\":\"runtime.health\",\"collaborationCleanup\":{"
                 "\"state\":\"healthy\",\"pendingCount\":0,"
                 "\"quarantinedCount\":0,\"message\":7}}"),
         }) {
        model.ingestSystemEvent(malformed);
    }

    QCOMPARE(errors.count(), 5);
    QVERIFY(model.hasRuntimeHealth());
    QCOMPARE(
        model.cleanupState(),
        kodosi::RuntimeDiagnosticsModel::CleanupState::Healthy);
    QCOMPARE(model.cleanupPendingCount(), 0U);
    QCOMPARE(model.cleanupQuarantinedCount(), 0U);
    QVERIFY(model.cleanupMessage().isEmpty());
}

void RuntimeDiagnosticsModelTest::clearsRuntimeAuthority()
{
    kodosi::RuntimeBridge runtime;
    kodosi::RuntimeDiagnosticsModel model(runtime);
    model.ingestSystemEvent(QByteArrayLiteral("{\"type\":\"heartbeat\"}"));
    model.ingestSystemEvent(QByteArrayLiteral(
        "{\"type\":\"error\",\"message\":\"failed\"}"));
    model.ingestSystemEvent(QByteArrayLiteral(
        "{\"type\":\"runtime.health\",\"collaborationCleanup\":{"
        "\"state\":\"unavailable\",\"pendingCount\":0,"
        "\"quarantinedCount\":0}}"));
    QCOMPARE(
        model.cleanupState(),
        kodosi::RuntimeDiagnosticsModel::CleanupState::Unavailable);
    QCOMPARE(model.cleanupPendingCount(), 0U);
    QCOMPARE(model.cleanupQuarantinedCount(), 0U);

    model.resetRuntimeAuthority();
    QVERIFY(!model.systemReady());
    QVERIFY(!model.hasRuntimeHealth());
    QVERIFY(model.runtimeError().isEmpty());
    QCOMPARE(
        model.cleanupState(),
        kodosi::RuntimeDiagnosticsModel::CleanupState::Waiting);
    QCOMPARE(model.cleanupPendingCount(), 0U);
    QCOMPARE(model.cleanupQuarantinedCount(), 0U);
}

void RuntimeDiagnosticsModelTest::notifiesRuntimeStopWithoutPriorHealth()
{
    kodosi::RuntimeBridge runtime;
    kodosi::RuntimeDiagnosticsModel model(runtime);
    QSignalSpy changed(
        &model,
        &kodosi::RuntimeDiagnosticsModel::stateChanged);

    QVERIFY(QMetaObject::invokeMethod(
        &runtime,
        "runningChanged",
        Qt::DirectConnection,
        Q_ARG(bool, false)));
    QCOMPARE(changed.count(), 1);
}

void RuntimeDiagnosticsModelTest::diagnosticsSurfaceExposesSwiftParityContracts()
{
    QFile drawer(
        QStringLiteral(KODOSI_SOURCE_DIR)
        + QStringLiteral("/src/qml/Diagnostics/DiagnosticsDrawer.qml"));
    QVERIFY(drawer.open(QIODevice::ReadOnly));
    const auto qml = drawer.readAll();
    for (const auto& contract : {
             QByteArrayLiteral("objectName: \"panel.diagnostics\""),
             QByteArrayLiteral(
                 "\"panel.diagnostics.runtime.collaborationCleanup\""),
             QByteArrayLiteral("Models.RuntimeDiagnostics.protocolVersion"),
             QByteArrayLiteral("Models.SessionActions.inactiveCleanupCount"),
             QByteArrayLiteral("Models.AgentGlobal.mcpServers"),
             QByteArrayLiteral("Models.People.incomingCount"),
             QByteArrayLiteral(
                 "Models.RuntimeDiagnostics.cleanupPendingCount === 0"),
             QByteArrayLiteral(
                 "Models.RuntimeDiagnostics.cleanupState\n"
                 "                === Models.RuntimeDiagnostics.Quarantined"),
             QByteArrayLiteral("qsTr(\"Cleanup store is quarantined\")"),
             QByteArrayLiteral("activeFocusOnTab: false"),
         }) {
        QVERIFY2(qml.contains(contract), contract.constData());
    }

    QFile main(
        QStringLiteral(KODOSI_SOURCE_DIR)
        + QStringLiteral("/src/qml/Main.qml"));
    QVERIFY(main.open(QIODevice::ReadOnly));
    const auto mainQml = main.readAll();
    QVERIFY(mainQml.contains(QByteArrayLiteral(
        "sequence: \"Ctrl+Shift+D\"")));
    QVERIFY(mainQml.contains(QByteArrayLiteral(
        "enabled: diagnosticsDrawer.opened\n"
        "            || window.shortcutContextAvailable")));
    QVERIFY(mainQml.contains(QByteArrayLiteral(
        "RuntimeHealthBanner {")));
}

QTEST_APPLESS_MAIN(RuntimeDiagnosticsModelTest)

#include "tst_runtime_diagnostics_model.moc"
