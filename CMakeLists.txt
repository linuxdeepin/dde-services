# SPDX-FileCopyrightText: 2023 - 2026 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: LGPL-3.0-or-later
cmake_minimum_required(VERSION 3.16)

set (VERSION "1.0.0" CACHE STRING "define project version")

project(dde-services)

include(CTest)

# Coverage instrumentation is applied by each module's tests/CMakeLists.txt,
# so only the test targets are instrumented; production plugins are unaffected.
option(ENABLE_COVERAGE "Enable coverage instrumentation for tests" ON)

if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX /usr)
endif()

include(GNUInstallDirs)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
# 设置二进制输出目录 方便调试
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lib")

set(QT_VERSION_MAJOR 6)
set(DTK_VERSION_MAJOR 6)

add_subdirectory("src")

if(BUILD_TESTING)
    # ---- coverage: execute all module tests and generate a coverage report ----
    find_program(LCOV_BIN lcov)
    find_program(GENHTML_BIN genhtml)
    set(_run_test_coverage "")
    if(LCOV_BIN AND GENHTML_BIN)
        set(_run_test_coverage
            COMMAND ${LCOV_BIN} --capture --directory ${CMAKE_BINARY_DIR}
                    --rc lcov_branch_coverage=1 --output-file ${CMAKE_BINARY_DIR}/coverage.info
            COMMAND ${LCOV_BIN} --extract ${CMAKE_BINARY_DIR}/coverage.info
                    --rc lcov_branch_coverage=1
                    "${CMAKE_SOURCE_DIR}/src/plugin-qt/thememanager/*"
                    "${CMAKE_SOURCE_DIR}/src/plugin-qt/wallpaperslideshow/*"
                    "${CMAKE_SOURCE_DIR}/src/plugin-qt/xsettings/*"
                    --output-file ${CMAKE_BINARY_DIR}/coverage_filtered.info
            COMMAND ${GENHTML_BIN} ${CMAKE_BINARY_DIR}/coverage_filtered.info
                    --branch-coverage --output-directory ${CMAKE_BINARY_DIR}/coverage_report
        )
    endif()
    add_custom_target(coverage
        # Scope to this suite: other tests registered by the project may not be
        # built when only this target's dependencies are requested.
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
                -R "^(thememanager-sunrisesunset|wallpaperslideshow-(utils|format|dbus)|xsettings-(keyfile|xsutils|xsdatainfo))$"
        ${_run_test_coverage}
        DEPENDS
            tst-sunrisesunset
            tst-wpssl-utils
            tst-format
            tst-wallpaperslideshow-dbus
            tst-keyfile
            tst-xsutils
            tst-xsdatainfo
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Run all tests and generate coverage report"
        VERBATIM
    )
endif()
