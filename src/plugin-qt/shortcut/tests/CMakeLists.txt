# SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: LGPL-3.0-or-later

find_package(Qt6 REQUIRED COMPONENTS Test)

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
