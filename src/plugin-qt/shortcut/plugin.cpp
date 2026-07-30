// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "shortcutlogging.h"

#include "pluginshortcutmanager.h"

#include <QDBusConnection>
#include <QDebug>

static PluginShortcutManager *g_pluginManager = nullptr;

/**
 * @brief DSMRegister - Plugin registration entry function
 * 
 * This function is called by deepin-service-manager when loading the plugin
 * 
 * @param name DBus service name (from JSON config)
 * @param data QDBusConnection pointer (provided by service-manager)
 * @return 0 on success, non-zero on failure
 */
extern "C" int DSMRegister(const char *name, void *data)
{
    qCInfo(logShortcut) << "[ShortcutPlugin] DSMRegister called with name:" << name;

    if (!data) {
        qCCritical(logShortcut) << "[ShortcutPlugin] Invalid data pointer";
        return -1;
    }

    // Get DBus connection
    auto connection = reinterpret_cast<QDBusConnection *>(data);

    // Create plugin manager
    g_pluginManager = new PluginShortcutManager();

    // Initialize plugin
    if (!g_pluginManager->init(connection)) {
        qCCritical(logShortcut) << "[ShortcutPlugin] Failed to initialize plugin";
        delete g_pluginManager;
        g_pluginManager = nullptr;
        return -1;
    }

    qCInfo(logShortcut) << "[ShortcutPlugin] Plugin registered successfully";
    return 0;
}

/**
 * @brief DSMUnRegister - Plugin unregistration entry function
 * 
 * This function is called by deepin-service-manager when unloading the plugin
 * Used to cleanup resources and prevent memory leaks
 * 
 * @param name DBus service name
 * @param data User data (unused)
 * @return 0 on success, non-zero on failure
 */
extern "C" int DSMUnRegister(const char *name, void *data)
{
    Q_UNUSED(name);
    Q_UNUSED(data);

    qCInfo(logShortcut) << "[ShortcutPlugin] DSMUnRegister called";

    if (g_pluginManager) {
        g_pluginManager->cleanup();
        delete g_pluginManager;
        g_pluginManager = nullptr;
    }

    qCInfo(logShortcut) << "[ShortcutPlugin] Plugin unregistered successfully";
    return 0;
}
