#include "attention/ApprovalNotifications.hpp"
#include "attention/TerminalNotifications.hpp"
#include "app/ApplicationLifecycleModel.hpp"
#include "app/DeepLinkController.hpp"
#include "app/DeepLinkRouter.hpp"
#include "app/QmlModelTypes.hpp"
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
#include "models/ProviderConversationsModel.hpp"
#include "models/TrustModel.hpp"
#include "platform/FreedesktopNotificationDriver.hpp"
#include "platform/DesktopFileIntegration.hpp"
#include "terminal/TerminalSessionRegistry.hpp"
#include "terminal/TerminalSurfaceController.hpp"
#include "terminal/TerminalAccessibility.hpp"
#include "terminal/TerminalView.hpp"

#include <QApplication>
#include <QLoggingCategory>
#include <QQuickStyle>
#include <QRegularExpression>
#include <QQmlApplicationEngine>
#include <QSettings>
#include <QStringList>
#include <QUrl>
#include <QVariant>
#include <QWindow>

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
        kodosi::SingleInstanceGuard::standardRuntimeDirectory(),
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
    kodosi::ApplicationLifecycleModel::StartOperation lifecycleStart =
        [&runtime](
            kodosi::ApplicationLifecycleModel::Completion completion) {
            completion(runtime.start());
        };
    kodosi::ApplicationLifecycleModel applicationLifecycle(
        runtime,
        std::move(lifecycleStart),
        [&runtime] { runtime.stop(); });
    QObject::connect(
        &applicationLifecycle,
        &kodosi::ApplicationLifecycleModel::runtimeGenerationReady,
        &agentGlobal,
        [&agentGlobal, &runtime](quint64) {
            if (runtime.isRunning()) {
                (void)agentGlobal.refresh();
            }
        });
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
        std::make_unique<QSettings>(),
        true);
    desktopState.attachSessionCatalog(&sessionCatalog);
    kodosi::DesktopFileIntegration desktopFiles(
        sessionCatalog,
        &applicationLog);
    kodosi::AgentAutoModeRulesModel agentAutoModeRules(runtime);
    kodosi::ExternalDiscoveryModel externalDiscovery(runtime, desktopFiles);
    kodosi::ProviderConversationsModel providerConversations(
        runtime,
        desktopFiles,
        desktopSettings);
    kodosi::ProjectIntelligenceModel projectIntelligence(
        runtime,
        agentConversation,
        desktopFiles);
    kodosi::TerminalTilingLayoutModel terminalTiling;
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
    sessionActions.setProviderConversationResumeResolver(
        &providerConversations);
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
    kodosi::DevicesModel devices;
    kodosi::DeviceActions deviceActions(runtime, devices);
    kodosi::MissionDirectoryModel missions(runtime);
    kodosi::SessionShareScope sessionShareScope(
        runtime,
        sessionCatalog,
        missions);
    kodosi::SteeringModel steering(runtime, sessionCatalog);
    kodosi::PeopleActions peopleActions(runtime, people);
    kodosi::PendingPermissionsModel pendingPermissions(runtime, sessionCatalog);
    kodosi::DeepLinkController deepLinks(
        sessionCatalog,
        pendingPermissions,
        applicationLifecycle);
    kodosi::AttentionModel attention(
        pendingPermissions,
        agentSessionIntel,
        sessionCatalog,
        sessionActions);
    kodosi::MissionDetailModel missionDetail(
        runtime,
        missions,
        {
            .people = &people,
            .sessions = &sessionCatalog,
            .attention = &attention,
            .desktopState = &desktopState,
            .steering = &steering,
            .sessionActions = &sessionActions,
        });
    kodosi::MissionActions missionActions(
        runtime,
        missions,
        missionDetail,
        people,
        sessionCatalog);
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
         &providerConversations,
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
                providerConversations.ingestAuthEvent(json);
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
                providerConversations.ingestAgentIntelEvent(json);
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
         &providerConversations,
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
            providerConversations.resetRuntimeAuthority();
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
        providerConversations,
        people,
        peopleActions,
        applicationLifecycle,
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
    desktopState.attachWindow(mainWindow, windowSize);
    activateMainWindow(launchActivation.activationToken);
    launchActivation.activationToken.clear();
    if (initialDeepLink) {
        if (initialDeepLink->destination) {
            deepLinks.enqueue(*initialDeepLink->destination);
        } else {
            deepLinks.reject(initialDeepLink->error);
        }
    }
    const auto endpointReady = singleInstance.publishEndpoint();
    if (endpointReady.state
        != kodosi::SingleInstanceGuard::StartState::Owner) {
        qCritical().noquote() << endpointReady.error;
        return EXIT_FAILURE;
    }
    applicationLifecycle.scheduleInitialStart();
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

    QObject::connect(
        &application,
        &QCoreApplication::aboutToQuit,
        &applicationLifecycle,
        [&applicationLifecycle] { applicationLifecycle.stop(); });

    return application.exec();
}
