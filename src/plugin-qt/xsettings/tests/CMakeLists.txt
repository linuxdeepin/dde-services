# SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: LGPL-3.0-or-later

# xsettings unit tests (Qt6::Test). Plugged in from the plugin CMakeLists
# under BUILD_TESTING.

set(CMAKE_AUTOMOC ON)
set(PLUGIN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)

find_package(Qt6 REQUIRED COMPONENTS Core Test)

if(ENABLE_COVERAGE)
    add_compile_options(--coverage)
    add_link_options(--coverage)
endif()

# KeyFile ini/.desktop parser unit test
add_executable(tst-keyfile
    tst_keyfile.cpp
    ${PLUGIN_DIR}/modules/api/keyfile.cpp
)
target_include_directories(tst-keyfile PRIVATE ${PLUGIN_DIR}/modules/api)
target_link_libraries(tst-keyfile PRIVATE Qt6::Core Qt6::Test)
add_test(NAME xsettings-keyfile COMMAND tst-keyfile)

# Utils byte-manipulation helper unit test
add_executable(tst-xsutils
    tst_xsutils.cpp
    ${PLUGIN_DIR}/modules/api/utils.cpp
)
target_include_directories(tst-xsutils PRIVATE
    ${PLUGIN_DIR}/modules/api
    ${PLUGIN_DIR}/modules
)
target_link_libraries(tst-xsutils PRIVATE Qt6::Core Qt6::Test)
add_test(NAME xsettings-xsutils COMMAND tst-xsutils)

# XSItemInfo/XSDataInfo marshal/unmarshal unit test. Pure byte-serialization
# layer (XSETTINGS wire format); no D-Bus/DConfig/XCB dependencies.
add_executable(tst-xsdatainfo
    tst_xsdatainfo.cpp
    ${PLUGIN_DIR}/impl/xsdatainfo.cpp
    ${PLUGIN_DIR}/modules/api/utils.cpp
)
target_include_directories(tst-xsdatainfo PRIVATE
    ${PLUGIN_DIR}/impl
    ${PLUGIN_DIR}
    ${PLUGIN_DIR}/modules/api
    ${PLUGIN_DIR}/modules
)
target_link_libraries(tst-xsdatainfo PRIVATE Qt6::Core Qt6::Test)
add_test(NAME xsettings-xsdatainfo COMMAND tst-xsdatainfo)
