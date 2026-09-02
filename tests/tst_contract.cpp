#include "bridge/RuntimeBridge.hpp"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class ContractTest final : public QObject
{
    Q_OBJECT

private slots:
    void dependencyBaselineIsCurrent();
    void desktopStateQmlContractIsNativeOwned();
    void runtimeBridgeStartsAndStopsPinnedAbi();
};

void ContractTest::dependencyBaselineIsCurrent()
{
    QFile file(QStringLiteral(KODOSI_SOURCE_DIR "/dependencies.lock.json"));
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));

    const auto document = QJsonDocument::fromJson(file.readAll());
    QVERIFY(document.isObject());
    const auto root = document.object();
    QCOMPARE(root.value(QStringLiteral("schemaVersion")).toInt(), 1);

    const auto frameworks = root.value(QStringLiteral("frameworks")).toObject();
    const auto qt = frameworks.value(QStringLiteral("qt")).toObject();
    QCOMPARE(qt.value(QStringLiteral("version")).toString(), QStringLiteral("6.11.2"));

    const auto tools = root.value(QStringLiteral("tools")).toObject();
    const auto cmake = tools.value(QStringLiteral("cmake")).toObject();
    QCOMPARE(cmake.value(QStringLiteral("version")).toString(), QStringLiteral("4.4.3"));

    const auto kodosi = root.value(QStringLiteral("kodosi")).toObject();
    QCOMPARE(kodosi.value(QStringLiteral("ffiAbiVersion")).toInt(), 5);
    QCOMPARE(kodosi.value(QStringLiteral("desktopProtocolVersion")).toInt(), 36);
}

void ContractTest::desktopStateQmlContractIsNativeOwned()
{
    QFile mainQml(QStringLiteral(KODOSI_SOURCE_DIR "/src/qml/Main.qml"));
    QVERIFY2(mainQml.open(QIODevice::ReadOnly), qPrintable(mainQml.errorString()));
    const auto mainSource = mainQml.readAll();
    QVERIFY(mainSource.contains("visible: false"));
    QVERIFY(!mainSource.contains("property int activeView"));
    QVERIFY(!mainSource.contains("property bool sidebarOpen"));
    QVERIFY(!mainSource.contains("property string selectedSessionId"));
    QVERIFY(!mainSource.contains("width: 1240"));
    QVERIFY(!mainSource.contains("height: 800"));
    QVERIFY(mainSource.contains(
        "Models.DesktopState.activeView = 0"));
    QVERIFY(mainSource.contains(
        "Models.DesktopState.sidebarOpen ="));
    QVERIFY(mainSource.contains(
        "Models.DesktopState.selectSession(sessionId)"));
    QVERIFY(mainSource.contains(
        "target: Models.DesktopState"));
    QVERIFY(!mainSource.contains(
        "Models.DesktopState.selectedSessionId ="));
    QVERIFY(!mainSource.contains("onRemoteRestoreRequested"));

    QFile compositionCpp(
        QStringLiteral(KODOSI_SOURCE_DIR "/src/app/main.cpp"));
    QVERIFY2(
        compositionCpp.open(QIODevice::ReadOnly),
        qPrintable(compositionCpp.errorString()));
    const auto restoreCompositionSource = compositionCpp.readAll();
    QVERIFY(restoreCompositionSource.contains(
        "&kodosi::DesktopStateModel::remoteRestoreRequested"));
    QVERIFY(restoreCompositionSource.contains("Qt::DirectConnection"));
    QVERIFY(restoreCompositionSource.contains(
        "desktopState.reportRemoteRestoreDispatch("));

    QFile sidebarQml(
        QStringLiteral(
            KODOSI_SOURCE_DIR "/src/qml/Workbench/SessionSidebar.qml"));
    QVERIFY2(
        sidebarQml.open(QIODevice::ReadOnly),
        qPrintable(sidebarQml.errorString()));
    const auto sidebarSource = sidebarQml.readAll();
    QVERIFY(sidebarSource.contains("signal sessionSelectionRequested("));
    QVERIFY(!sidebarSource.contains(
        "selectedSessionId = item.sessionId"));
    QVERIFY(!sidebarSource.contains("selectedSessionId = \"\""));
    QVERIFY(!sidebarSource.contains(
        "selectedSessionId = current.sessionId"));
    QVERIFY(!sidebarSource.contains(
        "function selectFirstAvailable()"));
    QVERIFY(!sidebarSource.contains(
        "function reconcileSelection()"));
    QVERIFY(sidebarSource.contains(
        "sessionSelectionRequested(item.sessionId, false)"));
    QVERIFY(sidebarSource.contains(
        "sessionSelectionRequested(item.sessionId, true)"));

    QFile stageQml(
        QStringLiteral(
            KODOSI_SOURCE_DIR "/src/qml/Workbench/TerminalStage.qml"));
    QVERIFY2(
        stageQml.open(QIODevice::ReadOnly),
        qPrintable(stageQml.errorString()));
    const auto stageSource = stageQml.readAll();
    QVERIFY(stageSource.contains(
        "model: Models.TerminalTiling"));
    QVERIFY(stageSource.contains(
        "value: Models.DesktopState.stagedSessionIds"));
    QVERIFY(stageSource.contains(
        "interactive: !root.focusMode"));
    QVERIFY(stageSource.contains(
        "if (root.focusMode && contentY !== 0)"));
    QVERIFY(stageSource.contains(
        "function setTerminalSubtreeAccessibility(item, ignored)"));
    QVERIFY(stageSource.contains(
        "onEntryVisibleChanged:"));
    QVERIFY(stageSource.contains(
        "root.setTerminalSubtreeAccessibility("));
    QVERIFY(stageSource.contains(
        "Accessible.name: qsTr(\"Terminal grid scroll bar\")"));
    QVERIFY(stageSource.contains(
        "Accessible.name: qsTr(\"Terminal stage, %1\")"));
    QVERIFY(stageSource.contains(
        "Accessible.name: qsTr(\"Terminal grid, %1\")"));
    QVERIFY(stageSource.contains(
        "qsTr(\"Scroll for more terminals\")"));
    QVERIFY(stageSource.contains(
        "anchors.rightMargin: root.verticalOverflow"));
    QVERIFY(stageSource.contains("prominent: true"));
    QVERIFY(stageSource.contains(
        "anchors.top: stageViewport.top"));
    QVERIFY(stageSource.contains(
        "anchors.bottom: stageViewport.bottom"));
    QVERIFY(stageSource.contains(
        "position * stageFlick.contentHeight"));
    QVERIFY(stageSource.contains(
        "iconName: \"chevron-left\""));
    QVERIFY(!stageSource.contains(
        "qsTr(\"Focused: %1\")"));
    QVERIFY(stageSource.contains(
        "accessibilityTextScale"));
    QVERIFY(mainSource.contains(
        "interactionEnabled: !window.blockingOverlayOpen"));
    QVERIFY(stageSource.contains("scheduleTerminalFocus()"));
    QVERIFY(stageSource.contains("registerTerminalTile(item)"));
    QVERIFY(stageSource.contains("terminalTiles[index]"));
    const auto unregisterStart =
        stageSource.indexOf("function unregisterTerminalTile(tile)");
    const auto focusSchedule =
        stageSource.indexOf("scheduleTerminalFocus()", unregisterStart);
    const auto nextFunction =
        stageSource.indexOf("function scheduleTerminalFocus()", unregisterStart);
    QVERIFY(unregisterStart >= 0);
    QVERIFY(focusSchedule > unregisterStart);
    QVERIFY(focusSchedule < nextFunction);
    QVERIFY(stageSource.contains("Models.DesktopState.lastError"));
    QVERIFY(stageSource.contains("Models.DesktopState.clearError()"));
    QVERIFY(!stageSource.contains("Math.ceil("));

    QFile tileQml(
        QStringLiteral(
            KODOSI_SOURCE_DIR "/src/qml/Workbench/TerminalTile.qml"));
    QVERIFY2(
        tileQml.open(QIODevice::ReadOnly),
        qPrintable(tileQml.errorString()));
    const auto tileSource = tileQml.readAll();
    QVERIFY(tileSource.contains("width < 340 ? 0"));
    QVERIFY(tileSource.contains("width < 640 ? 1 : 2"));
    QVERIFY(tileSource.contains("visible: root.chromeTier >= 1"));
    QVERIFY(tileSource.contains("visible: root.chromeTier < 2"));
    QVERIFY(tileSource.contains(
        "Accessible.description: terminalAccessibilityStatus"));
    QVERIFY(tileSource.contains(
        "qsTr(\"Connecting terminal\")"));
    QVERIFY(tileSource.contains(
        "qsTr(\"Terminal unavailable: %1\")"));
    QVERIFY(tileSource.contains(
        "? KodosiTheme.accentMuted"));
    QVERIFY(!tileSource.contains("height: 2"));
    QVERIFY(tileSource.contains("text: qsTr(\"Read only\")"));
    QVERIFY(tileSource.contains(
        "focusedSizeAuthority: root.focusedSizeAuthority"));
    QVERIFY(tileSource.contains("function forceTerminalFocus()"));
    QVERIFY(tileSource.contains(
        "terminal.forceActiveFocus(Qt.ShortcutFocusReason)"));

    QFile dividerQml(
        QStringLiteral(
            KODOSI_SOURCE_DIR "/src/qml/Workbench/TerminalDivider.qml"));
    QVERIFY2(
        dividerQml.open(QIODevice::ReadOnly),
        qPrintable(dividerQml.errorString()));
    const auto dividerSource = dividerQml.readAll();
    QVERIFY(dividerSource.contains("preventStealing: true"));
    QVERIFY(dividerSource.contains("property real minimumValue: 0"));
    QVERIFY(dividerSource.contains("property real maximumValue: 100"));
    QVERIFY(dividerSource.contains("property real stepSize: 1"));
    QVERIFY(dividerSource.contains("stableCoordinate(mouse)"));
    QVERIFY(dividerSource.contains(
        "appliedDelta += root.applyAdjustment(incremental)"));
    QVERIFY(dividerSource.contains(
        "const incremental = cumulative - appliedDelta"));

    QFile tilingCpp(
        QStringLiteral(
            KODOSI_SOURCE_DIR
            "/src/models/TerminalTilingLayoutModel.cpp"));
    QVERIFY2(
        tilingCpp.open(QIODevice::ReadOnly),
        qPrintable(tilingCpp.errorString()));
    QVERIFY(!tilingCpp.readAll().contains("beginResetModel()"));

    QFile settingsQml(
        QStringLiteral(
            KODOSI_SOURCE_DIR "/src/qml/Settings/SettingsDrawer.qml"));
    QVERIFY2(
        settingsQml.open(QIODevice::ReadOnly),
        qPrintable(settingsQml.errorString()));
    QVERIFY(settingsQml.readAll().contains("Models.DesktopState.lastError"));

    QFile mainCpp(QStringLiteral(KODOSI_SOURCE_DIR "/src/app/main.cpp"));
    QVERIFY2(mainCpp.open(QIODevice::ReadOnly), qPrintable(mainCpp.errorString()));
    const auto compositionSource = mainCpp.readAll();
    QCOMPARE(
        compositionSource.count("desktopState.attachWindow("),
        1);
    QVERIFY(!compositionSource.contains("mainWindow->show()"));
    QVERIFY(compositionSource.contains(
        "argument.startsWith(QStringLiteral(\"--ui-probe-\"))"));
    QVERIFY(compositionSource.contains(
        "desktopStateSettings(!syntheticMode)"));
    QVERIFY(compositionSource.contains(
        "build/synthetic-config/"));
    QVERIFY(compositionSource.contains(
        "removeSyntheticConfig"));
    QVERIFY(compositionSource.contains(
        "\"XDG_CONFIG_HOME\""));

    QFile desktopStateCpp(
        QStringLiteral(
            KODOSI_SOURCE_DIR "/src/models/DesktopStateModel.cpp"));
    QVERIFY2(
        desktopStateCpp.open(QIODevice::ReadOnly),
        qPrintable(desktopStateCpp.errorString()));
    QVERIFY(desktopStateCpp.readAll().contains(
        "&QWindow::screenChanged"));

    QFile smokeScript(
        QStringLiteral(KODOSI_SOURCE_DIR "/scripts/smoke-ui-probe.sh"));
    QVERIFY2(
        smokeScript.open(QIODevice::ReadOnly),
        qPrintable(smokeScript.errorString()));
    const auto smokeSource = smokeScript.readAll();
    QVERIFY(smokeSource.contains(
        "config_dir=\"$artifact_dir/xdg-config.$$\""));
    QVERIFY(smokeSource.contains(
        "data_root=\"$artifact_dir/kodosi-data.$$\""));
    QVERIFY(smokeSource.contains(
        "production_data_root=\"$artifact_dir/kodosi-production-data.$$\""));
    QVERIFY(smokeSource.contains("mkdir -p \"$config_dir\""));
    QVERIFY(smokeSource.contains("XDG_CONFIG_HOME=\"$config_dir\""));
    QVERIFY(smokeSource.contains("KODOSI_DATA_ROOT=\"$data_root\""));
    QVERIFY(smokeSource.contains(
        "KODOSI_PRODUCTION_DATA_ROOT=\"$production_data_root\""));
    QVERIFY(smokeSource.contains(
        "rm -rf -- \"$config_dir\" \"$data_root\" \"$production_data_root\""));
    QVERIFY(smokeSource.contains(
        "--text \"Ready proof\""));
    QVERIFY(smokeSource.contains(
        ">\"$artifact_dir/tiling-ready-tree.json\""));
    QVERIFY(smokeSource.contains(
        "native terminal accessible text is empty"));
}

void ContractTest::runtimeBridgeStartsAndStopsPinnedAbi()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto isolatedRoot = directory.filePath(QStringLiteral("isolated"));
    const auto productionRoot = directory.filePath(QStringLiteral("production"));
    QVERIFY(QDir().mkpath(isolatedRoot));
    QVERIFY(QDir().mkpath(productionRoot));

    const auto previousDataRoot = qgetenv("KODOSI_DATA_ROOT");
    const auto previousProductionRoot = qgetenv("KODOSI_PRODUCTION_DATA_ROOT");
    const auto restoreEnvironment = qScopeGuard([&] {
        qputenv("KODOSI_DATA_ROOT", previousDataRoot);
        qputenv("KODOSI_PRODUCTION_DATA_ROOT", previousProductionRoot);
    });
    qputenv("KODOSI_DATA_ROOT", isolatedRoot.toUtf8());
    qputenv("KODOSI_PRODUCTION_DATA_ROOT", productionRoot.toUtf8());

    kodosi::RuntimeBridge runtime;
    QVERIFY(!runtime.isRunning());
    const auto started = runtime.start();
    if (!started) {
        QFAIL(qPrintable(started.error().message));
    }
    QVERIFY(runtime.isRunning());
    runtime.stop();
    QVERIFY(!runtime.isRunning());

    QSignalSpy events(&runtime, &kodosi::RuntimeBridge::eventReceived);
    QCoreApplication::processEvents();
    QCOMPARE(events.count(), 0);

    const auto restarted = runtime.start();
    if (!restarted) {
        QFAIL(qPrintable(restarted.error().message));
    }
    QTRY_VERIFY_WITH_TIMEOUT(!events.isEmpty(), 2'000);
    runtime.stop();
}

QTEST_MAIN(ContractTest)

#include "tst_contract.moc"
