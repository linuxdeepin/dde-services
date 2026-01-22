// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QObject>
#include <QDBusConnection>

class ShortcutManager;

/**
 * @brief PluginShortcutManager - Plugin adapter
 * 
 * This class serves as an adapter layer between ShortcutManager and the deepin-service-manager plugin framework
 * Main responsibilities:
 * 1. Manage the lifecycle of ShortcutManager
 * 2. Provide plugin initialization and cleanup interfaces
 * 3. Handle DBus connection passing
 */
class PluginShortcutManager : public QObject
{
    Q_OBJECT
public:
    explicit PluginShortcutManager(QObject *parent = nullptr);
    ~PluginShortcutManager() override;

    /**
     * @brief Initialize plugin
     * @param connection DBus connection object (provided by service-manager)
     * @return true on success, false on failure
     */
    bool init(QDBusConnection *connection);

    /**
     * @brief Cleanup plugin resources
     */
    void cleanup();

private:
    ShortcutManager *m_shortcutManager = nullptr;
    QDBusConnection *m_connection = nullptr;
};
