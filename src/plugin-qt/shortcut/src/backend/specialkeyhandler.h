// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "core/shortcutconfig.h"

#include <QObject>
#include <QMap>
#include <QSet>


/**
 * @brief Handler for special keycodes via org.deepin.dde.KeyEvent1 DBus signal
 * 
 * This handler listens to the system KeyEvent1 service which uses libinput
 * to capture special keys that may not have Qt key names (e.g., vendor-specific keys).
 * 
 * Works on both X11 and Wayland since it uses libinput directly.
 */
class SpecialKeyHandler : public QObject
{
    Q_OBJECT
public:
    explicit SpecialKeyHandler(QObject *parent = nullptr);
    ~SpecialKeyHandler() override;

    /**
     * @brief Register a special keycode shortcut
     * @param config KeyConfig with keycode in hotkeys (e.g., "530" or "0x212")
     * @return true if registration successful
     */
    bool registerKey(const KeyConfig &config);

    /**
     * @brief Unregister a special keycode shortcut
     * @param shortcutId The shortcut ID to unregister
     * @return true if unregistration successful
     */
    bool unregisterKey(const QString &shortcutId);

    /**
     * @brief Check if a keycode is already registered
     * @param keycode The keycode to check
     * @return The shortcut ID if registered, empty string otherwise
     */
    QString lookupConflict(uint32_t keycode) const;

    /**
     * @brief Check if a hotkey string is a keycode (numeric)
     * @param hotkey The hotkey string to check
     * @return true if it's a keycode (starts with digit or "0x")
     */
    static bool isKeycode(const QString &hotkey);

    /**
     * @brief Parse a keycode string to uint32
     * @param hotkey The hotkey string (e.g., "530" or "0x212")
     * @return The parsed keycode, or 0 if invalid
     */
    static uint32_t parseKeycode(const QString &hotkey);

signals:
    /**
     * @brief Emitted when a registered special key is activated
     * @param shortcutId The ID of the activated shortcut
     */
    void keyActivated(const QString &shortcutId);

private slots:
    /**
     * @brief Handle KeyEvent signal from org.deepin.dde.KeyEvent1
     */
    void onKeyEvent(uint keycode, bool pressed, bool ctrlPressed, 
                    bool shiftPressed, bool altPressed, bool superPressed);

private:
    struct KeyBinding {
        QString shortcutId;
        int keyEventFlags;  // KeyEventFlag bitfield
    };

    // Map keycode -> KeyBinding
    QMap<uint32_t, KeyBinding> m_keycodeBindings;
    
    // Map shortcutId -> list of keycodes (for unregister)
    QMap<QString, QList<uint32_t>> m_shortcutKeycodes;
    
    // Track currently held keys for repeat detection
    QSet<uint32_t> m_keysHeld;
    
    bool m_connected;
};
