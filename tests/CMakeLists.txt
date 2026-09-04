qt_add_executable(kodosi-qt-contract-tests
    tst_contract.cpp
)

target_link_libraries(kodosi-qt-contract-tests
    PRIVATE
        kodosi-runtime-bridge
        kodosi-terminal-kernel
        kodosi-ghostty-vt
        Qt6::Core
        Qt6::Gui
        Qt6::Test
)

target_compile_definitions(kodosi-qt-contract-tests
    PRIVATE
        KODOSI_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
)

add_test(NAME kodosi-qt-contract-tests COMMAND kodosi-qt-contract-tests)

qt_add_executable(kodosi-application-lifecycle-model-tests
    tst_application_lifecycle_model.cpp
)
target_link_libraries(kodosi-application-lifecycle-model-tests
    PRIVATE
        kodosi-runtime-bridge
        Qt6::Core
        Qt6::Test
)
target_compile_definitions(kodosi-application-lifecycle-model-tests
    PRIVATE
        KODOSI_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
)
add_test(
    NAME kodosi-application-lifecycle-model-tests
    COMMAND kodosi-application-lifecycle-model-tests
)

qt_add_executable(kodosi-application-log-store-tests
    tst_application_log_store.cpp
)
target_link_libraries(kodosi-application-log-store-tests
    PRIVATE
        kodosi-runtime-bridge
        Qt6::Core
        Qt6::Test
)
target_compile_definitions(kodosi-application-log-store-tests
    PRIVATE
        KODOSI_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
)
add_test(
    NAME kodosi-application-log-store-tests
    COMMAND kodosi-application-log-store-tests
)

qt_add_executable(kodosi-desktop-settings-tests
    tst_desktop_settings.cpp
)
target_link_libraries(kodosi-desktop-settings-tests
    PRIVATE
        kodosi-runtime-bridge
        Qt6::Core
        Qt6::Test
)
target_compile_definitions(kodosi-desktop-settings-tests
    PRIVATE
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
)
add_test(NAME kodosi-desktop-settings-tests COMMAND kodosi-desktop-settings-tests)

qt_add_executable(kodosi-deep-links-tests
    tst_deep_links.cpp
)
target_link_libraries(kodosi-deep-links-tests
    PRIVATE
        kodosi-runtime-bridge
        Qt6::Core
        Qt6::Test
)
target_compile_definitions(kodosi-deep-links-tests
    PRIVATE
        KODOSI_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
        QT_NO_URL_CAST_FROM_STRING
)
add_test(NAME kodosi-deep-links-tests COMMAND kodosi-deep-links-tests)

qt_add_executable(kodosi-single-instance-tests
    tst_single_instance.cpp
)
target_link_libraries(kodosi-single-instance-tests
    PRIVATE
        kodosi-runtime-bridge
        Qt6::Core
        Qt6::Network
        Qt6::Test
)
target_compile_definitions(kodosi-single-instance-tests
    PRIVATE
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
        QT_NO_URL_CAST_FROM_STRING
)
add_test(
    NAME kodosi-single-instance-tests
    COMMAND kodosi-single-instance-tests
)

qt_add_executable(kodosi-appearance-model-tests
    tst_appearance_model.cpp
)
target_link_libraries(kodosi-appearance-model-tests
    PRIVATE
        kodosi-runtime-bridge
        Qt6::Core
        Qt6::DBus
        Qt6::Gui
        Qt6::Test
)
target_compile_definitions(kodosi-appearance-model-tests
    PRIVATE
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
)
add_test(
    NAME kodosi-appearance-model-tests
    COMMAND
        "${DBUS_RUN_SESSION_EXECUTABLE}"
        --
        "$<TARGET_FILE:kodosi-appearance-model-tests>"
)

qt_add_executable(kodosi-desktop-file-integration-tests
    tst_desktop_file_integration.cpp
)
target_link_libraries(kodosi-desktop-file-integration-tests
    PRIVATE
        kodosi-desktop-integration
        kodosi-runtime-bridge
        Qt6::Core
        Qt6::Gui
        Qt6::Test
        Qt6::Widgets
)
target_compile_definitions(kodosi-desktop-file-integration-tests
    PRIVATE
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
        QT_NO_URL_CAST_FROM_STRING
)
add_test(
    NAME kodosi-desktop-file-integration-tests
    COMMAND kodosi-desktop-file-integration-tests
)

qt_add_executable(kodosi-project-intelligence-model-tests
    tst_project_intelligence_model.cpp
)
target_link_libraries(kodosi-project-intelligence-model-tests
    PRIVATE
        kodosi-desktop-integration
        kodosi-runtime-bridge
        Qt6::Core
        Qt6::Gui
        Qt6::Test
        Qt6::Widgets
)
target_compile_definitions(kodosi-project-intelligence-model-tests
    PRIVATE
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
        QT_NO_URL_CAST_FROM_STRING
)
add_test(
    NAME kodosi-project-intelligence-model-tests
    COMMAND kodosi-project-intelligence-model-tests
)
set_tests_properties(kodosi-project-intelligence-model-tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 30
)
set_tests_properties(kodosi-desktop-file-integration-tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 30
)

qt_add_executable(kodosi-desktop-state-model-tests
    tst_desktop_state_model.cpp
)
target_link_libraries(kodosi-desktop-state-model-tests
    PRIVATE
        kodosi-runtime-bridge
        Qt6::Core
        Qt6::Gui
        Qt6::Test
)
target_compile_definitions(kodosi-desktop-state-model-tests
    PRIVATE
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
)
add_test(
    NAME kodosi-desktop-state-model-tests
    COMMAND kodosi-desktop-state-model-tests
)
set_tests_properties(kodosi-desktop-state-model-tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 60
)

qt_add_executable(kodosi-terminal-tiling-layout-model-tests
    tst_terminal_tiling_layout_model.cpp
)
target_link_libraries(kodosi-terminal-tiling-layout-model-tests
    PRIVATE
        kodosi-runtime-bridge
        Qt6::Core
        Qt6::Test
)
target_compile_definitions(kodosi-terminal-tiling-layout-model-tests
    PRIVATE
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
)
add_test(
    NAME kodosi-terminal-tiling-layout-model-tests
    COMMAND kodosi-terminal-tiling-layout-model-tests
)

add_test(
    NAME kodosi-window-size-missing-rejected
    COMMAND kodosi-qt --window-size
)
set_tests_properties(kodosi-window-size-missing-rejected PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    WILL_FAIL TRUE
    TIMEOUT 10
)

add_test(
    NAME kodosi-window-size-malformed-rejected
    COMMAND kodosi-qt --window-size 900x600junk
)
set_tests_properties(kodosi-window-size-malformed-rejected PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    WILL_FAIL TRUE
    TIMEOUT 10
)

qt_add_executable(kodosi-session-catalog-tests
    tst_session_catalog.cpp
)
target_link_libraries(kodosi-session-catalog-tests
    PRIVATE
        kodosi-runtime-bridge
        Qt6::Core
        Qt6::Test
)
target_compile_definitions(kodosi-session-catalog-tests
    PRIVATE
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
)
add_test(NAME kodosi-session-catalog-tests COMMAND kodosi-session-catalog-tests)

qt_add_executable(kodosi-session-actions-tests
    tst_session_actions.cpp
)
target_link_libraries(kodosi-session-actions-tests
    PRIVATE
        kodosi-runtime-bridge
        Qt6::Core
        Qt6::Test
)
target_compile_definitions(kodosi-session-actions-tests
    PRIVATE
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
)
add_test(NAME kodosi-session-actions-tests COMMAND kodosi-session-actions-tests)

qt_add_executable(kodosi-session-access-tests
    tst_session_access.cpp
)
target_link_libraries(kodosi-session-access-tests
    PRIVATE
        kodosi-runtime-bridge
        Qt6::Core
        Qt6::Test
)
target_compile_definitions(kodosi-session-access-tests
    PRIVATE
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
)
add_test(NAME kodosi-session-access-tests COMMAND kodosi-session-access-tests)

qt_add_executable(kodosi-session-share-scope-tests
    tst_session_share_scope.cpp
)
target_link_libraries(kodosi-session-share-scope-tests
    PRIVATE
        kodosi-runtime-bridge
        Qt6::Core
        Qt6::Test
)
target_compile_definitions(kodosi-session-share-scope-tests
    PRIVATE
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
)
add_test(
    NAME kodosi-session-share-scope-tests
    COMMAND kodosi-session-share-scope-tests
)

qt_add_executable(kodosi-steering-model-tests
    tst_steering_model.cpp
)
target_link_libraries(kodosi-steering-model-tests
    PRIVATE
        kodosi-runtime-bridge
        Qt6::Core
        Qt6::Test
)
target_compile_definitions(kodosi-steering-model-tests
    PRIVATE
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
)
add_test(NAME kodosi-steering-model-tests COMMAND kodosi-steering-model-tests)

qt_add_executable(kodosi-auth-state-tests
    tst_auth_state.cpp
)
target_link_libraries(kodosi-auth-state-tests
    PRIVATE
        kodosi-runtime-bridge
        Qt6::Core
        Qt6::Test
)
target_compile_definitions(kodosi-auth-state-tests
    PRIVATE
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
)
add_test(NAME kodosi-auth-state-tests COMMAND kodosi-auth-state-tests)

qt_add_executable(kodosi-auth-actions-tests
    tst_auth_actions.cpp
)
target_link_libraries(kodosi-auth-actions-tests
    PRIVATE
        kodosi-runtime-bridge
        Qt6::Core
        Qt6::Gui
        Qt6::Test
)
target_compile_definitions(kodosi-auth-actions-tests
    PRIVATE
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
)
add_test(NAME kodosi-auth-actions-tests COMMAND kodosi-auth-actions-tests)

qt_add_executable(kodosi-people-model-tests
    tst_people_model.cpp
)
target_link_libraries(kodosi-people-model-tests
    PRIVATE
        kodosi-runtime-bridge
        Qt6::Core
        Qt6::Test
)
target_compile_definitions(kodosi-people-model-tests
    PRIVATE
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
)
add_test(NAME kodosi-people-model-tests COMMAND kodosi-people-model-tests)

qt_add_executable(kodosi-people-actions-tests
    tst_people_actions.cpp
)
target_link_libraries(kodosi-people-actions-tests
    PRIVATE
        kodosi-runtime-bridge
        Qt6::Core
        Qt6::Test
)
target_compile_definitions(kodosi-people-actions-tests
    PRIVATE
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
)
add_test(NAME kodosi-people-actions-tests COMMAND kodosi-people-actions-tests)

qt_add_executable(kodosi-devices-model-tests
    tst_devices_model.cpp
)
target_link_libraries(kodosi-devices-model-tests
    PRIVATE
        kodosi-runtime-bridge
        Qt6::Core
        Qt6::Test
)
target_compile_definitions(kodosi-devices-model-tests
    PRIVATE
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
)
add_test(NAME kodosi-devices-model-tests COMMAND kodosi-devices-model-tests)

qt_add_executable(kodosi-device-actions-tests
    tst_device_actions.cpp
)
target_link_libraries(kodosi-device-actions-tests
    PRIVATE
        kodosi-runtime-bridge
        Qt6::Core
        Qt6::Test
)
target_compile_definitions(kodosi-device-actions-tests
    PRIVATE
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
)
add_test(NAME kodosi-device-actions-tests COMMAND kodosi-device-actions-tests)

qt_add_executable(kodosi-trust-model-tests
    tst_trust_model.cpp
)
target_link_libraries(kodosi-trust-model-tests
    PRIVATE
        kodosi-runtime-bridge
        Qt6::Core
        Qt6::Test
)
target_compile_definitions(kodosi-trust-model-tests
    PRIVATE
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
)
add_test(NAME kodosi-trust-model-tests COMMAND kodosi-trust-model-tests)

qt_add_executable(kodosi-mission-directory-model-tests
    tst_mission_directory_model.cpp
)
target_link_libraries(kodosi-mission-directory-model-tests
    PRIVATE
        kodosi-runtime-bridge
        Qt6::Core
        Qt6::Test
)
target_compile_definitions(kodosi-mission-directory-model-tests
    PRIVATE
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
)
add_test(
    NAME kodosi-mission-directory-model-tests
    COMMAND kodosi-mission-directory-model-tests
)

qt_add_executable(kodosi-mission-detail-model-tests
    tst_mission_detail_model.cpp
)
target_link_libraries(kodosi-mission-detail-model-tests
    PRIVATE
        kodosi-runtime-bridge
        Qt6::Core
        Qt6::Test
)
target_compile_definitions(kodosi-mission-detail-model-tests
    PRIVATE
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
)
add_test(
    NAME kodosi-mission-detail-model-tests
    COMMAND kodosi-mission-detail-model-tests
)

qt_add_executable(kodosi-mission-actions-tests
    tst_mission_actions.cpp
)
target_link_libraries(kodosi-mission-actions-tests
    PRIVATE
        kodosi-runtime-bridge
        Qt6::Core
        Qt6::Test
)
target_compile_definitions(kodosi-mission-actions-tests
    PRIVATE
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
)
add_test(NAME kodosi-mission-actions-tests COMMAND kodosi-mission-actions-tests)

qt_add_executable(kodosi-agent-global-model-tests
    tst_agent_global_model.cpp
)
target_link_libraries(kodosi-agent-global-model-tests
    PRIVATE
        kodosi-runtime-bridge
        Qt6::Core
        Qt6::Test
)
target_compile_definitions(kodosi-agent-global-model-tests
    PRIVATE
        KODOSI_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
)
add_test(NAME kodosi-agent-global-model-tests COMMAND kodosi-agent-global-model-tests)

qt_add_executable(kodosi-runtime-diagnostics-model-tests
    tst_runtime_diagnostics_model.cpp
)
target_link_libraries(kodosi-runtime-diagnostics-model-tests
    PRIVATE
        kodosi-runtime-bridge
        Qt6::Core
        Qt6::Test
)
target_compile_definitions(kodosi-runtime-diagnostics-model-tests
    PRIVATE
        KODOSI_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
)
add_test(
    NAME kodosi-runtime-diagnostics-model-tests
    COMMAND kodosi-runtime-diagnostics-model-tests
)

qt_add_executable(kodosi-agent-session-intel-model-tests
    tst_agent_session_intel_model.cpp
)
target_link_libraries(kodosi-agent-session-intel-model-tests
    PRIVATE
        kodosi-runtime-bridge
        Qt6::Core
        Qt6::Test
)
target_compile_definitions(kodosi-agent-session-intel-model-tests
    PRIVATE
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
)
add_test(
    NAME kodosi-agent-session-intel-model-tests
    COMMAND kodosi-agent-session-intel-model-tests
)

qt_add_executable(kodosi-agent-conversation-model-tests
    tst_agent_conversation_model.cpp
)
target_link_libraries(kodosi-agent-conversation-model-tests
    PRIVATE
        kodosi-runtime-bridge
        Qt6::Core
        Qt6::Test
)
target_compile_definitions(kodosi-agent-conversation-model-tests
    PRIVATE
        KODOSI_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
)
add_test(
    NAME kodosi-agent-conversation-model-tests
    COMMAND kodosi-agent-conversation-model-tests
)

qt_add_executable(kodosi-provider-conversations-model-tests
    tst_provider_conversations_model.cpp
)
target_link_libraries(kodosi-provider-conversations-model-tests
    PRIVATE
        kodosi-desktop-integration
        kodosi-runtime-bridge
        Qt6::Core
        Qt6::Gui
        Qt6::Test
        Qt6::Widgets
)
target_compile_definitions(kodosi-provider-conversations-model-tests
    PRIVATE
        KODOSI_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
        QT_NO_URL_CAST_FROM_STRING
)
add_test(
    NAME kodosi-provider-conversations-model-tests
    COMMAND kodosi-provider-conversations-model-tests
)
set_tests_properties(kodosi-provider-conversations-model-tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 30
)

qt_add_executable(kodosi-agent-memory-model-tests
    tst_agent_memory_model.cpp
)
target_link_libraries(kodosi-agent-memory-model-tests
    PRIVATE
        kodosi-runtime-bridge
        Qt6::Core
        Qt6::Test
)
target_compile_definitions(kodosi-agent-memory-model-tests
    PRIVATE
        KODOSI_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
)
add_test(
    NAME kodosi-agent-memory-model-tests
    COMMAND kodosi-agent-memory-model-tests
)

qt_add_executable(kodosi-agent-custom-agents-model-tests
    tst_agent_custom_agents_model.cpp
)
target_link_libraries(kodosi-agent-custom-agents-model-tests
    PRIVATE
        kodosi-runtime-bridge
        Qt6::Core
        Qt6::Test
)
target_compile_definitions(kodosi-agent-custom-agents-model-tests
    PRIVATE
        KODOSI_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
)
add_test(
    NAME kodosi-agent-custom-agents-model-tests
    COMMAND kodosi-agent-custom-agents-model-tests
)

qt_add_executable(kodosi-pending-permissions-model-tests
    tst_pending_permissions_model.cpp
)
target_link_libraries(kodosi-pending-permissions-model-tests
    PRIVATE
        kodosi-runtime-bridge
        Qt6::Core
        Qt6::Test
)
target_compile_definitions(kodosi-pending-permissions-model-tests
    PRIVATE
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
)
add_test(
    NAME kodosi-pending-permissions-model-tests
    COMMAND kodosi-pending-permissions-model-tests
)

qt_add_executable(kodosi-attention-model-tests
    tst_attention_model.cpp
)
target_link_libraries(kodosi-attention-model-tests
    PRIVATE
        kodosi-runtime-bridge
        Qt6::Core
        Qt6::Test
)
target_compile_definitions(kodosi-attention-model-tests
    PRIVATE
        KODOSI_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
)
add_test(
    NAME kodosi-attention-model-tests
    COMMAND kodosi-attention-model-tests
)

qt_add_executable(kodosi-approval-notifications-tests
    tst_approval_notifications.cpp
)
target_link_libraries(kodosi-approval-notifications-tests
    PRIVATE
        kodosi-desktop-integration
        kodosi-runtime-bridge
        Qt6::Core
        Qt6::Test
)
target_compile_definitions(kodosi-approval-notifications-tests
    PRIVATE
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
)
add_test(
    NAME kodosi-approval-notifications-tests
    COMMAND kodosi-approval-notifications-tests
)

qt_add_executable(kodosi-terminal-notifications-tests
    tst_terminal_notifications.cpp
)
target_link_libraries(kodosi-terminal-notifications-tests
    PRIVATE
        kodosi-desktop-integration
        kodosi-runtime-bridge
        Qt6::Core
        Qt6::Test
)
target_compile_definitions(kodosi-terminal-notifications-tests
    PRIVATE
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
)
add_test(
    NAME kodosi-terminal-notifications-tests
    COMMAND kodosi-terminal-notifications-tests
)

qt_add_executable(kodosi-freedesktop-notification-driver-tests
    tst_freedesktop_notification_driver.cpp
)
target_link_libraries(kodosi-freedesktop-notification-driver-tests
    PRIVATE
        kodosi-desktop-integration
        Qt6::Core
        Qt6::DBus
        Qt6::Test
)
target_compile_definitions(kodosi-freedesktop-notification-driver-tests
    PRIVATE
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
)
add_test(
    NAME kodosi-freedesktop-notification-driver-tests
    COMMAND
        "${DBUS_RUN_SESSION_EXECUTABLE}"
        --
        "$<TARGET_FILE:kodosi-freedesktop-notification-driver-tests>"
)

qt_add_executable(kodosi-terminal-kernel-tests
    tst_terminal_kernel.cpp
)

target_link_libraries(kodosi-terminal-kernel-tests
    PRIVATE
        kodosi-terminal-kernel
        kodosi-ghostty-vt
        Qt6::Core
        Qt6::Gui
        Qt6::Test
)

target_compile_definitions(kodosi-terminal-kernel-tests
    PRIVATE
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
)

add_test(NAME kodosi-terminal-kernel-tests COMMAND kodosi-terminal-kernel-tests)
set_tests_properties(kodosi-terminal-kernel-tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 10
)

qt_add_executable(kodosi-fake-atspi-service
    fake_atspi_service.cpp
)
target_link_libraries(kodosi-fake-atspi-service
    PRIVATE
        Qt6::Core
        Qt6::DBus
)
target_compile_definitions(kodosi-fake-atspi-service
    PRIVATE
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
)
if(MSVC)
    target_compile_options(kodosi-fake-atspi-service PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(kodosi-fake-atspi-service PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

qt_add_executable(kodosi-ui-probe-tests
    tst_ui_probe.cpp
)
target_link_libraries(kodosi-ui-probe-tests
    PRIVATE
        kodosi-ui-probe-core
        Qt6::Core
        Qt6::Test
)
target_compile_definitions(kodosi-ui-probe-tests
    PRIVATE
        KODOSI_FAKE_ATSPI_PATH="$<TARGET_FILE:kodosi-fake-atspi-service>"
        KODOSI_UI_PROBE_PATH="$<TARGET_FILE:kodosi-ui-probe>"
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
)
if(MSVC)
    target_compile_options(kodosi-ui-probe-tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(kodosi-ui-probe-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_dependencies(
    kodosi-ui-probe-tests
    kodosi-fake-atspi-service
    kodosi-ui-probe
)
add_test(
    NAME kodosi-ui-probe-tests
    COMMAND
        "${DBUS_RUN_SESSION_EXECUTABLE}"
        --
        "$<TARGET_FILE:kodosi-ui-probe-tests>"
)
set_tests_properties(kodosi-ui-probe-tests PROPERTIES TIMEOUT 60)

add_test(
    NAME kodosi-accessible-id-audit
    COMMAND python3 "${PROJECT_SOURCE_DIR}/scripts/audit-accessible-ids.py"
)
