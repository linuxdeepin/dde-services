// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QString>

namespace X11WmAccelConverter {

// Convert the physical shortcut representation used by DConfig and the
// control center to the legacy com.deepin.wm accelerator wire format.
QString toDaemonAccel(const QString &wmShortcutId, const QString &hotkey);

}
