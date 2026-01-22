// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "backend/abstractkeyhandler.h"

#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>
#include <QSocketNotifier>
#include <QMap>
#include <QSet>

// Forward declaration
class ModifierKeyMonitor;

class X11KeyHandler : public AbstractKeyHandler
{
    Q_OBJECT
public:
    explicit X11KeyHandler(QObject *parent = nullptr);
    ~X11KeyHandler() override;

    bool registerKey(const KeyConfig &config) override;
    bool unregisterKey(const QString &appId) override;

    // Lock key state operations
    bool getCapsLockState() const override;
    bool getNumLockState() const override;
    void setCapsLockState(bool on) override;
    void setNumLockState(bool on) override;

private slots:
    void handleXcbEvents();
    void onModifierKeyReleased(unsigned long keysym);

private:
    bool grabKey(xcb_keycode_t keycode, uint16_t modifiers);
    bool ungrabKey(xcb_keycode_t keycode, uint16_t modifiers);
    
    // Helpers
    bool parseHotkey(const QString &hotkey, xcb_keycode_t &keycode, uint16_t &modifiers);
    uint16_t getConcernedMods(uint16_t state);
    
    // Standalone modifier helpers
    bool isStandaloneModifierKey(xcb_keysym_t keysym, uint16_t mods) const;
    uint16_t clearModifierBit(xcb_keysym_t keysym, uint16_t mods) const;

    xcb_connection_t *m_connection;
    xcb_screen_t *m_screen;
    xcb_key_symbols_t *m_keySymbols;
    QSocketNotifier *m_notifier;

    // Modifier key monitor (独立模块)
    ModifierKeyMonitor *m_modifierMonitor;

    // Map (keycode | (mods << 16)) -> shortcutId
    QMap<uint32_t, QString> m_grabbedKeys;
    // Map shortcutId -> List of (keycode | mods)
    QMap<QString, QList<uint32_t>> m_shortcutKeys;
    // Map shortcutId -> keyEventFlags
    QMap<QString, int> m_shortcutFlags;
    // Track keys currently held down for repeat detection
    QSet<uint32_t> m_keysHeld;
};
