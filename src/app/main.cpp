#include "attention/ApprovalNotifications.hpp"
#include "attention/TerminalNotifications.hpp"
#include "app/QmlModelTypes.hpp"
#include "app/DeepLinkController.hpp"
#include "app/DeepLinkRouter.hpp"
#include "app/SingleInstanceGuard.hpp"
#include "bridge/RuntimeBridge.hpp"
#include "logging/ApplicationLogStore.hpp"
#include "models/AgentConversationModel.hpp"
#include "models/AgentAutoModeRulesModel.hpp"
#include "models/AgentCustomAgentsModel.hpp"
#include "models/AgentGlobalModel.hpp"
#include "models/AgentMemoryModel.hpp"
#include "models/AgentSessionIntelModel.hpp"
#include "models/AppearanceModel.hpp"
#include "models/AttentionModel.hpp"
#include "models/SessionCatalogModel.hpp"
#include "models/SessionAccess.hpp"
#include "models/SessionActions.hpp"
#include "models/SessionShareScope.hpp"
#include "models/SteeringModel.hpp"
#include "models/TerminalTilingLayoutModel.hpp"
#include "models/AuthStateModel.hpp"
#include "models/AuthActions.hpp"
#include "models/DevicesModel.hpp"
#include "models/DeviceActions.hpp"
#include "models/DesktopSettings.hpp"
#include "models/DesktopStateModel.hpp"
#include "models/ExternalDiscoveryModel.hpp"
#include "models/MissionDirectoryModel.hpp"
#include "models/MissionDetailModel.hpp"
#include "models/MissionActions.hpp"
#include "models/PeopleModel.hpp"
#include "models/PeopleActions.hpp"
#include "models/RuntimeDiagnosticsModel.hpp"
#include "models/PendingPermissionsModel.hpp"
#include "models/ProjectIntelligenceModel.hpp"
#include "models/TrustModel.hpp"
#include "platform/FreedesktopNotificationDriver.hpp"
#include "platform/DesktopFileIntegration.hpp"
#include "terminal/TerminalSessionRegistry.hpp"
#include "terminal/TerminalSurfaceController.hpp"
#include "terminal/TerminalAccessibility.hpp"
#include "terminal/TerminalView.hpp"

#include <QApplication>
#include <QColor>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QQuickItem>
#include <QQuickStyle>
#include <QRegularExpression>
#include <QQmlApplicationEngine>
#include <QSettings>
#include <QSet>
#include <QScopeGuard>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QVariant>
#include <QWindow>

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>

namespace {

bool parseWindowSize(
    const QStringList& arguments,
    std::optional<QSize>& windowSize,
    QString& error)
{
    const auto option = QStringLiteral("--window-size");
    const auto firstIndex = arguments.indexOf(option);
    if (firstIndex < 0) {
        return true;
    }
    if (arguments.indexOf(option, firstIndex + 1) >= 0) {
        error = QStringLiteral("--window-size may only be specified once");
        return false;
    }
    if (firstIndex + 1 >= arguments.size()) {
        error = QStringLiteral(
            "--window-size requires WIDTHxHEIGHT at or above 820x560");
        return false;
    }
    static const QRegularExpression pattern(
        QStringLiteral(R"(^([0-9]+)x([0-9]+)$)"));
    const auto match = pattern.match(arguments.at(firstIndex + 1));
    if (!match.hasMatch()) {
        error = QStringLiteral(
            "--window-size requires WIDTHxHEIGHT at or above 820x560");
        return false;
    }
    bool widthValid = false;
    bool heightValid = false;
    const auto width = match.captured(1).toInt(&widthValid);
    const auto height = match.captured(2).toInt(&heightValid);
    if (!widthValid || !heightValid || width < 820 || height < 560) {
        error = QStringLiteral(
            "--window-size requires WIDTHxHEIGHT at or above 820x560");
        return false;
    }
    windowSize = QSize(width, height);
    return true;
}

std::optional<kodosi::DeepLinkParseResult> commandLineDeepLink(
    const QStringList& arguments)
{
    QStringList positional;
    for (auto index = 1; index < arguments.size(); ++index) {
        const auto& argument = arguments.at(index);
        if (argument == QStringLiteral("--window-size")) {
            ++index;
            continue;
        }
        if (argument.startsWith(u'-')) {
            continue;
        }
        positional.append(argument);
    }
    if (positional.isEmpty()) {
        return std::nullopt;
    }
    if (positional.size() != 1) {
        return kodosi::DeepLinkParseResult {
            .destination = std::nullopt,
            .error = kodosi::DeepLinkParseError::UnsupportedRoute,
        };
    }
    return kodosi::DeepLinkRouter::parse(positional.constFirst());
}

std::unique_ptr<QSettings> desktopStateSettings(const bool persistent)
{
    if (persistent) {
        return std::make_unique<QSettings>();
    }
    auto settings = std::make_unique<QSettings>(
        QSettings::IniFormat,
        QSettings::UserScope,
        QStringLiteral("Kodosi-Ephemeral"),
        QUuid::createUuid().toString(QUuid::WithoutBraces));
    settings->setFallbacksEnabled(false);
    return settings;
}

QQuickItem* findQuickItem(
    QQuickItem* root,
    const QString& objectName)
{
    if (root == nullptr) {
        return nullptr;
    }
    if (root->objectName() == objectName) {
        return root;
    }
    for (auto* child : root->childItems()) {
        if (auto* found = findQuickItem(child, objectName)) {
            return found;
        }
    }
    return nullptr;
}

double relativeLuminance(const QColor& color)
{
    const auto linear = [](const double channel) {
        return channel <= 0.04045
            ? channel / 12.92
            : std::pow((channel + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(color.redF())
        + 0.7152 * linear(color.greenF())
        + 0.0722 * linear(color.blueF());
}

double contrastRatio(const QColor& first, const QColor& second)
{
    const auto firstLuminance = relativeLuminance(first);
    const auto secondLuminance = relativeLuminance(second);
    const auto lighter = std::max(firstLuminance, secondLuminance);
    const auto darker = std::min(firstLuminance, secondLuminance);
    return (lighter + 0.05) / (darker + 0.05);
}

bool themeContrastPasses(const QObject* rootObject)
{
    const auto canvas =
        rootObject->property("themeCanvas").value<QColor>();
    const auto surface =
        rootObject->property("themeSurface").value<QColor>();
    const auto primary =
        rootObject->property("themeTextPrimary").value<QColor>();
    const auto secondary =
        rootObject->property("themeTextSecondary").value<QColor>();
    const auto accent =
        rootObject->property("themeAccent").value<QColor>();
    const auto accentForeground =
        rootObject->property("themeAccentForeground").value<QColor>();
    return contrastRatio(primary, canvas) >= 4.5
        && contrastRatio(primary, surface) >= 4.5
        && contrastRatio(secondary, canvas) >= 4.5
        && contrastRatio(accentForeground, accent) >= 4.5;
}

} // namespace

int main(int argc, char* argv[])
{
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORMTHEME")) {
        (void)qputenv(
            "QT_QPA_PLATFORMTHEME",
            QByteArrayLiteral("xdgdesktopportal"));
    }
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Kodosi"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("kodosi.com"));
    QCoreApplication::setApplicationName(QStringLiteral("Kodosi"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    const auto arguments = application.arguments();
    const auto initialDeepLink = commandLineDeepLink(arguments);
    std::optional<QSize> windowSize;
    QString windowSizeError;
    if (!parseWindowSize(arguments, windowSize, windowSizeError)) {
        qCritical().noquote() << windowSizeError;
        return EXIT_FAILURE;
    }
    const auto settingsSmokeTest =
        arguments.contains(QStringLiteral("--smoke-test"));
    const auto agentIntelSmokeTest =
        arguments.contains(
            QStringLiteral("--smoke-test-agent-intel"));
    const auto projectIntelSmokeTest =
        arguments.contains(
            QStringLiteral("--smoke-test-project-intel"));
    const auto agentSettingsSmokeTest =
        arguments.contains(
            QStringLiteral("--smoke-test-agent-settings"));
    const auto attentionSmokeTest =
        arguments.contains(
            QStringLiteral("--smoke-test-attention"));
    const auto deepLinkApprovalSmokeTest =
        arguments.contains(
            QStringLiteral("--smoke-test-deep-link-approval"));
    const auto deepLinkMissingApprovalSmokeTest =
        arguments.contains(
            QStringLiteral("--smoke-test-deep-link-missing-approval"));
    const auto deepLinkStatusSmokeTest =
        arguments.contains(
            QStringLiteral("--smoke-test-deep-link-status"));
    const auto diagnosticsSmokeTest =
        arguments.contains(
            QStringLiteral("--smoke-test-diagnostics"));
    const auto appearanceSmokeTest =
        arguments.contains(
            QStringLiteral("--smoke-test-appearance"));
    const auto shellParitySmokeTest =
        arguments.contains(
            QStringLiteral("--smoke-test-shell-parity"));
    const auto desktopStateSmokeTest =
        arguments.contains(
            QStringLiteral("--smoke-test-desktop-state"));
    const auto tilingSmokeTest =
        arguments.contains(
            QStringLiteral("--smoke-test-tiling"));
    const auto tilingProbeSynthetic =
        arguments.contains(
            QStringLiteral("--ui-probe-tiling-synthetic"));
    const auto attentionProbePopulated =
        arguments.contains(
            QStringLiteral("--ui-probe-attention-populated"));
    const auto agentIntelProbeOpen =
        arguments.contains(
            QStringLiteral("--ui-probe-open-agent-intel"));
    const auto projectIntelProbePopulated =
        arguments.contains(
            QStringLiteral("--ui-probe-project-intel-populated"));
    const auto projectIntelProbeEmpty =
        arguments.contains(
            QStringLiteral("--ui-probe-project-intel-empty"));
    const auto projectIntelProbeArchive =
        arguments.contains(
            QStringLiteral("--ui-probe-project-intel-archive"));
    const auto agentSettingsProbePopulated =
        arguments.contains(
            QStringLiteral("--ui-probe-agent-settings-populated"));
    const auto authProbeError =
        arguments.contains(
            QStringLiteral("--ui-probe-auth-error"));
    const auto smokeTest = settingsSmokeTest
        || agentIntelSmokeTest
        || projectIntelSmokeTest
        || agentSettingsSmokeTest
        || attentionSmokeTest
        || deepLinkApprovalSmokeTest
        || deepLinkMissingApprovalSmokeTest
        || deepLinkStatusSmokeTest
        || diagnosticsSmokeTest
        || appearanceSmokeTest
        || shellParitySmokeTest
        || desktopStateSmokeTest
        || tilingSmokeTest;
    const auto syntheticMode = windowSize.has_value()
        || std::any_of(
            arguments.cbegin(),
            arguments.cend(),
            [](const QString& argument) {
                return argument.startsWith(QStringLiteral("--smoke-test"))
                    || argument.startsWith(QStringLiteral("--ui-probe-"));
            });
    QString syntheticRootDirectory;
    QString syntheticRuntimeDirectory;
    auto removeSyntheticConfig = qScopeGuard([&] {
        if (!syntheticRootDirectory.isEmpty()) {
            (void)QDir(syntheticRootDirectory).removeRecursively();
        }
        if (!syntheticRuntimeDirectory.isEmpty()) {
            (void)QDir(syntheticRuntimeDirectory).removeRecursively();
        }
    });
    if (syntheticMode) {
        const auto syntheticId =
            QUuid::createUuid().toString(QUuid::WithoutBraces);
        syntheticRootDirectory = QDir::current().absoluteFilePath(
            QStringLiteral("build/synthetic-roots/")
            + syntheticId);
        syntheticRuntimeDirectory = QDir::current().absoluteFilePath(
            QStringLiteral("build/run/")
            + syntheticId.left(12));
        const auto syntheticConfigDirectory =
            QDir(syntheticRootDirectory).filePath(QStringLiteral("config"));
        const auto syntheticStateDirectory =
            QDir(syntheticRootDirectory).filePath(QStringLiteral("state"));
        if (!QDir().mkpath(syntheticConfigDirectory)
            || !QDir().mkpath(syntheticStateDirectory)) {
            qCritical().noquote()
                << "Synthetic mode could not create isolated state directories.";
            return EXIT_FAILURE;
        }
        if (!qputenv(
                "XDG_CONFIG_HOME",
                syntheticConfigDirectory.toUtf8())
            || !qputenv(
                "XDG_STATE_HOME",
                syntheticStateDirectory.toUtf8())) {
            qCritical()
                << "Synthetic mode could not isolate XDG roots.";
            return EXIT_FAILURE;
        }
    }
    const auto singleInstanceRuntime = syntheticMode
        ? syntheticRuntimeDirectory
        : kodosi::SingleInstanceGuard::standardRuntimeDirectory();
    auto launchActivation =
        kodosi::SingleInstanceGuard::activation(initialDeepLink);
    qunsetenv("XDG_ACTIVATION_TOKEN");
    const auto endpointNamespace =
        kodosi::SingleInstanceGuard::endpointNamespace();
    if (!endpointNamespace.valid()) {
        qCritical().noquote() << endpointNamespace.error;
        return EXIT_FAILURE;
    }
    kodosi::SingleInstanceGuard singleInstance;
    const auto singleInstanceResult = singleInstance.start(
        singleInstanceRuntime,
        endpointNamespace.value,
        launchActivation);
    if (singleInstanceResult.state
        == kodosi::SingleInstanceGuard::StartState::Forwarded) {
        return EXIT_SUCCESS;
    }
    if (singleInstanceResult.state
        == kodosi::SingleInstanceGuard::StartState::Failed) {
        qCritical().noquote() << singleInstanceResult.error;
        return EXIT_FAILURE;
    }
    kodosi::ApplicationLogStore applicationLog({
        .directory = kodosi::ApplicationLogStore::standardLogDirectory(),
    });
    kodosi::TerminalSessionRegistry terminalSessions;
    kodosi::RuntimeBridge runtime(terminalSessions);
    kodosi::RuntimeDiagnosticsModel runtimeDiagnostics(runtime);
    kodosi::AgentGlobalModel agentGlobal(runtime);
    kodosi::installTerminalAccessibility();
    kodosi::SessionCatalogModel sessionCatalog;
    kodosi::AgentSessionIntelModel agentSessionIntel(
        runtime,
        sessionCatalog);
    kodosi::AgentConversationModel agentConversation(
        runtime,
        sessionCatalog,
        agentSessionIntel);
    kodosi::AgentCustomAgentsModel agentCustomAgents(
        runtime,
        sessionCatalog,
        agentSessionIntel);
    kodosi::AgentMemoryModel agentMemory(
        runtime,
        sessionCatalog,
        agentSessionIntel);
    kodosi::PeopleModel people;
    kodosi::AppearanceModel appearance;
    kodosi::DesktopSettings desktopSettings;
    kodosi::DesktopStateModel desktopState(
        desktopStateSettings(!syntheticMode),
        !syntheticMode);
    desktopState.attachSessionCatalog(&sessionCatalog);
    kodosi::DesktopFileIntegration desktopFiles(
        sessionCatalog,
        &applicationLog);
    kodosi::AgentAutoModeRulesModel agentAutoModeRules(runtime);
    kodosi::ExternalDiscoveryModel externalDiscovery(runtime, desktopFiles);
    kodosi::ProjectIntelligenceModel projectIntelligence(
        runtime,
        agentConversation,
        desktopFiles);
    if (projectIntelProbeEmpty || projectIntelProbeArchive) {
        projectIntelligence.installSyntheticEmptyFixture(
            projectIntelProbeArchive);
    } else if (projectIntelSmokeTest || agentSettingsSmokeTest
        || projectIntelProbePopulated || agentSettingsProbePopulated) {
        projectIntelligence.installSyntheticFixture();
        agentAutoModeRules.installSyntheticFixture();
        externalDiscovery.installSyntheticFixture();
        if (agentSettingsSmokeTest || agentSettingsProbePopulated) {
            agentGlobal.installSyntheticFixture();
        }
    }
    kodosi::TerminalTilingLayoutModel terminalTiling;
    if (desktopStateSmokeTest) {
        desktopState.setActiveView(2);
        desktopState.setSidebarOpen(false);
        desktopState.setSelectedSessionId(
            QStringLiteral("pre-authoritative-session"));
    }
    kodosi::FreedesktopNotificationDriver notificationDriver;
    kodosi::TerminalNotifications terminalNotifications(
        sessionCatalog,
        notificationDriver);
    kodosi::SessionAccess sessionAccess(runtime, sessionCatalog, people);
    kodosi::SessionActions sessionActions(
        runtime,
        sessionCatalog,
        sessionAccess,
        desktopSettings);
    QObject::connect(
        &desktopState,
        &kodosi::DesktopStateModel::remoteRestoreRequested,
        &sessionActions,
        [&desktopState, &sessionActions](
            const QString& sessionId,
            const QString& incarnationId) {
            desktopState.reportRemoteRestoreDispatch(
                sessionId,
                incarnationId,
                sessionActions.restoreRemote(sessionId));
        },
        Qt::DirectConnection);
    QObject::connect(
        &sessionActions,
        &kodosi::SessionActions::remoteOpenSucceeded,
        &desktopState,
        &kodosi::DesktopStateModel::remoteOpenSucceeded);
    QObject::connect(
        &sessionActions,
        &kodosi::SessionActions::remoteOpenFailed,
        &desktopState,
        &kodosi::DesktopStateModel::remoteOpenFailed);
    kodosi::TerminalSurfaceController terminalSurfaces(
        terminalSessions,
        runtime,
        sessionCatalog);
    terminalSurfaces.setNotificationSink(&terminalNotifications);
    kodosi::AuthStateModel authState;
    kodosi::AuthActions authActions(runtime);
    if (appearanceSmokeTest) {
        (void)appearance.setPreference(
            static_cast<int>(
                kodosi::AppearanceModel::Preference::Light));
        appearance.injectReducedMotionForTesting(true);
        authState.ingestAuthEvent(QByteArrayLiteral(
            "{\"type\":\"auth.ready\",\"userId\":\"appearance-smoke\","
            "\"accountEpoch\":77}"));
    }
    if (tilingSmokeTest || tilingProbeSynthetic) {
        constexpr auto tilingEpoch = 88;
        const auto auth = QJsonDocument(QJsonObject {
            {QStringLiteral("type"), QStringLiteral("auth.ready")},
            {QStringLiteral("userId"), QStringLiteral("tiling-user")},
            {QStringLiteral("accountEpoch"), tilingEpoch},
        }).toJson(QJsonDocument::Compact);
        authState.ingestAuthEvent(auth);
        desktopState.ingestAuthEvent(auth);
        sessionCatalog.ingestAuthEvent(auth);
        QJsonArray sessions;
        for (auto index = 0; index < 4; ++index) {
            const auto suffix = QString::number(index);
            sessions.append(QJsonObject {
                {QStringLiteral("kind"), QStringLiteral("local")},
                {QStringLiteral("id"),
                 QStringLiteral("tiling-") + suffix},
                {QStringLiteral("incarnationId"),
                 QStringLiteral(
                     "01900000-0000-7000-8000-00000000008")
                     + suffix},
                {QStringLiteral("name"),
                 QStringLiteral("Tiling ") + suffix},
                {QStringLiteral("project"),
                 QStringLiteral("/repo/tiling-") + suffix},
                {QStringLiteral("mode"),
                 index == 1
                     ? QStringLiteral("plan")
                     : QStringLiteral("normal")},
                {QStringLiteral("status"),
                 index == 3
                     ? QStringLiteral("waiting")
                     : QStringLiteral("active")},
                {QStringLiteral("recovery"), QStringLiteral("live")},
                {QStringLiteral("scope"), QStringLiteral("justMe")},
                {QStringLiteral("access"), QStringLiteral("approve")},
            });
        }
        sessionCatalog.ingestSessionEvent(
            QJsonDocument(QJsonObject {
                {QStringLiteral("authority"),
                 QStringLiteral("accountContext")},
                {QStringLiteral("accountUserId"),
                 QStringLiteral("tiling-user")},
                {QStringLiteral("accountEpoch"), tilingEpoch},
                {QStringLiteral("type"), QStringLiteral("session.list")},
                {QStringLiteral("sessions"), sessions},
            }).toJson(QJsonDocument::Compact));
        if (tilingSmokeTest) {
            const auto extra = QJsonObject {
                {QStringLiteral("kind"), QStringLiteral("local")},
                {QStringLiteral("id"), QStringLiteral("tiling-4")},
                {QStringLiteral("incarnationId"),
                 QStringLiteral(
                     "01900000-0000-7000-8000-000000000084")},
                {QStringLiteral("name"), QStringLiteral("Tiling 4")},
                {QStringLiteral("project"),
                 QStringLiteral("/repo/tiling-4")},
                {QStringLiteral("mode"), QStringLiteral("normal")},
                {QStringLiteral("status"), QStringLiteral("active")},
                {QStringLiteral("recovery"), QStringLiteral("live")},
                {QStringLiteral("scope"), QStringLiteral("justMe")},
                {QStringLiteral("access"), QStringLiteral("approve")},
            };
            sessionCatalog.ingestSessionEvent(
                QJsonDocument(QJsonObject {
                    {QStringLiteral("authority"),
                     QStringLiteral("accountContext")},
                    {QStringLiteral("accountUserId"),
                     QStringLiteral("tiling-user")},
                    {QStringLiteral("accountEpoch"), tilingEpoch},
                    {QStringLiteral("type"),
                     QStringLiteral("session.upsert")},
                    {QStringLiteral("session"), extra},
                }).toJson(QJsonDocument::Compact));
        }
    }
    if (authProbeError) {
        authActions.ingestAuthEvent(QByteArrayLiteral(
            "{\"type\":\"auth.error\",\"operation\":\"login.start\","
            "\"message\":\"Sign-in probe error\"}"));
    }
    kodosi::DevicesModel devices;
    kodosi::DeviceActions deviceActions(runtime, devices);
    kodosi::MissionDirectoryModel missions(runtime);
    kodosi::SessionShareScope sessionShareScope(
        runtime,
        sessionCatalog,
        missions);
    kodosi::SteeringModel steering(runtime, sessionCatalog);
    kodosi::MissionDetailModel missionDetail(runtime, missions);
    kodosi::MissionActions missionActions(
        runtime,
        missions,
        missionDetail,
        people,
        sessionCatalog);
    kodosi::PeopleActions peopleActions(runtime, people);
    kodosi::PendingPermissionsModel pendingPermissions(runtime, sessionCatalog);
    kodosi::DeepLinkController deepLinks(
        sessionCatalog,
        pendingPermissions);
    if (deepLinkApprovalSmokeTest || deepLinkMissingApprovalSmokeTest) {
        constexpr auto smokeEpoch = 77;
        const auto authEvent = QJsonDocument(QJsonObject {
            {QStringLiteral("type"), QStringLiteral("auth.required")},
            {QStringLiteral("accountEpoch"), smokeEpoch},
        }).toJson(QJsonDocument::Compact);
        deepLinks.ingestAuthEvent(authEvent);
        pendingPermissions.ingestAuthEvent(authEvent);
        pendingPermissions.ingestAgentIntelEvent(
            QJsonDocument(QJsonObject {
                {QStringLiteral("authority"),
                 QStringLiteral("accountContext")},
                {QStringLiteral("accountUserId"), QString {}},
                {QStringLiteral("accountEpoch"), smokeEpoch},
                {QStringLiteral("type"),
                 QStringLiteral(
                     "agent.intel.pendingPermissionsSnapshot")},
                {QStringLiteral("generation"), 1},
                {QStringLiteral("requests"),
                 QJsonArray {
                     QJsonObject {
                         {QStringLiteral("sessionId"),
                          QStringLiteral("attention-smoke")},
                         {QStringLiteral("sessionIncarnationId"),
                          QStringLiteral(
                              "01900000-0000-7000-8000-000000000077")},
                         {QStringLiteral("requestGeneration"), 1},
                         {QStringLiteral("toolUseId"),
                          deepLinkMissingApprovalSmokeTest
                              ? QStringLiteral("other-tool-smoke")
                              : QStringLiteral("tool-smoke")},
                         {QStringLiteral("toolName"),
                          QStringLiteral("Bash")},
                         {QStringLiteral("toolInput"), QJsonObject {}},
                         {QStringLiteral("createdAtMs"),
                          1'700'000'000'000.0},
                         {QStringLiteral("deadlineAtMs"),
                          1'800'000'000'000.0},
                         {QStringLiteral("risk"),
                          QStringLiteral("safe")},
                         {QStringLiteral("decisionPhase"),
                          QStringLiteral("actionable")},
                     },
                 }},
            }).toJson(QJsonDocument::Compact));
    }
    kodosi::AttentionModel attention(
        pendingPermissions,
        agentSessionIntel,
        sessionCatalog,
        sessionActions);
    if (shellParitySmokeTest) {
        constexpr auto shellEpoch = 93;
        const auto auth = QJsonDocument(QJsonObject {
            {QStringLiteral("type"), QStringLiteral("auth.ready")},
            {QStringLiteral("userId"), QStringLiteral("shell-user")},
            {QStringLiteral("accountEpoch"), shellEpoch},
        }).toJson(QJsonDocument::Compact);
        authState.ingestAuthEvent(auth);
        desktopState.ingestAuthEvent(auth);
        devices.ingestAuthEvent(auth);
        sessionActions.ingestAuthEvent(auth);
        sessionCatalog.ingestAuthEvent(auth);
        sessionShareScope.ingestAuthEvent(auth);
        sessionCatalog.ingestSessionEvent(
            QJsonDocument(QJsonObject {
                {QStringLiteral("authority"),
                 QStringLiteral("accountContext")},
                {QStringLiteral("accountUserId"),
                 QStringLiteral("shell-user")},
                {QStringLiteral("accountEpoch"), shellEpoch},
                {QStringLiteral("type"), QStringLiteral("session.list")},
                {QStringLiteral("sessions"),
                 QJsonArray {
                     QJsonObject {
                         {QStringLiteral("kind"),
                          QStringLiteral("local")},
                         {QStringLiteral("id"),
                          QStringLiteral("shell-session")},
                         {QStringLiteral("incarnationId"),
                          QStringLiteral(
                              "01900000-0000-7000-8000-000000000093")},
                         {QStringLiteral("name"),
                          QStringLiteral("Shell parity")},
                         {QStringLiteral("project"),
                          QStringLiteral("/repo/shell")},
                         {QStringLiteral("mode"),
                          QStringLiteral("normal")},
                         {QStringLiteral("status"),
                          QStringLiteral("active")},
                         {QStringLiteral("recovery"),
                          QStringLiteral("live")},
                         {QStringLiteral("scope"),
                          QStringLiteral("justMe")},
                         {QStringLiteral("access"),
                          QStringLiteral("approve")},
                     },
                 }},
            }).toJson(QJsonDocument::Compact));
        desktopState.setSelectedSessionId(
            QStringLiteral("shell-session"));
        devices.ingestDevicesEvent(QByteArrayLiteral(
            "{\"authority\":\"accountContext\","
            "\"accountUserId\":\"shell-user\",\"accountEpoch\":93,"
            "\"type\":\"devices.list\",\"selfDeviceId\":\"device-self\","
            "\"localDeviceEnrolled\":true,\"devices\":["
            "{\"deviceId\":\"device-self\",\"label\":\"Linux workstation\","
            "\"certSignerDeviceId\":\"device-signer\","
            "\"certIssuedAtMs\":1700000000000},"
            "{\"deviceId\":\"device-other\",\"label\":\"Travel laptop\","
            "\"certSignerDeviceId\":\"device-self\","
            "\"certIssuedAtMs\":1700000001000}]}"));
        devices.ingestDevicesEvent(QByteArrayLiteral(
            "{\"authority\":\"accountContext\","
            "\"accountUserId\":\"shell-user\",\"accountEpoch\":93,"
            "\"type\":\"devices.link.snapshot\",\"requests\":["
            "{\"userCode\":\"BCDF-2345\","
            "\"deviceLabel\":\"Tablet\","
            "\"expiresAt\":\"2099-09-03T12:00:00Z\"}]}"));
        devices.ingestDevicesEvent(QByteArrayLiteral(
            "{\"authority\":\"accountContext\","
            "\"accountUserId\":\"shell-user\",\"accountEpoch\":93,"
            "\"type\":\"devices.link.selfPending\","
            "\"userCode\":\"MNPQ-2345\","
            "\"expiresAt\":\"2099-09-03T12:30:00Z\"}"));
    }
    if (attentionSmokeTest || deepLinkApprovalSmokeTest
        || deepLinkMissingApprovalSmokeTest
        || attentionProbePopulated || agentIntelProbeOpen) {
        constexpr auto smokeEpoch = 77;
        sessionCatalog.ingestAuthEvent(
            QJsonDocument(QJsonObject {
                {QStringLiteral("type"), QStringLiteral("auth.required")},
                {QStringLiteral("accountEpoch"), smokeEpoch},
            }).toJson(QJsonDocument::Compact));
        sessionCatalog.ingestSessionEvent(
            QJsonDocument(QJsonObject {
                {QStringLiteral("authority"),
                 QStringLiteral("accountContext")},
                {QStringLiteral("accountUserId"), QString {}},
                {QStringLiteral("accountEpoch"), smokeEpoch},
                {QStringLiteral("type"), QStringLiteral("session.list")},
                {QStringLiteral("sessions"),
                 QJsonArray {
                     QJsonObject {
                         {QStringLiteral("kind"),
                          QStringLiteral("local")},
                         {QStringLiteral("id"),
                          QStringLiteral("attention-smoke")},
                         {QStringLiteral("incarnationId"),
                          QStringLiteral(
                              "01900000-0000-7000-8000-000000000077")},
                         {QStringLiteral("name"),
                          QStringLiteral("Attention smoke")},
                         {QStringLiteral("project"),
                          QStringLiteral("/repo")},
                         {QStringLiteral("mode"),
                          QStringLiteral("normal")},
                         {QStringLiteral("status"),
                          QStringLiteral("waiting")},
                         {QStringLiteral("recovery"),
                          QStringLiteral("live")},
                         {QStringLiteral("scope"),
                          QStringLiteral("justMe")},
                         {QStringLiteral("access"),
                          QStringLiteral("approve")},
                     },
                 }},
            }).toJson(QJsonDocument::Compact));
    }
    kodosi::ApprovalNotifications approvalNotifications(
        pendingPermissions,
        sessionCatalog,
        desktopSettings,
        notificationDriver);
    kodosi::TrustModel trust(runtime);
    QObject::connect(
        &approvalNotifications,
        &kodosi::ApprovalNotifications::deliveryError,
        &application,
        [](const QString& message) {
            qWarning().noquote() << message;
        });
    QObject::connect(
        &terminalNotifications,
        &kodosi::TerminalNotifications::deliveryError,
        &application,
        [](const QString& message) {
            qWarning().noquote() << message;
        });
    QObject::connect(
        &runtime,
        &kodosi::RuntimeBridge::eventReceived,
        &sessionCatalog,
        [&agentGlobal,
         &agentAutoModeRules,
         &agentConversation,
         &agentCustomAgents,
         &agentMemory,
         &agentSessionIntel,
         &authActions,
         &authState,
         &desktopState,
         &deepLinks,
         &devices,
         &externalDiscovery,
         &missions,
         &missionDetail,
         &missionActions,
         &pendingPermissions,
         &people,
         &projectIntelligence,
         &runtimeDiagnostics,
         &sessionCatalog,
         &sessionAccess,
         &sessionActions,
         &sessionShareScope,
         &steering,
         &trust](
            const kodosi::EventLane lane,
            QByteArray json) {
            if (lane == kodosi::EventLane::Auth) {
                deepLinks.ingestAuthEvent(json);
                authActions.ingestAuthEvent(json);
                agentAutoModeRules.ingestAuthEvent(json);
                agentConversation.ingestAuthEvent(json);
                agentCustomAgents.ingestAuthEvent(json);
                agentMemory.ingestAuthEvent(json);
                agentSessionIntel.ingestAuthEvent(json);
                authState.ingestAuthEvent(json);
                desktopState.ingestAuthEvent(json);
                sessionCatalog.ingestAuthEvent(json);
                sessionAccess.ingestAuthEvent(json);
                sessionActions.ingestAuthEvent(json);
                sessionShareScope.ingestAuthEvent(json);
                steering.ingestAuthEvent(json);
                devices.ingestAuthEvent(json);
                externalDiscovery.ingestAuthEvent(json);
                missions.ingestAuthEvent(json);
                missionDetail.ingestAuthEvent(json);
                missionActions.ingestAuthEvent(json);
                pendingPermissions.ingestAuthEvent(json);
                people.ingestAuthEvent(json);
                projectIntelligence.ingestAuthEvent(json);
                trust.ingestAuthEvent(json);
            } else if (lane == kodosi::EventLane::Devices) {
                devices.ingestDevicesEvent(std::move(json));
            } else if (lane == kodosi::EventLane::AgentGlobal) {
                agentGlobal.ingestAgentGlobalEvent(std::move(json));
            } else if (lane == kodosi::EventLane::System) {
                runtimeDiagnostics.ingestSystemEvent(std::move(json));
            } else if (lane == kodosi::EventLane::AgentIntel) {
                agentConversation.ingestAgentIntelEvent(json);
                agentAutoModeRules.ingestAgentIntelEvent(json);
                agentCustomAgents.ingestAgentIntelEvent(json);
                agentMemory.ingestAgentIntelEvent(json);
                agentSessionIntel.ingestAgentIntelEvent(json);
                pendingPermissions.ingestAgentIntelEvent(json);
                projectIntelligence.ingestAgentIntelEvent(json);
                externalDiscovery.ingestAgentIntelEvent(json);
                steering.ingestAgentIntelEvent(std::move(json));
            } else if (lane == kodosi::EventLane::Friends) {
                people.ingestFriendsEvent(std::move(json));
            } else if (lane == kodosi::EventLane::Sessions) {
                sessionCatalog.ingestSessionEvent(json);
                sessionAccess.ingestSessionEvent(json);
                sessionActions.ingestSessionEvent(json);
                sessionShareScope.ingestSessionEvent(std::move(json));
            } else if (lane == kodosi::EventLane::Rooms) {
                missions.ingestRoomEvent(json);
                missionDetail.ingestRoomEvent(json);
                missionActions.ingestRoomEvent(std::move(json));
            } else if (lane == kodosi::EventLane::Trust) {
                trust.ingestTrustEvent(std::move(json));
            }
        });
    QObject::connect(
        &runtime,
        &kodosi::RuntimeBridge::runningChanged,
        &application,
        [&authActions,
         &agentAutoModeRules,
         &agentConversation,
         &agentCustomAgents,
         &agentMemory,
         &agentSessionIntel,
         &authState,
         &desktopState,
         &deepLinks,
         &devices,
         &externalDiscovery,
         &missions,
         &missionDetail,
         &missionActions,
         &pendingPermissions,
         &people,
         &projectIntelligence,
         &sessionCatalog,
         &sessionAccess,
         &sessionActions,
         &sessionShareScope,
         &steering,
         &trust](const bool running) {
            if (running) {
                return;
            }
            authState.resetRuntimeAuthority();
            deepLinks.resetRuntimeAuthority();
            desktopState.resetRuntimeAuthority();
            agentConversation.resetRuntimeAuthority();
            agentAutoModeRules.resetRuntimeAuthority();
            agentCustomAgents.resetRuntimeAuthority();
            agentMemory.resetRuntimeAuthority();
            agentSessionIntel.resetRuntimeAuthority();
            authActions.resetRuntimeAuthority();
            devices.resetRuntimeAuthority();
            externalDiscovery.resetRuntimeAuthority();
            missions.resetRuntimeAuthority();
            missionDetail.resetRuntimeAuthority();
            missionActions.resetRuntimeAuthority();
            pendingPermissions.resetRuntimeAuthority();
            people.resetRuntimeAuthority();
            projectIntelligence.resetRuntimeAuthority();
            sessionCatalog.resetRuntimeAuthority();
            sessionAccess.resetRuntimeAuthority();
            sessionActions.resetRuntimeAuthority();
            sessionShareScope.resetRuntimeAuthority();
            steering.resetRuntimeAuthority();
            trust.resetRuntimeAuthority();
        });
    kodosi::qml::configureModelInstances(
        agentGlobal,
        agentAutoModeRules,
        agentConversation,
        agentCustomAgents,
        agentMemory,
        agentSessionIntel,
        attention,
        authState,
        authActions,
        devices,
        deviceActions,
        appearance,
        desktopSettings,
        desktopState,
        deepLinks,
        desktopFiles,
        externalDiscovery,
        missions,
        missionDetail,
        missionActions,
        pendingPermissions,
        projectIntelligence,
        people,
        peopleActions,
        runtimeDiagnostics,
        applicationLog,
        sessionCatalog,
        sessionAccess,
        sessionActions,
        sessionShareScope,
        steering,
        terminalTiling,
        trust,
        terminalSurfaces);
    QQmlApplicationEngine engine;
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &application,
        [] { QCoreApplication::exit(EXIT_FAILURE); },
        Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("Kodosi"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) {
        return EXIT_FAILURE;
    }
    auto* rootObject = engine.rootObjects().constFirst();
    auto* mainWindow = qobject_cast<QWindow*>(rootObject);
    if (mainWindow == nullptr) {
        qCritical() << "The root QML object is not a window.";
        return EXIT_FAILURE;
    }
    desktopFiles.setTransientParent(mainWindow);
    const auto activateMainWindow =
        [mainWindow](const QString& activationToken) {
        if (mainWindow == nullptr) {
            return;
        }
        mainWindow->setVisible(true);
        kodosi::SingleInstanceGuard::withActivationToken(
            activationToken,
            [mainWindow] {
                mainWindow->raise();
                mainWindow->requestActivate();
            });
    };
    QObject::connect(
        &singleInstance,
        &kodosi::SingleInstanceGuard::activationReceived,
        &application,
        activateMainWindow);
    QObject::connect(
        &singleInstance,
        &kodosi::SingleInstanceGuard::routeReceived,
        &deepLinks,
        [&deepLinks, &activateMainWindow](
            kodosi::DeepLinkDestination destination,
            const QString& activationToken) {
            activateMainWindow(activationToken);
            deepLinks.enqueue(std::move(destination));
        });
    QObject::connect(
        &singleInstance,
        &kodosi::SingleInstanceGuard::invalidRouteReceived,
        &deepLinks,
        [&deepLinks, &activateMainWindow](
            const kodosi::DeepLinkParseError error,
            const QString& activationToken) {
            activateMainWindow(activationToken);
            deepLinks.reject(error);
        });
    QObject::connect(
        &deepLinks,
        &kodosi::DeepLinkController::activationRequested,
        &application,
        [&activateMainWindow] { activateMainWindow({}); });
    QObject::connect(
        &deepLinks,
        &kodosi::DeepLinkController::openSessionRequested,
        &application,
        [&activateMainWindow] { activateMainWindow({}); });
    QObject::connect(
        &deepLinks,
        &kodosi::DeepLinkController::reviewApprovalRequested,
        &application,
        [&activateMainWindow] { activateMainWindow({}); });
    QObject::connect(
        &deepLinks,
        &kodosi::DeepLinkController::missingApprovalRequested,
        &application,
        [&activateMainWindow] { activateMainWindow({}); });
    const auto attachSize = windowSize
        ? windowSize
        : agentIntelSmokeTest
        ? std::optional<QSize>(QSize(820, 800))
        : std::nullopt;
    desktopState.attachWindow(mainWindow, attachSize);
    activateMainWindow(launchActivation.activationToken);
    launchActivation.activationToken.clear();
    if (initialDeepLink) {
        if (initialDeepLink->destination) {
            deepLinks.enqueue(*initialDeepLink->destination);
        } else {
            deepLinks.reject(initialDeepLink->error);
        }
    }
    if (deepLinkApprovalSmokeTest) {
        QTimer::singleShot(
            0,
            &application,
            [&application, rootObject] {
                const auto* drawer = rootObject->findChild<QObject*>(
                    QStringLiteral("panel.agentIntel"));
                if (drawer == nullptr
                    || !drawer->property("opened").toBool()
                    || drawer->property("sessionId").toString()
                        != QStringLiteral("attention-smoke")
                    || drawer->property("approvalIdentityToken")
                           .toString()
                           .isEmpty()) {
                    qCritical()
                        << "Approval deep link did not open the exact synthetic review.";
                    application.exit(EXIT_FAILURE);
                    return;
                }
                application.quit();
            });
    }
    if (deepLinkMissingApprovalSmokeTest) {
        QTimer::singleShot(
            0,
            &application,
            [&application, rootObject, &pendingPermissions] {
                const auto* drawer = rootObject->findChild<QObject*>(
                    QStringLiteral("panel.agentIntel"));
                const auto approval =
                    drawer == nullptr
                    ? QVariantMap {}
                    : drawer->property("approval").toMap();
                if (drawer == nullptr
                    || !drawer->property("opened").toBool()
                    || drawer->property("sessionId").toString()
                        != QStringLiteral("attention-smoke")
                    || !drawer->property("approvalMissing").toBool()
                    || !drawer->property("approvalIdentityToken")
                            .toString()
                            .isEmpty()
                    || !approval.isEmpty()) {
                    qCritical()
                        << "Missing approval deep link exposed an unrelated request.";
                    application.exit(EXIT_FAILURE);
                    return;
                }
                const auto descendants = drawer->findChildren<QObject*>();
                const auto before =
                    pendingPermissions.presentationForSession(
                        QStringLiteral("attention-smoke"));
                auto foundActionControl = false;
                for (auto* descendant : descendants) {
                    const auto name = descendant->objectName();
                    if (!name.startsWith(
                            QStringLiteral(
                                "panel.agentIntel.approval.allow."))
                        && !name.startsWith(
                            QStringLiteral(
                                "panel.agentIntel.approval.deny."))) {
                        continue;
                    }
                    foundActionControl = true;
                    if (descendant->property("visible").toBool()
                        || descendant->property("enabled").toBool()
                        || !QMetaObject::invokeMethod(
                            descendant,
                            "clicked",
                            Qt::DirectConnection)) {
                        qCritical()
                            << "Missing approval deep link retained an actionable control.";
                        application.exit(EXIT_FAILURE);
                        return;
                    }
                }
                if (!foundActionControl
                    || pendingPermissions.presentationForSession(
                           QStringLiteral("attention-smoke"))
                        != before) {
                    qCritical()
                        << "Missing approval deep link authorized an unrelated request.";
                    application.exit(EXIT_FAILURE);
                    return;
                }
                application.quit();
            });
    }
    if (agentIntelProbeOpen) {
        QTimer::singleShot(
            0,
            rootObject,
            [rootObject] {
                QVariant opened;
                if (!QMetaObject::invokeMethod(
                        rootObject,
                        "openAgentIntel",
                        Qt::DirectConnection,
                        Q_RETURN_ARG(QVariant, opened),
                        Q_ARG(
                            QVariant,
                            QVariant::fromValue(
                                QStringLiteral("attention-smoke"))),
                        Q_ARG(
                            QVariant,
                            QVariant::fromValue(QString {})))
                    || !opened.toBool()) {
                    qCritical()
                        << "The Agent Intelligence probe surface could not be opened.";
                    QCoreApplication::exit(EXIT_FAILURE);
                }
            });
    }
    if (projectIntelSmokeTest || projectIntelProbePopulated
        || projectIntelProbeEmpty || projectIntelProbeArchive) {
        QTimer::singleShot(
            0,
            rootObject,
            [rootObject, &projectIntelligence] {
                QVariant opened;
                if (!QMetaObject::invokeMethod(
                        rootObject,
                        "openProjectIntelSource",
                        Qt::DirectConnection,
                        Q_RETURN_ARG(QVariant, opened),
                        Q_ARG(
                            QVariant,
                            QVariant::fromValue(
                                projectIntelligence.selectedSourceId())))
                    || !opened.toBool()) {
                    qCritical()
                        << "The Project Intelligence smoke surface could not be opened.";
                    QCoreApplication::exit(EXIT_FAILURE);
                }
            });
    } else if (agentSettingsSmokeTest || agentSettingsProbePopulated) {
        QTimer::singleShot(
            0,
            rootObject,
            [rootObject] {
                QVariant opened;
                if (!QMetaObject::invokeMethod(
                        rootObject,
                        "openAgentSettings",
                        Qt::DirectConnection,
                        Q_RETURN_ARG(QVariant, opened))
                    || !opened.toBool()) {
                    qCritical()
                        << "The Agent Settings smoke surface could not be opened.";
                    QCoreApplication::exit(EXIT_FAILURE);
                }
            });
    }
    QObject::connect(
        &approvalNotifications,
        &kodosi::ApprovalNotifications::reviewRequested,
        &application,
        [rootObject, mainWindow](
            const QString& identityToken,
            const QString& sessionId,
            const QString& activationToken) {
            QVariant reviewed;
            const auto invoked = QMetaObject::invokeMethod(
                rootObject,
                "reviewApproval",
                Qt::DirectConnection,
                Q_RETURN_ARG(QVariant, reviewed),
                Q_ARG(QVariant, QVariant::fromValue(identityToken)),
                Q_ARG(QVariant, QVariant::fromValue(sessionId)));
            if (!invoked || !reviewed.toBool() || mainWindow == nullptr) {
                qWarning() << "The approval review surface could not be activated.";
                return;
            }
            kodosi::SingleInstanceGuard::withActivationToken(
                activationToken,
                [mainWindow] {
                    mainWindow->raise();
                    mainWindow->requestActivate();
                });
        });
    QObject::connect(
        &terminalNotifications,
        &kodosi::TerminalNotifications::sessionRequested,
        &application,
        [rootObject, mainWindow](
            const QString& sessionId,
            const QString& activationToken) {
            QVariant activated;
            const auto invoked = QMetaObject::invokeMethod(
                rootObject,
                "activateSession",
                Qt::DirectConnection,
                Q_RETURN_ARG(QVariant, activated),
                Q_ARG(QVariant, QVariant::fromValue(sessionId)));
            if (!invoked || !activated.toBool() || mainWindow == nullptr) {
                qWarning() << "The terminal notification session could not be activated.";
                return;
            }
            kodosi::SingleInstanceGuard::withActivationToken(
                activationToken,
                [mainWindow] {
                    mainWindow->raise();
                    mainWindow->requestActivate();
                });
        });

    if (desktopStateSmokeTest) {
        QTimer::singleShot(
            0,
            &application,
            [&application,
             &desktopState,
             &sessionCatalog,
             rootObject,
             mainWindow,
             windowSize] {
                const auto* views =
                    rootObject->findChild<QObject*>(
                        QStringLiteral("shell.views"));
                const auto* sidebar =
                    rootObject->findChild<QObject*>(
                        QStringLiteral("sidebar.sessions"));
                const auto available =
                    mainWindow->screen()->availableGeometry().size();
                const auto expectedSize = windowSize
                    ? QSize(
                          std::min(windowSize->width(), available.width()),
                          std::min(windowSize->height(), available.height()))
                    : mainWindow->size();
                if (!mainWindow->isVisible() || views == nullptr
                    || sidebar == nullptr
                    || views->property("currentIndex").toInt() != 2
                    || sidebar->property("visible").toBool()
                    || desktopState.selectedSessionId()
                        != QStringLiteral("pre-authoritative-session")
                    || mainWindow->size() != expectedSize) {
                    qCritical()
                        << "Desktop state composition was not restored before mapping.";
                    application.exit(EXIT_FAILURE);
                    return;
                }
                QTimer::singleShot(
                    0,
                    &application,
                    [&application, &desktopState, &sessionCatalog] {
                        sessionCatalog.resetRuntimeAuthority();
                        QTimer::singleShot(
                            0,
                            &application,
                            [&application, &desktopState, &sessionCatalog] {
                                if (desktopState.selectedSessionId()
                                    != QStringLiteral(
                                        "pre-authoritative-session")) {
                                    qCritical()
                                        << "Desktop state selection did not survive a pre-authoritative catalog reset.";
                                    application.exit(EXIT_FAILURE);
                                    return;
                                }
                                constexpr auto loadedEpoch = 91;
                                sessionCatalog.ingestAuthEvent(
                                    QJsonDocument(QJsonObject {
                                        {QStringLiteral("type"),
                                         QStringLiteral("auth.required")},
                                        {QStringLiteral("accountEpoch"),
                                         loadedEpoch},
                                    }).toJson(QJsonDocument::Compact));
                                sessionCatalog.ingestSessionEvent(
                                    QJsonDocument(QJsonObject {
                                        {QStringLiteral("authority"),
                                         QStringLiteral("accountContext")},
                                        {QStringLiteral("accountUserId"),
                                         QString {}},
                                        {QStringLiteral("accountEpoch"),
                                         loadedEpoch},
                                        {QStringLiteral("type"),
                                         QStringLiteral("session.list")},
                                        {QStringLiteral("sessions"),
                                         QJsonArray {
                                             QJsonObject {
                                                 {QStringLiteral("kind"),
                                                  QStringLiteral("local")},
                                                 {QStringLiteral("id"),
                                                  QStringLiteral(
                                                      "fallback-session")},
                                                 {QStringLiteral(
                                                      "incarnationId"),
                                                  QStringLiteral(
                                                      "01900000-0000-7000-8000-000000000091")},
                                                 {QStringLiteral("name"),
                                                  QStringLiteral(
                                                      "Fallback smoke")},
                                                 {QStringLiteral("project"),
                                                  QStringLiteral("/repo")},
                                                 {QStringLiteral("mode"),
                                                  QStringLiteral("normal")},
                                                 {QStringLiteral("status"),
                                                  QStringLiteral("waiting")},
                                                 {QStringLiteral("recovery"),
                                                  QStringLiteral("live")},
                                                 {QStringLiteral("scope"),
                                                  QStringLiteral("justMe")},
                                                 {QStringLiteral("access"),
                                                  QStringLiteral("approve")},
                                             },
                                         }},
                                    }).toJson(QJsonDocument::Compact));
                                QTimer::singleShot(
                                    0,
                                    &application,
                                    [&application, &desktopState] {
                                        if (!desktopState
                                                 .selectedSessionId()
                                                 .isEmpty()
                                            || !desktopState
                                                    .stagedSessionIds()
                                                    .isEmpty()) {
                                            qCritical()
                                                << "Desktop state retained a missing persisted Stage after the session catalog became authoritative.";
                                            application.exit(
                                                EXIT_FAILURE);
                                            return;
                                        }
                                        application.exit(EXIT_SUCCESS);
                                    });
                            });
                    });
            });
    } else if (tilingSmokeTest) {
        QTimer::singleShot(
            100,
            &application,
            [&application,
             &desktopState,
             &terminalTiling,
             rootObject] {
                auto* rootItem = qobject_cast<QQuickItem*>(
                    rootObject->property("contentItem").value<QObject*>());
                if (rootItem == nullptr) {
                    qCritical()
                        << "The tiling smoke could not inspect the window content item.";
                    application.exit(EXIT_FAILURE);
                    return;
                }
                QList<QObject*> originalTiles;
                QSet<QObject*> terminals;
                for (auto index = 0; index < 4; ++index) {
                    const auto suffix = QString::number(index);
                    auto* tile = findQuickItem(
                        rootItem,
                        QStringLiteral("stage.tile.tiling-") + suffix);
                    auto* terminal = findQuickItem(
                        rootItem,
                        QStringLiteral("stage.tile.tiling-")
                        + suffix
                        + QStringLiteral(".terminal"));
                    if (tile == nullptr || terminal == nullptr) {
                        qCritical()
                            << "The tiling smoke did not create four independent tiles:"
                            << index
                            << "staged"
                            << desktopState.stagedSessionIds()
                            << "layout entries"
                            << terminalTiling.rowCount()
                            << "viewport"
                            << terminalTiling.viewportWidth()
                            << terminalTiling.viewportHeight()
                            << "repeater count"
                            << (findQuickItem(
                                    rootItem,
                                    QStringLiteral("stage.entries"))
                                    == nullptr
                                ? -1
                                : findQuickItem(
                                      rootItem,
                                      QStringLiteral("stage.entries"))
                                      ->property("count")
                                      .toInt());
                        application.exit(EXIT_FAILURE);
                        return;
                    }
                    originalTiles.append(tile);
                    terminals.insert(terminal);
                }
                if (terminals.size() != 4
                    || desktopState.stagedSessionIds().size() != 4) {
                    qCritical()
                        << "The tiling smoke reused a terminal surface or lost staged membership.";
                    application.exit(EXIT_FAILURE);
                    return;
                }

                desktopState.enterFocusMode(
                    QStringLiteral("tiling-2"));
                QTimer::singleShot(
                    50,
                    &application,
                    [&application,
                     &desktopState,
                     &terminalTiling,
                     rootObject,
                     originalTiles] {
                        auto* rootItem = qobject_cast<QQuickItem*>(
                            rootObject->property("contentItem")
                                .value<QObject*>());
                        auto* focusExit =
                            findQuickItem(
                                rootItem,
                                QStringLiteral("stage.focus.exit"));
                        auto* selectedTile =
                            findQuickItem(
                                rootItem,
                                QStringLiteral(
                                    "stage.tile.tiling-2"));
                        auto visibleTiles = 0;
                        for (auto index = 0; index < 4; ++index) {
                            auto* tile = findQuickItem(
                                rootItem,
                                QStringLiteral(
                                    "stage.tile.tiling-")
                                + QString::number(index));
                            if (tile != nullptr && tile->isVisible()) {
                                ++visibleTiles;
                            }
                            if (tile != originalTiles.at(index)) {
                                qCritical()
                                    << "Focus mode recreated a terminal tile.";
                                application.exit(EXIT_FAILURE);
                                return;
                            }
                        }
                        if (focusExit == nullptr
                            || !focusExit->isVisible()
                            || selectedTile == nullptr
                            || !selectedTile->isVisible()
                            || visibleTiles != 1) {
                            qCritical()
                                << "Focus mode did not present exactly the selected terminal.";
                            application.exit(EXIT_FAILURE);
                            return;
                        }
                        desktopState.exitFocusMode();
                        if (!terminalTiling.adjustDivider(
                                QStringLiteral(
                                    "row.1"),
                                24)) {
                            qCritical()
                                << "The tiling divider could not be adjusted.";
                            application.exit(EXIT_FAILURE);
                            return;
                        }
                        auto* divider = findQuickItem(
                            rootItem,
                            QStringLiteral(
                                "stage.divider.row.1"));
                        if (divider == nullptr
                            || divider->property("value").toReal()
                                <= 25
                            || divider->property("minimumValue")
                                    .toReal()
                                != 0
                            || divider->property("maximumValue")
                                    .toReal()
                                != 100
                            || divider->property("stepSize").toReal()
                                != 1) {
                            qCritical()
                                << "The divider did not expose its accessible percentage.";
                            application.exit(EXIT_FAILURE);
                            return;
                        }
                        if (!desktopState.unstageSession(
                                QStringLiteral("tiling-2"))
                            || desktopState.stagedSessionIds().size()
                                != 3
                            || desktopState.selectedSessionId()
                                != QStringLiteral("tiling-3")) {
                            qCritical()
                                << "Unstaging did not select the last remaining terminal.";
                            application.exit(EXIT_FAILURE);
                            return;
                        }
                        if (!desktopState.stageSession(
                                QStringLiteral("tiling-4"))) {
                            qCritical()
                                << "The tiling smoke could not add a fifth Stage tile.";
                            application.exit(EXIT_FAILURE);
                            return;
                        }
                        QTimer::singleShot(
                            50,
                            &application,
                            [&application,
                             &desktopState,
                             &terminalTiling,
                             rootObject,
                             originalTiles] {
                                auto* rootItem =
                                    qobject_cast<QQuickItem*>(
                                        rootObject
                                            ->property("contentItem")
                                            .value<QObject*>());
                                auto* added = findQuickItem(
                                    rootItem,
                                    QStringLiteral(
                                        "stage.tile.tiling-4"));
                                for (const auto index : {0, 1, 3}) {
                                    if (findQuickItem(
                                            rootItem,
                                            QStringLiteral(
                                                "stage.tile.tiling-")
                                                + QString::number(index))
                                        != originalTiles.at(index)) {
                                        qCritical()
                                            << "Adding a Stage tile recreated an existing terminal tile.";
                                        application.exit(
                                            EXIT_FAILURE);
                                        return;
                                    }
                                }
                                if (added == nullptr
                                    || !desktopState.unstageSession(
                                        QStringLiteral("tiling-4"))) {
                                    qCritical()
                                        << "The fifth Stage tile was not created and removed exactly.";
                                    application.exit(EXIT_FAILURE);
                                    return;
                                }
                                rootObject->setProperty("width", 820);
                                rootObject->setProperty("height", 560);
                                QTimer::singleShot(
                                    100,
                                    &application,
                                    [&application,
                                     &desktopState,
                                     &terminalTiling,
                                     rootObject,
                                     originalTiles] {
                                        auto* rootItem =
                                            qobject_cast<QQuickItem*>(
                                                rootObject
                                                    ->property(
                                                        "contentItem")
                                                    .value<QObject*>());
                                        for (const auto index :
                                             {0, 1, 3}) {
                                            if (findQuickItem(
                                                    rootItem,
                                                    QStringLiteral(
                                                        "stage.tile.tiling-")
                                                        + QString::number(
                                                            index))
                                                != originalTiles.at(index)) {
                                                qCritical()
                                                    << "A column breakpoint recreated an existing terminal tile.";
                                                application.exit(
                                                    EXIT_FAILURE);
                                                return;
                                            }
                                        }
                                        auto* stageScroll =
                                            findQuickItem(
                                                rootItem,
                                                QStringLiteral(
                                                    "stage.scroll"));
                                        if (terminalTiling.columnCount()
                                                != 1
                                            || terminalTiling
                                                       .contentHeight()
                                                <= terminalTiling
                                                       .viewportHeight()
                                            || stageScroll == nullptr) {
                                            qCritical()
                                                << "The minimum window did not switch to a scrolling one-column Stage.";
                                            application.exit(
                                                EXIT_FAILURE);
                                            return;
                                        }
                                        stageScroll->setProperty(
                                            "contentY",
                                            100);
                                        if (!desktopState.selectSession(
                                                QStringLiteral(
                                                    "tiling-1"))) {
                                            application.exit(
                                                EXIT_FAILURE);
                                            return;
                                        }
                                        desktopState.enterFocusMode();
                                        QTimer::singleShot(
                                            50,
                                            &application,
                                            [&application,
                                             &desktopState,
                                             &terminalTiling,
                                             rootObject,
                                             originalTiles] {
                                                auto* rootItem =
                                                    qobject_cast<
                                                        QQuickItem*>(
                                                        rootObject
                                                            ->property(
                                                                "contentItem")
                                                            .value<
                                                                QObject*>());
                                                auto* stageScroll =
                                                    findQuickItem(
                                                        rootItem,
                                                        QStringLiteral(
                                                            "stage.scroll"));
                                                if (stageScroll
                                                        == nullptr
                                                    || stageScroll
                                                               ->property(
                                                                   "contentY")
                                                               .toReal()
                                                        != 0
                                                    || stageScroll
                                                        ->property(
                                                            "interactive")
                                                        .toBool()
                                                    || terminalTiling
                                                           .contentHeight()
                                                        != terminalTiling
                                                           .viewportHeight()) {
                                                    qCritical()
                                                        << "Focus mode did not lock the Stage viewport.";
                                                    application.exit(
                                                        EXIT_FAILURE);
                                                    return;
                                                }
                                                for (const auto index :
                                                     {0, 1, 3}) {
                                                    if (findQuickItem(
                                                            rootItem,
                                                            QStringLiteral(
                                                                "stage.tile.tiling-")
                                                                + QString::
                                                                    number(
                                                                        index))
                                                        != originalTiles.at(
                                                            index)) {
                                                        qCritical()
                                                            << "Focus after a breakpoint recreated a terminal tile.";
                                                        application.exit(
                                                            EXIT_FAILURE);
                                                        return;
                                                    }
                                                }
                                                desktopState
                                                    .exitFocusMode();
                                                application.exit(
                                                    EXIT_SUCCESS);
                                            });
                                    });
                            });
                    });
            });
    } else if (shellParitySmokeTest) {
        rootObject->setProperty("width", 820);
        rootObject->setProperty("height", 560);
        QTimer::singleShot(
            50,
            &application,
            [&application,
             &authActions,
             &authState,
             &desktopState,
             &devices,
             rootObject] {
                const QStringList shortcutObjects {
                    QStringLiteral("shortcut.settings"),
                    QStringLiteral("shortcut.session.new"),
                    QStringLiteral("shortcut.session.intelligence"),
                    QStringLiteral("shortcut.session.share"),
                    QStringLiteral("shortcut.session.close"),
                    QStringLiteral("shortcut.sidebar"),
                    QStringLiteral("shortcut.keyboardOverlay"),
                    QStringLiteral("shortcut.diagnostics"),
                    QStringLiteral("shortcut.stage.focus"),
                    QStringLiteral("shortcut.stage.next"),
                    QStringLiteral("shortcut.stage.previous"),
                };
                for (const auto& name : shortcutObjects) {
                    auto* shortcut =
                        rootObject->findChild<QObject*>(name);
                    if (shortcut == nullptr
                        || !shortcut->property("enabled").toBool()) {
                        qCritical()
                            << "A semantic shell shortcut is missing or gated:"
                            << name;
                        application.exit(EXIT_FAILURE);
                        return;
                    }
                }
                auto* exitFocus =
                    rootObject->findChild<QObject*>(
                        QStringLiteral("shortcut.stage.exitFocus"));
                if (exitFocus == nullptr
                    || exitFocus->property("enabled").toBool()) {
                    qCritical()
                        << "Exit-focus shortcut gating is incorrect.";
                    application.exit(EXIT_FAILURE);
                    return;
                }
                if (rootObject->findChild<QObject*>(
                        QStringLiteral("shortcut.approval.approve"))
                        != nullptr
                    || rootObject->findChild<QObject*>(
                           QStringLiteral("shortcut.approval.deny"))
                        != nullptr
                    || !QMetaObject::invokeMethod(
                        rootObject,
                        "toggleKeyboardShortcuts",
                        Qt::DirectConnection)) {
                    qCritical()
                        << "Approval shortcuts were invented without authority"
                        << "or the shortcut overlay did not open.";
                    application.exit(EXIT_FAILURE);
                    return;
                }
                QTimer::singleShot(
                    20,
                    rootObject,
                    [&application,
                     &authActions,
                     &authState,
                     &desktopState,
                     &devices,
                     rootObject] {
                        auto* overlay =
                            rootObject->findChild<QObject*>(
                                QStringLiteral("panel.shortcuts"));
                        auto* close =
                            rootObject->findChild<QObject*>(
                                QStringLiteral("panel.shortcuts.close"));
                        auto* escape =
                            rootObject->findChild<QObject*>(
                                QStringLiteral("panel.shortcuts.escape"));
                        auto* settingsShortcut =
                            rootObject->findChild<QObject*>(
                                QStringLiteral("shortcut.settings"));
                        if (overlay == nullptr
                            || !overlay->property("opened").toBool()
                            || overlay->property("width").toReal() > 620
                            || overlay->property("height").toReal() > 460
                            || close == nullptr
                            || !close->property("activeFocus").toBool()
                            || escape == nullptr
                            || settingsShortcut == nullptr
                            || settingsShortcut
                                   ->property("enabled")
                                   .toBool()
                            || !QMetaObject::invokeMethod(
                                escape,
                                "activated",
                                Qt::DirectConnection)) {
                            qCritical()
                                << "The shortcut overlay compact focus or Escape contract failed.";
                            application.exit(EXIT_FAILURE);
                            return;
                        }
                        QTimer::singleShot(
                            0,
                            rootObject,
                            [&application,
                             &authActions,
                             &authState,
                             &desktopState,
                             &devices,
                             rootObject,
                             overlay] {
                                if (overlay->property("opened").toBool()) {
                                    qCritical()
                                        << "Escape did not close the shortcut overlay.";
                                    application.exit(EXIT_FAILURE);
                                    return;
                                }
                                desktopState.setActiveView(2);
                                QTimer::singleShot(
                                    50,
                                    rootObject,
                                    [&application,
                                     &authActions,
                                     &authState,
                                     &desktopState,
                                     &devices,
                                     rootObject] {
                                        const QStringList deviceObjects {
                                            QStringLiteral("devices.incoming"),
                                            QStringLiteral(
                                                "devices.self-link.expiry"),
                                            QStringLiteral(
                                                "devices.self-link.cancel"),
                                            QStringLiteral(
                                                "devices.current.signer"),
                                            QStringLiteral(
                                                "devices.current.issued"),
                                        };
                                        for (const auto& name :
                                             deviceObjects) {
                                            auto* object =
                                                rootObject
                                                    ->findChild<QObject*>(
                                                        name);
                                            if (object == nullptr
                                                || !object
                                                        ->property(
                                                            "visible")
                                                        .toBool()) {
                                                qCritical()
                                                    << "The device detail presentation is incomplete:"
                                                    << name
                                                    << "pending"
                                                    << devices.pendingLinks()
                                                           ->rowCount();
                                                for (auto* candidate :
                                                     rootObject
                                                         ->findChildren<
                                                             QObject*>()) {
                                                    if (candidate
                                                            ->objectName()
                                                            .startsWith(
                                                                QStringLiteral(
                                                                    "devices.incoming"))) {
                                                        qCritical()
                                                            << "Available:"
                                                            << candidate
                                                                   ->objectName();
                                                    }
                                                }
                                                application.exit(
                                                    EXIT_FAILURE);
                                                return;
                                            }
                                        }
                                        auto* incomingRepeater =
                                            rootObject
                                                ->findChild<QObject*>(
                                                    QStringLiteral(
                                                        "devices.incoming.repeater"));
                                        if (incomingRepeater == nullptr
                                            || incomingRepeater
                                                   ->property("count")
                                                   .toInt()
                                                != 1) {
                                            qCritical()
                                                << "Incoming device links were not instantiated.";
                                            application.exit(
                                                EXIT_FAILURE);
                                            return;
                                        }
                                        devices.ingestDevicesEvent(
                                            QByteArrayLiteral(
                                                "{\"authority\":\"accountContext\","
                                                "\"accountUserId\":\"shell-user\","
                                                "\"accountEpoch\":93,"
                                                "\"type\":\"devices.link.selfResolved\","
                                                "\"outcome\":\"expired\"}"));
                                        devices.ingestDevicesEvent(
                                            QByteArrayLiteral(
                                                "{\"authority\":\"accountContext\","
                                                "\"accountUserId\":\"shell-user\","
                                                "\"accountEpoch\":93,"
                                                "\"type\":\"devices.link.resolved\","
                                                "\"userCode\":\"BCDF-2345\","
                                                "\"outcome\":\"approved\"}"));
                                        devices.ingestDevicesEvent(
                                            QByteArrayLiteral(
                                                "{\"authority\":\"accountContext\","
                                                "\"accountUserId\":\"shell-user\","
                                                "\"accountEpoch\":93,"
                                                "\"type\":\"devices.error\","
                                                "\"operation\":\"link.approve\","
                                                "\"userCode\":\"BCDF-2345\","
                                                "\"message\":\"approval detail\"}"));
                                        QTimer::singleShot(
                                            0,
                                            rootObject,
                                            [&application,
                                             &authActions,
                                             &authState,
                                             &desktopState,
                                             rootObject] {
                                                auto* selfOutcome =
                                                    rootObject
                                                        ->findChild<QObject*>(
                                                            QStringLiteral(
                                                                "devices.self-link.outcome"));
                                                auto* linkOutcome =
                                                    rootObject
                                                        ->findChild<QObject*>(
                                                            QStringLiteral(
                                                                "devices.link.outcome"));
                                                auto* regenerate =
                                                    rootObject
                                                        ->findChild<QObject*>(
                                                            QStringLiteral(
                                                                "devices.self-link.regenerate"));
                                                auto* deviceError =
                                                    rootObject
                                                        ->findChild<QObject*>(
                                                            QStringLiteral(
                                                                "devices.error"));
                                                auto* errorRetry =
                                                    rootObject
                                                        ->findChild<QObject*>(
                                                            QStringLiteral(
                                                                "devices.error.retry"));
                                                auto* errorDismiss =
                                                    rootObject
                                                        ->findChild<QObject*>(
                                                            QStringLiteral(
                                                                "devices.error.dismiss"));
                                                if (selfOutcome == nullptr
                                                    || !selfOutcome
                                                            ->property(
                                                                "visible")
                                                            .toBool()
                                                    || linkOutcome == nullptr
                                                    || !linkOutcome
                                                            ->property(
                                                                "visible")
                                                            .toBool()
                                                    || regenerate == nullptr
                                                    || !regenerate
                                                            ->property(
                                                                "visible")
                                                            .toBool()
                                                    || deviceError == nullptr
                                                    || !deviceError
                                                            ->property(
                                                                "visible")
                                                            .toBool()
                                                    || errorRetry == nullptr
                                                    || !errorRetry
                                                            ->property(
                                                                "visible")
                                                            .toBool()
                                                    || errorDismiss == nullptr
                                                    || !QMetaObject::
                                                        invokeMethod(
                                                            errorDismiss,
                                                            "click",
                                                            Qt::
                                                                DirectConnection)
                                                    || !QMetaObject::
                                                        invokeMethod(
                                                            rootObject,
                                                            "openSettings",
                                                            Qt::
                                                                DirectConnection)) {
                                                    qCritical()
                                                        << "Device outcome presentation or Settings navigation failed.";
                                                    application.exit(
                                                        EXIT_FAILURE);
                                                    return;
                                                }
                                                QTimer::singleShot(
                                                    20,
                                                    rootObject,
                                                    [&application,
                                                     &authActions,
                                                     &authState,
                                                     &desktopState,
                                                     rootObject] {
                                                        auto* settings =
                                                            rootObject
                                                                ->findChild<
                                                                    QObject*>(
                                                                    QStringLiteral(
                                                                        "panel.settings"));
                                                        if (settings
                                                            == nullptr) {
                                                            application.exit(
                                                                EXIT_FAILURE);
                                                            return;
                                                        }
                                                        settings->setProperty(
                                                            "selectedCategory",
                                                            QStringLiteral(
                                                                "account"));
                                                        QTimer::singleShot(
                                                            0,
                                                            settings,
                                                            [&application,
                                                             &authActions,
                                                             &authState,
                                                             &desktopState,
                                                             rootObject,
                                                             settings] {
                                                                auto* reset =
                                                                    rootObject
                                                                        ->findChild<
                                                                            QObject*>(
                                                                            QStringLiteral(
                                                                                "panel.settings.account.resetIdentity"));
                                                                auto* input =
                                                                    rootObject
                                                                        ->findChild<
                                                                            QObject*>(
                                                                            QStringLiteral(
                                                                                "panel.settings.account.resetConfirm.input"));
                                                                auto* confirm =
                                                                    rootObject
                                                                        ->findChild<
                                                                            QObject*>(
                                                                            QStringLiteral(
                                                                                "panel.settings.account.resetConfirm.confirm"));
                                                                if (reset
                                                                        == nullptr
                                                                    || input
                                                                        == nullptr
                                                                    || confirm
                                                                        == nullptr
                                                                    || !QMetaObject::
                                                                        invokeMethod(
                                                                            reset,
                                                                            "click",
                                                                            Qt::
                                                                                DirectConnection)) {
                                                                    qCritical()
                                                                        << "Identity reset confirmation controls are incomplete.";
                                                                    application
                                                                        .exit(
                                                                            EXIT_FAILURE);
                                                                    return;
                                                                }
                                                                input
                                                                    ->setProperty(
                                                                        "text",
                                                                        QStringLiteral(
                                                                            "reset"));
                                                                (void)QMetaObject::
                                                                    invokeMethod(
                                                                        confirm,
                                                                        "click",
                                                                        Qt::
                                                                            DirectConnection);
                                                                if (authActions
                                                                        .failedOperation()
                                                                    == QStringLiteral(
                                                                        "identity.reset")) {
                                                                    qCritical()
                                                                        << "Lowercase reset text triggered identity reset.";
                                                                    application
                                                                        .exit(
                                                                            EXIT_FAILURE);
                                                                    return;
                                                                }
                                                                input
                                                                    ->setProperty(
                                                                        "text",
                                                                        QStringLiteral(
                                                                            "RESET"));
                                                                if (!confirm
                                                                         ->property(
                                                                             "enabled")
                                                                         .toBool()
                                                                    || !QMetaObject::
                                                                        invokeMethod(
                                                                            confirm,
                                                                            "click",
                                                                            Qt::
                                                                                DirectConnection)
                                                                    || authActions
                                                                           .failedOperation()
                                                                        != QStringLiteral(
                                                                            "identity.reset")) {
                                                                    qCritical()
                                                                        << "Exact RESET did not dispatch identity recovery.";
                                                                    application
                                                                        .exit(
                                                                            EXIT_FAILURE);
                                                                    return;
                                                                }
                                                                (void)QMetaObject::
                                                                    invokeMethod(
                                                                        settings,
                                                                        "close",
                                                                        Qt::
                                                                            DirectConnection);
                                                                authActions
                                                                    .clearError();
                                                                authState
                                                                    .ingestAuthEvent(
                                                                        QByteArrayLiteral(
                                                                            "{\"type\":\"auth.required\","
                                                                            "\"reason\":\"signedOut\","
                                                                            "\"accountEpoch\":94}"));
                                                                desktopState
                                                                    .setActiveView(
                                                                        1);
                                                                QTimer::
                                                                    singleShot(
                                                                        0,
                                                                        rootObject,
                                                                        [&application,
                                                                         &desktopState,
                                                                         rootObject] {
                                                                            const QStringList signedOutObjects {
                                                                                QStringLiteral(
                                                                                    "header.auth.signIn"),
                                                                                QStringLiteral(
                                                                                    "auth.gate.missions"),
                                                                            };
                                                                            for (const auto& name :
                                                                                 signedOutObjects) {
                                                                                auto* object =
                                                                                    rootObject
                                                                                        ->findChild<
                                                                                            QObject*>(
                                                                                            name);
                                                                                if (object
                                                                                        == nullptr
                                                                                    || !object
                                                                                            ->property(
                                                                                                "visible")
                                                                                            .toBool()) {
                                                                                    qCritical()
                                                                                        << "Signed-out navigation is missing:"
                                                                                        << name;
                                                                                    application
                                                                                        .exit(
                                                                                            EXIT_FAILURE);
                                                                                    return;
                                                                                }
                                                                            }
                                                                            if (rootObject
                                                                                    ->findChild<
                                                                                        QObject*>(
                                                                                        QStringLiteral(
                                                                                            "panel.settings.account.signIn"))
                                                                                == nullptr) {
                                                                                qCritical()
                                                                                    << "Settings has no signed-out account path.";
                                                                                application
                                                                                    .exit(
                                                                                        EXIT_FAILURE);
                                                                                return;
                                                                            }
                                                                            desktopState
                                                                                .setActiveView(
                                                                                    0);
                                                                            auto* newSession =
                                                                                rootObject
                                                                                    ->findChild<
                                                                                        QObject*>(
                                                                                        QStringLiteral(
                                                                                            "sidebar.session.new"));
                                                                            if (newSession
                                                                                    == nullptr
                                                                                || !newSession
                                                                                        ->property(
                                                                                            "enabled")
                                                                                        .toBool()) {
                                                                                qCritical()
                                                                                    << "Local My Agents was disabled while signed out.";
                                                                                application
                                                                                    .exit(
                                                                                        EXIT_FAILURE);
                                                                                return;
                                                                            }
                                                                            application
                                                                                .exit(
                                                                                    EXIT_SUCCESS);
                                                                        });
                                                            });
                                                    });
                                            });
                                    });
                            });
                    });
            });
    } else if (appearanceSmokeTest) {
        QTimer::singleShot(
            50,
            &application,
            [&application,
             &appearance,
             &authActions,
             rootObject] {
                auto* utilityButton = rootObject->findChild<QObject*>(
                    QStringLiteral("header.utility.menu"));
                auto* menu = rootObject->findChild<QObject*>(
                    QStringLiteral("panel.utility"));
                auto* close = rootObject->findChild<QObject*>(
                    QStringLiteral("panel.utility.close"));
                auto* settings = rootObject->findChild<QObject*>(
                    QStringLiteral("panel.utility.settings"));
                auto* light = rootObject->findChild<QObject*>(
                    QStringLiteral(
                        "panel.utility.appearance.light"));
                auto* dark = rootObject->findChild<QObject*>(
                    QStringLiteral(
                        "panel.utility.appearance.dark"));
                auto* system = rootObject->findChild<QObject*>(
                    QStringLiteral(
                        "panel.utility.appearance.system"));
                auto* signOut = rootObject->findChild<QObject*>(
                    QStringLiteral("panel.utility.signOut"));
                if (utilityButton == nullptr || menu == nullptr
                    || close == nullptr || settings == nullptr
                    || light == nullptr || dark == nullptr
                    || system == nullptr || signOut == nullptr
                    || appearance.preference()
                        != kodosi::AppearanceModel::Preference::Light
                    || appearance.effectiveScheme()
                        != kodosi::AppearanceModel::EffectiveScheme::
                            LightScheme
                    || rootObject->property("themeMotionFast").toInt()
                        != 0
                    || rootObject->property("themeMotionNormal").toInt()
                        != 0
                    || !themeContrastPasses(rootObject)
                    || !QMetaObject::invokeMethod(
                        utilityButton,
                        "click",
                        Qt::DirectConnection)) {
                    qCritical()
                        << "The appearance menu or reduced-motion QML contract is incomplete.";
                    application.exit(EXIT_FAILURE);
                    return;
                }
                QTimer::singleShot(
                    20,
                    menu,
                    [&application,
                     &appearance,
                     &authActions,
                     rootObject,
                     menu,
                     settings,
                     dark,
                     system,
                     signOut] {
                        if (!menu->property("opened").toBool()
                            || !QMetaObject::invokeMethod(
                                dark,
                                "click",
                                Qt::DirectConnection)) {
                            qCritical()
                                << "The header utility control did not open or select Dark.";
                            application.exit(EXIT_FAILURE);
                            return;
                        }
                        QTimer::singleShot(
                            0,
                            menu,
                            [&application,
                             &appearance,
                             &authActions,
                             rootObject,
                             menu,
                             settings,
                             system,
                             signOut] {
                                if (appearance.preference()
                                        != kodosi::AppearanceModel::
                                            Preference::Dark
                                    || appearance.effectiveScheme()
                                        != kodosi::AppearanceModel::
                                            EffectiveScheme::DarkScheme
                                    || !themeContrastPasses(rootObject)
                                    || !QMetaObject::invokeMethod(
                                        system,
                                        "click",
                                        Qt::DirectConnection)
                                    || appearance.preference()
                                        != kodosi::AppearanceModel::
                                            Preference::System
                                    || !QMetaObject::invokeMethod(
                                        settings,
                                        "click",
                                        Qt::DirectConnection)) {
                                    qCritical()
                                        << "Appearance actions or authored palette contrast failed.";
                                    application.exit(EXIT_FAILURE);
                                    return;
                                }
                                QTimer::singleShot(
                                    0,
                                    rootObject,
                                    [&application,
                                     &authActions,
                                     rootObject,
                                     menu,
                                     signOut] {
                                        auto* settingsPanel =
                                            rootObject
                                                ->findChild<QObject*>(
                                                    QStringLiteral(
                                                        "panel.settings"));
                                        if (settingsPanel == nullptr
                                            || !settingsPanel
                                                    ->property("opened")
                                                    .toBool()
                                            || menu
                                                ->property("opened")
                                                .toBool()
                                            || !QMetaObject::
                                                invokeMethod(
                                                    settingsPanel,
                                                    "close",
                                                    Qt::DirectConnection)
                                            || !QMetaObject::
                                                invokeMethod(
                                                    menu,
                                                    "open",
                                                    Qt::DirectConnection)) {
                                            qCritical()
                                                << "The utility Settings action did not preserve Settings behavior.";
                                            application.exit(
                                                EXIT_FAILURE);
                                            return;
                                        }
                                        QTimer::singleShot(
                                            0,
                                            menu,
                                            [&application,
                                             &authActions,
                                             signOut] {
                                                if (!signOut
                                                         ->property(
                                                             "visible")
                                                         .toBool()
                                                    || !QMetaObject::
                                                        invokeMethod(
                                                            signOut,
                                                            "click",
                                                            Qt::
                                                                DirectConnection)
                                                    || authActions
                                                           .failedOperation()
                                                        != QStringLiteral(
                                                            "logout")) {
                                                    qCritical()
                                                        << "The utility Sign out action is not wired.";
                                                    application.exit(
                                                        EXIT_FAILURE);
                                                    return;
                                                }
                                                application.exit(
                                                    EXIT_SUCCESS);
                                            });
                                    });
                            });
                    });
            });
    } else if (deepLinkStatusSmokeTest) {
        QTimer::singleShot(
            50,
            &application,
            [&application,
             &deepLinks,
             mainWindow,
             rootObject] {
                auto* rootItem = qobject_cast<QQuickItem*>(
                    rootObject->property("contentItem")
                        .value<QObject*>());
                auto* banner = findQuickItem(
                    rootItem,
                    QStringLiteral("deepLink.status"));
                auto* label = findQuickItem(
                    rootItem,
                    QStringLiteral("deepLink.status.label"));
                auto* dismiss = findQuickItem(
                    rootItem,
                    QStringLiteral("deepLink.status.dismiss"));
                if (banner == nullptr || label == nullptr
                    || dismiss == nullptr || mainWindow == nullptr
                    || !banner->isVisible()
                    || !dismiss->isVisible()
                    || !dismiss->isEnabled()
                    || banner->width() > mainWindow->width()
                    || banner->height() > mainWindow->height()
                    || label->width() <= 0
                    || label->x() + label->width()
                        > dismiss->x()) {
                    qCritical()
                        << "The compact deep-link status banner layout is invalid.";
                    application.exit(EXIT_FAILURE);
                    return;
                }
                const auto priorText =
                    label->property("text").toString();
                if (!QMetaObject::invokeMethod(
                        dismiss,
                        "clicked",
                        Qt::DirectConnection)
                    || !deepLinks.statusCode().isEmpty()
                    || banner->isVisible()) {
                    qCritical()
                        << "The deep-link status dismiss action is not wired.";
                    application.exit(EXIT_FAILURE);
                    return;
                }
                deepLinks.reject(
                    kodosi::DeepLinkParseError::UnsupportedRoute);
                if (!banner->isVisible()
                    || deepLinks.statusCode()
                        != QStringLiteral("invalid")
                    || label->property("text").toString()
                        == priorText) {
                    qCritical()
                        << "A later route did not replace the deep-link status.";
                    application.exit(EXIT_FAILURE);
                    return;
                }
                application.exit(EXIT_SUCCESS);
            });
    } else if (smokeTest) {
        QVariant staleSession;
        if (!QMetaObject::invokeMethod(
                rootObject,
                "activateSession",
                Qt::DirectConnection,
                Q_RETURN_ARG(QVariant, staleSession),
                Q_ARG(
                    QVariant,
                    QVariant::fromValue(QStringLiteral("missing"))))
            || staleSession.toBool()) {
            qCritical() << "The terminal notification activation contract is invalid.";
            return EXIT_FAILURE;
        }
        QVariant staleReview;
        if (!QMetaObject::invokeMethod(
                rootObject,
                "reviewApproval",
                Qt::DirectConnection,
                Q_RETURN_ARG(QVariant, staleReview),
                Q_ARG(
                    QVariant,
                    QVariant::fromValue(QStringLiteral("missing"))),
                Q_ARG(
                    QVariant,
                    QVariant::fromValue(QStringLiteral("missing"))))
            || staleReview.toBool()) {
            qCritical() << "The approval review surface contract is invalid.";
            return EXIT_FAILURE;
        }
        const auto panelName = settingsSmokeTest || agentSettingsSmokeTest
            ? QStringLiteral("panel.settings")
            : projectIntelSmokeTest
            ? QStringLiteral("panel.projectIntel")
            : agentIntelSmokeTest
            ? QStringLiteral("panel.agentIntel")
            : attentionSmokeTest
            ? QStringLiteral("panel.attention")
            : QStringLiteral("panel.diagnostics");
        auto* panel = rootObject->findChild<QObject*>(panelName);
        if (panel == nullptr
            || !QMetaObject::invokeMethod(
                panel,
                "open",
                Qt::DirectConnection)) {
            qCritical() << "The requested smoke-test surface could not be opened.";
            return EXIT_FAILURE;
        }
        if (agentIntelSmokeTest) {
            panel->setProperty("sessionId", QStringLiteral("smoke-session"));
            QTimer::singleShot(
                50,
                &application,
                [panel,
                 &agentConversation,
                 &agentCustomAgents,
                 &agentMemory] {
                auto* overviewTab = panel->findChild<QObject*>(
                    QStringLiteral("panel.agentIntel.tab.overview"));
                if (overviewTab == nullptr
                    || !overviewTab->property("activeFocus").toBool()) {
                    qCritical()
                        << "Agent Intelligence did not defer focus to Overview.";
                    QCoreApplication::exit(EXIT_FAILURE);
                    return;
                }
                if (agentConversation.loading()
                    || agentConversation.rowCount() != 0
                    || !agentConversation.error().isEmpty()) {
                    qCritical()
                        << "Opening Agent Intelligence eagerly acquired History demand.";
                    QCoreApplication::exit(EXIT_FAILURE);
                    return;
                }
                if (agentMemory.state()
                        != kodosi::AgentMemoryModel::State::Dormant
                    || agentMemory.rowCount() != 0) {
                    qCritical()
                        << "Opening Agent Intelligence eagerly acquired Memory demand.";
                    QCoreApplication::exit(EXIT_FAILURE);
                    return;
                }
                if (agentCustomAgents.state()
                        != kodosi::AgentCustomAgentsModel::State::Dormant
                    || agentCustomAgents.rowCount() != 0) {
                    qCritical()
                        << "Opening Agent Intelligence eagerly acquired Custom Agents demand.";
                    QCoreApplication::exit(EXIT_FAILURE);
                    return;
                }
                const QStringList steeringObjects {
                    QStringLiteral("panel.agentIntel.steer"),
                    QStringLiteral("panel.agentIntel.steer.input"),
                    QStringLiteral("panel.agentIntel.steer.mode"),
                    QStringLiteral("panel.agentIntel.steer.send"),
                    QStringLiteral("panel.agentIntel.steer.status"),
                    QStringLiteral("panel.agentIntel.steer.cancel"),
                    QStringLiteral("panel.agentIntel.steer.retry"),
                    QStringLiteral("panel.agentIntel.steer.error.dismiss"),
                    QStringLiteral("panel.agentIntel.tab.overview"),
                    QStringLiteral("panel.agentIntel.tab.history"),
                    QStringLiteral("panel.agentIntel.tab.memory"),
                    QStringLiteral("panel.agentIntel.tab.agents"),
                    QStringLiteral("panel.agentIntel.history"),
                    QStringLiteral("panel.agentIntel.history.notice"),
                    QStringLiteral("panel.agentIntel.history.loadEarlier"),
                    QStringLiteral("panel.agentIntel.history.empty"),
                    QStringLiteral("panel.agentIntel.history.error"),
                    QStringLiteral("panel.agentIntel.history.retry"),
                    QStringLiteral("panel.agentIntel.history.list"),
                    QStringLiteral("panel.agentIntel.memory"),
                    QStringLiteral("panel.agentIntel.memory.notice"),
                    QStringLiteral("panel.agentIntel.memory.notice.retry"),
                    QStringLiteral("panel.agentIntel.memory.empty"),
                    QStringLiteral("panel.agentIntel.memory.status"),
                    QStringLiteral("panel.agentIntel.memory.retry"),
                    QStringLiteral("panel.agentIntel.memory.list"),
                    QStringLiteral("panel.agentIntel.memory.preview"),
                    QStringLiteral("panel.agentIntel.memory.preview.title"),
                    QStringLiteral("panel.agentIntel.memory.preview.loading"),
                    QStringLiteral("panel.agentIntel.memory.preview.error"),
                    QStringLiteral("panel.agentIntel.memory.preview.retry"),
                    QStringLiteral("panel.agentIntel.memory.preview.content"),
                    QStringLiteral("panel.agentIntel.agents"),
                    QStringLiteral("panel.agentIntel.agents.notice"),
                    QStringLiteral("panel.agentIntel.agents.notice.retry"),
                    QStringLiteral("panel.agentIntel.agents.empty"),
                    QStringLiteral("panel.agentIntel.agents.status"),
                    QStringLiteral("panel.agentIntel.agents.retry"),
                    QStringLiteral("panel.agentIntel.agents.list"),
                    QStringLiteral("panel.agentIntel.agents.detail"),
                    QStringLiteral("panel.agentIntel.agents.detail.title"),
                    QStringLiteral("panel.agentIntel.agents.detail.errors"),
                    QStringLiteral("panel.agentIntel.agents.detail.frontmatter"),
                    QStringLiteral("panel.agentIntel.agents.detail.prompt"),
                };
                for (const auto& objectName : steeringObjects) {
                    if (panel->findChild<QObject*>(objectName) == nullptr) {
                        qCritical() << "The steering footer smoke contract is incomplete:"
                                    << objectName;
                        QCoreApplication::exit(EXIT_FAILURE);
                        return;
                    }
                }
                auto* steeringFooter =
                    panel->findChild<QObject*>(
                        QStringLiteral("panel.agentIntel.steer"));
                auto* steeringLayout = steeringFooter->parent();
                if (steeringLayout == nullptr) {
                    qCritical() << "The steering footer has no layout parent.";
                    QCoreApplication::exit(EXIT_FAILURE);
                    return;
                }
                const auto footerWidth =
                    steeringFooter->property("width").toReal();
                const auto layoutWidth =
                    steeringLayout->property("width").toReal();
                if (footerWidth + 1.0 < layoutWidth) {
                    qCritical()
                        << "The steering footer does not span the Agent Intelligence surface"
                        << "at the minimum supported window width:"
                        << footerWidth << "of" << layoutWidth;
                    QCoreApplication::exit(EXIT_FAILURE);
                    return;
                }
                if (!QMetaObject::invokeMethod(
                        panel,
                        "showSurface",
                        Qt::DirectConnection,
                        Q_ARG(QVariant, QVariant::fromValue(2)))) {
                    qCritical() << "The Project Memory tab could not be activated.";
                    QCoreApplication::exit(EXIT_FAILURE);
                    return;
                }
                QTimer::singleShot(
                    50,
                    panel,
                    [panel, &agentCustomAgents, &agentMemory] {
                        auto* memorySurface =
                            panel->findChild<QObject*>(
                                QStringLiteral("panel.agentIntel.memory"));
                        auto* memoryPreview =
                            panel->findChild<QObject*>(
                                QStringLiteral(
                                    "panel.agentIntel.memory.preview"));
                        if (memorySurface == nullptr
                            || memoryPreview == nullptr
                            || memorySurface->property("width").toReal() <= 0
                            || memoryPreview->property("width").toReal() <= 0) {
                            qCritical()
                                << "Project Memory does not lay out at the minimum supported width.";
                            QCoreApplication::exit(EXIT_FAILURE);
                            return;
                        }
                        if (!QMetaObject::invokeMethod(
                                panel,
                                "showSurface",
                                Qt::DirectConnection,
                                Q_ARG(
                                    QVariant,
                                    QVariant::fromValue(0)))
                            || agentMemory.state()
                                != kodosi::AgentMemoryModel::State::Dormant) {
                            qCritical()
                                << "Leaving Project Memory did not release demand.";
                            QCoreApplication::exit(EXIT_FAILURE);
                            return;
                        }
                        if (!QMetaObject::invokeMethod(
                                panel,
                                "showSurface",
                                Qt::DirectConnection,
                                Q_ARG(
                                    QVariant,
                                    QVariant::fromValue(3)))) {
                            qCritical() << "The Custom Agents tab could not be activated.";
                            QCoreApplication::exit(EXIT_FAILURE);
                            return;
                        }
                        QTimer::singleShot(
                            50,
                            panel,
                            [panel, &agentCustomAgents] {
                                auto* agentsSurface =
                                    panel->findChild<QObject*>(
                                        QStringLiteral(
                                            "panel.agentIntel.agents"));
                                auto* agentsDetail =
                                    panel->findChild<QObject*>(
                                        QStringLiteral(
                                            "panel.agentIntel.agents.detail"));
                                if (agentsSurface == nullptr
                                    || agentsDetail == nullptr
                                    || !agentsSurface->property("visible").toBool()
                                    || agentsSurface->property("width").toReal()
                                        <= 0
                                    || agentsDetail->property("width").toReal()
                                        <= 0) {
                                    qCritical()
                                        << "Custom Agents does not lay out at the minimum supported width.";
                                    QCoreApplication::exit(EXIT_FAILURE);
                                    return;
                                }
                                if (agentCustomAgents.state()
                                    != kodosi::AgentCustomAgentsModel::State::Waiting) {
                                    qCritical()
                                        << "Opening Custom Agents did not retain pre-hydration demand.";
                                    QCoreApplication::exit(EXIT_FAILURE);
                                    return;
                                }
                                if (!QMetaObject::invokeMethod(
                                        panel,
                                        "showSurface",
                                        Qt::DirectConnection,
                                        Q_ARG(
                                            QVariant,
                                            QVariant::fromValue(0)))
                                    || agentCustomAgents.state()
                                        != kodosi::AgentCustomAgentsModel::State::Dormant) {
                                    qCritical()
                                        << "Leaving Custom Agents did not release demand.";
                                    QCoreApplication::exit(EXIT_FAILURE);
                                    return;
                                }
                                QCoreApplication::quit();
                            });
                    });
            });
        } else if (projectIntelSmokeTest) {
            QTimer::singleShot(
                100,
                &application,
                [panel, rootObject, &projectIntelligence] {
                    const QStringList requiredObjects {
                        QStringLiteral("panel.projectIntel"),
                        QStringLiteral("panel.projectIntel.close"),
                        QStringLiteral("panel.projectIntel.refresh"),
                        QStringLiteral("panel.projectIntel.sources"),
                        QStringLiteral("panel.projectIntel.tabs"),
                        QStringLiteral("panel.projectIntel.tab.sessions"),
                        QStringLiteral("panel.projectIntel.tab.memory"),
                        QStringLiteral("panel.projectIntel.tab.servers"),
                        QStringLiteral("panel.projectIntel.tab.agents"),
                        QStringLiteral("panel.projectIntel.sessions.list"),
                        QStringLiteral("panel.projectIntel.memory.list"),
                        QStringLiteral("panel.projectIntel.memory.open"),
                        QStringLiteral("panel.projectIntel.memory.copy"),
                        QStringLiteral("panel.projectIntel.servers.list"),
                        QStringLiteral("panel.projectIntel.agents.list"),
                        QStringLiteral("panel.projectIntel.agent.open"),
                        QStringLiteral(
                            "panel.projectIntel.agents.detail.parseErrors"),
                        QStringLiteral(
                            "panel.projectIntel.agents.detail.frontmatter"),
                        QStringLiteral(
                            "panel.projectIntel.agents.detail.prompt"),
                    };
                    for (const auto& objectName : requiredObjects) {
                        if (rootObject->findChild<QObject*>(objectName)
                            == nullptr) {
                            qCritical()
                                << "The Project Intelligence smoke contract is incomplete:"
                                << objectName;
                            QCoreApplication::exit(EXIT_FAILURE);
                            return;
                        }
                    }
                    auto* sources = rootObject->findChild<QObject*>(
                        QStringLiteral("panel.projectIntel.sources"));
                    auto* focusedItem = QGuiApplication::focusObject();
                    const auto selectedSourceName =
                        QStringLiteral(
                            "panel.projectIntel.source.selected");
                    if (panel->property("width").toReal() > 820
                        || panel->property("height").toReal() > 560
                        || sources == nullptr
                        || focusedItem == nullptr
                        || focusedItem->objectName()
                            != selectedSourceName
                        || projectIntelligence.sources()->rowCount() < 2
                        || projectIntelligence.sessions()->rowCount() < 2
                        || projectIntelligence.memories()->rowCount() < 2
                        || projectIntelligence.agents()->rowCount() < 1
                        || projectIntelligence.customizations()->rowCount() < 1) {
                        qCritical()
                            << "Project Intelligence did not expose its populated"
                            << "minimum-size state or initial focus."
                            << "size" << panel->property("width")
                            << panel->property("height")
                            << "sourceList" << sources
                            << (sources == nullptr
                                    ? QVariant {}
                                    : sources->property("activeFocus"))
                            << "focusedItem" << focusedItem
                            << (focusedItem == nullptr
                                    ? QString {}
                                    : focusedItem->objectName())
                            << "expected" << selectedSourceName
                            << "rows"
                            << projectIntelligence.sources()->rowCount()
                            << projectIntelligence.sessions()->rowCount()
                            << projectIntelligence.memories()->rowCount()
                            << projectIntelligence.agents()->rowCount()
                            << projectIntelligence.customizations()->rowCount();
                        QCoreApplication::exit(EXIT_FAILURE);
                        return;
                    }
                    for (auto tab = 0; tab < 4; ++tab) {
                        if (!QMetaObject::invokeMethod(
                                panel,
                                "activateTab",
                                Qt::DirectConnection,
                                Q_ARG(QVariant, QVariant::fromValue(tab)),
                                Q_ARG(QVariant, QVariant::fromValue(false)))
                            || panel->property("activeTab").toInt() != tab) {
                            qCritical()
                                << "Project Intelligence tab navigation failed:"
                                << tab;
                            QCoreApplication::exit(EXIT_FAILURE);
                            return;
                        }
                    }
                    QCoreApplication::quit();
                });
        } else if (agentSettingsSmokeTest) {
            QTimer::singleShot(
                100,
                &application,
                [panel,
                 rootObject,
                 &agentAutoModeRules,
                 &projectIntelligence,
                 &externalDiscovery] {
                    const QStringList requiredObjects {
                        QStringLiteral("panel.settings"),
                        QStringLiteral("panel.settings.close"),
                        QStringLiteral("panel.settings.agents.surface"),
                        QStringLiteral("panel.settings.agents.refresh"),
                        QStringLiteral("panel.settings.agents.workspace"),
                        QStringLiteral("panel.settings.agents.integration"),
                        QStringLiteral("panel.settings.agents.tree"),
                        QStringLiteral("panel.settings.agents.sources"),
                        QStringLiteral("panel.settings.autoMode.reload"),
                        QStringLiteral("panel.settings.autoMode.save"),
                        QStringLiteral(
                            "panel.settings.autoMode.reload.confirm"),
                        QStringLiteral(
                            "panel.settings.autoMode.reload.cancel"),
                        QStringLiteral(
                            "panel.settings.autoMode.reload.replace"),
                        QStringLiteral("panel.settings.externalDiscovery.refresh"),
                        QStringLiteral(
                            "panel.settings.externalDiscovery.mcp.title"),
                        QStringLiteral(
                            "panel.settings.externalDiscovery.sessions.title"),
                    };
                    for (const auto& objectName : requiredObjects) {
                        if (rootObject->findChild<QObject*>(objectName)
                            == nullptr) {
                            qCritical()
                                << "The Agent Settings smoke contract is incomplete:"
                                << objectName;
                            for (auto* object :
                                 rootObject->findChildren<QObject*>()) {
                                const auto candidate =
                                    object->objectName();
                                if (candidate.startsWith(
                                        QStringLiteral(
                                            "panel.settings.externalDiscovery"))) {
                                    qCritical() << "Available:" << candidate;
                                }
                            }
                            QCoreApplication::exit(EXIT_FAILURE);
                            return;
                        }
                    }
                    auto* close = rootObject->findChild<QObject*>(
                        QStringLiteral("panel.settings.close"));
                    if (panel->property("width").toReal() > 820
                        || panel->property("height").toReal() > 560
                        || panel->property("selectedCategory").toString()
                            != QStringLiteral("agents")
                        || close == nullptr
                        || !close->property("activeFocus").toBool()
                        || projectIntelligence.settingsTree()->rowCount() < 3
                        || externalDiscovery.servers()->rowCount() < 1
                        || externalDiscovery.sessions()->rowCount() < 1) {
                        qCritical()
                            << "Agent Settings did not expose settings,"
                            << "rules, discovery, or initial focus.";
                        QCoreApplication::exit(EXIT_FAILURE);
                        return;
                    }
                    agentAutoModeRules.setAllowText(
                        QStringLiteral("unsaved smoke draft"));
                    auto* surface = rootObject->findChild<QObject*>(
                        QStringLiteral("panel.settings.agents.surface"));
                    if (surface == nullptr
                        || !QMetaObject::invokeMethod(
                            surface,
                            "requestAutoModeReload",
                            Qt::DirectConnection)) {
                        qCritical()
                            << "The Auto Mode reload confirmation could not be opened.";
                        QCoreApplication::exit(EXIT_FAILURE);
                        return;
                    }
                    QTimer::singleShot(
                        50,
                        panel,
                        [rootObject, &agentAutoModeRules] {
                            auto* confirmation =
                                rootObject->findChild<QObject*>(
                                    QStringLiteral(
                                        "panel.settings.autoMode.reload.confirm"));
                            auto* cancel =
                                rootObject->findChild<QObject*>(
                                    QStringLiteral(
                                        "panel.settings.autoMode.reload.cancel"));
                            if (confirmation == nullptr
                                || !confirmation->property("opened").toBool()
                                || cancel == nullptr
                                || !cancel->property("activeFocus").toBool()
                                || agentAutoModeRules.allowText()
                                    != QStringLiteral(
                                        "unsaved smoke draft")) {
                                qCritical()
                                    << "The Auto Mode reload confirmation did not"
                                    << "isolate focus or preserve the draft.";
                                QCoreApplication::exit(EXIT_FAILURE);
                                return;
                            }
                            (void)QMetaObject::invokeMethod(
                                confirmation,
                                "close",
                                Qt::DirectConnection);
                            QCoreApplication::quit();
                        });
                });
        } else if (attentionSmokeTest) {
            rootObject->setProperty("width", 820);
            rootObject->setProperty("height", 560);
            QTimer::singleShot(
                100,
                &application,
                [panel, rootObject, &attention] {
                    const QStringList requiredObjects {
                        QStringLiteral("header.attention"),
                        QStringLiteral("panel.attention"),
                        QStringLiteral("panel.attention.close"),
                        QStringLiteral("panel.attention.loading"),
                        QStringLiteral("panel.attention.error"),
                        QStringLiteral("panel.attention.empty"),
                        QStringLiteral("panel.attention.list"),
                        QStringLiteral("sidebar.attention.rail"),
                        QStringLiteral("sidebar.attention.list"),
                    };
                    for (const auto& objectName : requiredObjects) {
                        if (rootObject->findChild<QObject*>(objectName)
                            == nullptr) {
                            qCritical()
                                << "The Attention smoke contract is incomplete:"
                                << objectName;
                            QCoreApplication::exit(EXIT_FAILURE);
                            return;
                        }
                    }
                    if (attention.rowCount() != 1
                        || panel->property("width").toReal() > 440
                        || panel->property("height").toReal() > 528) {
                        qCritical()
                            << "Attention did not lay out its populated state"
                            << "at the minimum supported window size.";
                        QCoreApplication::exit(EXIT_FAILURE);
                        return;
                    }
                    const auto* list = rootObject->findChild<QObject*>(
                        QStringLiteral("panel.attention.list"));
                    const auto* loading = rootObject->findChild<QObject*>(
                        QStringLiteral("panel.attention.loading"));
                    const auto* rail = qobject_cast<QQuickItem*>(
                        rootObject->findChild<QObject*>(
                            QStringLiteral("sidebar.attention.rail")));
                    if (list == nullptr
                        || list->property("count").toInt() != 1
                        || list->property("contentHeight").toReal() <= 0
                        || loading == nullptr
                        || loading->property("visible").toBool()
                        || rail == nullptr || !rail->isVisible()
                        || rail->height() <= 0 || rail->y() < 48) {
                        qCritical()
                            << "Attention cards or sidebar rail are not visible.";
                        QCoreApplication::exit(EXIT_FAILURE);
                        return;
                    }
                    auto* close = panel->findChild<QObject*>(
                        QStringLiteral("panel.attention.close"));
                    if (close == nullptr
                        || !close->property("activeFocus").toBool()) {
                        qCritical()
                            << "Attention did not establish keyboard focus.";
                        QCoreApplication::exit(EXIT_FAILURE);
                        return;
                    }
                    QCoreApplication::quit();
                });
        } else if (diagnosticsSmokeTest) {
            rootObject->setProperty("width", 820);
            rootObject->setProperty("height", 560);
            runtimeDiagnostics.ingestSystemEvent(
                QByteArrayLiteral("{\"type\":\"heartbeat\"}"));
            runtimeDiagnostics.ingestSystemEvent(QByteArrayLiteral(
                "{\"type\":\"runtime.health\",\"collaborationCleanup\":{"
                "\"state\":\"quarantined\",\"pendingCount\":2,"
                "\"quarantinedCount\":1,"
                "\"message\":\"operator action required\"}}"));
            QTimer::singleShot(
                50,
                &application,
                [panel, rootObject, &runtimeDiagnostics] {
                    const QStringList requiredObjects {
                        QStringLiteral("panel.diagnostics"),
                        QStringLiteral("panel.diagnostics.close"),
                        QStringLiteral("panel.diagnostics.scroll"),
                        QStringLiteral(
                            "panel.diagnostics.runtime.collaborationCleanup"),
                    };
                    for (const auto& objectName : requiredObjects) {
                        if (rootObject->findChild<QObject*>(objectName)
                            == nullptr) {
                            qCritical()
                                << "The Diagnostics smoke contract is incomplete:"
                                << objectName;
                            QCoreApplication::exit(EXIT_FAILURE);
                            return;
                        }
                    }
                    auto* close = panel->findChild<QObject*>(
                        QStringLiteral("panel.diagnostics.close"));
                    if (panel->property("width").toReal() > 780
                        || panel->property("height").toReal() > 520
                        || close == nullptr
                        || !close->property("activeFocus").toBool()
                        || !runtimeDiagnostics.systemReady()
                        || runtimeDiagnostics.cleanupQuarantinedCount() != 1) {
                        qCritical()
                            << "Diagnostics did not expose runtime health"
                            << "or establish focus at minimum window size.";
                        QCoreApplication::exit(EXIT_FAILURE);
                        return;
                    }
                    QCoreApplication::quit();
                });
        } else {
            QTimer::singleShot(
                50,
                &application,
                [panel, rootObject] {
                    panel->setProperty(
                        "selectedCategory",
                        QStringLiteral("sessions"));
                    auto* scroll = panel->findChild<QObject*>(
                        QStringLiteral("panel.settings.scroll"));
                    auto* workingDirectory = panel->findChild<QObject*>(
                        QStringLiteral(
                            "panel.settings.sessions.workingDirectory"));
                    auto* browse = panel->findChild<QObject*>(
                        QStringLiteral(
                            "panel.settings.sessions.browse"));
                    auto* openFolder = panel->findChild<QObject*>(
                        QStringLiteral(
                            "panel.settings.sessions.openFolder"));
                    if (scroll == nullptr || workingDirectory == nullptr
                        || browse == nullptr || openFolder == nullptr
                        || !QMetaObject::invokeMethod(
                            workingDirectory,
                            "forceActiveFocus",
                            Qt::DirectConnection)) {
                        qCritical()
                            << "The minimum-size Settings focus contract is incomplete.";
                        QCoreApplication::exit(EXIT_FAILURE);
                        return;
                    }
                    QTimer::singleShot(
                        0,
                        panel,
                        [panel, rootObject, scroll, workingDirectory] {
                            if (!workingDirectory->property("activeFocus")
                                     .toBool()) {
                                qCritical()
                                    << "Settings lost focus before the resize.";
                                QCoreApplication::exit(EXIT_FAILURE);
                                return;
                            }
                            rootObject->setProperty("width", 820);
                            rootObject->setProperty("height", 560);
                            QTimer::singleShot(
                                100,
                                panel,
                                [scroll, workingDirectory] {
                                    auto* scrollItem =
                                        qobject_cast<QQuickItem*>(scroll);
                                    auto* workingItem =
                                        qobject_cast<QQuickItem*>(
                                            workingDirectory);
                                    auto* contentItem =
                                        scroll->property("contentItem")
                                            .value<QObject*>();
                                    const auto fieldTopLeft =
                                        workingItem != nullptr
                                            && scrollItem != nullptr
                                        ? workingItem->mapToItem(
                                            scrollItem,
                                            QPointF {})
                                        : QPointF {};
                                    if (!workingDirectory
                                             ->property("activeFocus")
                                             .toBool()
                                        || scrollItem == nullptr
                                        || workingItem == nullptr
                                        || contentItem == nullptr
                                        || contentItem->property("contentY")
                                                .toReal()
                                            < 0
                                        || fieldTopLeft.y() < 0
                                        || fieldTopLeft.y()
                                                + workingItem->height()
                                            > scrollItem->height()) {
                                        qCritical()
                                            << "Settings did not keep the"
                                            << " focused working-directory"
                                            << " field inside the viewport"
                                            << " after shrinking from"
                                            << " 1240x800 to 820x560.";
                                        QCoreApplication::exit(EXIT_FAILURE);
                                        return;
                                    }
                                    QCoreApplication::quit();
                                });
                        });
                });
        }
    } else if (tilingProbeSynthetic || projectIntelProbePopulated
        || projectIntelProbeEmpty || projectIntelProbeArchive
        || agentSettingsProbePopulated) {
        qInfo() << "Synthetic presentation probe is ready.";
    } else if (auto result = runtime.start(); !result) {
        qCritical().noquote() << result.error().message;
        return EXIT_FAILURE;
    } else {
        (void)agentGlobal.refresh();
    }
    const auto endpointReady = singleInstance.publishEndpoint();
    if (endpointReady.state
        != kodosi::SingleInstanceGuard::StartState::Owner) {
        qCritical().noquote() << endpointReady.error;
        return EXIT_FAILURE;
    }
    QObject::connect(&application, &QCoreApplication::aboutToQuit, &runtime, [&runtime] {
        runtime.stop();
    });

    return application.exec();
}
