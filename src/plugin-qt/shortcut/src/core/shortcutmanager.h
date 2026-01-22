// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QObject>

class ConfigLoader;
class ActionExecutor;
class TranslationManager;
class KeybindingManager;
class GestureManager;
class AbstractKeyHandler;
class AbstractGestureHandler;
class TreelandShortcutWrapper;

/**
 * @brief ShortcutManager - Shortcut service coordinator
 * 
 * Responsibilities:
 * 1. Create and manage all sub-components
 * 2. Handle platform differences (Wayland/X11)
 * 3. Coordinate protocol lifecycle, unified commit
 * 4. Register DBus services
 */
class ShortcutManager : public QObject
{
    Q_OBJECT
public:
    explicit ShortcutManager(QObject *parent = nullptr);
    ~ShortcutManager() override;

    /**
     * @brief Initialize the service
     * @return true on success, false on failure
     */
    bool init();

    // Public accessors for plugin mode
    KeybindingManager* keybindingManager() const { return m_keybindingManager; }
    GestureManager* gestureManager() const { return m_gestureManager; }
    bool isWayland() const { return m_isWayland; }

private slots:
    void registerAll();
    void onProtocolInactive();

private:
    bool registerDBusService();

    // Platform identifier
    bool m_isWayland = false;

    // Wayland protocol wrapper (Wayland platform only)
    TreelandShortcutWrapper *m_treelandShortcutWrapper = nullptr;

    // Shared components
    ConfigLoader *m_loader = nullptr;
    ActionExecutor *m_executor = nullptr;
    TranslationManager *m_translationManager = nullptr;

    // Handler (created by ShortcutManager, passed to Manager)
    AbstractKeyHandler *m_keyHandler = nullptr;
    AbstractGestureHandler *m_gestureHandler = nullptr;

    // Business managers
    KeybindingManager *m_keybindingManager = nullptr;
    GestureManager *m_gestureManager = nullptr;
};
