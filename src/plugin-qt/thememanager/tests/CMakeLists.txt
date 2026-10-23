# SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: LGPL-3.0-or-later

# thememanager unit tests (Qt6::Test). Plugged in from the plugin CMakeLists
# under BUILD_TESTING.

set(CMAKE_AUTOMOC ON)
set(PLUGIN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)

find_package(Qt6 REQUIRED COMPONENTS Core Test)

if(ENABLE_COVERAGE)
    add_compile_options(--coverage)
    add_link_options(--coverage)
endif()

# SunriseSunset pure-math unit test
add_executable(tst-sunrisesunset
    tst_sunrisesunset.cpp
    ${PLUGIN_DIR}/sunrisesunset.cpp
)
target_include_directories(tst-sunrisesunset PRIVATE ${PLUGIN_DIR})
target_link_libraries(tst-sunrisesunset PRIVATE Qt6::Core Qt6::Test)
add_test(NAME thememanager-sunrisesunset COMMAND tst-sunrisesunset)
