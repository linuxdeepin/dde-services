# SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: LGPL-3.0-or-later

# Include GNUInstallDirs for standard installation paths
include(GNUInstallDirs)

find_package(Qt6 REQUIRED COMPONENTS Core DBus Gui WaylandClient)
find_package(Dtk6 REQUIRED COMPONENTS Core Gui)
find_package(PkgConfig REQUIRED)
find_package(TreelandProtocols REQUIRED)

# XCB library for X11 lock key state queries
pkg_check_modules(XCB REQUIRED xcb xcb-keysyms)
pkg_check_modules(WAYLAND_CLIENT REQUIRED wayland-client)

# Define dde-shortcut-tool executable
add_executable(dde-shortcut-tool
    main.cpp
    commandparser.cpp
    commandparser.h
    basecontroller.h
    audiocontroller.cpp
    audiocontroller.h
    displaycontroller.cpp
    displaycontroller.h
    touchpadcontroller.cpp
    touchpadcontroller.h
    powercontroller.cpp
    powercontroller.h
    treelandbrightnesscontroller.cpp
    treelandbrightnesscontroller.h
    treelandlockscreenwrapper.cpp
    treelandlockscreenwrapper.h
    kbdlightcontroller.cpp
    kbdlightcontroller.h
    mediaplayercontroller.cpp
    mediaplayercontroller.h
    lockkeycontroller.cpp
    lockkeycontroller.h
    launchcontroller.cpp
    launchcontroller.h
    networkcontroller.cpp
    networkcontroller.h
    wmcontroller.cpp
    wmcontroller.h
    constant.h
)

# Generate Wayland client bindings for treeland-dde-shell-v1 (lockscreen
# operations: lock, shutdown UI, switch_user).
# NO_INCLUDE_CORE_ONLY pulls in the core protocol headers so generated code
# can reference wl_callback_interface and other core types.
qt6_generate_wayland_protocol_client_sources(dde-shortcut-tool
    NO_INCLUDE_CORE_ONLY
    FILES
        ${TREELAND_PROTOCOLS_DATA_DIR}/treeland-dde-shell-v1.xml
        ${TREELAND_PROTOCOLS_DATA_DIR}/treeland-output-manager-v1.xml
)

target_link_libraries(dde-shortcut-tool PRIVATE
    Qt6::Core
    Qt6::DBus
    Qt6::Gui
    Qt6::WaylandClient
    Dtk6::Core
    Dtk6::Gui
    ${XCB_LIBRARIES}
    ${WAYLAND_CLIENT_LIBRARIES}
)

target_include_directories(dde-shortcut-tool PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${XCB_INCLUDE_DIRS}
    ${WAYLAND_CLIENT_INCLUDE_DIRS}
)

install(TARGETS dde-shortcut-tool 
    DESTINATION ${CMAKE_INSTALL_BINDIR}
)
