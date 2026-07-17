// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "waylandkeyhandler.h"
#include "core/triggeractioncatalog.h"
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

    int action = QtWayland::treeland_shortcut_manager_v2::action_notify;
    if (config.triggerType == static_cast<int>(TriggerType::Action)) {
        const TriggerActionId actionId = config.triggerValue.isEmpty()
                ? TriggerActionId::Invalid
                : TriggerActionCatalog::resolve(config.triggerValue.first());
        const std::optional<int> treelandAction = TriggerActionCatalog::treelandActionId(actionId);
        if (!treelandAction) {
            // TODO: register this action after Treeland adds the corresponding
            // compositor protocol support.  Keep the config visible meanwhile.
            qInfo() << "WaylandKeyHandler: compositor action is not supported by Treeland:"
                    << config.getId() << config.triggerValue.value(0);
            return false;
        }
        action = *treelandAction;
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
        qDebug() << "WaylandKeyHandler::onActivated name:" << name << "id:" << id
                 << "flags:" << flags;
        
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
