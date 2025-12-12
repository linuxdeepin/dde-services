# SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: LGPL-3.0-or-later

cmake_minimum_required(VERSION 3.13)

project(ipwatchd-plugin)

# Find required packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(Systemd REQUIRED IMPORTED_TARGET libsystemd)
pkg_check_modules(PCAP REQUIRED libpcap)
find_library(NET_LIB net)

include_directories(${PCAP_INCLUDE_DIRS})
link_directories(${PCAP_LIBRARY_DIRS})

# Upstream ipwatchd core sources
set(IPWATCHD_CORE_SOURCES
    upstream/analyse.c
    upstream/config.c
    upstream/devinfo.c
    upstream/genarp.c
    upstream/message.c
    upstream/signal.c
    upstream/daemonize.c
)

# Hook sources
set(IPWATCHD_HOOKS_SOURCES
    hooks/ipwatchd_hooks.c
)

# Plugin sources
set(PLUGIN_SOURCES
    plugin/plugin_adapter.c
    plugin/service.c
    plugin/plugin.c
)

# Target 1: Standalone ipwatchd executable (for debugging)
add_executable(ipwatchd
    upstream/ipwatchd.c
    ${IPWATCHD_CORE_SOURCES}
    ${IPWATCHD_HOOKS_SOURCES}
)

target_link_libraries(ipwatchd
    ${PCAP_LIBRARIES}
    ${NET_LIB}
)

# Target 2: Plugin library
set(Plugin_NAME "plugin-ipwatchd")

add_library(${Plugin_NAME} MODULE
    ${IPWATCHD_CORE_SOURCES}
    ${PLUGIN_SOURCES}
)

target_include_directories(${Plugin_NAME} PUBLIC
    PkgConfig::Systemd
)

target_link_libraries(${Plugin_NAME}
    PkgConfig::Systemd
    pthread
    ${PCAP_LIBRARIES}
    ${NET_LIB}
)

# Installation
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    # Debug mode: copy plugin config to build directory
    EXECUTE_PROCESS(COMMAND mkdir -p ${CMAKE_BINARY_DIR}/share/system/)
    EXECUTE_PROCESS(COMMAND cp -f ${CMAKE_CURRENT_SOURCE_DIR}/plugin-ipwatchd.json ${CMAKE_BINARY_DIR}/share/system/)
    target_compile_definitions(${Plugin_NAME} PRIVATE _DEBUG)
else()
    # Release mode: install plugin and config
    install(TARGETS ${Plugin_NAME} DESTINATION ${CMAKE_INSTALL_LIBDIR}/deepin-service-manager/)
    install(FILES plugin-ipwatchd.json DESTINATION share/deepin-service-manager/system/)
    
    # Optionally install standalone ipwatchd
    install(TARGETS ipwatchd DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# Install D-Bus configuration
install(FILES org.deepin.ipwatchd.conf
    DESTINATION share/dbus-1/system.d/
)

# Install drop-in configuration for deepin-service-group@deepin-daemon.service
configure_file(deepin-service-group@deepin-daemon.service.d/ipwatchd-override.conf.in 
    deepin-service-group@deepin-daemon.service.d/ipwatchd-override.conf @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/deepin-service-group@deepin-daemon.service.d/ipwatchd-override.conf
    DESTINATION lib/systemd/system/deepin-service-group@deepin-daemon.service.d/)

# Install ipwatchd configuration
install(FILES ipwatchd.conf
    DESTINATION /var/lib/ipwatchd/
)
