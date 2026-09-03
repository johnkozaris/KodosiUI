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
    void shellAccountDeviceParityContract();
    void desktopFileIntegrationContractIsNativeOwned();
    void applicationLogUrlRedactionUsesTypedQStringSetters();
    void projectIntelligenceQmlContractIsProductSafe();
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
    QCOMPARE(kodosi.value(QStringLiteral("desktopProtocolVersion")).toInt(), 37);
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
    QVERIFY(smokeSource.contains(
        "KODOSI_UI_PROBE_AGENT_PARITY_ONLY"));
    QVERIFY(smokeSource.contains(
        "--ui-probe-project-intel-empty"));
    QVERIFY(smokeSource.contains(
        "--ui-probe-project-intel-archive"));
    QVERIFY(smokeSource.contains(
        "panel.projectIntel.source.selected"));
    QVERIFY(smokeSource.contains(
        "panel.settings.autoMode.reload.cancel"));
    QVERIFY(smokeSource.contains(
        "agent-settings-external-stale.json"));
}

void ContractTest::shellAccountDeviceParityContract()
{
    QFile mainQml(QStringLiteral(KODOSI_SOURCE_DIR "/src/qml/Main.qml"));
    QVERIFY2(mainQml.open(QIODevice::ReadOnly), qPrintable(mainQml.errorString()));
    const auto mainSource = mainQml.readAll();
    for (const auto shortcut : {
             QByteArrayLiteral("sequence: \"Ctrl+,\""),
             QByteArrayLiteral("sequence: \"Ctrl+S\""),
             QByteArrayLiteral("sequence: \"Ctrl+I\""),
             QByteArrayLiteral("sequence: \"Ctrl+Shift+S\""),
             QByteArrayLiteral("sequence: \"Ctrl+Shift+W\""),
             QByteArrayLiteral("sequence: \"Ctrl+B\""),
             QByteArrayLiteral("sequence: \"Ctrl+Shift+/\""),
             QByteArrayLiteral("sequence: \"Ctrl+Shift+D\""),
             QByteArrayLiteral("sequence: \"Ctrl+Shift+Enter\""),
         }) {
        QVERIFY2(mainSource.contains(shortcut), shortcut.constData());
    }
    QVERIFY(mainSource.contains("textInputOwnsShortcuts()"));
    QVERIFY(mainSource.contains("window.canShareSelectedSession"));
    QVERIFY(mainSource.contains("window.canCloseSelectedSession"));
    QVERIFY(mainSource.contains("sessionSidebar.modalOpen"));
    QVERIFY(mainSource.contains(
        "Models.SessionActions.requestCloseConfirmation("));
    QVERIFY(mainSource.contains("Models.SessionActions.createDefault()"));
    QVERIFY(mainSource.contains("KeyboardShortcutsOverlay"));
    QVERIFY(mainSource.contains("header.auth.signIn"));
    QVERIFY(!mainSource.contains("sequence: \"Ctrl+Shift+A\""));
    QVERIFY(!mainSource.contains("sequence: \"Ctrl+Shift+X\""));
    QVERIFY(!mainSource.contains("shortcut.approval.approve"));
    QVERIFY(!mainSource.contains("shortcut.approval.deny"));

    QFile sidebar(QStringLiteral(
        KODOSI_SOURCE_DIR "/src/qml/Workbench/SessionSidebar.qml"));
    QVERIFY2(sidebar.open(QIODevice::ReadOnly), qPrintable(sidebar.errorString()));
    const auto sidebarSource = sidebar.readAll();
    QVERIFY(sidebarSource.contains("readonly property bool modalOpen:"));
    QVERIFY(sidebarSource.contains("shareDialog.opened"));
    QVERIFY(sidebarSource.contains("deleteConfirmation.visible"));
    QVERIFY(sidebarSource.contains("leaveConfirmation.visible"));
    QVERIFY(sidebarSource.contains("revokeConfirmation.visible"));

    QFile overlay(QStringLiteral(
        KODOSI_SOURCE_DIR
        "/src/qml/Shell/KeyboardShortcutsOverlay.qml"));
    QVERIFY2(overlay.open(QIODevice::ReadOnly), qPrintable(overlay.errorString()));
    const auto overlaySource = overlay.readAll();
    for (const auto contract : {
             QByteArrayLiteral("objectName: \"panel.shortcuts\""),
             QByteArrayLiteral("objectName: \"panel.shortcuts.close\""),
             QByteArrayLiteral("objectName: \"panel.shortcuts.escape\""),
             QByteArrayLiteral("width: Math.min(620"),
             QByteArrayLiteral("height: Math.min(460"),
             QByteArrayLiteral("closePolicy: Popup.CloseOnEscape"),
             QByteArrayLiteral("Accessible.role: Accessible.Dialog"),
             QByteArrayLiteral("qsTr(\"Agent Intelligence\")"),
             QByteArrayLiteral("qsTr(\"Close selected session\")"),
         }) {
        QVERIFY2(overlaySource.contains(contract), contract.constData());
    }
    QVERIFY(!overlaySource.contains("most-urgent"));

    QFile utility(QStringLiteral(
        KODOSI_SOURCE_DIR
        "/src/qml/Appearance/AppearanceMenu.qml"));
    QVERIFY2(utility.open(QIODevice::ReadOnly), qPrintable(utility.errorString()));
    QVERIFY(utility.readAll().contains(
        "objectName: \"panel.utility.shortcuts\""));

    QFile settings(QStringLiteral(
        KODOSI_SOURCE_DIR
        "/src/qml/Settings/SettingsDrawer.qml"));
    QVERIFY2(settings.open(QIODevice::ReadOnly), qPrintable(settings.errorString()));
    const auto settingsSource = settings.readAll();
    for (const auto contract : {
             QByteArrayLiteral(
                 "\"panel.settings.account.resetIdentity\""),
             QByteArrayLiteral(
                 "\"panel.settings.account.resetConfirm.input\""),
             QByteArrayLiteral(
                 "\"panel.settings.account.resetConfirm.cancel\""),
             QByteArrayLiteral(
                 "\"panel.settings.account.resetConfirm.confirm\""),
             QByteArrayLiteral(
                 "\"panel.settings.account.reset.pending\""),
             QByteArrayLiteral(
                 "\"panel.settings.account.reset.error\""),
             QByteArrayLiteral(
                 "resetConfirmInput.text === \"RESET\""),
             QByteArrayLiteral("!== \"RESET\""),
             QByteArrayLiteral("clears your local keypair"),
             QByteArrayLiteral("All linked devices unlink"),
             QByteArrayLiteral("friend trust pins are forgotten"),
             QByteArrayLiteral(
                 "\"panel.settings.account.signIn\""),
             QByteArrayLiteral(
                 "selectedCategory !== \"agents\"\n"
                 "                && selectedCategory !== \"account\""),
             QByteArrayLiteral(
                 "if (selectedCategory !== \"account\")\n"
                 "            clearResetConfirmation()"),
             QByteArrayLiteral(
                 "readonly property bool identityRecoveryActive:"),
             QByteArrayLiteral(
                 "resetConfirmationOpen || resetPending || resetFailed"),
         }) {
        QVERIFY2(settingsSource.contains(contract), contract.constData());
    }
    QVERIFY(!settingsSource.contains(
        "resetConfirmInput.text.toUpperCase()"));
    QVERIFY(!settingsSource.contains(
        "resetConfirmInput.text.trim()"));

    QFile authOverlay(QStringLiteral(
        KODOSI_SOURCE_DIR "/src/qml/Auth/AuthOverlay.qml"));
    QVERIFY2(
        authOverlay.open(QIODevice::ReadOnly),
        qPrintable(authOverlay.errorString()));
    const auto authOverlaySource = authOverlay.readAll();
    QVERIFY(authOverlaySource.contains(
        "visible: root.failed && !root.identityReset"));
    QVERIFY(authOverlaySource.contains(
        "objectName: \"auth.identity-reset.return\""));
    QVERIFY(authOverlaySource.contains(
        "visible: !root.identityReset"));
    QVERIFY(authOverlaySource.contains(
        "&& !accountRecoverySurfaceOpen"));
    QVERIFY(!authOverlaySource.contains(
        "Models.AuthActions.clearError()\n"
        "                            root.accountRecoveryRequested()"));

    QFile authBanner(QStringLiteral(
        KODOSI_SOURCE_DIR "/src/qml/Auth/AuthErrorBanner.qml"));
    QVERIFY2(
        authBanner.open(QIODevice::ReadOnly),
        qPrintable(authBanner.errorString()));
    const auto authBannerSource = authBanner.readAll();
    QVERIFY(authBannerSource.contains(
        "Models.AuthActions.failedOperation === \"identity.reset\""));
    QVERIFY(authBannerSource.contains("root.openAccountRequested()"));
    QVERIFY(authBannerSource.contains("Models.AuthActions.retry()"));
    QVERIFY(!authBannerSource.contains(
        "Models.AuthActions.clearError()\n"
        "                    root.openAccountRequested()"));

    QFile missions(QStringLiteral(
        KODOSI_SOURCE_DIR "/src/qml/People/PeopleView.qml"));
    QVERIFY2(missions.open(QIODevice::ReadOnly), qPrintable(missions.errorString()));
    const auto missionsSource = missions.readAll();
    QVERIFY(missionsSource.contains(
        "accessibleId: \"auth.gate.missions\""));
    QVERIFY(missionsSource.contains(
        "visible: Models.AuthState.signedIn"));

    QFile smoke(QStringLiteral(
        KODOSI_SOURCE_DIR "/scripts/smoke-ui-probe.sh"));
    QVERIFY2(smoke.open(QIODevice::ReadOnly), qPrintable(smoke.errorString()));
    QVERIFY(smoke.readAll().contains(
        "id=panel.settings.account.signIn"));

    QFile devices(QStringLiteral(
        KODOSI_SOURCE_DIR "/src/qml/Devices/DevicesView.qml"));
    QVERIFY2(devices.open(QIODevice::ReadOnly), qPrintable(devices.errorString()));
    const auto deviceSource = devices.readAll();
    for (const auto contract : {
             QByteArrayLiteral(
                 "model: Models.Devices.pendingLinkPresentations"),
             QByteArrayLiteral("\"devices.incoming.\" + userCode"),
             QByteArrayLiteral("\"devices.self-link.expiry\""),
             QByteArrayLiteral("\"devices.self-link.outcome\""),
             QByteArrayLiteral("\"devices.self-link.regenerate\""),
             QByteArrayLiteral("\"devices.link.outcome\""),
             QByteArrayLiteral("\"devices.link.outcome.dismiss\""),
             QByteArrayLiteral("\"devices.error.retry\""),
             QByteArrayLiteral("\"devices.error.dismiss\""),
             QByteArrayLiteral("\"devices.current.signer\""),
             QByteArrayLiteral("\"devices.current.issued\""),
             QByteArrayLiteral("Models.Devices.isCanonicalUserCode("),
             QByteArrayLiteral(
                 "accessibleId: \"auth.gate.devices\""),
         }) {
        QVERIFY2(deviceSource.contains(contract), contract.constData());
    }
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
    QVERIFY(rootCMakeText.contains("LinuxGhostty.ref"));
    QVERIFY(rootCMakeText.contains("linux-vt-inventory.json"));
}

void ContractTest::applicationLogUrlRedactionUsesTypedQStringSetters()
{
    QFile source(
        QStringLiteral(
            KODOSI_SOURCE_DIR "/src/logging/ApplicationLogStore.cpp"));
    QVERIFY2(source.open(QIODevice::ReadOnly), qPrintable(source.errorString()));
    const auto contents = source.readAll();
    for (const auto setter : {
             QByteArrayLiteral("url.setUserName(QString {});"),
             QByteArrayLiteral("url.setPassword(QString {});"),
             QByteArrayLiteral("url.setQuery(QString {});"),
             QByteArrayLiteral("url.setFragment(QString {});"),
         }) {
        QVERIFY2(contents.contains(setter), setter.constData());
    }
    QVERIFY(!contents.contains("url.setUserName({});"));
    QVERIFY(!contents.contains("url.setPassword({});"));
    QVERIFY(!contents.contains("url.setQuery({});"));
    QVERIFY(!contents.contains("url.setFragment({});"));
}

void ContractTest::projectIntelligenceQmlContractIsProductSafe()
{
    QFile projectModal(
        QStringLiteral(
            KODOSI_SOURCE_DIR
            "/src/qml/AgentIntel/ProjectIntelligenceModal.qml"));
    QVERIFY2(
        projectModal.open(QIODevice::ReadOnly),
        qPrintable(projectModal.errorString()));
    const auto projectSource = projectModal.readAll();

    for (const auto copy : {
             QByteArrayLiteral("qsTr(\"Sessions\")"),
             QByteArrayLiteral("qsTr(\"Memory\")"),
             QByteArrayLiteral("qsTr(\"MCP Servers\")"),
             QByteArrayLiteral("qsTr(\"Agents\")"),
             QByteArrayLiteral("qsTr(\"Project intelligence\")"),
             QByteArrayLiteral("qsTr(\"Claude project archive\")"),
             QByteArrayLiteral("qsTr(\"Copilot repository archive\")"),
             QByteArrayLiteral("qsTr(\"No sessions for this project\")"),
             QByteArrayLiteral("qsTr(\"No archived sessions for this project\")"),
             QByteArrayLiteral("qsTr(\"No memory files for this project\")"),
             QByteArrayLiteral("qsTr(\"Select a file to preview\")"),
             QByteArrayLiteral("qsTr(\"Copy to project…\")"),
             QByteArrayLiteral(
                 "\"panel.projectIntel.memory.retry\""),
             QByteArrayLiteral("qsTr(\"MCP configuration requires an active project\")"),
             QByteArrayLiteral("qsTr(\"No MCP servers configured\")"),
             QByteArrayLiteral("Agents browser is only available for active projects."),
             QByteArrayLiteral("qsTr(\"No custom agents defined\")"),
             QByteArrayLiteral(
                 "\"panel.projectIntel.agent.retry\""),
             QByteArrayLiteral("qsTr(\"Parse errors\")"),
             QByteArrayLiteral("qsTr(\"Frontmatter\")"),
             QByteArrayLiteral("qsTr(\"System prompt\")"),
         }) {
        QVERIFY2(projectSource.contains(copy), copy.constData());
    }
    QVERIFY(projectSource.contains(
        "selectedSourceButton.forceActiveFocus("));
    QVERIFY(!projectSource.contains("sourceList.forceActiveFocus("));
    QVERIFY(projectSource.contains("Accessible.PageTab"));
    QVERIFY(projectSource.contains("Keys.onLeftPressed"));
    QVERIFY(projectSource.contains("Keys.onRightPressed"));
    QVERIFY(projectSource.contains("Keys.onUpPressed"));
    QVERIFY(projectSource.contains("Keys.onDownPressed"));
    QVERIFY(projectSource.contains("tonalSelection: true"));
    QVERIFY(projectSource.contains(
        "A current matching session incarnation is required."));
    QVERIFY(projectSource.contains(
        "objectName: \"panel.projectIntel.actionMessage\""));
    QVERIFY(projectSource.contains(
        "function onSourceChanged() {\n"
        "            root.conversationOpen = false"));
    QVERIFY(!projectSource.contains("No sessions for this source"));
    QVERIFY(!projectSource.contains("No MCP servers or customizations"));
    QVERIFY(!projectSource.contains("Open in editor"));
    QVERIFY(!projectSource.contains("selectionToken"));
    QVERIFY(!projectSource.contains("runtimeIncarnationId"));
    QVERIFY(!projectSource.contains("mutationId"));
    QVERIFY(!projectSource.contains("requestId"));

    QFile agentsSettings(
        QStringLiteral(
            KODOSI_SOURCE_DIR
            "/src/qml/Settings/AgentsSettingsSurface.qml"));
    QVERIFY2(
        agentsSettings.open(QIODevice::ReadOnly),
        qPrintable(agentsSettings.errorString()));
    const auto agentsSource = agentsSettings.readAll();
    QVERIFY(!agentsSource.contains("Component.onCompleted"));
    QVERIFY(agentsSource.contains(
        "Models.ProjectIntelligence.refreshSources(true)"));
    QVERIFY(agentsSource.contains(
        "Models.AgentAutoModeRules.refresh(false)"));
    QVERIFY(agentsSource.contains(
        "Models.ExternalDiscovery.refresh(true)"));
    QVERIFY(agentsSource.contains("Models.AgentGlobal.refresh()"));
    QVERIFY(agentsSource.contains(
        "Unsaved Auto Mode rule changes will be replaced"));
    QVERIFY(agentsSource.contains(
        "objectName: \"panel.settings.autoMode.reload.confirm\""));
    QVERIFY(agentsSource.contains(
        "autoModeReloadCancel.forceActiveFocus("));
    for (const auto copy : {
             QByteArrayLiteral("qsTr(\"Not refreshed\")"),
             QByteArrayLiteral("qsTr(\"Refreshing…\")"),
             QByteArrayLiteral("qsTr(\"Detected\")"),
             QByteArrayLiteral("qsTr(\"Not detected\")"),
             QByteArrayLiteral("qsTr(\"Degraded\")"),
             QByteArrayLiteral("qsTr(\"Refresh failed\")"),
             QByteArrayLiteral("qsTr(\"External MCP servers\")"),
             QByteArrayLiteral("No external MCP configs found."),
             QByteArrayLiteral("qsTr(\"External sessions\")"),
             QByteArrayLiteral("No external sessions found."),
             QByteArrayLiteral("qsTr(\"Copy Source Path\")"),
             QByteArrayLiteral("Reveal in File Manager"),
         }) {
        QVERIFY2(agentsSource.contains(copy), copy.constData());
    }
    QVERIFY(agentsSource.contains("canCopySourcePath("));
    QVERIFY(agentsSource.contains("canOpenSource("));
    QVERIFY(agentsSource.contains("canRevealSource("));
    QVERIFY(agentsSource.contains(
        "objectName:\n            \"panel.settings.externalDiscovery.status\""));
    QVERIFY(!agentsSource.contains("JSON.stringify"));
    QVERIFY(!agentsSource.contains("selectionToken"));
    QVERIFY(!agentsSource.contains("runtimeIncarnationId"));
    QVERIFY(!agentsSource.contains("mutationId"));
    QVERIFY(!agentsSource.contains("requestId"));

    QFile settingsDrawer(
        QStringLiteral(
            KODOSI_SOURCE_DIR "/src/qml/Settings/SettingsDrawer.qml"));
    QVERIFY2(
        settingsDrawer.open(QIODevice::ReadOnly),
        qPrintable(settingsDrawer.errorString()));
    const auto settingsSource = settingsDrawer.readAll();
    const auto acquireStart =
        settingsSource.indexOf("function acquireAgentsVisit()");
    const auto releaseStart =
        settingsSource.indexOf("function releaseAgentsVisit(");
    const auto focusStart =
        settingsSource.indexOf("function focusCategory(");
    QVERIFY(acquireStart >= 0);
    QVERIFY(releaseStart > acquireStart);
    QVERIFY(focusStart > releaseStart);
    const auto acquireSource = settingsSource.mid(
        acquireStart,
        releaseStart - acquireStart);
    QCOMPARE(acquireSource.count("refreshSources(false)"), 1);
    QCOMPARE(acquireSource.count("AgentAutoModeRules.refresh(false)"), 1);
    QCOMPARE(acquireSource.count("ExternalDiscovery.refresh(false)"), 1);
    QCOMPARE(acquireSource.count("AgentGlobal.refresh()"), 1);
    const auto releaseSource = settingsSource.mid(
        releaseStart,
        focusStart - releaseStart);
    QCOMPARE(releaseSource.count("AgentAutoModeRules.close()"), 1);
    QCOMPARE(releaseSource.count("ExternalDiscovery.close()"), 1);
    QCOMPARE(releaseSource.count("ProjectIntelligence.closeSource()"), 1);
    QVERIFY(settingsSource.contains(
        "releaseAgentsVisit(transferringProjectIntel)"));
    QVERIFY(settingsSource.contains("Layout.minimumHeight: 0"));
    QVERIFY(settingsSource.contains("Layout.minimumHeight: 66"));
    QVERIFY(settingsSource.contains("Layout.maximumHeight: 66"));

    const auto scopePosition =
        agentsSource.indexOf("objectName: \"panel.settings.agents.scope\"");
    const auto integrationPosition =
        agentsSource.indexOf(
            "objectName: \"panel.settings.agents.integration\"");
    QVERIFY(scopePosition >= 0);
    QVERIFY(integrationPosition > scopePosition);

    QFile smoke(
        QStringLiteral(
            KODOSI_SOURCE_DIR "/scripts/smoke-ui-probe.sh"));
    QVERIFY2(smoke.open(QIODevice::ReadOnly), qPrintable(smoke.errorString()));
    const auto smokeSource = smoke.readAll();
    QVERIFY(smokeSource.contains("agent-settings-compact-scope.json"));
    QVERIFY(smokeSource.contains("agent-settings-wide-scope.json"));
    QVERIFY(smokeSource.contains(
        "settings scroll viewport overlaps the 66px footer"));

    QFile projectModel(
        QStringLiteral(
            KODOSI_SOURCE_DIR
            "/src/models/ProjectIntelligenceModel.cpp"));
    QVERIFY2(
        projectModel.open(QIODevice::ReadOnly),
        qPrintable(projectModel.errorString()));
    const auto modelSource = projectModel.readAll();
    QVERIFY(modelSource.contains(
        "kind->contains(QStringLiteral(\"mcp\"), Qt::CaseInsensitive)"));
    QVERIFY(modelSource.contains(
        "Select an active local session to inspect its bound settings snapshot."));
    QVERIFY(modelSource.contains(
        "No values for the %1 scope."));
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
