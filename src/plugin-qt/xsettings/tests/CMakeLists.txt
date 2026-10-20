# SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: LGPL-3.0-or-later

find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(tst-xftdpiupdate
    tst_xftdpiupdate.cpp
)

target_include_directories(tst-xftdpiupdate PRIVATE
    ..
)

target_link_libraries(tst-xftdpiupdate PRIVATE
    Qt6::Core
    Qt6::Test
)

add_test(NAME xsettings-xftdpiupdate COMMAND tst-xftdpiupdate)
