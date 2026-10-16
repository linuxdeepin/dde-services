# SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: LGPL-3.0-or-later

cmake_minimum_required(VERSION 3.16)
project(test_wallpaper_cache)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core DBus Gui)

add_executable(test_wallpaper_cache test_wallpaper_cache.cpp)

target_link_libraries(test_wallpaper_cache
    Qt6::Core
    Qt6::DBus
    Qt6::Gui
)

target_compile_definitions(test_wallpaper_cache PRIVATE QT_MESSAGELOGCONTEXT)

message(STATUS "Test build: test_wallpaper_cache")
message(STATUS "Run: sudo ./test_wallpaper_cache [wallpaper_path]")

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(DtkGui REQUIRED IMPORTED_TARGET dtk6gui)
    set(WALLPAPER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")
    add_executable(test_wallpaper_cache_unit
        test_wallpaper_cache_unit.cpp
        ${WALLPAPER_SOURCE_DIR}/wallpapercacheservice.cpp
        ${WALLPAPER_SOURCE_DIR}/cachedwallpaper.cpp
        ${WALLPAPER_SOURCE_DIR}/wallpapercachemanager.cpp
        ${WALLPAPER_SOURCE_DIR}/scaleimagethread.cpp
        ${WALLPAPER_SOURCE_DIR}/imageeffectprocessor.cpp
    )
    target_include_directories(test_wallpaper_cache_unit PRIVATE ${WALLPAPER_SOURCE_DIR})
    target_compile_definitions(test_wallpaper_cache_unit PRIVATE
        WALLPAPER_CACHE_DIR="${CMAKE_CURRENT_BINARY_DIR}/unit-cache")
    target_link_libraries(test_wallpaper_cache_unit PRIVATE
        Qt6::Core Qt6::DBus Qt6::Gui Qt6::Test PkgConfig::DtkGui)
    add_test(NAME wallpaper-cache-unit COMMAND test_wallpaper_cache_unit)
endif()
