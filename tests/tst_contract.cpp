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
    void desktopFileIntegrationContractIsNativeOwned();
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
        "build/synthetic-roots/"));
    QVERIFY(compositionSource.contains(
        "removeSyntheticConfig"));
    QVERIFY(compositionSource.contains(
        "\"XDG_CONFIG_HOME\""));
    QVERIFY(compositionSource.contains(
        "\"XDG_STATE_HOME\""));

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

void ContractTest::desktopFileIntegrationContractIsNativeOwned()
{
    QFile header(QStringLiteral(
        KODOSI_SOURCE_DIR
        "/src/platform/DesktopFileIntegration.hpp"));
    QVERIFY2(header.open(QIODevice::ReadOnly), qPrintable(header.errorString()));
    const auto headerSource = header.readAll();
    QVERIFY(headerSource.contains("Q_PROPERTY(bool busy"));
    QVERIFY(headerSource.contains("Q_PROPERTY(ErrorCode errorCode"));
    QVERIFY(headerSource.contains("requestDirectory("));
    QVERIFY(headerSource.contains("cancelDirectory("));
    QVERIFY(headerSource.contains("openPath("));
    QVERIFY(headerSource.contains("openSessionProject("));
    QVERIFY(headerSource.contains("canOpenSessionProject("));
    QVERIFY(headerSource.contains("QPointer<QWindow> transientParent"));
    QVERIFY(headerSource.contains("directoryPickCancelled("));
    QVERIFY(headerSource.contains("operationFailed("));

    QFile implementation(QStringLiteral(
        KODOSI_SOURCE_DIR
        "/src/platform/DesktopFileIntegration.cpp"));
    QVERIFY2(
        implementation.open(QIODevice::ReadOnly),
        qPrintable(implementation.errorString()));
    const auto implementationSource = implementation.readAll();
    QVERIFY(implementationSource.contains("QFileDialog::Directory"));
    QVERIFY(implementationSource.contains("Qt::ApplicationModal"));
    QVERIFY(implementationSource.contains("dialog->show()"));
    QVERIFY(!implementationSource.contains("dialog->open()"));
    QVERIFY(implementationSource.contains(
        "dialog->setAttribute(Qt::WA_NativeWindow)"));
    QVERIFY(implementationSource.contains(
        "dialogWindow->setTransientParent(request.transientParent)"));
    QVERIFY(!implementationSource.contains("winId()"));
    QVERIFY(!implementationSource.contains("focusWindow()"));
    QVERIFY(implementationSource.contains(
        "QFileDialog::DontUseNativeDialog, false"));
    QVERIFY(implementationSource.contains(
        "setSupportedSchemes({QStringLiteral(\"file\")})"));
    QVERIFY(implementationSource.contains(
        "QDesktopServices::openUrl(url)"));
    QVERIFY(implementationSource.contains("QUrl::fromLocalFile("));
    QVERIFY(implementationSource.contains(
        "::access(nativePath.constData(), R_OK)"));
    QVERIFY(implementationSource.contains(
        "::access(nativePath.constData(), X_OK)"));
    QVERIFY(!implementationSource.contains("hasAnyPermission"));
    QVERIFY(!implementationSource.contains("std::system"));
    QVERIFY(!implementationSource.contains("QProcess"));
    QVERIFY(!implementationSource.contains("/bin/sh"));

    QFile mainCpp(QStringLiteral(KODOSI_SOURCE_DIR "/src/app/main.cpp"));
    QVERIFY2(mainCpp.open(QIODevice::ReadOnly), qPrintable(mainCpp.errorString()));
    const auto mainSource = mainCpp.readAll();
    QVERIFY(mainSource.contains("QApplication application(argc, argv)"));
    QVERIFY(!mainSource.contains("QGuiApplication application(argc, argv)"));
    QVERIFY(mainSource.contains(
        "qEnvironmentVariableIsSet(\"QT_QPA_PLATFORMTHEME\")"));
    QVERIFY(mainSource.contains(
        "QByteArrayLiteral(\"xdgdesktopportal\")"));
    QVERIFY(
        mainSource.indexOf("qEnvironmentVariableIsSet")
        < mainSource.indexOf("QApplication application"));
    QVERIFY(mainSource.contains(
        "desktopFiles.setTransientParent(mainWindow)"));
    QVERIFY(mainSource.contains("\"XDG_STATE_HOME\""));
    QVERIFY(mainSource.contains(
        "kodosi::ApplicationLogStore applicationLog"));
    QVERIFY(
        mainSource.indexOf("kodosi::ApplicationLogStore applicationLog")
        < mainSource.indexOf("kodosi::RuntimeBridge runtime"));

    QFile sidebar(QStringLiteral(
        KODOSI_SOURCE_DIR
        "/src/qml/Workbench/SessionSidebar.qml"));
    QVERIFY2(sidebar.open(QIODevice::ReadOnly), qPrintable(sidebar.errorString()));
    const auto sidebarSource = sidebar.readAll();
    QVERIFY(sidebarSource.contains(
        "objectName: \"session.create.directory.browse\""));
    QVERIFY(sidebarSource.contains(
        "Models.DesktopFiles.NewSessionWorkingDirectory"));
    QVERIFY(sidebarSource.contains(
        "Models.DesktopFiles.cancelDirectory("));
    QVERIFY(sidebarSource.contains("Component.onDestruction:"));
    QVERIFY(sidebarSource.contains(
        "requestId !== root.directoryPickerRequestId"));
    QVERIFY(sidebarSource.contains(
        "objectName: \"sidebar.session.openProject.\""));
    QVERIFY(sidebarSource.contains(
        "Models.DesktopFiles.openSessionProject("));
    QVERIFY(!sidebarSource.contains("Models.DesktopFiles.openPath("));

    QFile settings(QStringLiteral(
        KODOSI_SOURCE_DIR
        "/src/qml/Settings/SettingsDrawer.qml"));
    QVERIFY2(settings.open(QIODevice::ReadOnly), qPrintable(settings.errorString()));
    const auto settingsSource = settings.readAll();
    QVERIFY(settingsSource.contains(
        "\"panel.settings.sessions.browse\""));
    QVERIFY(settingsSource.contains(
        "\"panel.settings.sessions.openFolder\""));
    QVERIFY(settingsSource.contains(
        "Models.DesktopFiles.SettingsWorkingDirectory"));
    QVERIFY(settingsSource.contains(
        "Models.DesktopFiles.cancelDirectory("));
    QVERIFY(settingsSource.contains("Component.onDestruction:"));
    QVERIFY(settingsSource.contains(
        "requestId !== root.directoryPickerRequestId"));

    QFile tile(QStringLiteral(
        KODOSI_SOURCE_DIR
        "/src/qml/Workbench/TerminalTile.qml"));
    QVERIFY2(tile.open(QIODevice::ReadOnly), qPrintable(tile.errorString()));
    const auto tileSource = tile.readAll();
    QVERIFY(tileSource.contains(".overflow.openProject"));
    QVERIFY(tileSource.contains(
        "\"stage.tile.\" + root.sessionId + \".openProject\""));
    QVERIFY(tileSource.contains("canOpenSessionProject(root.sessionId)"));
    QVERIFY(tileSource.contains("Models.DesktopFiles.TerminalProject"));
    QVERIFY(tileSource.contains(
        "Models.DesktopFiles.openSessionProject("));
    QVERIFY(!tileSource.contains("Models.DesktopFiles.openPath("));

    QFile banner(QStringLiteral(
        KODOSI_SOURCE_DIR
        "/src/qml/Workbench/DesktopFileErrorBanner.qml"));
    QVERIFY2(banner.open(QIODevice::ReadOnly), qPrintable(banner.errorString()));
    const auto bannerSource = banner.readAll();
    QVERIFY(bannerSource.contains(
        "objectName: \"banner.desktopFile.error\""));
    QVERIFY(bannerSource.contains(
        "objectName: \"banner.desktopFile.error.dismiss\""));
    QVERIFY(bannerSource.contains(
        "Models.DesktopFiles.SessionProject"));

    QFile shell(QStringLiteral(KODOSI_SOURCE_DIR "/src/qml/Main.qml"));
    QVERIFY2(shell.open(QIODevice::ReadOnly), qPrintable(shell.errorString()));
    QVERIFY(shell.readAll().contains("DesktopFileErrorBanner"));

    QFile sourceCMake(QStringLiteral(
        KODOSI_SOURCE_DIR "/src/CMakeLists.txt"));
    QVERIFY2(
        sourceCMake.open(QIODevice::ReadOnly),
        qPrintable(sourceCMake.errorString()));
    const auto sourceCMakeText = sourceCMake.readAll();
    QVERIFY(sourceCMakeText.contains("Qt6::Widgets"));
    QVERIFY(sourceCMakeText.contains("QGtk3ThemePlugin"));
    QVERIFY(sourceCMakeText.contains("QXdgDesktopPortalThemePlugin"));
    QVERIFY(sourceCMakeText.contains(
        "qml/Workbench/DesktopFileErrorBanner.qml"));

    QFile rootCMake(QStringLiteral(KODOSI_SOURCE_DIR "/CMakeLists.txt"));
    QVERIFY2(
        rootCMake.open(QIODevice::ReadOnly),
        qPrintable(rootCMake.errorString()));
    const auto rootCMakeText = rootCMake.readAll();
    QVERIFY(rootCMakeText.contains("xdg-desktop-portal"));
    QVERIFY(rootCMakeText.contains("xdg-utils"));
    QVERIFY(rootCMakeText.contains("--bin kodosi"));
    QVERIFY(rootCMakeText.contains("generate-source-identity.py"));
    QVERIFY(rootCMakeText.contains("generate-rust-license-inventory.py"));
    QVERIFY(rootCMakeText.contains(
        "\"${KODOSI_GHOSTTY_ROOT}/LICENSE\""));
    QVERIFY(rootCMakeText.contains("LICENSE-GHOSTTY"));
    QVERIFY(rootCMakeText.contains("linux-vt-inventory.json"));
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
