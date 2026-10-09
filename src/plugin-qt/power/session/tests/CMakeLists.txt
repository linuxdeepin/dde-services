# SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
# SPDX-License-Identifier: LGPL-3.0-or-later

find_package(Qt6 REQUIRED COMPONENTS Test)
find_program(DBUS_RUN_SESSION dbus-run-session REQUIRED)

add_executable(tst-screensaverproxy
    tst-screensaverproxy.cpp
    ../sessiondbusproxy.cpp
    ../sessiondbusproxy.h
)
target_include_directories(tst-screensaverproxy PRIVATE ..)
target_link_libraries(tst-screensaverproxy PRIVATE Qt6::Test Qt6::DBus Dtk6::Core)
add_test(NAME power-screensaver-proxy
    COMMAND ${DBUS_RUN_SESSION} --config-file=${CMAKE_CURRENT_SOURCE_DIR}/session.conf
            -- $<TARGET_FILE:tst-screensaverproxy>)
set_tests_properties(power-screensaver-proxy PROPERTIES TIMEOUT 20)
