// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QString>

namespace X11ShortcutPolicy {

// Identifies legacy combination shortcuts that use RECORD to remain available
// while another X11 client owns an active keyboard grab. Standalone modifier
// shortcuts (for example Super/Meta, CapsLock, and NumLock) are intentionally
// absent: ModifierKeyMonitor handles them directly. All other combinations use
// the normal XGrabKey path and are not grab-resilient.
bool isLegacyGrabResilientShortcut(const QString &shortcutId);

}
