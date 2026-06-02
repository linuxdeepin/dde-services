// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "waylandkeyhandler.h"
#include "core/shortcutconfig.h"

#include <QDebug>
#include <QGuiApplication>
#include <QSet>
#include <QtWaylandClient/QWaylandClientExtension>
#include <wayland-client.h>

// WaylandKeyHandler implementation
WaylandKeyHandler::WaylandKeyHandler(TreelandShortcutWrapper *wrapper, QObject *parent)
    : AbstractKeyHandler(parent)
    , m_wrapper(wrapper)
{
    connect(m_wrapper, &TreelandShortcutWrapper::activated, this, &WaylandKeyHandler::onActivated);
    connect(m_wrapper, &TreelandShortcutWrapper::protocolInactive, this, [this]() {
        qWarning() << "WaylandKeyHandler: Protocol inactive, clearing all bindings";
        m_nameToId.clear();
        m_idToNames.clear();
    });
}

WaylandKeyHandler::~WaylandKeyHandler()
{
}

// Lock keys (NumLock, CapsLock) are rejected by Treeland at commit time.
// Skip bindKey() for these so the bulk commit does not fail; the config
// remains visible on D-Bus and the physical keys keep working.
static bool isLockKey(const QString &hotkey)
{
    static const QSet<QString> lockKeys = { "NumLock", "CapsLock", "Num_Lock", "Caps_Lock" };
    return lockKeys.contains(hotkey);
}

bool WaylandKeyHandler::registerKey(const KeyConfig &config)
{
    if (!m_wrapper) return false;

    if (m_idToNames.contains(config.getId())) {
        unregisterKey(config.getId());
    }

    QStringList bindings;
    bool allSuccess = true;

    for (const QString &hotkey : config.hotkeys) {
        if (isLockKey(hotkey)) {
            qDebug() << "Skipping lock key registration on Wayland:" << hotkey
                     << "for" << config.getId();
            continue;
        }

        QString name = config.getId() + "_" + hotkey;
        
        // Determine action: if triggerType is Action (3), use value? 
        // But KeyConfig usually maps to Command/App, which means we want 'notify' action (1).
        // If triggerType is Action (3), we might want to pass that action ID to compositor?
        // Design doc says: "WaylandGestureHandler ... if config.triggerType == 3 (Action), action = config.triggerValue[0].toInt()"
        // For KeyHandler, it says "WaylandKeyHandler uses bind_key".
        // And "ActionExecutor ... triggerType 3 (Action) is handled by compositor".
        // So if triggerType is 3, we should bind with that action ID.
        // If triggerType is 1 or 2, we bind with 'notify' (1) and handle execution ourselves.
        
        int action = QtWayland::treeland_shortcut_manager_v2::action_notify; // notify
        if (config.triggerType == (int)TriggerType::Action && !config.triggerValue.isEmpty()) {
             // Try to parse action enum value from string? 
             // Or assumes triggerValue contains the integer value as string?
             // Design doc example for gesture says "action = config.triggerValue[0].toInt()".
             // I'll assume the same for keys.
             bool ok;
             int val = config.triggerValue.first().toInt(&ok);
             if (ok) action = val;
        }

        // Use keyEventFlags from config, default to key_release (0x2)
        int flags = config.keyEventFlags;
        if (m_wrapper->bindKey(name, hotkey, flags, action)) {
            m_nameToId.insert(name, config.getId());
            bindings.append(name);
        } else {
            allSuccess = false;
            qWarning() << "Failed to bind key:" << hotkey << "for" << config.getId();
        }
    }

    if (!bindings.isEmpty()) {
        m_idToNames.insert(config.getId(), bindings);
    }

    return allSuccess;
}

bool WaylandKeyHandler::unregisterKey(const QString &id)
{
    if (!m_idToNames.contains(id)) return false;

    QStringList bindings = m_idToNames.take(id);

    for (const QString &name : bindings) {
        m_wrapper->unbind(name);
        m_nameToId.remove(name);
    }

    return true;
}

bool WaylandKeyHandler::commit()
{
    m_wrapper->commitDeferred();
    return true;
}

bool WaylandKeyHandler::commitSync()
{
    bool success = m_wrapper->commitAndWait();
    if (!success) {
        qWarning() << "WaylandKeyHandler::commitSync failed";
    }

    return success;
}

void WaylandKeyHandler::onActivated(const QString &name, uint32_t flags)
{
    Q_UNUSED(flags);
    // Forward the activation signal to KeybindingManager
    if (m_nameToId.contains(name)) {
        QString id = m_nameToId.value(name);
        qDebug() << "WaylandKeyHandler::onActivated name:" << name << "id:" << id << "flags:" << flags;
        
        emit keyActivated(id);
    }
}

// Wayland does not expose CapsLock / NumLock state to regular clients, and
// the old org_kde_kwin_keystate dependency has been removed. These methods
// remain part of the AbstractKeyHandler contract for the X11 backend; on
// Wayland they are intentional no-ops. Users see and toggle the lock state
// via the physical keys; the shortcut service does not need to mirror it.

bool WaylandKeyHandler::getCapsLockState() const
{
    return false;
}

bool WaylandKeyHandler::getNumLockState() const
{
    return false;
}

void WaylandKeyHandler::setCapsLockState(bool on)
{
    Q_UNUSED(on);
}

void WaylandKeyHandler::setNumLockState(bool on)
{
    Q_UNUSED(on);
}
