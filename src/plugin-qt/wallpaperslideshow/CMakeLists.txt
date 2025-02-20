# SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: LGPL-3.0-or-later

cmake_minimum_required(VERSION 3.13)

set(BIN_NAME "plugin-qt-wallpaperslideshow")

project(${BIN_NAME})

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

include(GNUInstallDirs)
file(GLOB_RECURSE SRCS "*.h" "*.cpp")

find_package(Qt5 COMPONENTS Core DBus REQUIRED)
find_package(DtkCore REQUIRED)
find_package(DtkGui REQUIRED)

qt5_add_dbus_adaptor(ADAPTER_SOURCES
                     org.deepin.dde.WallpaperSlideshow.xml
                     wallpaperslideshow.h
                     WallpaperSlideshow)

add_library(${BIN_NAME} MODULE
    ${SRCS}
    ${ADAPTER_SOURCES}
)

target_include_directories(${BIN_NAME} PUBLIC
  Qt5::Core
  Qt5::DBus
)

target_link_libraries(${BIN_NAME} PRIVATE
  Qt5::Core
  Qt5::DBus
  ${DtkCore_LIBRARIES}
  ${DtkGui_LIBRARIES}
)

install(TARGETS ${BIN_NAME} DESTINATION ${CMAKE_INSTALL_LIBDIR}/deepin-service-manager/)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/plugin-qt-wallpaperslideshow.json DESTINATION share/deepin-service-manager/user/)
