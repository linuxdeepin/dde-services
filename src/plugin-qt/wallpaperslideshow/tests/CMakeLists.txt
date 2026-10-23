# SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: LGPL-3.0-or-later

# wallpaperslideshow unit + D-Bus contract tests (Qt6::Test).
# Plugged in from the plugin CMakeLists under BUILD_TESTING.

set(CMAKE_AUTOMOC ON)
set(PLUGIN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)

find_package(Qt6 REQUIRED COMPONENTS Core DBus Gui Test)

if(ENABLE_COVERAGE)
    add_compile_options(--coverage)
    add_link_options(--coverage)
endif()

# utils helper unit test
add_executable(tst-wpssl-utils
    tst_wpssl_utils.cpp
    ${PLUGIN_DIR}/utils.cpp
)
target_include_directories(tst-wpssl-utils PRIVATE ${PLUGIN_DIR})
target_link_libraries(tst-wpssl-utils PRIVATE Qt6::Core Qt6::DBus Qt6::Test)
add_test(NAME wallpaperslideshow-utils COMMAND tst-wpssl-utils)
# writeWallpaperConfig writes to a path derived from XDG_CONFIG_HOME at
# static-init time; redirect it to a throwaway build-dir path so the test
# does not pollute the real user config dir. (The test slot itself QSKIPs
# when XDG_CONFIG_HOME is unset, e.g. when the binary is run directly.)
set_tests_properties(wallpaperslideshow-utils PROPERTIES
    ENVIRONMENT "XDG_CONFIG_HOME=${CMAKE_BINARY_DIR}/dde_svc_test_xdgconfig")

# FormatPicture MIME mapping unit test
add_executable(tst-format
    tst_format.cpp
    ${PLUGIN_DIR}/background/format.cpp
)
target_include_directories(tst-format PRIVATE ${PLUGIN_DIR}/background)
target_link_libraries(tst-format PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME wallpaperslideshow-format COMMAND tst-format)

# org.deepin.dde.WallpaperSlideshow D-Bus contract test: generate the
# production adaptor from the introspection XML around the test-side fake, so
# the interface contract is validated against a real D-Bus connection.
qt_add_dbus_adaptor(WSS_ADAPTOR_SOURCES
    ${PLUGIN_DIR}/org.deepin.dde.WallpaperSlideshow.xml
    ${CMAKE_CURRENT_SOURCE_DIR}/fakeservice.h
    FakeWallpaperSlideshowService
    wallpaperslideshowadaptor
    WallpaperSlideshowAdaptor
)

add_executable(tst-wallpaperslideshow-dbus
    tst_wallpaperslideshow_dbus.cpp
    ${WSS_ADAPTOR_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/fakeservice.h
)
target_include_directories(tst-wallpaperslideshow-dbus PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)
target_link_libraries(tst-wallpaperslideshow-dbus PRIVATE
    Qt6::Core
    Qt6::DBus
    Qt6::Test
)
add_test(NAME wallpaperslideshow-dbus COMMAND tst-wallpaperslideshow-dbus)
