// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
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

    // In plugin mode ShortcutManager does not register D-Bus services itself;
    // use the connection provided by deepin-service-manager here.
    const auto registerEndpoint = [connection](const QString &service,
                                               const QString &path,
                                               QObject *object) {
        if (!connection->registerService(service)) {
            qCritical() << "[PluginShortcutManager] Failed to register service" << service << ":"
                        << connection->lastError().message();
            return false;
        }

        if (!connection->registerObject(path, object,
                                        QDBusConnection::ExportScriptableContents)) {
            qCritical() << "[PluginShortcutManager] Failed to register object" << path << ":"
                        << connection->lastError().message();
            connection->unregisterService(service);
            return false;
        }

        qInfo() << "[PluginShortcutManager] Registered" << service << "at" << path;
        return true;
    };

    auto *keybindingManager = m_shortcutManager->keybindingManager();
    auto *gestureManager = m_shortcutManager->gestureManager();

    bool keybindingRegistered = false;
    if (!keybindingManager) {
        qCritical() << "[PluginShortcutManager] KeybindingManager not found";
    } else {
        keybindingRegistered = registerEndpoint(
                QStringLiteral("org.deepin.dde.Keybinding1"),
                QStringLiteral("/org/deepin/dde/Keybinding1"),
                keybindingManager);
    }

    bool gestureRegistered = false;
    if (!gestureManager) {
        qWarning() << "[PluginShortcutManager] GestureManager not found";
    } else {
        gestureRegistered = registerEndpoint(
                QStringLiteral("org.deepin.dde.Gesture1"),
                QStringLiteral("/org/deepin/dde/Gesture1"),
                gestureManager);
    }

    if (!keybindingRegistered && !gestureRegistered) {
        qCritical() << "[PluginShortcutManager] Failed to register any DBus endpoint";
        return false;
    }

    qInfo() << "[PluginShortcutManager] Plugin initialized successfully";
    return true;
}

void PluginShortcutManager::cleanup()
{
    if (m_shortcutManager) {
        qInfo() << "[PluginShortcutManager] Cleaning up plugin";
        
        // Unregister DBus services
        if (m_connection) {
            m_connection->unregisterService("org.deepin.dde.Keybinding1");
            m_connection->unregisterService("org.deepin.dde.Gesture1");
            m_connection->unregisterObject("/org/deepin/dde/Keybinding1");
            m_connection->unregisterObject("/org/deepin/dde/Gesture1");
        }

        delete m_shortcutManager;
        m_shortcutManager = nullptr;
    }
}
