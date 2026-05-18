# SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: LGPL-3.0-or-later

cmake_minimum_required(VERSION 3.16)

set(BIN_NAME "plugin-power-system")
project(${BIN_NAME})

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

include(GNUInstallDirs)

find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core DBus)
find_package(Dtk${DTK_VERSION_MAJOR} REQUIRED COMPONENTS Core)
find_package(PkgConfig REQUIRED)
pkg_check_modules(UDEV REQUIRED libudev)

file(GLOB_RECURSE SRCS "*.cpp" "*.h")

add_library(${BIN_NAME} MODULE ${SRCS})

target_include_directories(${BIN_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${UDEV_INCLUDE_DIRS}
)

target_link_libraries(${BIN_NAME} PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::DBus
    Dtk${DTK_VERSION_MAJOR}::Core
    ${UDEV_LIBRARIES}
)

install(TARGETS ${BIN_NAME}
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/deepin-service-manager/
)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/misc/plugin-power-system.json
    DESTINATION ${CMAKE_INSTALL_FULL_DATADIR}/deepin-service-manager/system/
)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/misc/org.deepin.dde.Power1.service
    DESTINATION lib/systemd/system/
)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/misc/dbus/org.deepin.dde.Power1.service
    DESTINATION ${CMAKE_INSTALL_FULL_DATADIR}/dbus-1/system-services/
)
