# SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: LGPL-3.0-or-later

find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(tst-brightnesspolicy
    tst_brightnesspolicy.cpp
)

target_include_directories(tst-brightnesspolicy PRIVATE
    ../tools/dde-shortcut-tool
    $<TARGET_PROPERTY:dde-shortcut-tool,BINARY_DIR>
)

target_link_libraries(tst-brightnesspolicy PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Test
    Qt6::WaylandClient
)

add_dependencies(tst-brightnesspolicy dde-shortcut-tool)

add_test(NAME shortcut-brightnesspolicy COMMAND tst-brightnesspolicy)

add_executable(tst-physicalkeyalias
    tst_physicalkeyalias.cpp
    ../src/core/physicalkeyalias.cpp
)

target_include_directories(tst-physicalkeyalias PRIVATE
    ../src
)

target_link_libraries(tst-physicalkeyalias PRIVATE
    Qt6::Core
    Qt6::Test
)

add_test(NAME shortcut-physicalkeyalias COMMAND tst-physicalkeyalias)

add_executable(tst-triggeractioncatalog
    tst_triggeractioncatalog.cpp
    ../src/core/triggeractioncatalog.cpp
)

target_include_directories(tst-triggeractioncatalog PRIVATE
    ../src
)

target_link_libraries(tst-triggeractioncatalog PRIVATE
    Qt6::Core
    Qt6::Test
)

add_test(NAME shortcut-triggeractioncatalog COMMAND tst-triggeractioncatalog)

add_executable(tst-gestureactioncatalog
    tst_gestureactioncatalog.cpp
    ../src/core/triggeractioncatalog.cpp
    ../src/core/gestureactioncatalog.cpp
)

target_include_directories(tst-gestureactioncatalog PRIVATE
    ../src
)

target_link_libraries(tst-gestureactioncatalog PRIVATE
    Qt6::Core
    Qt6::Test
)

add_test(NAME shortcut-gestureactioncatalog COMMAND tst-gestureactioncatalog)

add_executable(tst-qkeysequenceconverter
    tst_qkeysequenceconverter.cpp
    ../src/core/qkeysequenceconverter.cpp
)

target_include_directories(tst-qkeysequenceconverter PRIVATE
    ../src
)

target_link_libraries(tst-qkeysequenceconverter PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Test
)

add_test(NAME shortcut-qkeysequenceconverter COMMAND tst-qkeysequenceconverter)

add_executable(tst-modifierkeystate
    tst_modifierkeystate.cpp
    ../src/backend/x11/modifierkeystate.cpp
)

target_include_directories(tst-modifierkeystate PRIVATE
    ../src
)

target_link_libraries(tst-modifierkeystate PRIVATE
    Qt6::Core
    Qt6::Test
)

add_test(NAME shortcut-modifierkeystate COMMAND tst-modifierkeystate)

add_executable(tst-x11shortcutpolicy
    tst_x11shortcutpolicy.cpp
    ../src/backend/x11/x11shortcutpolicy.cpp
)

target_include_directories(tst-x11shortcutpolicy PRIVATE
    ../src
)

target_link_libraries(tst-x11shortcutpolicy PRIVATE
    Qt6::Core
    Qt6::Test
)

add_test(NAME shortcut-x11shortcutpolicy COMMAND tst-x11shortcutpolicy)

add_executable(tst-x11wmaccelconverter
    tst_x11wmaccelconverter.cpp
    ../src/backend/x11/x11wmaccelconverter.cpp
    ../src/core/qkeysequenceconverter.cpp
)

target_include_directories(tst-x11wmaccelconverter PRIVATE
    ../src
)

target_link_libraries(tst-x11wmaccelconverter PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Test
)

add_test(NAME shortcut-x11wmaccelconverter COMMAND tst-x11wmaccelconverter)

add_executable(tst-x11recordmonitor
    tst_x11recordmonitor.cpp
    ../shortcutlogging.cpp
    ../src/backend/x11/modifierkeystate.cpp
    ../src/backend/x11/modifierkeymonitor.cpp
)

target_include_directories(tst-x11recordmonitor PRIVATE
    ..
    ../src
    ${XCB_INCLUDE_DIRS}
    ${XCB_XINPUT_INCLUDE_DIRS}
    ${X11_INCLUDE_DIRS}
    ${X11_XCB_INCLUDE_DIRS}
    ${XTST_INCLUDE_DIRS}
)

target_link_libraries(tst-x11recordmonitor PRIVATE
    Qt6::Core
    Qt6::Test
    ${XCB_LIBRARIES}
    ${XCB_XINPUT_LIBRARIES}
    ${X11_LIBRARIES}
    ${X11_XCB_LIBRARIES}
    ${XTST_LIBRARIES}
)

add_executable(tst-x11grabresilientshortcuts
    tst_x11grabresilientshortcuts.cpp
)

target_include_directories(tst-x11grabresilientshortcuts PRIVATE
    ../src
    ${X11_INCLUDE_DIRS}
    ${XTST_INCLUDE_DIRS}
)

target_link_libraries(tst-x11grabresilientshortcuts PRIVATE
    Qt6::Core
    Qt6::Test
    plugin-dde-shortcut
    ${X11_LIBRARIES}
    ${XTST_LIBRARIES}
)

# XTestFakeKeyEvent injects into the whole X server. Never run these tests on
# the developer's active display, where desktop shortcuts would also fire.
find_program(XVFB_RUN_EXECUTABLE NAMES xvfb-run)
if (XVFB_RUN_EXECUTABLE)
    add_test(
        NAME shortcut-x11recordmonitor
        COMMAND ${XVFB_RUN_EXECUTABLE} -a
                -s "-screen 0 1280x1024x24 +extension RECORD"
                $<TARGET_FILE:tst-x11recordmonitor>
    )
    add_test(
        NAME shortcut-x11grabresilientshortcuts
        COMMAND ${XVFB_RUN_EXECUTABLE} -a
                -s "-screen 0 1280x1024x24 +extension RECORD"
                $<TARGET_FILE:tst-x11grabresilientshortcuts>
    )
else()
    message(WARNING "xvfb-run was not found; unsafe X11 input-injection tests are disabled")
endif()
