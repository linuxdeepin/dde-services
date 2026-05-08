# SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: LGPL-3.0-or-later

set(PLUGIN_NAME "plugin-dde-screenscale")

project(${PLUGIN_NAME})

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

include(GNUInstallDirs)
file(GLOB_RECURSE SRCS "*.h" "*.cpp")

find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core DBus)
find_package(Dtk${DTK_VERSION_MAJOR} REQUIRED COMPONENTS Core)
find_package(Dtk${DTK_VERSION_MAJOR}DConfig REQUIRED)

add_library(${PLUGIN_NAME} MODULE
    ${SRCS}
)

target_link_libraries(${PLUGIN_NAME}
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::DBus
    Dtk${DTK_VERSION_MAJOR}::Core
)

# Install module
install(TARGETS ${PLUGIN_NAME} DESTINATION ${CMAKE_INSTALL_LIBDIR}/deepin-service-manager/)

# Install dbus system service file
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/misc/dbus/org.deepin.dde.ScreenScale1.service DESTINATION ${CMAKE_INSTALL_FULL_DATADIR}/dbus-1/system-services)

# Install dbus policy config
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/misc/dbus/org.deepin.dde.ScreenScale1.conf DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/dbus-1/system.d)

# Install plugin config to system directory
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/misc/plugin-dde-screenscale.json DESTINATION ${CMAKE_INSTALL_FULL_DATADIR}/deepin-service-manager/system)

# Install dconf config meta
dtk_add_config_meta_files(
    APPID org.deepin.dde.daemon
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/misc/org.deepin.dde.ScreenScale1.json
)
