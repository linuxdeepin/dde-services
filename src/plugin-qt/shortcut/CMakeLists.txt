# Shortcut Manager Plugin for deepin-service-manager
cmake_minimum_required(VERSION 3.16)

set(PLUGIN_NAME plugin-dde-shortcut)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core DBus Gui WaylandClient)
find_package(Dtk6 REQUIRED COMPONENTS Core DConfig)
find_package(PkgConfig REQUIRED)
find_package(TreelandProtocols REQUIRED)

pkg_check_modules(XCB REQUIRED xcb xcb-keysyms xcb-xtest)
pkg_check_modules(X11 REQUIRED x11)
pkg_check_modules(WAYLAND_CLIENT REQUIRED wayland-client)

# Set source file path (using sources from plugin directory)
set(SHORTCUT_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Common sources shared between plugin and debug binary
set(SHORTCUT_COMMON_SOURCES
    ${SHORTCUT_SRC_DIR}/core/shortcutmanager.cpp
    ${SHORTCUT_SRC_DIR}/core/keybindingmanager.cpp
    ${SHORTCUT_SRC_DIR}/core/gesturemanager.cpp
    ${SHORTCUT_SRC_DIR}/core/qkeysequenceconverter.cpp
    ${SHORTCUT_SRC_DIR}/core/actionexecutor.cpp
    ${SHORTCUT_SRC_DIR}/core/translationmanager.cpp
    ${SHORTCUT_SRC_DIR}/config/configloader.cpp
    ${SHORTCUT_SRC_DIR}/backend/abstractkeyhandler.h
    ${SHORTCUT_SRC_DIR}/backend/abstractgesturehandler.h
    ${SHORTCUT_SRC_DIR}/backend/specialkeyhandler.cpp
    ${SHORTCUT_SRC_DIR}/backend/x11/x11keyhandler.cpp
    ${SHORTCUT_SRC_DIR}/backend/x11/x11helper.cpp
    ${SHORTCUT_SRC_DIR}/backend/x11/modifierkeymonitor.cpp
    ${SHORTCUT_SRC_DIR}/backend/wayland/treelandshortcutwrapper.cpp
    ${SHORTCUT_SRC_DIR}/backend/wayland/waylandkeyhandler.cpp
    ${SHORTCUT_SRC_DIR}/backend/wayland/waylandgesturehandler.cpp
)

# Plugin sources (plugin-specific + common)
set(PLUGIN_SOURCES
    plugin.cpp
    pluginshortcutmanager.cpp
    pluginshortcutmanager.h
    ${SHORTCUT_COMMON_SOURCES}
)

# Create plugin library
add_library(${PLUGIN_NAME} SHARED ${PLUGIN_SOURCES})

# Wayland Protocol Generation
qt6_generate_wayland_protocol_client_sources(
    ${PLUGIN_NAME}
    FILES 
        ${TREELAND_PROTOCOLS_DATA_DIR}/treeland-shortcut-manager-v2.xml
        ${SHORTCUT_SRC_DIR}/3rdparty/kde-keystate.xml
)

# Add generated protocol files
target_sources(${PLUGIN_NAME} PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/wayland-treeland-shortcut-manager-v2-protocol.c
    ${CMAKE_CURRENT_BINARY_DIR}/wayland-kde-keystate-protocol.c
)

target_link_libraries(${PLUGIN_NAME} PRIVATE
    Qt6::Core
    Qt6::DBus
    Qt6::Gui
    Qt6::WaylandClient
    Dtk6::Core
    ${XCB_LIBRARIES}
    ${X11_LIBRARIES}
    ${WAYLAND_CLIENT_LIBRARIES}
)

target_include_directories(${PLUGIN_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SHORTCUT_SRC_DIR}
    ${XCB_INCLUDE_DIRS}
    ${X11_INCLUDE_DIRS}
    ${WAYLAND_CLIENT_INCLUDE_DIRS}
)

# Install plugin
install(TARGETS ${PLUGIN_NAME}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/deepin-service-manager)

# Install configuration files
install(FILES plugin-dde-shortcut.json
        DESTINATION share/deepin-service-manager/user)

# Install DConfig configuration files
# Get all shortcut and gesture config subdirectories (using absolute paths)
file(GLOB CONFIG_SUBDIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/configs/org.deepin.dde.keybinding.shortcut.*"
    "${CMAKE_CURRENT_SOURCE_DIR}/configs/org.deepin.dde.keybinding.gesture.*"
)

# Install JSON config file for each subdirectory
# Note: BASE must be the absolute path to the configs directory so that
# dtk_add_config_meta_files can correctly calculate the relative path of subdirectories
foreach(SUBDIR_PATH ${CONFIG_SUBDIRS})
    dtk_add_config_meta_files(
        APPID "org.deepin.dde.keybinding"
        BASE "${CMAKE_CURRENT_SOURCE_DIR}/configs"
        FILES "${SUBDIR_PATH}/org.deepin.shortcut.json"
    )
endforeach()

install(FILES configs/org.deepin.dde.keybinding.ini 
        DESTINATION share/deepin/org.deepin.dde.keybinding)

# ============ i18n Translation Support ============
# Install i18n extraction tool
install(PROGRAMS tools/extract_shortcuts_i18n.py 
        DESTINATION bin 
        RENAME extract_shortcuts_i18n)

# Install CMake helper module
install(FILES cmake/DdeShortcutI18n.cmake 
        DESTINATION lib/${CMAKE_LIBRARY_ARCHITECTURE}/cmake/DdeShortcutI18n
        RENAME DdeShortcutI18nConfig.cmake)

# i18n integration
find_package(Qt6 REQUIRED COMPONENTS LinguistTools)
include(cmake/DdeShortcutI18n.cmake)

set(I18N_LANGUAGES 
    "zh_CN" "zh_HK" "zh_TW" "en_US" "ast" "az" "bg" "bo" "ca" "cs" "da" 
    "de" "el" "es" "et" "eu" "fa" "fi" "fr" "gl" "he" "hi" "hr" "hu" 
    "hy" "id" "it" "ja" "ka" "kk" "ko" "ky" "lt" "lv" "ms" "nb" "ne" 
    "nl" "pa" "pl" "pt_BR" "pt" "ro" "ru" "sk" "sl" "sq" "sr" "sv" "th" 
    "tr" "ug" "uk" "vi"
)

dde_shortcut_add_translations(
    APP_ID "org.deepin.dde.keybinding"
    CONFIG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/configs"
    LANGUAGES ${I18N_LANGUAGES}
)

# subdirectory
add_subdirectory(tools)

# ============ Debug Standalone Binary (Optional) ============
option(BUILD_SHORTCUT_DEBUG_BINARY "Build standalone debug binary for dde-shortcut" ON)

if(BUILD_SHORTCUT_DEBUG_BINARY)
    message(STATUS "Building standalone debug binary: dde-shortcut-debug")
    
    # Create standalone executable (reuse common sources)
    add_executable(dde-shortcut-debug
        ${SHORTCUT_SRC_DIR}/main.cpp
        ${SHORTCUT_COMMON_SOURCES}
        # Reuse Wayland protocol files generated for plugin
        ${CMAKE_CURRENT_BINARY_DIR}/wayland-treeland-shortcut-manager-v2-protocol.c
        ${CMAKE_CURRENT_BINARY_DIR}/wayland-kde-keystate-protocol.c
        ${CMAKE_CURRENT_BINARY_DIR}/qwayland-treeland-shortcut-manager-v2.cpp
        ${CMAKE_CURRENT_BINARY_DIR}/qwayland-kde-keystate.cpp
    )
    
    # Enable AUTOMOC for debug binary
    set_target_properties(dde-shortcut-debug PROPERTIES AUTOMOC ON)
    
    # Add dependency on plugin to ensure protocol files are generated first
    add_dependencies(dde-shortcut-debug ${PLUGIN_NAME})
    
    target_link_libraries(dde-shortcut-debug PRIVATE
        Qt6::Core
        Qt6::DBus
        Qt6::Gui
        Qt6::WaylandClient
        Dtk6::Core
        ${XCB_LIBRARIES}
        ${X11_LIBRARIES}
        ${WAYLAND_CLIENT_LIBRARIES}
        xcb-xtest  # Explicitly link xcb-xtest
    )
    
    target_include_directories(dde-shortcut-debug PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${SHORTCUT_SRC_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}  # For generated Wayland headers
        ${XCB_INCLUDE_DIRS}
        ${X11_INCLUDE_DIRS}
        ${WAYLAND_CLIENT_INCLUDE_DIRS}
    )
endif()
