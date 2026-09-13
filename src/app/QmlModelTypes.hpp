#pragma once
#include "accessibility/AccessibilityScope.hpp"
#include "app/ApplicationLifecycleModel.hpp"
#include "models/AppearanceModel.hpp"
#include "models/DesktopSettings.hpp"
#include "models/DesktopStateModel.hpp"
#include "models/ProviderTools.hpp"
#include "models/SessionCatalogModel.hpp"
#include "models/TerminalTilingLayoutModel.hpp"
#include "models/Workspace.hpp"
#include "platform/DesktopFileIntegration.hpp"
#include "terminal/TerminalSurfaceController.hpp"
#include "terminal/TerminalView.hpp"
#include <QJSEngine>
#include <QQmlEngine>
#include <QtQml/qqmlregistration.h>
namespace kodosi::qml {
template <typename T> T* singleton(T* instance, QQmlEngine* engine)
{
    Q_ASSERT(instance && engine && instance->thread() == engine->thread());
    QQmlEngine::setObjectOwnership(instance, QQmlEngine::CppOwnership);
    return instance;
}
struct WorkspaceForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::Workspace)
    QML_NAMED_ELEMENT(Workspace)
    QML_SINGLETON
public:
    inline static kodosi::Workspace* instance = nullptr;
    static kodosi::Workspace* create(QQmlEngine* engine, QJSEngine*) { return singleton(instance, engine); }
};

struct ProviderToolsForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::ProviderTools)
    QML_NAMED_ELEMENT(ProviderTools)
    QML_SINGLETON
public:
    inline static kodosi::ProviderTools* instance = nullptr;
    static kodosi::ProviderTools* create(QQmlEngine* engine, QJSEngine*) { return singleton(instance, engine); }
};

struct AppearanceModelForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::AppearanceModel)
    QML_NAMED_ELEMENT(Appearance)
    QML_SINGLETON
public:
    inline static kodosi::AppearanceModel* instance = nullptr;
    static kodosi::AppearanceModel* create(QQmlEngine* engine, QJSEngine*) { return singleton(instance, engine); }
};

struct DesktopSettingsForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::DesktopSettings)
    QML_NAMED_ELEMENT(DesktopSettings)
    QML_SINGLETON
public:
    inline static kodosi::DesktopSettings* instance = nullptr;
    static kodosi::DesktopSettings* create(QQmlEngine* engine, QJSEngine*) { return singleton(instance, engine); }
};

struct DesktopStateModelForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::DesktopStateModel)
    QML_NAMED_ELEMENT(DesktopState)
    QML_SINGLETON
public:
    inline static kodosi::DesktopStateModel* instance = nullptr;
    static kodosi::DesktopStateModel* create(QQmlEngine* engine, QJSEngine*) { return singleton(instance, engine); }
};

struct SessionCatalogModelForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::SessionCatalogModel)
    QML_NAMED_ELEMENT(Sessions)
    QML_SINGLETON
public:
    inline static kodosi::SessionCatalogModel* instance = nullptr;
    static kodosi::SessionCatalogModel* create(QQmlEngine* engine, QJSEngine*) { return singleton(instance, engine); }
};

struct TerminalTilingLayoutModelForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::TerminalTilingLayoutModel)
    QML_NAMED_ELEMENT(TerminalTiling)
    QML_SINGLETON
public:
    inline static kodosi::TerminalTilingLayoutModel* instance = nullptr;
    static kodosi::TerminalTilingLayoutModel* create(QQmlEngine* engine, QJSEngine*) { return singleton(instance, engine); }
};

struct DesktopFileIntegrationForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::DesktopFileIntegration)
    QML_NAMED_ELEMENT(DesktopFiles)
    QML_SINGLETON
public:
    inline static kodosi::DesktopFileIntegration* instance = nullptr;
    static kodosi::DesktopFileIntegration* create(QQmlEngine* engine, QJSEngine*) { return singleton(instance, engine); }
};

struct ApplicationLifecycleModelForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::ApplicationLifecycleModel)
    QML_NAMED_ELEMENT(ApplicationLifecycle)
    QML_SINGLETON
public:
    inline static kodosi::ApplicationLifecycleModel* instance = nullptr;
    static kodosi::ApplicationLifecycleModel* create(QQmlEngine* engine, QJSEngine*) { return singleton(instance, engine); }
};

struct TerminalSurfaceControllerForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::TerminalSurfaceController)
    QML_NAMED_ELEMENT(TerminalSurfaces)
    QML_SINGLETON
public:
    inline static kodosi::TerminalSurfaceController* instance = nullptr;
    static kodosi::TerminalSurfaceController* create(QQmlEngine* engine, QJSEngine*) { return singleton(instance, engine); }
};

struct TerminalViewForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::TerminalView)
    QML_NAMED_ELEMENT(TerminalView)
};

struct AccessibilityScopeForeign {
    Q_GADGET
    QML_FOREIGN(kodosi::AccessibilityScope)
    QML_NAMED_ELEMENT(AccessibilityScope)
};
void configureModelInstances(Workspace&, ProviderTools&, AppearanceModel&, DesktopSettings&,
    DesktopStateModel&, SessionCatalogModel&, TerminalTilingLayoutModel&, DesktopFileIntegration&,
    ApplicationLifecycleModel&, TerminalSurfaceController&);
}
