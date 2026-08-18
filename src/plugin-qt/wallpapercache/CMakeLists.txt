# SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: LGPL-3.0-or-later

cmake_minimum_required(VERSION 3.16)

set(PLUGIN_NAME "plugin-qt-wallpapercache")

project(${PLUGIN_NAME})

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

include(GNUInstallDirs)
file(GLOB_RECURSE SRCS "*.h" "*.cpp")

find_package(PkgConfig REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui DBus)
pkg_check_modules(DtkCore REQUIRED dtk6core)
pkg_check_modules(DtkGui REQUIRED dtk6gui)

add_library(${PLUGIN_NAME} MODULE
    ${SRCS}
)

target_include_directories(${PLUGIN_NAME} PRIVATE
    ${DtkCore_INCLUDE_DIRS}
    ${DtkGui_INCLUDE_DIRS}
)

target_link_libraries(${PLUGIN_NAME} PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::DBus
    ${DtkCore_LIBRARIES}
    ${DtkGui_LIBRARIES}
)

target_compile_options(${PLUGIN_NAME} PRIVATE
    ${DtkCore_CFLAGS_OTHER}
    ${DtkGui_CFLAGS_OTHER}
)

set(MISC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/misc)

install(TARGETS ${PLUGIN_NAME} DESTINATION ${CMAKE_INSTALL_LIBDIR}/deepin-service-manager/)
# Plugin descriptor for deepin-service-manager (system bus)
install(FILES ${MISC_DIR}/plugin-qt-wallpapercache.json DESTINATION share/deepin-service-manager/system/)
# D-Bus access control policy files
install(FILES
    ${MISC_DIR}/org.deepin.dde.WallpaperCache.conf
    ${MISC_DIR}/org.deepin.dde.ImageEffect1.conf
    ${MISC_DIR}/org.deepin.dde.ImageBlur1.conf
    DESTINATION share/dbus-1/system.d/
)
# D-Bus activation service files (required for on-demand activation)
install(FILES
    ${MISC_DIR}/org.deepin.dde.WallpaperCache.service
    ${MISC_DIR}/org.deepin.dde.ImageEffect1.service
    ${MISC_DIR}/org.deepin.dde.ImageBlur1.service
    DESTINATION share/dbus-1/system-services/
)
# systemd drop-in override (User, CacheDirectory, sandbox settings)
install(FILES
    ${MISC_DIR}/deepin-service-plugin@org.deepin.dde.WallpaperCache.service.d/override.conf
    DESTINATION lib/systemd/system/deepin-service-plugin@org.deepin.dde.WallpaperCache.service.d/
)
