# SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: LGPL-3.0-or-later

cmake_minimum_required(VERSION 3.16)

set(BIN_NAME "plugin-power-session")
project(${BIN_NAME})

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

include(GNUInstallDirs)

find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core DBus Gui WaylandClient LinguistTools)
find_package(Dtk${DTK_VERSION_MAJOR} REQUIRED COMPONENTS Core)
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols)
pkg_check_modules(WLR_PROTOCOLS REQUIRED wlr-protocols)
pkg_check_modules(TREELAND_PROTOCOLS REQUIRED treeland-protocols)
pkg_get_variable(WAYLAND_PROTOCOLS_DATADIR wayland-protocols pkgdatadir)
pkg_get_variable(WLR_PROTOCOLS_DATADIR wlr-protocols pkgdatadir)
pkg_get_variable(TREELAND_PROTOCOLS_DATADIR treeland-protocols pkgdatadir)

set(_src_idle
    idle/idlewatcher_wl.cpp
    idle/idlewatcher_x11.cpp
)
set(_src_screen
    screen/screencontroller.cpp
    screen/screencontroller_wl.cpp
    screen/screencontroller_x11.cpp
)
set(_src_core
    plugin-power-session.cpp
    powermanager.cpp
    sessiondbusproxy.cpp
    powersaveplan.cpp
    lidswitchhandler.cpp
    sleepinhibitor.cpp
    lowpowermanager.cpp
)
set(_src_headers
    idle/idlewatcher.h
    idle/idlewatcher_wl.h
    screen/screencontroller.h
    screen/screencontroller_wl.h
    powersaveplan.h
    lidswitchhandler.h
    sleepinhibitor.h
    lowpowermanager.h
    powermanager.h
    sessiondbusproxy.h
)

file(GLOB TS_FILES translations/*.ts)

add_library(${BIN_NAME} MODULE
    ${_src_idle} ${_src_screen} ${_src_core} ${_src_headers}
)

qt6_generate_wayland_protocol_client_sources(${BIN_NAME}
    FILES
    ${WAYLAND_PROTOCOLS_DATADIR}/staging/ext-idle-notify/ext-idle-notify-v1.xml
    ${WLR_PROTOCOLS_DATADIR}/unstable/wlr-output-power-management-unstable-v1.xml
    ${TREELAND_PROTOCOLS_DATADIR}/treeland-output-manager-v1.xml
)

qt_add_translations(${BIN_NAME}
    TS_FILES ${TS_FILES}
    QM_FILES_OUTPUT_VARIABLE QM_FILES
    LUPDATE_OPTIONS -no-ui-lines -locations none
)


target_include_directories(${BIN_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(${BIN_NAME} PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::DBus
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::WaylandClient
    Dtk${DTK_VERSION_MAJOR}::Core
)

install(TARGETS ${BIN_NAME}
DESTINATION ${CMAKE_INSTALL_LIBDIR}/deepin-service-manager/
)

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/misc/plugin-power-session.json
DESTINATION ${CMAKE_INSTALL_FULL_DATADIR}/deepin-service-manager/user/
)

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/misc/dbus/org.deepin.dde.Power1.service
DESTINATION ${CMAKE_INSTALL_FULL_DATADIR}/dbus-1/services/
)

install(FILES ${QM_FILES} 
DESTINATION ${CMAKE_INSTALL_DATADIR}/${BIN_NAME}/translations
)
