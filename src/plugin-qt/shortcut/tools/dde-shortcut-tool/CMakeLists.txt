# SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: LGPL-3.0-or-later

# Include GNUInstallDirs for standard installation paths
include(GNUInstallDirs)

find_package(Qt6 REQUIRED COMPONENTS Core DBus)
find_package(Dtk6 REQUIRED COMPONENTS Core)
find_package(PkgConfig REQUIRED)

# XCB library for X11 lock key state queries
pkg_check_modules(XCB REQUIRED xcb xcb-keysyms)

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
    constant.h
)

target_link_libraries(dde-shortcut-tool PRIVATE
    Qt6::Core
    Qt6::DBus
    Dtk6::Core
    ${XCB_LIBRARIES}
)

target_include_directories(dde-shortcut-tool PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${XCB_INCLUDE_DIRS}
)

install(TARGETS dde-shortcut-tool 
    DESTINATION ${CMAKE_INSTALL_BINDIR}
)
