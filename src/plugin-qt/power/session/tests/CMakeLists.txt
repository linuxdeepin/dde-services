# SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
# SPDX-License-Identifier: LGPL-3.0-or-later

find_package(Qt6 REQUIRED COMPONENTS Test)
find_program(DBUS_DAEMON dbus-daemon REQUIRED)

add_executable(tst-screensaverproxy
    tst-screensaverproxy.cpp
    ../sessiondbusproxy.cpp
    ../sessiondbusproxy.h
)
target_include_directories(tst-screensaverproxy PRIVATE ..)
target_link_libraries(tst-screensaverproxy PRIVATE Qt6::Test Qt6::DBus Dtk6::Core)

# run-with-private-bus.sh, not dbus-run-session: the usec LSM denies signals to
# dbus-daemon, which would make dbus-run-session hang and the test time out.
add_test(NAME power-screensaver-proxy
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run-with-private-bus.sh
            ${CMAKE_CURRENT_SOURCE_DIR}/session.conf
            $<TARGET_FILE:tst-screensaverproxy>)
set_tests_properties(power-screensaver-proxy PROPERTIES TIMEOUT 20
    ENVIRONMENT "DBUS_DAEMON_BIN=${DBUS_DAEMON}")
