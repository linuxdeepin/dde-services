# SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: LGPL-3.0-or-later

find_package(Qt6 REQUIRED COMPONENTS Core DBus Test)

set(CMAKE_AUTOMOC ON)

# BatteryDevice (system): battery sysfs parsing math + D-Bus object-path escaping.
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

# Warn-level policy (session): pure low-battery threshold computation, header-only.
add_executable(tst-warnlevel
    tst_warnlevel.cpp
)

target_include_directories(tst-warnlevel PRIVATE
    ../session
    ..
)

target_link_libraries(tst-warnlevel PRIVATE
    Qt6::Core
    Qt6::Test
)

add_test(NAME power-warnlevel COMMAND tst-warnlevel)

# Scheduled shutdown (session): next-shutdown time computation, header-only.
add_executable(tst-scheduledshutdown
    tst_scheduledshutdown.cpp
)

target_include_directories(tst-scheduledshutdown PRIVATE
    ../session
    ..
)

target_link_libraries(tst-scheduledshutdown PRIVATE
    Qt6::Core
    Qt6::Test
)

add_test(NAME power-scheduledshutdown COMMAND tst-scheduledshutdown)

# Power save plan (session): task coalescing + min-delay rules, header-only.
add_executable(tst-powersaveplan
    tst_powersaveplan.cpp
)

target_include_directories(tst-powersaveplan PRIVATE
    ../session
    ..
)

target_link_libraries(tst-powersaveplan PRIVATE
    Qt6::Core
    Qt6::Test
)

add_test(NAME power-powersaveplan COMMAND tst-powersaveplan)

# Power mode (system): DSPC mapping + low-battery decision, header-only.
add_executable(tst-powermode
    tst_powermode.cpp
)

target_include_directories(tst-powermode PRIVATE
    ../system
    ..
)

target_link_libraries(tst-powermode PRIVATE
    Qt6::Core
    Qt6::Test
)

add_test(NAME power-powermode COMMAND tst-powermode)

# Short idle (session): application blacklist + third-party blocking decision, header-only.
add_executable(tst-shortidle
    tst_shortidle.cpp
)

target_include_directories(tst-shortidle PRIVATE
    ../session
    ..
)

target_link_libraries(tst-shortidle PRIVATE
    Qt6::Core
    Qt6::Test
)

add_test(NAME power-shortidle COMMAND tst-shortidle)
