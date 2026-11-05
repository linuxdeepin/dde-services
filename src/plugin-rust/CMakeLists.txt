# SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: LGPL-3.0-or-later

option(ENABLE_PLUGIN_RUST_AUDIO "Build Rust audio plugin" OFF)

if (ENABLE_PLUGIN_RUST_AUDIO)
    add_subdirectory("audio")
endif()
