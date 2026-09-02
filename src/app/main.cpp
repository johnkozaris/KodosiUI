#include "attention/ApprovalNotifications.hpp"
#include "attention/TerminalNotifications.hpp"
#include "app/QmlModelTypes.hpp"
#include "bridge/RuntimeBridge.hpp"
#include "models/AgentConversationModel.hpp"
#include "models/AgentCustomAgentsModel.hpp"
#include "models/AgentGlobalModel.hpp"
#include "models/AgentMemoryModel.hpp"
#include "models/AgentSessionIntelModel.hpp"
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
#include "models/MissionDirectoryModel.hpp"
#include "models/MissionDetailModel.hpp"
#include "models/MissionActions.hpp"
#include "models/PeopleModel.hpp"
#include "models/PeopleActions.hpp"
#include "models/RuntimeDiagnosticsModel.hpp"
#include "models/PendingPermissionsModel.hpp"
#include "models/TrustModel.hpp"
#include "platform/FreedesktopNotificationDriver.hpp"
#include "terminal/TerminalSessionRegistry.hpp"
#include "terminal/TerminalSurfaceController.hpp"
#include "terminal/TerminalAccessibility.hpp"
#include "terminal/TerminalView.hpp"

#include <QGuiApplication>
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

} // namespace

int main(int argc, char* argv[])
{
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QGuiApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Kodosi"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("kodosi.com"));
    QCoreApplication::setApplicationName(QStringLiteral("Kodosi"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    const auto arguments = application.arguments();
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
    const auto attentionSmokeTest =
        arguments.contains(
            QStringLiteral("--smoke-test-attention"));
    const auto diagnosticsSmokeTest =
        arguments.contains(
            QStringLiteral("--smoke-test-diagnostics"));
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
    const auto authProbeError =
        arguments.contains(
            QStringLiteral("--ui-probe-auth-error"));
    const auto smokeTest = settingsSmokeTest
        || agentIntelSmokeTest
        || attentionSmokeTest
        || diagnosticsSmokeTest
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
    QString syntheticConfigDirectory;
    auto removeSyntheticConfig = qScopeGuard([&] {
        if (!syntheticConfigDirectory.isEmpty()) {
            (void)QDir(syntheticConfigDirectory).removeRecursively();
        }
    });
    if (syntheticMode) {
        syntheticConfigDirectory = QDir::current().absoluteFilePath(
            QStringLiteral("build/synthetic-config/")
            + QUuid::createUuid().toString(QUuid::WithoutBraces));
        if (!QDir().mkpath(syntheticConfigDirectory)) {
            qCritical().noquote()
                << "Synthetic mode could not create an isolated settings directory.";
            return EXIT_FAILURE;
        }
        if (!qputenv(
                "XDG_CONFIG_HOME",
                syntheticConfigDirectory.toUtf8())) {
            qCritical()
                << "Synthetic mode could not isolate XDG_CONFIG_HOME.";
            return EXIT_FAILURE;
        }
    }
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
    kodosi::DesktopSettings desktopSettings;
    kodosi::DesktopStateModel desktopState(
        desktopStateSettings(!syntheticMode),
        !syntheticMode);
    desktopState.attachSessionCatalog(&sessionCatalog);
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
    kodosi::AttentionModel attention(
        pendingPermissions,
        agentSessionIntel,
        sessionCatalog,
        sessionActions);
    if (attentionSmokeTest || attentionProbePopulated || agentIntelProbeOpen) {
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
         &agentConversation,
         &agentCustomAgents,
         &agentMemory,
         &agentSessionIntel,
         &authActions,
         &authState,
         &desktopState,
         &devices,
         &missions,
         &missionDetail,
         &missionActions,
         &pendingPermissions,
         &people,
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
                authActions.ingestAuthEvent(json);
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
                missions.ingestAuthEvent(json);
                missionDetail.ingestAuthEvent(json);
                missionActions.ingestAuthEvent(json);
                pendingPermissions.ingestAuthEvent(json);
                people.ingestAuthEvent(json);
                trust.ingestAuthEvent(json);
            } else if (lane == kodosi::EventLane::Devices) {
                devices.ingestDevicesEvent(std::move(json));
            } else if (lane == kodosi::EventLane::AgentGlobal) {
                agentGlobal.ingestAgentGlobalEvent(std::move(json));
            } else if (lane == kodosi::EventLane::System) {
                runtimeDiagnostics.ingestSystemEvent(std::move(json));
            } else if (lane == kodosi::EventLane::AgentIntel) {
                agentConversation.ingestAgentIntelEvent(json);
                agentCustomAgents.ingestAgentIntelEvent(json);
                agentMemory.ingestAgentIntelEvent(json);
                agentSessionIntel.ingestAgentIntelEvent(json);
                pendingPermissions.ingestAgentIntelEvent(json);
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
         &agentConversation,
         &agentCustomAgents,
         &agentMemory,
         &agentSessionIntel,
         &authState,
         &desktopState,
         &devices,
         &missions,
         &missionDetail,
         &missionActions,
         &pendingPermissions,
         &people,
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
            desktopState.resetRuntimeAuthority();
            agentConversation.resetRuntimeAuthority();
            agentCustomAgents.resetRuntimeAuthority();
            agentMemory.resetRuntimeAuthority();
            agentSessionIntel.resetRuntimeAuthority();
            authActions.resetRuntimeAuthority();
            devices.resetRuntimeAuthority();
            missions.resetRuntimeAuthority();
            missionDetail.resetRuntimeAuthority();
            missionActions.resetRuntimeAuthority();
            pendingPermissions.resetRuntimeAuthority();
            people.resetRuntimeAuthority();
            sessionCatalog.resetRuntimeAuthority();
            sessionAccess.resetRuntimeAuthority();
            sessionActions.resetRuntimeAuthority();
            sessionShareScope.resetRuntimeAuthority();
            steering.resetRuntimeAuthority();
            trust.resetRuntimeAuthority();
        });
    kodosi::qml::configureModelInstances(
        agentGlobal,
        agentConversation,
        agentCustomAgents,
        agentMemory,
        agentSessionIntel,
        attention,
        authState,
        authActions,
        devices,
        deviceActions,
        desktopSettings,
        desktopState,
        missions,
        missionDetail,
        missionActions,
        pendingPermissions,
        people,
        peopleActions,
        runtimeDiagnostics,
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
    const auto attachSize = windowSize
        ? windowSize
        : agentIntelSmokeTest
        ? std::optional<QSize>(QSize(820, 800))
        : std::nullopt;
    desktopState.attachWindow(mainWindow, attachSize);
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
            mainWindow->raise();
            if (!activationToken.isEmpty()) {
                (void)qputenv(
                    "XDG_ACTIVATION_TOKEN",
                    activationToken.toUtf8());
            }
            mainWindow->requestActivate();
            qunsetenv("XDG_ACTIVATION_TOKEN");
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
            mainWindow->raise();
            if (!activationToken.isEmpty()) {
                (void)qputenv(
                    "XDG_ACTIVATION_TOKEN",
                    activationToken.toUtf8());
            }
            mainWindow->requestActivate();
            qunsetenv("XDG_ACTIVATION_TOKEN");
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
        const auto panelName = settingsSmokeTest
            ? QStringLiteral("panel.settings")
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
                    if (scroll == nullptr || workingDirectory == nullptr
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
    } else if (tilingProbeSynthetic) {
        qInfo() << "Synthetic terminal tiling probe is ready.";
    } else if (auto result = runtime.start(); !result) {
        qCritical().noquote() << result.error().message;
        QTimer::singleShot(0, &application, [] { QCoreApplication::exit(EXIT_FAILURE); });
    } else {
        (void)agentGlobal.refresh();
    }
    QObject::connect(&application, &QCoreApplication::aboutToQuit, &runtime, [&runtime] {
        runtime.stop();
    });

    return application.exec();
}
