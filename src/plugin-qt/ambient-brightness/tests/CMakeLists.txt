# SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
# SPDX-License-Identifier: LGPL-3.0-or-later

find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(tst-ambientbrightnesspolicy
    tst_ambientbrightnesspolicy.cpp
    ../ambientbrightnessservice.cpp
    ../ambientbrightnesspolicyfactory.cpp
    ../ambientbrightnessmodel.cpp
    ../ambientbrightnesslogging.cpp
    ../brightnesscurve.cpp
    ../continuous/continuousambientlightpolicy.cpp
)

target_include_directories(tst-ambientbrightnesspolicy PRIVATE ..)

target_link_libraries(tst-ambientbrightnesspolicy PRIVATE
    Qt6::Core
    Qt6::DBus
    Dtk6::Core
    Qt6::Test
)

add_test(NAME ambient-brightness-policy COMMAND tst-ambientbrightnesspolicy)
