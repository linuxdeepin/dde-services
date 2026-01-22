// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "pluginshortcutmanager.h"
#include "core/shortcutmanager.h"
#include "core/keybindingmanager.h"
#include "core/gesturemanager.h"

#include <QDebug>
#include <QDBusConnection>

PluginShortcutManager::PluginShortcutManager(QObject *parent)
    : QObject(parent)
{
}

PluginShortcutManager::~PluginShortcutManager()
{
    cleanup();
}

bool PluginShortcutManager::init(QDBusConnection *connection)
{
    if (!connection) {
        qCritical() << "[PluginShortcutManager] Invalid DBus connection";
        return false;
    }

    m_connection = connection;

    m_shortcutManager = new ShortcutManager(this);
    
    // Initialize ShortcutManager
    if (!m_shortcutManager->init()) {
        qCritical() << "[PluginShortcutManager] Failed to initialize ShortcutManager";
        delete m_shortcutManager;
        m_shortcutManager = nullptr;
        return false;
    }

    // Register DBus service
    // Note: In plugin mode, ShortcutManager::init() does not register DBus service
    // We need to use the connection provided by the plugin to register
    
    // Get KeybindingManager and GestureManager
    auto keybindingManager = m_shortcutManager->keybindingManager();
    auto gestureManager = m_shortcutManager->gestureManager();

    if (!keybindingManager) {
        qCritical() << "[PluginShortcutManager] KeybindingManager not found";
        return false;
    }

    // Register Keybinding DBus object
    if (!connection->registerObject("/org/deepin/dde/Keybinding1",
                                    keybindingManager,
                                    QDBusConnection::ExportScriptableContents)) {
        qCritical() << "[PluginShortcutManager] Failed to register Keybinding1 object:"
                    << connection->lastError().message();
        return false;
    }

    qInfo() << "[PluginShortcutManager] Registered Keybinding1 object at /org/deepin/dde/Keybinding1";

    // Register Gesture DBus object (if exists)
    if (gestureManager) {
        if (!connection->registerObject("/org/deepin/dde/Gesture1",
                                        gestureManager,
                                        QDBusConnection::ExportScriptableContents)) {
            qCritical() << "[PluginShortcutManager] Failed to register Gesture1 object:"
                        << connection->lastError().message();
            return false;
        }
        qInfo() << "[PluginShortcutManager] Registered Gesture1 object at /org/deepin/dde/Gesture1";
    }

    // Register DBus service name
    if (!connection->registerService("org.deepin.dde.Keybinding1")) {
        qCritical() << "[PluginShortcutManager] Failed to register service org.deepin.dde.Keybinding1:"
                    << connection->lastError().message();
        return false;
    }

    qInfo() << "[PluginShortcutManager] Plugin initialized successfully";
    return true;
}

void PluginShortcutManager::cleanup()
{
    if (m_shortcutManager) {
        qInfo() << "[PluginShortcutManager] Cleaning up plugin";
        
        // Unregister DBus service
        if (m_connection) {
            m_connection->unregisterService("org.deepin.dde.Keybinding1");
            m_connection->unregisterObject("/org/deepin/dde/Keybinding1");
            m_connection->unregisterObject("/org/deepin/dde/Gesture1");
        }

        delete m_shortcutManager;
        m_shortcutManager = nullptr;
    }
}
