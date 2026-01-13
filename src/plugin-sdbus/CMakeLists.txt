# SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: LGPL-3.0-or-later

option(ENABLE_PLUGIN_IPWATCHD "Build plugin-ipwatchd" OFF)

if (ENABLE_PLUGIN_IPWATCHD)
    add_subdirectory("plugin-ipwatchd")
endif()