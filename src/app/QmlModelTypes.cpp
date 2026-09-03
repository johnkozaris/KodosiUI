#include "app/QmlModelTypes.hpp"

namespace kodosi::qml {

void configureModelInstances(
    AgentGlobalModel& agentGlobal,
    AgentAutoModeRulesModel& agentAutoModeRules,
    AgentConversationModel& agentConversation,
    AgentCustomAgentsModel& agentCustomAgents,
    AgentMemoryModel& agentMemory,
    AgentSessionIntelModel& agentSessionIntel,
    AttentionModel& attention,
    AuthStateModel& authState,
    AuthActions& authActions,
    DevicesModel& devices,
    DeviceActions& deviceActions,
    AppearanceModel& appearance,
    DesktopSettings& desktopSettings,
    DesktopStateModel& desktopState,
    DeepLinkController& deepLinks,
    DesktopFileIntegration& desktopFiles,
    ExternalDiscoveryModel& externalDiscovery,
    MissionDirectoryModel& missions,
    MissionDetailModel& missionDetail,
    MissionActions& missionActions,
    PendingPermissionsModel& pendingPermissions,
    ProjectIntelligenceModel& projectIntelligence,
    ProviderConversationsModel& providerConversations,
    PeopleModel& people,
    PeopleActions& peopleActions,
    RuntimeDiagnosticsModel& runtimeDiagnostics,
    ApplicationLogStore& applicationLog,
    SessionCatalogModel& sessions,
    SessionAccess& sessionAccess,
    SessionActions& sessionActions,
    SessionShareScope& sessionShareScope,
    SteeringModel& steering,
    TerminalTilingLayoutModel& terminalTiling,
    TrustModel& trust,
    TerminalSurfaceController& terminalSurfaces)
{
    AgentGlobalModelForeign::instance = &agentGlobal;
    AgentAutoModeRulesModelForeign::instance = &agentAutoModeRules;
    AgentConversationModelForeign::instance = &agentConversation;
    AgentCustomAgentsModelForeign::instance = &agentCustomAgents;
    AgentMemoryModelForeign::instance = &agentMemory;
    AgentSessionIntelModelForeign::instance = &agentSessionIntel;
    AttentionModelForeign::instance = &attention;
    AuthStateModelForeign::instance = &authState;
    AuthActionsForeign::instance = &authActions;
    DevicesModelForeign::instance = &devices;
    DeviceActionsForeign::instance = &deviceActions;
    AppearanceModelForeign::instance = &appearance;
    DesktopSettingsForeign::instance = &desktopSettings;
    DesktopStateModelForeign::instance = &desktopState;
    DeepLinkControllerForeign::instance = &deepLinks;
    DesktopFileIntegrationForeign::instance = &desktopFiles;
    ExternalDiscoveryModelForeign::instance = &externalDiscovery;
    MissionDirectoryModelForeign::instance = &missions;
    MissionDetailModelForeign::instance = &missionDetail;
    MissionActionsForeign::instance = &missionActions;
    PendingPermissionsModelForeign::instance = &pendingPermissions;
    ProjectIntelligenceModelForeign::instance = &projectIntelligence;
    ProviderConversationsModelForeign::instance = &providerConversations;
    PeopleModelForeign::instance = &people;
    PeopleActionsForeign::instance = &peopleActions;
    RuntimeDiagnosticsModelForeign::instance = &runtimeDiagnostics;
    ApplicationLogStoreForeign::instance = &applicationLog;
    SessionCatalogModelForeign::instance = &sessions;
    SessionAccessForeign::instance = &sessionAccess;
    SessionActionsForeign::instance = &sessionActions;
    SessionShareScopeForeign::instance = &sessionShareScope;
    SteeringModelForeign::instance = &steering;
    TerminalTilingLayoutModelForeign::instance = &terminalTiling;
    TrustModelForeign::instance = &trust;
    TerminalSurfaceControllerForeign::instance = &terminalSurfaces;
}

} // namespace kodosi::qml
