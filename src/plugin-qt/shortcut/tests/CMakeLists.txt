# SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: LGPL-3.0-or-later

find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
find_program(DBUS_RUN_SESSION_EXECUTABLE dbus-run-session REQUIRED)

add_executable(tst-brightnesspolicy
    tst_brightnesspolicy.cpp
)

target_include_directories(tst-brightnesspolicy PRIVATE
    ../tools/dde-shortcut-tool
    $<TARGET_PROPERTY:dde-shortcut-tool,BINARY_DIR>
)

target_link_libraries(tst-brightnesspolicy PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Test
    Qt6::WaylandClient
)

add_dependencies(tst-brightnesspolicy dde-shortcut-tool)

add_test(NAME shortcut-brightnesspolicy COMMAND tst-brightnesspolicy)

add_executable(tst-physicalkeyalias
    tst_physicalkeyalias.cpp
    ../src/core/physicalkeyalias.cpp
)

target_include_directories(tst-physicalkeyalias PRIVATE
    ../src
)

target_link_libraries(tst-physicalkeyalias PRIVATE
    Qt6::Core
    Qt6::Test
)

add_test(NAME shortcut-physicalkeyalias COMMAND tst-physicalkeyalias)

add_executable(tst-triggeractioncatalog
    tst_triggeractioncatalog.cpp
    ../src/core/triggeractioncatalog.cpp
)

target_include_directories(tst-triggeractioncatalog PRIVATE
    ../src
    ${CMAKE_CURRENT_BINARY_DIR}/..
)

add_dependencies(tst-triggeractioncatalog plugin-dde-shortcut)

target_link_libraries(tst-triggeractioncatalog PRIVATE
    Qt6::Core
    Qt6::Test
)

add_test(NAME shortcut-triggeractioncatalog COMMAND tst-triggeractioncatalog)

add_executable(tst-gestureactioncatalog
    tst_gestureactioncatalog.cpp
    ../src/core/triggeractioncatalog.cpp
    ../src/core/gestureactioncatalog.cpp
)

target_include_directories(tst-gestureactioncatalog PRIVATE
    ../src
    ${CMAKE_CURRENT_BINARY_DIR}/..
)

add_dependencies(tst-gestureactioncatalog plugin-dde-shortcut)

target_link_libraries(tst-gestureactioncatalog PRIVATE
    Qt6::Core
    Qt6::Test
)

add_test(NAME shortcut-gestureactioncatalog COMMAND tst-gestureactioncatalog)

pkg_check_modules(WAYLAND_SERVER REQUIRED IMPORTED_TARGET wayland-server)
find_program(WAYLAND_SCANNER_EXECUTABLE NAMES wayland-scanner REQUIRED)
set(SHORTCUT_PROTOCOL_XML ${TREELAND_PROTOCOLS_DATA_DIR}/treeland-shortcut-manager-unstable-v3.xml)
set(SHORTCUT_SERVER_HEADER ${CMAKE_CURRENT_BINARY_DIR}/wayland-treeland-shortcut-manager-v3-server-protocol.h)
set(SHORTCUT_TEST_PROTOCOL_CODE ${CMAKE_CURRENT_BINARY_DIR}/wayland-shortcut-test-protocol.c)
pkg_get_variable(WAYLAND_PROTOCOLS_DATA_DIR wayland-protocols pkgdatadir)
set(SHORTCUT_TEST_XDG_XML ${WAYLAND_PROTOCOLS_DATA_DIR}/stable/xdg-shell/xdg-shell.xml)
set(SHORTCUT_TEST_XDG_HEADER ${CMAKE_CURRENT_BINARY_DIR}/wayland-shortcut-test-xdg-shell-server-protocol.h)
set(SHORTCUT_TEST_XDG_CODE ${CMAKE_CURRENT_BINARY_DIR}/wayland-shortcut-test-xdg-shell-protocol.c)
add_custom_command(
    OUTPUT ${SHORTCUT_SERVER_HEADER} ${SHORTCUT_TEST_PROTOCOL_CODE}
    COMMAND ${WAYLAND_SCANNER_EXECUTABLE} server-header ${SHORTCUT_PROTOCOL_XML} ${SHORTCUT_SERVER_HEADER}
    COMMAND ${WAYLAND_SCANNER_EXECUTABLE} private-code ${SHORTCUT_PROTOCOL_XML} ${SHORTCUT_TEST_PROTOCOL_CODE}
    DEPENDS ${SHORTCUT_PROTOCOL_XML}
    VERBATIM
)
add_custom_command(
    OUTPUT ${SHORTCUT_TEST_XDG_HEADER} ${SHORTCUT_TEST_XDG_CODE}
    COMMAND ${WAYLAND_SCANNER_EXECUTABLE} server-header ${SHORTCUT_TEST_XDG_XML} ${SHORTCUT_TEST_XDG_HEADER}
    COMMAND ${WAYLAND_SCANNER_EXECUTABLE} private-code ${SHORTCUT_TEST_XDG_XML} ${SHORTCUT_TEST_XDG_CODE}
    DEPENDS ${SHORTCUT_TEST_XDG_XML}
    VERBATIM
)
add_executable(tst-waylandshortcuts
    tst_waylandshortcuts.cpp
    ${SHORTCUT_SERVER_HEADER}
    ${SHORTCUT_TEST_PROTOCOL_CODE}
    ${SHORTCUT_TEST_XDG_HEADER}
    ${SHORTCUT_TEST_XDG_CODE}
)
target_include_directories(tst-waylandshortcuts PRIVATE
    ../src
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/..
)
target_link_libraries(tst-waylandshortcuts PRIVATE
    plugin-dde-shortcut
    Qt6::Core
    Qt6::Gui
    Qt6::Test
    Qt6::DBus
    Qt6::WaylandClient
    Dtk6::Core
    PkgConfig::WAYLAND_SERVER
)
find_program(DBUS_RUN_SESSION_EXECUTABLE NAMES dbus-run-session REQUIRED)
add_test(NAME shortcut-waylandshortcuts
    COMMAND ${DBUS_RUN_SESSION_EXECUTABLE} -- $<TARGET_FILE:tst-waylandshortcuts>
)
set_tests_properties(shortcut-waylandshortcuts PROPERTIES TIMEOUT 30)

add_executable(tst-qkeysequenceconverter
    tst_qkeysequenceconverter.cpp
    ../src/core/qkeysequenceconverter.cpp
)

target_include_directories(tst-qkeysequenceconverter PRIVATE
    ../src
)

target_link_libraries(tst-qkeysequenceconverter PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Test
)

add_test(NAME shortcut-qkeysequenceconverter COMMAND tst-qkeysequenceconverter)

add_executable(tst-specialkeyhandler
    tst_specialkeyhandler.cpp
    ../shortcutlogging.cpp
    ../src/backend/specialkeyhandler.cpp
)

target_include_directories(tst-specialkeyhandler PRIVATE
    ..
    ../src
)

target_link_libraries(tst-specialkeyhandler PRIVATE
    Qt6::Core
    Qt6::DBus
    Qt6::Test
)

add_test(NAME shortcut-specialkeyhandler COMMAND tst-specialkeyhandler)

add_executable(tst-crosschannelactivationguard
    tst_crosschannelactivationguard.cpp
    ../src/core/crosschannelactivationguard.cpp
)

target_include_directories(tst-crosschannelactivationguard PRIVATE
    ../src
)

target_link_libraries(tst-crosschannelactivationguard PRIVATE
    Qt6::Core
    Qt6::Test
)

add_test(NAME shortcut-crosschannelactivationguard COMMAND tst-crosschannelactivationguard)

add_executable(tst-modifierkeystate
    tst_modifierkeystate.cpp
    ../src/backend/x11/modifierkeystate.cpp
)

target_include_directories(tst-modifierkeystate PRIVATE
    ../src
)

target_link_libraries(tst-modifierkeystate PRIVATE
    Qt6::Core
    Qt6::Test
)

add_test(NAME shortcut-modifierkeystate COMMAND tst-modifierkeystate)

add_executable(tst-x11shortcutpolicy
    tst_x11shortcutpolicy.cpp
    ../src/backend/x11/x11shortcutpolicy.cpp
)

target_include_directories(tst-x11shortcutpolicy PRIVATE
    ../src
)

target_link_libraries(tst-x11shortcutpolicy PRIVATE
    Qt6::Core
    Qt6::Test
)

add_test(NAME shortcut-x11shortcutpolicy COMMAND tst-x11shortcutpolicy)

add_executable(tst-x11wmaccelconverter
    tst_x11wmaccelconverter.cpp
    ../src/backend/x11/x11wmaccelconverter.cpp
    ../src/core/qkeysequenceconverter.cpp
)

target_include_directories(tst-x11wmaccelconverter PRIVATE
    ../src
)

target_link_libraries(tst-x11wmaccelconverter PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Test
)

add_test(NAME shortcut-x11wmaccelconverter COMMAND tst-x11wmaccelconverter)

add_executable(tst-x11recordmonitor
    tst_x11recordmonitor.cpp
    ../shortcutlogging.cpp
    ../src/backend/x11/modifierkeystate.cpp
    ../src/backend/x11/modifierkeymonitor.cpp
)

target_include_directories(tst-x11recordmonitor PRIVATE
    ..
    ../src
    ${XCB_INCLUDE_DIRS}
    ${XCB_XINPUT_INCLUDE_DIRS}
    ${X11_INCLUDE_DIRS}
    ${X11_XCB_INCLUDE_DIRS}
    ${XTST_INCLUDE_DIRS}
)

target_link_libraries(tst-x11recordmonitor PRIVATE
    Qt6::Core
    Qt6::Test
    ${XCB_LIBRARIES}
    ${XCB_XINPUT_LIBRARIES}
    ${X11_LIBRARIES}
    ${X11_XCB_LIBRARIES}
    ${XTST_LIBRARIES}
)

add_executable(tst-x11grabresilientshortcuts
    tst_x11grabresilientshortcuts.cpp
)

target_include_directories(tst-x11grabresilientshortcuts PRIVATE
    ../src
    ${X11_INCLUDE_DIRS}
    ${XTST_INCLUDE_DIRS}
)

target_link_libraries(tst-x11grabresilientshortcuts PRIVATE
    Qt6::Core
    Qt6::Test
    plugin-dde-shortcut
    ${X11_LIBRARIES}
    ${XTST_LIBRARIES}
)

# XTestFakeKeyEvent injects into the whole X server. Never run these tests on
# the developer's active display, where desktop shortcuts would also fire.
find_program(XVFB_RUN_EXECUTABLE NAMES xvfb-run)
if (XVFB_RUN_EXECUTABLE)
    add_test(
        NAME shortcut-x11recordmonitor
        COMMAND ${XVFB_RUN_EXECUTABLE} -a
                -s "-screen 0 1280x1024x24 +extension RECORD"
                $<TARGET_FILE:tst-x11recordmonitor>
    )
    add_test(
        NAME shortcut-x11grabresilientshortcuts
        COMMAND ${XVFB_RUN_EXECUTABLE} -a
                -s "-screen 0 1280x1024x24 +extension RECORD"
                $<TARGET_FILE:tst-x11grabresilientshortcuts>
    )
else()
    message(WARNING "xvfb-run was not found; unsafe X11 input-injection tests are disabled")
endif()

qt_add_executable(tst-portalshortcutcontracts
    tst_portalshortcutcontracts.cpp
    ../src/core/portalshortcuttypes.cpp
    ../src/core/xdgshortcutparser.cpp
    ../src/core/qkeysequenceconverter.cpp
    ../src/config/portalshortcutstore.cpp
)

target_include_directories(tst-portalshortcutcontracts PRIVATE
    ../src
    ${XKBCOMMON_INCLUDE_DIRS}
)

target_compile_definitions(tst-portalshortcutcontracts PRIVATE
    PORTAL_SHORTCUT_INTERFACE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../interfaces/org.deepin.dde.Keybinding1.Portal.xml"
    PORTAL_SHORTCUT_SCHEMA_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../configs/org.deepin.shortcut.portal.json"
)

target_link_libraries(tst-portalshortcutcontracts PRIVATE
    Qt6::Core
    Qt6::DBus
    Qt6::Gui
    Qt6::Test
    Dtk6::Core
    ${XKBCOMMON_LIBRARIES}
    plugin-dde-shortcut
)

add_test(NAME shortcut-portal-contracts
         COMMAND ${DBUS_RUN_SESSION_EXECUTABLE} -- $<TARGET_FILE:tst-portalshortcutcontracts>)
set_tests_properties(shortcut-portal-contracts PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

qt_add_executable(tst-portalshortcutmanager
    tst_portalshortcutmanager.cpp
    ../src/core/portalshortcutmanager.cpp
    ../src/core/portalshortcuttypes.cpp
    ../src/core/xdgshortcutparser.cpp
    ../src/core/qkeysequenceconverter.cpp
    ../src/config/portalshortcutstore.cpp
)

target_include_directories(tst-portalshortcutmanager PRIVATE
    ../src
    ${XKBCOMMON_INCLUDE_DIRS}
)

target_link_libraries(tst-portalshortcutmanager PRIVATE
    Qt6::Core
    Qt6::DBus
    Qt6::Gui
    Qt6::Test
    Dtk6::Core
    ${XKBCOMMON_LIBRARIES}
)

add_test(NAME shortcut-portal-manager COMMAND tst-portalshortcutmanager)

qt_add_executable(tst-globalshortcutsprovidercontract
    tst_globalshortcutsprovidercontract.cpp
)

target_link_libraries(tst-globalshortcutsprovidercontract PRIVATE
    Qt6::Core
    Qt6::DBus
    Qt6::Test
)

target_compile_definitions(tst-globalshortcutsprovidercontract PRIVATE
    PROVIDER_EXECUTABLE_PATH="$<TARGET_FILE:dde-global-shortcuts-provider>"
    PROVIDER_SERVICE_FILE_PATH="${CMAKE_CURRENT_BINARY_DIR}/../org.deepin.dde.GlobalShortcutsProvider1.service"
    PROVIDER_SOURCE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../provider/main.cpp"
)

add_dependencies(tst-globalshortcutsprovidercontract dde-global-shortcuts-provider)
add_test(NAME shortcut-globalshortcuts-provider-contract
         COMMAND ${DBUS_RUN_SESSION_EXECUTABLE} -- $<TARGET_FILE:tst-globalshortcutsprovidercontract>)

qt_add_executable(tst-globalshortcutsprovider
    tst_globalshortcutsprovider.cpp
    ../provider/globalshortcutsprovider.cpp
    ../provider/globalshortcutsdialog.cpp
    ../provider/shortcutcapturecontroller.cpp
    ../provider/x11shortcutcapture.cpp
    ../provider/treelandshortcutcapture.cpp
    ../provider/waylandforeignparent.cpp
    ../src/core/portalshortcuttypes.cpp
    ../src/core/qkeysequenceconverter.cpp
)
qt6_generate_wayland_protocol_client_sources(tst-globalshortcutsprovider
    FILES
        ${TREELAND_PROTOCOLS_DATA_DIR}/treeland-shortcut-manager-unstable-v3.xml
        ${WAYLAND_PROTOCOLS_DATA_DIR}/unstable/xdg-foreign/xdg-foreign-unstable-v2.xml
)
target_include_directories(tst-globalshortcutsprovider PRIVATE
    ..
    ../src
    ${CMAKE_CURRENT_BINARY_DIR}
    ${XCB_INCLUDE_DIRS}
    ${X11_INCLUDE_DIRS}
)
target_link_libraries(tst-globalshortcutsprovider PRIVATE
    Qt6::Core
    Qt6::DBus
    Qt6::Gui
    Qt6::Widgets
    Qt6::Test
    Qt6::WaylandClient
    Qt6::WaylandClientPrivate
    ${XCB_LIBRARIES}
    ${X11_LIBRARIES}
    ${X11_XCB_LIBRARIES}
    ${WAYLAND_CLIENT_LIBRARIES}
)
add_dependencies(tst-globalshortcutsprovider dde-global-shortcuts-provider)
add_test(NAME shortcut-globalshortcuts-provider
    COMMAND ${DBUS_RUN_SESSION_EXECUTABLE} -- $<TARGET_FILE:tst-globalshortcutsprovider>
)
set_tests_properties(shortcut-globalshortcuts-provider PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 30
)

qt_add_executable(tst-x11shortcutcapture
    tst_x11shortcutcapture.cpp
    ../provider/x11shortcutcapture.cpp
)
target_include_directories(tst-x11shortcutcapture PRIVATE
    ${XCB_INCLUDE_DIRS}
    ${X11_INCLUDE_DIRS}
)
target_link_libraries(tst-x11shortcutcapture PRIVATE
    Qt6::Core
    Qt6::Test
    ${XCB_LIBRARIES}
    ${X11_LIBRARIES}
    ${X11_XCB_LIBRARIES}
    ${XTST_LIBRARIES}
)
if (XVFB_RUN_EXECUTABLE)
    add_test(NAME shortcut-x11shortcutcapture
        COMMAND ${XVFB_RUN_EXECUTABLE} -a $<TARGET_FILE:tst-x11shortcutcapture>
    )
    set_tests_properties(shortcut-x11shortcutcapture PROPERTIES TIMEOUT 10)
endif()
