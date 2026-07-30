# SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
# SPDX-License-Identifier: LGPL-3.0-or-later

cmake_minimum_required(VERSION 3.16)
project(plugin-ambient-brightness LANGUAGES CXX)

include(GNUInstallDirs)
if(NOT DEFINED QT_VERSION_MAJOR)
    set(QT_VERSION_MAJOR 6)
endif()
if(NOT DEFINED DTK_VERSION_MAJOR)
    set(DTK_VERSION_MAJOR 6)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PLUGIN_NAME "plugin-ambient-brightness")

find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core DBus)
find_package(Dtk${DTK_VERSION_MAJOR} REQUIRED COMPONENTS Core DConfig)

set(CMAKE_AUTOMOC ON)

add_library(${PLUGIN_NAME} MODULE
    plugin.cpp
    ambientbrightnessservice.cpp
    ambientbrightnessservice.h
    ambientlightlifecyclestate.h
    ambientbrightnesspolicyfactory.cpp
    ambientbrightnesspolicyfactory.h
    ambientbrightnesspolicy.h
    ambientbrightnessmodel.cpp
    ambientbrightnessmodel.h
    continuous/continuousambientlightpolicy.cpp
    continuous/continuousambientlightpolicy.h
    brightnesscurve.cpp
    brightnesscurve.h
    ambientbrightnesslogging.cpp
    ambientbrightnesslogging.h
)

target_include_directories(${PLUGIN_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(${PLUGIN_NAME} PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::DBus
    Dtk${DTK_VERSION_MAJOR}::Core
)

install(TARGETS ${PLUGIN_NAME}
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/deepin-service-manager/
)
install(FILES misc/plugin-ambient-brightness.json
    DESTINATION ${CMAKE_INSTALL_DATADIR}/deepin-service-manager/user/
)
install(FILES misc/ambient-brightness.service
    DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services/
)

install(FILES configs/org.deepin.dde.daemon.ambient-brightness.json
    DESTINATION ${CMAKE_INSTALL_DATADIR}/dsg/configs/org.deepin.dde.daemon/
)


if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
