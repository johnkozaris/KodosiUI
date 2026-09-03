set_source_files_properties(
    qml/Theme/KodosiTheme.qml
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE
)

qt_add_library(kodosi-runtime-bridge STATIC
    app/DeepLinkController.cpp
    app/DeepLinkController.hpp
    app/DeepLinkRouter.cpp
    app/DeepLinkRouter.hpp
    app/SingleInstanceGuard.cpp
    app/SingleInstanceGuard.hpp
    bridge/RuntimeBridge.cpp
    bridge/RuntimeBridge.hpp
    logging/ApplicationLogStore.cpp
    logging/ApplicationLogStore.hpp
    models/AccountContextFence.cpp
    models/AccountContextFence.hpp
    models/AgentConversationModel.cpp
    models/AgentConversationModel.hpp
    models/AgentAutoModeRulesModel.cpp
    models/AgentAutoModeRulesModel.hpp
    models/AgentCustomAgentsModel.cpp
    models/AgentCustomAgentsModel.hpp
    models/AgentMemoryModel.cpp
    models/AgentMemoryModel.hpp
    models/AgentGlobalModel.cpp
    models/AgentGlobalModel.hpp
    models/AgentSessionIntelModel.cpp
    models/AgentSessionIntelModel.hpp
    models/AppearanceModel.cpp
    models/AppearanceModel.hpp
    models/AttentionModel.cpp
    models/AttentionModel.hpp
    models/AuthStateModel.cpp
    models/AuthStateModel.hpp
    models/AuthActions.cpp
    models/AuthActions.hpp
    models/DevicesModel.cpp
    models/DevicesModel.hpp
    models/DeviceActions.cpp
    models/DeviceActions.hpp
    models/DesktopSettings.cpp
    models/DesktopSettings.hpp
    models/DesktopStateModel.cpp
    models/DesktopStateModel.hpp
    models/MissionDirectoryModel.cpp
    models/MissionDirectoryModel.hpp
    models/MissionDetailModel.cpp
    models/MissionDetailModel.hpp
    models/MissionActions.cpp
    models/MissionActions.hpp
    models/PeopleModel.cpp
    models/PeopleModel.hpp
    models/PeopleActions.cpp
    models/PeopleActions.hpp
    models/RuntimeDiagnosticsModel.cpp
    models/RuntimeDiagnosticsModel.hpp
    models/PendingPermissionsModel.cpp
    models/PendingPermissionsModel.hpp
    models/SessionCatalogModel.cpp
    models/SessionCatalogModel.hpp
    models/SessionAccess.cpp
    models/SessionAccess.hpp
    models/SessionActions.cpp
    models/SessionActions.hpp
    models/SessionShareScope.cpp
    models/SessionShareScope.hpp
    models/SteeringModel.cpp
    models/SteeringModel.hpp
    models/TerminalTilingLayoutModel.cpp
    models/TerminalTilingLayoutModel.hpp
    models/TrustModel.cpp
    models/TrustModel.hpp
)

target_include_directories(kodosi-runtime-bridge
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(kodosi-runtime-bridge
    PUBLIC
        Qt6::Core
        Qt6::Network
    PRIVATE
        Kodosi::RustRuntime
        Qt6::DBus
        Qt6::Gui
)

qt_add_library(kodosi-desktop-integration STATIC
    attention/ApprovalNotifications.cpp
    attention/ApprovalNotifications.hpp
    attention/TerminalNotifications.cpp
    attention/TerminalNotifications.hpp
    platform/DesktopNotificationDriver.hpp
    platform/DesktopFileIntegration.cpp
    platform/DesktopFileIntegration.hpp
    platform/FreedesktopNotificationDriver.cpp
    platform/FreedesktopNotificationDriver.hpp
    platform/NotificationText.cpp
    platform/NotificationText.hpp
    models/ExternalDiscoveryModel.cpp
    models/ExternalDiscoveryModel.hpp
    models/ProjectIntelligenceModel.cpp
    models/ProjectIntelligenceModel.hpp
    models/ProviderConversationsModel.cpp
    models/ProviderConversationsModel.hpp
)

target_include_directories(kodosi-desktop-integration
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(kodosi-desktop-integration
    PUBLIC
        Qt6::Core
        Qt6::DBus
        kodosi-runtime-bridge
    PRIVATE
        Qt6::Gui
        Qt6::Widgets
)

qt_add_library(kodosi-terminal-kernel STATIC
    terminal/GhosttyC.hpp
    terminal/TerminalAccessibility.cpp
    terminal/TerminalAccessibility.hpp
    terminal/GhosttyTerminalKernel.cpp
    terminal/GhosttyTerminalKernel.hpp
    terminal/TerminalLink.cpp
    terminal/TerminalLink.hpp
    terminal/TerminalFrame.hpp
    terminal/TerminalRasterizer.cpp
    terminal/TerminalRasterizer.hpp
    terminal/TerminalNotificationSink.hpp
    terminal/TerminalSessionRegistry.cpp
    terminal/TerminalSessionRegistry.hpp
    terminal/TerminalSettings.hpp
    terminal/TerminalSurfaceController.cpp
    terminal/TerminalSurfaceController.hpp
    terminal/TerminalView.cpp
    terminal/TerminalView.hpp
)

qt_add_library(kodosi-qml-models STATIC)
qt_add_qml_module(kodosi-qml-models
    URI Kodosi.Models
    VERSION 1.0
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Kodosi/Models"
    DEPENDENCIES
        QtQuick
    SOURCES
        app/QmlModelTypes.cpp
        app/QmlModelTypes.hpp
)

target_include_directories(kodosi-qml-models
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/app"
)

target_link_libraries(kodosi-qml-models
    PUBLIC
        Qt6::Qml
        kodosi-desktop-integration
        kodosi-runtime-bridge
        kodosi-terminal-kernel
)

get_target_property(_qt_quick_include_dirs Qt6::Quick INTERFACE_INCLUDE_DIRECTORIES)
foreach(_qt_quick_include_dir IN LISTS _qt_quick_include_dirs)
    if(_qt_quick_include_dir MATCHES "/include/QtQuick$")
        set(_qt_quick_private_root
            "${_qt_quick_include_dir}/${Qt6_VERSION}"
        )
    endif()
    get_target_property(_qt_qml_include_dirs Qt6::Qml INTERFACE_INCLUDE_DIRECTORIES)
    foreach(_qt_qml_include_dir IN LISTS _qt_qml_include_dirs)
        if(_qt_qml_include_dir MATCHES "/include/QtQml$")
            set(_qt_qml_private_root "${_qt_qml_include_dir}/${Qt6_VERSION}")
        endif()
    endforeach()
    get_target_property(_qt_core_include_dirs Qt6::Core INTERFACE_INCLUDE_DIRECTORIES)
    foreach(_qt_core_include_dir IN LISTS _qt_core_include_dirs)
        if(_qt_core_include_dir MATCHES "/include/QtCore$")
            set(_qt_core_private_root "${_qt_core_include_dir}/${Qt6_VERSION}")
        endif()
    endforeach()
    get_target_property(_qt_gui_include_dirs Qt6::Gui INTERFACE_INCLUDE_DIRECTORIES)
    foreach(_qt_gui_include_dir IN LISTS _qt_gui_include_dirs)
        if(_qt_gui_include_dir MATCHES "/include/QtGui$")
            set(_qt_gui_private_root "${_qt_gui_include_dir}/${Qt6_VERSION}")
        endif()
    endforeach()
endforeach()
if(NOT _qt_quick_private_root)
    message(FATAL_ERROR "Qt Quick private include root was not found for accessibility enrollment")
endif()

target_include_directories(kodosi-terminal-kernel
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE
        "${_qt_quick_private_root}"
        "${_qt_quick_private_root}/QtQuick"
        "${_qt_qml_private_root}"
        "${_qt_qml_private_root}/QtQml"
        "${_qt_core_private_root}"
        "${_qt_core_private_root}/QtCore"
        "${_qt_gui_private_root}"
        "${_qt_gui_private_root}/QtGui"
)

target_link_libraries(kodosi-terminal-kernel
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
        kodosi-runtime-bridge
    PRIVATE
        Kodosi::RustRuntime
        kodosi-ghostty-vt
)

qt_add_executable(kodosi-qt
    app/main.cpp
)

qt_add_library(kodosi-ui-probe-core STATIC
    ui_probe/AtSpiProbe.cpp
    ui_probe/AtSpiProbe.hpp
    ui_probe/InputAdapter.cpp
    ui_probe/InputAdapter.hpp
    ui_probe/PortalScreenshot.cpp
)

target_include_directories(kodosi-ui-probe-core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(kodosi-ui-probe-core
    PUBLIC
        Qt6::Core
        Qt6::DBus
        Qt6::Network
)

qt_add_executable(kodosi-ui-probe
    ui_probe/main.cpp
)

target_link_libraries(kodosi-ui-probe
    PRIVATE
        kodosi-ui-probe-core
)

qt_add_qml_module(kodosi-qt
    URI Kodosi
    VERSION 1.0
    DEPENDENCIES
        Kodosi.Models
    QML_FILES
        qml/Main.qml
        qml/Attention/AttentionButton.qml
        qml/Attention/AttentionCard.qml
        qml/Attention/AttentionPanel.qml
        qml/Attention/AttentionRail.qml
        qml/Attention/AttentionSpinner.qml
        qml/Auth/AuthOverlay.qml
        qml/Auth/AuthErrorBanner.qml
        qml/Auth/AuthGate.qml
        qml/Appearance/AppearanceMenu.qml
        qml/AgentIntel/AgentCustomAgentsSurface.qml
        qml/AgentIntel/AgentIntelDrawer.qml
        qml/AgentIntel/ProjectIntelligenceModal.qml
        qml/Controls/NavTab.qml
        qml/Controls/KButton.qml
        qml/Controls/KBusyIndicator.qml
        qml/Controls/KCheckBox.qml
        qml/Controls/KComboBox.qml
        qml/Controls/KDialog.qml
        qml/Controls/KIcon.qml
        qml/Controls/KIconButton.qml
        qml/Controls/KItemDelegate.qml
        qml/Controls/KMenu.qml
        qml/Controls/KMenuItem.qml
        qml/Controls/KMenuSeparator.qml
        qml/Controls/KPopover.qml
        qml/Controls/KReadOnlyText.qml
        qml/Controls/KScrollBar.qml
        qml/Controls/KScrollView.qml
        qml/Controls/KSegmentedBar.qml
        qml/Controls/KSlider.qml
        qml/Controls/KSpinBox.qml
        qml/Controls/KSwitch.qml
        qml/Controls/KTextArea.qml
        qml/Controls/KTextField.qml
        qml/Controls/PlainLabel.qml
        qml/Controls/StatusPill.qml
        qml/Controls/SurfaceCard.qml
        qml/Devices/DevicesView.qml
        qml/Devices/AgentIntegrationsPanel.qml
        qml/Devices/TrustPanel.qml
        qml/Diagnostics/DiagnosticsDrawer.qml
        qml/Diagnostics/RuntimeHealthBanner.qml
        qml/People/PeopleView.qml
        qml/People/MissionDetailView.qml
        qml/Settings/SettingsDrawer.qml
        qml/Settings/AgentsSettingsSurface.qml
        qml/Shell/KeyboardShortcutsOverlay.qml
        qml/Theme/KodosiTheme.qml
        qml/Workbench/SessionSidebar.qml
        qml/Workbench/ResumeAgentWorkModal.qml
        qml/Workbench/DesktopFileErrorBanner.qml
        qml/Workbench/TerminalDivider.qml
        qml/Workbench/TerminalStage.qml
        qml/Workbench/TerminalTile.qml
    RESOURCES
        qml/assets/kodosi-logo-dark.png
)

target_link_libraries(kodosi-qt
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Widgets
        kodosi-desktop-integration
        kodosi-runtime-bridge
        kodosi-terminal-kernel
        kodosi-qml-modelsplugin
)

target_compile_definitions(kodosi-runtime-bridge
    PUBLIC
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
        QT_NO_URL_CAST_FROM_STRING
)
target_compile_definitions(kodosi-desktop-integration
    PUBLIC
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
        QT_NO_URL_CAST_FROM_STRING
)
target_compile_definitions(kodosi-terminal-kernel
    PUBLIC
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
        QT_NO_URL_CAST_FROM_STRING
)
target_compile_definitions(kodosi-qt PRIVATE QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII)
target_compile_definitions(kodosi-ui-probe-core
    PUBLIC
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
        QT_NO_URL_CAST_FROM_STRING
)
target_compile_definitions(kodosi-ui-probe
    PRIVATE
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
        QT_NO_URL_CAST_FROM_STRING
)

if(MSVC)
    target_compile_options(kodosi-runtime-bridge PRIVATE /W4 /WX /permissive-)
    target_compile_options(kodosi-desktop-integration PRIVATE /W4 /WX /permissive-)
    target_compile_options(kodosi-terminal-kernel PRIVATE /W4 /WX /permissive-)
    target_compile_options(kodosi-qt PRIVATE /W4 /WX /permissive-)
    target_compile_options(kodosi-ui-probe-core PRIVATE /W4 /WX /permissive-)
    target_compile_options(kodosi-ui-probe PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(kodosi-runtime-bridge PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(kodosi-desktop-integration PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(kodosi-terminal-kernel PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(kodosi-qt PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(kodosi-ui-probe-core PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(kodosi-ui-probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

install(TARGETS kodosi-qt
    BUNDLE DESTINATION .
    RUNTIME DESTINATION lib/kodosi/bin
)

qt_generate_deploy_qml_app_script(
    TARGET kodosi-qt
    OUTPUT_SCRIPT kodosi_qt_deploy_script
    NO_TRANSLATIONS
    NO_UNSUPPORTED_PLATFORM_ERROR
)
set(kodosi_qt_private_deploy_script
    "${CMAKE_CURRENT_BINARY_DIR}/deploy-kodosi-private-runtime.cmake"
)
file(GENERATE
    OUTPUT "${kodosi_qt_private_deploy_script}"
    CONTENT
        "set(QT_DEPLOY_PLUGINS_DIR \"lib/kodosi/plugins\")\nset(QT_DEPLOY_QML_DIR \"lib/kodosi/qml\")\ninclude(\"${kodosi_qt_deploy_script}\")\n"
)
install(SCRIPT "${kodosi_qt_private_deploy_script}")
install(
    FILES
        "$<TARGET_FILE:Qt6::QOffscreenIntegrationPlugin>"
        "$<TARGET_FILE:Qt6::QWaylandIntegrationPlugin>"
    DESTINATION lib/kodosi/plugins/platforms
)
install(
    FILES
        "$<TARGET_FILE:Qt6::QGtk3ThemePlugin>"
        "$<TARGET_FILE:Qt6::QXdgDesktopPortalThemePlugin>"
    DESTINATION lib/kodosi/plugins/platformthemes
)
install(
    FILES "$<TARGET_FILE:Qt6::QWaylandEglClientBufferPlugin>"
    DESTINATION lib/kodosi/plugins/wayland-graphics-integration-client
)
install(
    FILES
        "$<TARGET_FILE:Qt6::QWaylandWlShellIntegrationPlugin>"
        "$<TARGET_FILE:Qt6::QWaylandXdgShellIntegrationPlugin>"
    DESTINATION lib/kodosi/plugins/wayland-shell-integration
)
