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
