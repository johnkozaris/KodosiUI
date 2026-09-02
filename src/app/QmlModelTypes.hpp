#pragma once

#include "models/AgentConversationModel.hpp"
#include "models/AgentCustomAgentsModel.hpp"
#include "models/AgentGlobalModel.hpp"
#include "models/AgentMemoryModel.hpp"
#include "models/AgentSessionIntelModel.hpp"
#include "models/AttentionModel.hpp"
#include "models/AuthStateModel.hpp"
#include "models/AuthActions.hpp"
#include "models/DevicesModel.hpp"
#include "models/DeviceActions.hpp"
#include "models/DesktopSettings.hpp"
#include "models/DesktopStateModel.hpp"
#include "models/MissionDirectoryModel.hpp"
#include "models/MissionDetailModel.hpp"
#include "models/MissionActions.hpp"
#include "models/PendingPermissionsModel.hpp"
#include "models/PeopleModel.hpp"
#include "models/PeopleActions.hpp"
#include "models/RuntimeDiagnosticsModel.hpp"
#include "models/SessionCatalogModel.hpp"
#include "models/SessionAccess.hpp"
#include "models/SessionActions.hpp"
#include "models/SessionShareScope.hpp"
#include "models/SteeringModel.hpp"
#include "models/TerminalTilingLayoutModel.hpp"
#include "models/TrustModel.hpp"
#include "platform/DesktopFileIntegration.hpp"
#include "terminal/TerminalSurfaceController.hpp"
#include "terminal/TerminalView.hpp"

#include <QJSEngine>
#include <QQmlEngine>
#include <QtQml/qqmlregistration.h>

namespace kodosi::qml {

template<typename T>
T* singleton(T* instance, QQmlEngine* engine)
{
    Q_ASSERT(instance != nullptr);
    Q_ASSERT(engine != nullptr);
    Q_ASSERT(instance->thread() == engine->thread());
    QQmlEngine::setObjectOwnership(instance, QQmlEngine::CppOwnership);
    return instance;
}

struct AgentCatalogModelForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::AgentCatalogModel)
    QML_ANONYMOUS
};

struct AgentConversationModelForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::AgentConversationModel)
    QML_NAMED_ELEMENT(AgentConversation)
    QML_SINGLETON

public:
    inline static kodosi::AgentConversationModel* instance = nullptr;
    static kodosi::AgentConversationModel* create(
        QQmlEngine* engine,
        QJSEngine*)
    {
        return singleton(instance, engine);
    }
};

struct AgentCustomAgentsModelForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::AgentCustomAgentsModel)
    QML_NAMED_ELEMENT(AgentCustomAgents)
    QML_SINGLETON

public:
    inline static kodosi::AgentCustomAgentsModel* instance = nullptr;
    static kodosi::AgentCustomAgentsModel* create(
        QQmlEngine* engine,
        QJSEngine*)
    {
        return singleton(instance, engine);
    }
};

struct AgentMemoryModelForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::AgentMemoryModel)
    QML_NAMED_ELEMENT(AgentMemory)
    QML_SINGLETON

public:
    inline static kodosi::AgentMemoryModel* instance = nullptr;
    static kodosi::AgentMemoryModel* create(
        QQmlEngine* engine,
        QJSEngine*)
    {
        return singleton(instance, engine);
    }
};

struct AgentSessionIntelModelForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::AgentSessionIntelModel)
    QML_NAMED_ELEMENT(AgentSessionIntel)
    QML_SINGLETON

public:
    inline static kodosi::AgentSessionIntelModel* instance = nullptr;
    static kodosi::AgentSessionIntelModel* create(
        QQmlEngine* engine,
        QJSEngine*)
    {
        return singleton(instance, engine);
    }
};

struct AttentionModelForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::AttentionModel)
    QML_NAMED_ELEMENT(Attention)
    QML_SINGLETON

public:
    inline static kodosi::AttentionModel* instance = nullptr;
    static kodosi::AttentionModel* create(QQmlEngine* engine, QJSEngine*)
    {
        return singleton(instance, engine);
    }
};

struct AgentMcpModelForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::AgentMcpModel)
    QML_ANONYMOUS
};

struct AgentGlobalModelForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::AgentGlobalModel)
    QML_NAMED_ELEMENT(AgentGlobal)
    QML_SINGLETON

public:
    inline static kodosi::AgentGlobalModel* instance = nullptr;
    static kodosi::AgentGlobalModel* create(QQmlEngine* engine, QJSEngine*)
    {
        return singleton(instance, engine);
    }
};

struct AuthStateModelForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::AuthStateModel)
    QML_NAMED_ELEMENT(AuthState)
    QML_SINGLETON

public:
    inline static kodosi::AuthStateModel* instance = nullptr;
    static kodosi::AuthStateModel* create(QQmlEngine* engine, QJSEngine*)
    {
        return singleton(instance, engine);
    }
};

struct AuthActionsForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::AuthActions)
    QML_NAMED_ELEMENT(AuthActions)
    QML_SINGLETON

public:
    inline static kodosi::AuthActions* instance = nullptr;
    static kodosi::AuthActions* create(QQmlEngine* engine, QJSEngine*)
    {
        return singleton(instance, engine);
    }
};

struct DeviceLinkRequestsModelForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::DeviceLinkRequestsModel)
    QML_ANONYMOUS
};

struct DevicesModelForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::DevicesModel)
    QML_NAMED_ELEMENT(Devices)
    QML_SINGLETON

public:
    inline static kodosi::DevicesModel* instance = nullptr;
    static kodosi::DevicesModel* create(QQmlEngine* engine, QJSEngine*)
    {
        return singleton(instance, engine);
    }
};

struct DeviceActionsForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::DeviceActions)
    QML_NAMED_ELEMENT(DeviceActions)
    QML_SINGLETON

public:
    inline static kodosi::DeviceActions* instance = nullptr;
    static kodosi::DeviceActions* create(QQmlEngine* engine, QJSEngine*)
    {
        return singleton(instance, engine);
    }
};

struct DesktopSettingsForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::DesktopSettings)
    QML_NAMED_ELEMENT(DesktopSettings)
    QML_SINGLETON

public:
    inline static kodosi::DesktopSettings* instance = nullptr;
    static kodosi::DesktopSettings* create(QQmlEngine* engine, QJSEngine*)
    {
        return singleton(instance, engine);
    }
};

struct DesktopStateModelForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::DesktopStateModel)
    QML_NAMED_ELEMENT(DesktopState)
    QML_SINGLETON

public:
    inline static kodosi::DesktopStateModel* instance = nullptr;
    static kodosi::DesktopStateModel* create(QQmlEngine* engine, QJSEngine*)
    {
        return singleton(instance, engine);
    }
};

struct DesktopFileIntegrationForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::DesktopFileIntegration)
    QML_NAMED_ELEMENT(DesktopFiles)
    QML_SINGLETON

public:
    inline static kodosi::DesktopFileIntegration* instance = nullptr;
    static kodosi::DesktopFileIntegration* create(
        QQmlEngine* engine,
        QJSEngine*)
    {
        return singleton(instance, engine);
    }
};

struct PendingPermissionsModelForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::PendingPermissionsModel)
    QML_NAMED_ELEMENT(PendingPermissions)
    QML_SINGLETON

public:
    inline static kodosi::PendingPermissionsModel* instance = nullptr;
    static kodosi::PendingPermissionsModel* create(QQmlEngine* engine, QJSEngine*)
    {
        return singleton(instance, engine);
    }
};

struct MissionInvitationsModelForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::MissionInvitationsModel)
    QML_ANONYMOUS
};

struct MissionDirectoryModelForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::MissionDirectoryModel)
    QML_NAMED_ELEMENT(Missions)
    QML_SINGLETON

public:
    inline static kodosi::MissionDirectoryModel* instance = nullptr;
    static kodosi::MissionDirectoryModel* create(QQmlEngine* engine, QJSEngine*)
    {
        return singleton(instance, engine);
    }
};

struct MissionMembersModelForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::MissionMembersModel)
    QML_ANONYMOUS
};

struct MissionMessagesModelForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::MissionMessagesModel)
    QML_ANONYMOUS
};

struct MissionTasksModelForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::MissionTasksModel)
    QML_ANONYMOUS
};

struct MissionDetailModelForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::MissionDetailModel)
    QML_NAMED_ELEMENT(MissionDetail)
    QML_SINGLETON

public:
    inline static kodosi::MissionDetailModel* instance = nullptr;
    static kodosi::MissionDetailModel* create(QQmlEngine* engine, QJSEngine*)
    {
        return singleton(instance, engine);
    }
};

struct MissionActionsForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::MissionActions)
    QML_NAMED_ELEMENT(MissionActions)
    QML_SINGLETON

public:
    inline static kodosi::MissionActions* instance = nullptr;
    static kodosi::MissionActions* create(QQmlEngine* engine, QJSEngine*)
    {
        return singleton(instance, engine);
    }
};

struct PeopleModelForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::PeopleModel)
    QML_NAMED_ELEMENT(People)
    QML_SINGLETON

public:
    inline static kodosi::PeopleModel* instance = nullptr;
    static kodosi::PeopleModel* create(QQmlEngine* engine, QJSEngine*)
    {
        return singleton(instance, engine);
    }
};

struct PeopleActionsForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::PeopleActions)
    QML_NAMED_ELEMENT(PeopleActions)
    QML_SINGLETON

public:
    inline static kodosi::PeopleActions* instance = nullptr;
    static kodosi::PeopleActions* create(QQmlEngine* engine, QJSEngine*)
    {
        return singleton(instance, engine);
    }
};

struct RuntimeDiagnosticsModelForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::RuntimeDiagnosticsModel)
    QML_NAMED_ELEMENT(RuntimeDiagnostics)
    QML_SINGLETON

public:
    inline static kodosi::RuntimeDiagnosticsModel* instance = nullptr;
    static kodosi::RuntimeDiagnosticsModel* create(
        QQmlEngine* engine,
        QJSEngine*)
    {
        return singleton(instance, engine);
    }
};

struct SessionCatalogModelForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::SessionCatalogModel)
    QML_NAMED_ELEMENT(Sessions)
    QML_SINGLETON

public:
    inline static kodosi::SessionCatalogModel* instance = nullptr;
    static kodosi::SessionCatalogModel* create(QQmlEngine* engine, QJSEngine*)
    {
        return singleton(instance, engine);
    }
};

struct HiddenSessionsModelForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::HiddenSessionsModel)
    QML_ANONYMOUS
};

struct SessionAccessGrantsModelForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::SessionAccessGrantsModel)
    QML_ANONYMOUS
};

struct SessionAccessForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::SessionAccess)
    QML_NAMED_ELEMENT(SessionAccess)
    QML_SINGLETON

public:
    inline static kodosi::SessionAccess* instance = nullptr;
    static kodosi::SessionAccess* create(QQmlEngine* engine, QJSEngine*)
    {
        return singleton(instance, engine);
    }
};

struct SessionActionsForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::SessionActions)
    QML_NAMED_ELEMENT(SessionActions)
    QML_SINGLETON

public:
    inline static kodosi::SessionActions* instance = nullptr;
    static kodosi::SessionActions* create(QQmlEngine* engine, QJSEngine*)
    {
        return singleton(instance, engine);
    }
};

struct SessionShareScopeForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::SessionShareScope)
    QML_NAMED_ELEMENT(SessionShareScope)
    QML_SINGLETON

public:
    inline static kodosi::SessionShareScope* instance = nullptr;
    static kodosi::SessionShareScope* create(
        QQmlEngine* engine,
        QJSEngine*)
    {
        return singleton(instance, engine);
    }
};

struct SteeringModelForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::SteeringModel)
    QML_NAMED_ELEMENT(Steering)
    QML_SINGLETON

public:
    inline static kodosi::SteeringModel* instance = nullptr;
    static kodosi::SteeringModel* create(QQmlEngine* engine, QJSEngine*)
    {
        return singleton(instance, engine);
    }
};

struct TrustModelForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::TrustModel)
    QML_NAMED_ELEMENT(Trust)
    QML_SINGLETON

public:
    inline static kodosi::TrustModel* instance = nullptr;
    static kodosi::TrustModel* create(QQmlEngine* engine, QJSEngine*)
    {
        return singleton(instance, engine);
    }
};

struct TerminalSurfaceControllerForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::TerminalSurfaceController)
    QML_NAMED_ELEMENT(TerminalSurfaces)
    QML_SINGLETON

public:
    inline static kodosi::TerminalSurfaceController* instance = nullptr;
    static kodosi::TerminalSurfaceController* create(
        QQmlEngine* engine,
        QJSEngine*)
    {
        return singleton(instance, engine);
    }
};

struct TerminalTilingLayoutModelForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::TerminalTilingLayoutModel)
    QML_NAMED_ELEMENT(TerminalTiling)
    QML_SINGLETON

public:
    inline static kodosi::TerminalTilingLayoutModel* instance = nullptr;
    static kodosi::TerminalTilingLayoutModel* create(
        QQmlEngine* engine,
        QJSEngine*)
    {
        return singleton(instance, engine);
    }
};

struct TerminalViewForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::TerminalView)
    QML_NAMED_ELEMENT(TerminalView)
};

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
    TerminalSurfaceController& terminalSurfaces);

} // namespace kodosi::qml
