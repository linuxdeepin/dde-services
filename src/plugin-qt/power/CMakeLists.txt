# SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: LGPL-3.0-or-later

add_subdirectory(session)
add_subdirectory(system)

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/misc/org.deepin.dde.daemon.power.json
    DESTINATION ${CMAKE_INSTALL_DATADIR}/dsg/configs/org.deepin.dde.daemon/
)
