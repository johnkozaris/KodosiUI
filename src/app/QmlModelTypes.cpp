#include "app/QmlModelTypes.hpp"
namespace kodosi::qml {
void configureModelInstances(Workspace& workspace, ProviderTools& providers, AppearanceModel& appearance,
    DesktopSettings& settings, DesktopStateModel& desktop, SessionCatalogModel& sessions,
    TerminalTilingLayoutModel& tiling, DesktopFileIntegration& files, ApplicationLifecycleModel& lifecycle,
    TerminalSurfaceController& surfaces)
{
    WorkspaceForeign::instance = &workspace;
    ProviderToolsForeign::instance = &providers;
    AppearanceModelForeign::instance = &appearance;
    DesktopSettingsForeign::instance = &settings;
    DesktopStateModelForeign::instance = &desktop;
    SessionCatalogModelForeign::instance = &sessions;
    TerminalTilingLayoutModelForeign::instance = &tiling;
    DesktopFileIntegrationForeign::instance = &files;
    ApplicationLifecycleModelForeign::instance = &lifecycle;
    TerminalSurfaceControllerForeign::instance = &surfaces;
}
}
