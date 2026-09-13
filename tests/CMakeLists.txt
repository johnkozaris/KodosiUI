function(kodosi_test name source)
    qt_add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE
        kodosi-runtime-bridge kodosi-desktop-integration kodosi-terminal-kernel
        kodosi-ghostty-vt Qt6::Test Qt6::Widgets Qt6::QuickControls2)
    target_compile_definitions(${name} PRIVATE KODOSI_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    add_test(NAME ${name} COMMAND "${DBUS_RUN_SESSION_EXECUTABLE}" -- "$<TARGET_FILE:${name}>")
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
        TIMEOUT 60)
endfunction()

qt_add_executable(kodosi-runtime-bridge-tests tst_runtime_bridge.cpp RuntimeApiFixture.cpp)
target_link_libraries(kodosi-runtime-bridge-tests PRIVATE kodosi-runtime-bridge Kodosi::RustRuntime Qt6::Test)
add_test(NAME kodosi-runtime-bridge-tests COMMAND kodosi-runtime-bridge-tests)

kodosi_test(kodosi-workspace-tests tst_workspace.cpp)
kodosi_test(kodosi-runtime-terminal-journey-tests tst_runtime_terminal_journey.cpp)
set_tests_properties(kodosi-runtime-terminal-journey-tests PROPERTIES TIMEOUT 90)
kodosi_test(kodosi-provider-tools-tests tst_provider_tools.cpp)
kodosi_test(kodosi-desktop-settings-tests tst_desktop_settings.cpp)
kodosi_test(kodosi-desktop-file-integration-tests tst_desktop_file_integration.cpp)
kodosi_test(kodosi-deep-links-tests tst_deep_links.cpp)
kodosi_test(kodosi-single-instance-tests tst_single_instance.cpp)
kodosi_test(kodosi-application-lifecycle-model-tests tst_application_lifecycle_model.cpp)
kodosi_test(kodosi-application-log-store-tests tst_application_log_store.cpp)
kodosi_test(kodosi-appearance-model-tests tst_appearance_model.cpp)
kodosi_test(kodosi-terminal-tiling-layout-model-tests tst_terminal_tiling_layout_model.cpp)
kodosi_test(kodosi-terminal-kernel-tests tst_terminal_kernel.cpp)
kodosi_test(kodosi-terminal-notifications-tests tst_terminal_notifications.cpp)
kodosi_test(kodosi-freedesktop-notification-driver-tests tst_freedesktop_notification_driver.cpp)
kodosi_test(kodosi-accessibility-scope-tests tst_accessibility_scope.cpp)

set(KODOSI_CONTROL_TEST_DIR "${CMAKE_CURRENT_BINARY_DIR}/control-fixture")
file(MAKE_DIRECTORY "${KODOSI_CONTROL_TEST_DIR}/Controls")
foreach(control KButton KFocusIndicator KIcon PlainLabel)
    configure_file("${PROJECT_SOURCE_DIR}/src/qml/Controls/${control}.qml"
        "${KODOSI_CONTROL_TEST_DIR}/Controls/${control}.qml" COPYONLY)
endforeach()
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/qml/KodosiTheme.qml"
    "${KODOSI_CONTROL_TEST_DIR}/Controls/KodosiTheme.qml" COPYONLY)
file(WRITE "${KODOSI_CONTROL_TEST_DIR}/Controls/qmldir"
    "singleton KodosiTheme 1.0 KodosiTheme.qml\n")
kodosi_test(kodosi-control-tests tst_controls.cpp)
target_compile_definitions(kodosi-control-tests PRIVATE
    KODOSI_CONTROL_TEST_DIR="${KODOSI_CONTROL_TEST_DIR}")

qt_add_executable(kodosi-fake-atspi-service fake_atspi_service.cpp)
target_link_libraries(kodosi-fake-atspi-service PRIVATE Qt6::Core Qt6::DBus)
qt_add_executable(kodosi-ui-probe-tests tst_ui_probe.cpp)
target_link_libraries(kodosi-ui-probe-tests PRIVATE kodosi-ui-probe-core Qt6::Test)
target_compile_definitions(kodosi-ui-probe-tests PRIVATE
    KODOSI_FAKE_ATSPI_PATH="$<TARGET_FILE:kodosi-fake-atspi-service>"
    KODOSI_UI_PROBE_PATH="$<TARGET_FILE:kodosi-ui-probe>")
add_dependencies(kodosi-ui-probe-tests kodosi-fake-atspi-service kodosi-ui-probe)
add_test(NAME kodosi-ui-probe-tests COMMAND "${DBUS_RUN_SESSION_EXECUTABLE}" -- "$<TARGET_FILE:kodosi-ui-probe-tests>")
set_tests_properties(kodosi-ui-probe-tests PROPERTIES TIMEOUT 60)

add_test(NAME kodosi-window-size-missing-rejected COMMAND kodosi-qt --window-size)
add_test(NAME kodosi-window-size-malformed-rejected COMMAND kodosi-qt --window-size 900x600junk)
set_tests_properties(kodosi-window-size-missing-rejected kodosi-window-size-malformed-rejected
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" WILL_FAIL TRUE TIMEOUT 10)
add_test(NAME kodosi-accessible-id-audit
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/audit-accessible-ids.py")

kodosi_test(kodosi-shell-tests tst_shell.cpp)
target_link_libraries(kodosi-shell-tests PRIVATE kodosi-qml-modelsplugin kodosi-uiplugin)
