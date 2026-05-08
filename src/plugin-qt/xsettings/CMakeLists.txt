# SPDX-FileCopyrightText: 2023 - 2026 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: LGPL-3.0-or-later
set(PLUGIN_NAME "plugin-dde-xsettings")

project(${PLUGIN_NAME})

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

include(GNUInstallDirs)
file(GLOB_RECURSE SRCS "*.h" "*.cpp")

find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core DBus)
find_package(Dtk${DTK_VERSION_MAJOR} REQUIRED COMPONENTS Core)
find_package(Dtk${DTK_VERSION_MAJOR}DConfig REQUIRED)
find_package(X11 REQUIRED)
find_package(XCB REQUIRED)
find_package(XCB REQUIRED COMPONENTS RANDR)

add_library(${PLUGIN_NAME} MODULE
    ${SRCS}
)

target_link_libraries(${PLUGIN_NAME}
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::DBus
    X11::X11
    XCB::XCB
    XCB::RANDR
    Dtk${DTK_VERSION_MAJOR}::Core
)
macro(install_user_symlink filepath wantsdir)
    file(MAKE_DIRECTORY ${PROJECT_BINARY_DIR}/link/${wantsdir}/)
    execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink ${CMAKE_INSTALL_PREFIX}/lib/systemd/user/${filepath} ${PROJECT_BINARY_DIR}/link/${wantsdir}/${filepath})
    install(FILES ${PROJECT_BINARY_DIR}/link/${wantsdir}/${filepath} DESTINATION lib/systemd/user/${wantsdir}/)
endmacro(install_user_symlink)

install(TARGETS ${PLUGIN_NAME} DESTINATION ${CMAKE_INSTALL_LIBDIR}/deepin-service-manager/)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/misc/plugin-dde-xsettings.json DESTINATION ${CMAKE_INSTALL_FULL_DATADIR}/deepin-service-manager/user)
dtk_add_config_meta_files(
    APPID org.deepin.dde.daemon
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/misc/org.deepin.XSettings.json
)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/misc/dbus/org.deepin.dde.XSettings1.service DESTINATION ${CMAKE_INSTALL_FULL_DATADIR}/dbus-1/services)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/misc/org.deepin.dde.XSettings1.service
    DESTINATION lib/systemd/user/
    RENAME org.deepin.dde.XSettings1.service
)
install_user_symlink(org.deepin.dde.XSettings1.service dde-session-pre.target.wants)
