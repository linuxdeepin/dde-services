// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "shortcutmanager.h"
#include "keybindingmanager.h"
#include "gesturemanager.h"
#include "actionexecutor.h"
#include "translationmanager.h"
#include "config/configloader.h"
#include "backend/x11/x11keyhandler.h"
#include "backend/wayland/waylandkeyhandler.h"
#include "backend/wayland/waylandgesturehandler.h"
#include "backend/wayland/treelandshortcutwrapper.h"

#include <QDBusConnection>
#include <QDebug>
#include <qobjectdefs.h>

ShortcutManager::ShortcutManager(QObject *parent)
    : QObject(parent)
    , m_loader(new ConfigLoader(this))
    , m_executor(new ActionExecutor(this))
    , m_translationManager(new TranslationManager(this))
{
    m_isWayland = (qgetenv("XDG_SESSION_TYPE").toLower() == "wayland");
}

ShortcutManager::~ShortcutManager()
{
}

bool ShortcutManager::init()
{
    m_loader->scanForConfigs();
    m_translationManager->init();

    // create Handler
    if (m_isWayland) {
        m_treelandShortcutWrapper = new TreelandShortcutWrapper(this);
        m_keyHandler = new WaylandKeyHandler(m_treelandShortcutWrapper, this);
        m_gestureHandler = new WaylandGestureHandler(m_treelandShortcutWrapper, this);
    } else {
        m_keyHandler = new X11KeyHandler(this);
        // X11 no need gestures, m_gestureHandler remains nullptr
        m_gestureHandler = nullptr;
    }

    m_keybindingManager = new KeybindingManager(m_loader, m_executor,
         m_translationManager, m_keyHandler, this);
    
    if (m_isWayland) {
        m_gestureManager = new GestureManager(m_loader, m_executor,
             m_translationManager, m_gestureHandler, this);
    }

    // Note: In plugin mode, DBus registration is handled by PluginShortcutManager
    // Uncomment the following lines for standalone application mode
    // if (!registerDBusService()) {
    //     return false;
    // }

    // 5. Handle protocol lifecycle
    if (m_isWayland) {
        // Connect signals first to ensure we don't miss the ready signal
        connect(m_treelandShortcutWrapper, &TreelandShortcutWrapper::ready,
                this, &ShortcutManager::registerAll);
        connect(m_treelandShortcutWrapper, &TreelandShortcutWrapper::protocolInactive,
                this, &ShortcutManager::onProtocolInactive);
        
        // Then initialize protocol (signals are connected, ready will only trigger once)
        m_treelandShortcutWrapper->initProtocol();
    } else {
        // X11: Register all directly
        registerAll();
    }

    return true;
}

bool ShortcutManager::registerDBusService()
{
    QDBusConnection connection = QDBusConnection::sessionBus();

    // Register Keybinding service
    if (!connection.registerService("org.deepin.dde.Keybinding1")) {
        qCritical() << "Failed to register DBus service org.deepin.dde.Keybinding1";
        return false;
    }

    if (!connection.registerObject("/org/deepin/dde/Keybinding1", m_keybindingManager,
                                   QDBusConnection::ExportAllSlots | 
                                   QDBusConnection::ExportAllSignals |
                                   QDBusConnection::ExportAllProperties)) {
        qCritical() << "Failed to register DBus object for Keybinding1";
        return false;
    }

    qInfo() << "Deepin Keybinding Service started.";

    // Register Gesture service
    if (m_isWayland) {
        if (!connection.registerService("org.deepin.dde.Gesture1")) {
            qCritical() << "Failed to register DBus service org.deepin.dde.Gesture1";
            return false;
        }

        if (!connection.registerObject("/org/deepin/dde/Gesture1", m_gestureManager,
                                    QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals)) {
            qCritical() << "Failed to register DBus object for Gesture1";
            return false;
        }

        qInfo() << "Deepin Gesture Service started.";
    }

    return true;
}

void ShortcutManager::registerAll()
{
    qInfo() << "ShortcutManager: Protocol ready, registering all shortcuts and gestures...";
    // Register all shortcuts
    m_keybindingManager->registerAllShortcuts();

    if (m_gestureManager) {
        // only for wayland
        m_gestureManager->registerAllGestures();
    }

    if (m_isWayland) {
        bool success = m_treelandShortcutWrapper->commitAndWait();
        if (success) {
            qInfo() << "ShortcutManager: All shortcuts and gestures registered successfully";
        } else {
            qWarning() << "ShortcutManager: Commit failed";
        }
    }
}

void ShortcutManager::onProtocolInactive()
{
    qWarning() << "ShortcutManager: Protocol inactive, clearing state...";
    
    // Clear state in both managers
    if (m_keybindingManager) {
        m_keybindingManager->clearState();
    }
    
    if (m_gestureManager) {
        m_gestureManager->clearState();
    }
}
