# SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: LGPL-3.0-or-later

find_package(Qt6 REQUIRED COMPONENTS Core DBus Test)

set(CMAKE_AUTOMOC ON)

# BatteryDevice (system): battery sysfs parsing math + D-Bus object-path escaping.
# Self-contained — compiles batterydevice.cpp directly, no production source changes.
add_executable(tst-batterydevice
    tst_batterydevice.cpp
    ../system/batterydevice.cpp
)

target_include_directories(tst-batterydevice PRIVATE
    ../system
    ..
)

target_link_libraries(tst-batterydevice PRIVATE
    Qt6::Core
    Qt6::DBus
    Qt6::Test
)

add_test(NAME power-batterydevice COMMAND tst-batterydevice)
