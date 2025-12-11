# SPDX-FileCopyrightText: 2023 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: LGPL-3.0-or-later
set(BIN_NAME "dde-xsettings")

project(${BIN_NAME})

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

include(GNUInstallDirs)
file(GLOB_RECURSE SRCS "*.h" "*.cpp")

find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core DBus)
find_package(Dtk${DTK_VERSION_MAJOR} REQUIRED COMPONENTS Gui Core)
find_package(PkgConfig REQUIRED)

pkg_check_modules(X11 REQUIRED IMPORTED_TARGET x11)
pkg_check_modules(XCB REQUIRED IMPORTED_TARGET xcb)

add_executable(${BIN_NAME}
    ${SRCS}
)

target_link_libraries(${BIN_NAME}
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::DBus
    PkgConfig::X11
    PkgConfig::XCB
    Dtk${DTK_VERSION_MAJOR}::Core
)
macro(install_user_symlink filepath wantsdir)
    file(MAKE_DIRECTORY ${PROJECT_BINARY_DIR}/link/${wantsdir}/)
    execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink ${CMAKE_INSTALL_PREFIX}/lib/systemd/user/${filepath} ${PROJECT_BINARY_DIR}/link/${wantsdir}/${filepath})
    install(FILES ${PROJECT_BINARY_DIR}/link/${wantsdir}/${filepath} DESTINATION lib/systemd/user/${wantsdir}/)
endmacro(install_user_symlink)

install(TARGETS ${BIN_NAME} DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/misc/dde-xsettings.json DESTINATION ${CMAKE_INSTALL_FULL_DATADIR}/deepin-service-manager/other)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/misc/org.deepin.XSettings.json DESTINATION ${CMAKE_INSTALL_FULL_DATADIR}/dsg/configs/org.deepin.dde.daemon)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/misc/dbus/org.deepin.dde.XSettings.service DESTINATION ${CMAKE_INSTALL_FULL_DATADIR}/dbus-1/services)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/misc/org.deepin.dde.XSettings.service
    DESTINATION lib/systemd/user/
    RENAME org.deepin.dde.XSettings.service
)
install_user_symlink(org.deepin.dde.XSettings.service dde-session-pre.target.wants)
