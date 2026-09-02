#include "app/QmlModelTypes.hpp"

namespace kodosi::qml {

void configureModelInstances(
    AgentGlobalModel& agentGlobal,
    AgentConversationModel& agentConversation,
    AgentCustomAgentsModel& agentCustomAgents,
    AgentMemoryModel& agentMemory,
    AgentSessionIntelModel& agentSessionIntel,
    AttentionModel& attention,
    AuthStateModel& authState,
    AuthActions& authActions,
    DevicesModel& devices,
    DeviceActions& deviceActions,
    DesktopSettings& desktopSettings,
    DesktopStateModel& desktopState,
    DesktopFileIntegration& desktopFiles,
    MissionDirectoryModel& missions,
    MissionDetailModel& missionDetail,
    MissionActions& missionActions,
    PendingPermissionsModel& pendingPermissions,
    PeopleModel& people,
    PeopleActions& peopleActions,
    RuntimeDiagnosticsModel& runtimeDiagnostics,
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
    AgentConversationModelForeign::instance = &agentConversation;
    AgentCustomAgentsModelForeign::instance = &agentCustomAgents;
    AgentMemoryModelForeign::instance = &agentMemory;
    AgentSessionIntelModelForeign::instance = &agentSessionIntel;
    AttentionModelForeign::instance = &attention;
    AuthStateModelForeign::instance = &authState;
    AuthActionsForeign::instance = &authActions;
    DevicesModelForeign::instance = &devices;
    DeviceActionsForeign::instance = &deviceActions;
    DesktopSettingsForeign::instance = &desktopSettings;
    DesktopStateModelForeign::instance = &desktopState;
    DesktopFileIntegrationForeign::instance = &desktopFiles;
    MissionDirectoryModelForeign::instance = &missions;
    MissionDetailModelForeign::instance = &missionDetail;
    MissionActionsForeign::instance = &missionActions;
    PendingPermissionsModelForeign::instance = &pendingPermissions;
    PeopleModelForeign::instance = &people;
    PeopleActionsForeign::instance = &peopleActions;
    RuntimeDiagnosticsModelForeign::instance = &runtimeDiagnostics;
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
