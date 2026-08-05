// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "x11shortcutpolicy.h"

#include <QSet>

namespace X11ShortcutPolicy {

bool isLegacyGrabResilientShortcut(const QString &shortcutId)
{
    static const QSet<QString> ids{
        QStringLiteral("org.deepin.dde.keybinding.shortcut.app.screenshot"),
        QStringLiteral("org.deepin.dde.keybinding.shortcut.app.fullscreen-screenshot"),
        QStringLiteral("org.deepin.dde.keybinding.shortcut.app.window-screenshot"),
        QStringLiteral("org.deepin.dde.keybinding.shortcut.app.delay-screenshot"),
        QStringLiteral("org.deepin.dde.keybinding.shortcut.app.screenshot-ocr"),
        QStringLiteral("org.deepin.dde.keybinding.shortcut.app.scroll-screenshot"),
        QStringLiteral("org.deepin.dde.keybinding.shortcut.app.screen-recorder"),
    };
    return ids.contains(shortcutId);
}

}
