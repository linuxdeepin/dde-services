# SPDX-FileCopyrightText: 2025 - 2026 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: LGPL-3.0-or-later

set(PLUGIN_NAME "plugin-qt-wallpaperslideshow")

project(${PLUGIN_NAME})

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

include(GNUInstallDirs)
file(GLOB_RECURSE SRCS "*.h" "*.cpp")

find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core DBus)
find_package(Dtk${DTK_VERSION_MAJOR} REQUIRED COMPONENTS Gui Core)

qt_add_dbus_adaptor(ADAPTER_SOURCES
                     org.deepin.dde.WallpaperSlideshow.xml
                     wallpaperslideshow.h
                     WallpaperSlideshow)

add_library(${PLUGIN_NAME} MODULE
    ${SRCS}
    ${ADAPTER_SOURCES}
)

target_link_libraries(${PLUGIN_NAME}
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::DBus
    Dtk${DTK_VERSION_MAJOR}::Core
    Dtk${DTK_VERSION_MAJOR}::Gui
)

install(TARGETS ${PLUGIN_NAME} DESTINATION ${CMAKE_INSTALL_LIBDIR}/deepin-service-manager/)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/plugin-qt-wallpaperslideshow.json DESTINATION ${CMAKE_INSTALL_FULL_DATADIR}/deepin-service-manager/user)
