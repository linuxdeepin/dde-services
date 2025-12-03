# SPDX-FileCopyrightText: 2023 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: LGPL-3.0-or-later
set(BIN_NAME "plugin-dde-xsettings")

project(${BIN_NAME})

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

include(GNUInstallDirs)
file(GLOB_RECURSE SRCS "*.h" "*.cpp")

find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core DBus)
find_package(Dtk${DTK_VERSION_MAJOR} REQUIRED COMPONENTS Gui Core)
find_package(PkgConfig REQUIRED)

pkg_check_modules(XCB REQUIRED IMPORTED_TARGET xcb)

add_library(${BIN_NAME} MODULE
    ${SRCS}
)

target_link_libraries(${BIN_NAME}
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::DBus
    PkgConfig::XCB
    Dtk${DTK_VERSION_MAJOR}::Core
)

install(TARGETS ${BIN_NAME} DESTINATION ${CMAKE_INSTALL_LIBDIR}/deepin-service-manager/)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/misc/plugin-dde-xsettings.json DESTINATION ${CMAKE_INSTALL_FULL_DATADIR}/deepin-service-manager/user/)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/misc/org.deepin.XSettings.json DESTINATION ${CMAKE_INSTALL_FULL_DATADIR}/dsg/configs/org.deepin.dde.daemon)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/misc/org.deepin.dde.XSettings.service DESTINATION ${CMAKE_INSTALL_FULL_DATADIR}/dbus-1/services/)
