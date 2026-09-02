cmake_minimum_required(VERSION 4.3...4.4)

project(
    KodosiQT
    VERSION 0.1.0
    DESCRIPTION "Cross-platform Qt Quick desktop client for Kodosi"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_INSTALL_BINDIR "lib/kodosi/bin" CACHE PATH "" FORCE)
set(CMAKE_INSTALL_LIBDIR "lib/kodosi/lib" CACHE PATH "" FORCE)
set(QT_DEPLOY_BIN_DIR "lib/kodosi/bin" CACHE STRING "" FORCE)
set(QT_DEPLOY_LIB_DIR "lib/kodosi/lib" CACHE STRING "" FORCE)
set(QT_DEPLOY_PLUGINS_DIR "lib/kodosi/plugins" CACHE STRING "" FORCE)
set(QT_DEPLOY_QML_DIR "lib/kodosi/qml" CACHE STRING "" FORCE)

find_package(
    Qt6 6.11.2
    EXACT
    REQUIRED
    COMPONENTS Core DBus Gui Network Qml Quick QuickControls2 Test
)
find_package(Threads REQUIRED)
find_program(CARGO_EXECUTABLE cargo REQUIRED)
find_program(DBUS_RUN_SESSION_EXECUTABLE dbus-run-session REQUIRED)

set(
    KODOSI_RUNTIME_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../Kodosi"
    CACHE PATH
    "Path to the authoritative Kodosi runtime checkout"
)
set(
    KODOSI_GHOSTTY_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../kodosi-ghostty"
    CACHE PATH
    "Path to the verified kodosi-ghostty checkout"
)
set(KODOSI_RUNTIME_MANIFEST "${KODOSI_RUNTIME_ROOT}/rustProcess/Cargo.toml")
set(KODOSI_RUNTIME_GENERATED_HEADER
    "${KODOSI_RUNTIME_ROOT}/rustProcess/target/include/kodosi_runtime.h"
)
set(KODOSI_RUNTIME_HEADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/kodosi-runtime")
set(
    KODOSI_RUNTIME_ARCHIVE
    "${KODOSI_RUNTIME_ROOT}/rustProcess/target/release/libkodosi_ffi_c.a"
)
file(MAKE_DIRECTORY "${KODOSI_RUNTIME_HEADER_DIR}")
set(
    KODOSI_GHOSTTY_VT_ARCHIVE
    "${KODOSI_GHOSTTY_ROOT}/Vendor/GhosttyVt/linux-x86_64/lib/libghostty-vt.a"
)

if(NOT EXISTS "${KODOSI_RUNTIME_MANIFEST}")
    message(FATAL_ERROR "Kodosi runtime manifest not found: ${KODOSI_RUNTIME_MANIFEST}")
endif()
if(NOT EXISTS "${KODOSI_GHOSTTY_ROOT}/Ghostty.ref")
    message(FATAL_ERROR "Verified kodosi-ghostty checkout not found: ${KODOSI_GHOSTTY_ROOT}")
endif()

add_custom_target(kodosi-rust-ffi-build
    COMMAND
        "${CMAKE_COMMAND}" -E env
        "KODOSI_GHOSTTY_DIR=${KODOSI_GHOSTTY_ROOT}"
        "${CARGO_EXECUTABLE}" build
        --manifest-path "${KODOSI_RUNTIME_MANIFEST}"
        --locked
        --release
        -p kodosi-ffi-c
    COMMAND
        "${CMAKE_COMMAND}" -E copy_if_different
        "${KODOSI_RUNTIME_GENERATED_HEADER}"
        "${KODOSI_RUNTIME_HEADER_DIR}/kodosi_runtime.h"
    BYPRODUCTS
        "${KODOSI_RUNTIME_ARCHIVE}"
        "${KODOSI_RUNTIME_HEADER_DIR}/kodosi_runtime.h"
    COMMENT "Building the pinned Kodosi ABI-5 runtime"
    WORKING_DIRECTORY "${KODOSI_RUNTIME_ROOT}"
    VERBATIM
)

add_library(kodosi-rust-runtime STATIC IMPORTED GLOBAL)
add_library(Kodosi::RustRuntime ALIAS kodosi-rust-runtime)
add_library(kodosi-ghostty-vt STATIC IMPORTED GLOBAL)
set_target_properties(kodosi-ghostty-vt PROPERTIES
    IMPORTED_LOCATION "${KODOSI_GHOSTTY_VT_ARCHIVE}"
    INTERFACE_INCLUDE_DIRECTORIES
        "${KODOSI_GHOSTTY_ROOT}/Vendor/GhosttyVt/linux-x86_64/include"
)
set_target_properties(kodosi-rust-runtime PROPERTIES
    IMPORTED_LOCATION "${KODOSI_RUNTIME_ARCHIVE}"
    INTERFACE_INCLUDE_DIRECTORIES "${KODOSI_RUNTIME_HEADER_DIR}"
)
set_property(TARGET kodosi-rust-runtime PROPERTY INTERFACE_LINK_LIBRARIES
    "kodosi-ghostty-vt;Threads::Threads;${CMAKE_DL_LIBS};util;rt;m"
)
add_dependencies(kodosi-rust-runtime kodosi-rust-ffi-build)

qt_standard_project_setup(
    REQUIRES 6.11.2
    I18N_TRANSLATED_LANGUAGES
)

include(CTest)
add_subdirectory(src)

install(
    FILES packaging/linux/com.kodosi.Kodosi.desktop
    DESTINATION share/applications
)
install(
    FILES packaging/linux/com.kodosi.Kodosi.metainfo.xml
    DESTINATION share/metainfo
)
install(
    FILES packaging/linux/com.kodosi.Kodosi.svg
    DESTINATION share/icons/hicolor/scalable/apps
)
install(
    PROGRAMS packaging/linux/kodosi-qt
    DESTINATION bin
)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

set(CPACK_PACKAGE_NAME "kodosi")
set(CPACK_PACKAGE_VENDOR "Kodosi")
set(CPACK_PACKAGE_CONTACT "support@kodosi.com")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Local-first mission control for coding agents"
)
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_GENERATOR "TGZ;DEB")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "libc6 (>= 2.39), libstdc++6, libgcc-s1, libatk1.0-0t64, libbrotli1, libcairo-gobject2, libcairo2, libdbus-1-3, libdrm2, libegl1, libfontconfig1, libfreetype6, libgbm1, libgdk-pixbuf-2.0-0, libgl1, libglib2.0-0t64, libgssapi-krb5-2, libgtk-3-0t64, libharfbuzz0b, libopengl0, libpango-1.0-0, libpangocairo-1.0-0, libwayland-client0, libwayland-cursor0, libwayland-egl1, libx11-6, libx11-xcb1, libxcb-cursor0, libxcb-glx0, libxcb-icccm4, libxcb-image0, libxcb-keysyms1, libxcb-randr0, libxcb-render-util0, libxcb-render0, libxcb-shape0, libxcb-shm0, libxcb-sync1, libxcb-util1, libxcb-xfixes0, libxcb-xkb1, libxcb1, libxkbcommon-x11-0, libxkbcommon0, libzstd1, zlib1g"
)
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://kodosi.com")
set(CPACK_ARCHIVE_FILE_NAME
    "kodosi-${PROJECT_VERSION}-linux-x86_64"
)
include(CPack)
