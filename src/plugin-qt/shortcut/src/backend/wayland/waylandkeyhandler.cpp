// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "shortcutlogging.h"

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
    connect(m_wrapper, &TreelandShortcutWrapper::bindFailed, this, &WaylandKeyHandler::onBindFailed);
    connect(m_wrapper, &TreelandShortcutWrapper::protocolInactive, this, [this]() {
        qCWarning(logShortcut) << "WaylandKeyHandler: Protocol inactive, clearing all bindings";
        m_nameToId.clear();
        m_idToNames.clear();
    });
}

WaylandKeyHandler::~WaylandKeyHandler()
{
}

// Lock keys are handled by the compositor rather than registered shortcuts.
// Keep their configuration visible while leaving the physical keys to it.
static bool isLockKey(const QString &hotkey)
{
    static const QSet<QString> lockKeys = { "NumLock", "CapsLock", "Num_Lock", "Caps_Lock" };
    return lockKeys.contains(hotkey);
}

bool WaylandKeyHandler::registerKey(const KeyConfig &config)
{
    if (!m_wrapper) return false;

    // Bare Sleep is still rejected by Treeland. Return failure before queuing
    // bindings so fixed hardware shortcuts can use their raw-keycode fallback.
    if (config.hotkeys.contains(QStringLiteral("Sleep"))) {
        qCInfo(logShortcut) << "WaylandKeyHandler: Sleep is not supported by Treeland:"
                           << config.getId();
        return false;
    }

    if (m_idToNames.contains(config.getId())) {
        unregisterKey(config.getId());
    }

    int action = QtWayland::treeland_shortcut_manager_v3::action_notify;
    if (config.triggerType == static_cast<int>(TriggerType::Action)) {
        const TriggerActionId actionId = config.triggerValue.isEmpty()
                ? TriggerActionId::Invalid
                : TriggerActionCatalog::resolve(config.triggerValue.first());
        const std::optional<int> treelandAction = TriggerActionCatalog::treelandActionId(actionId);
        if (!treelandAction) {
            // TODO: register this action after Treeland adds the corresponding
            // compositor protocol support.  Keep the config visible meanwhile.
            qCInfo(logShortcut) << "WaylandKeyHandler: compositor action is not supported by Treeland:"
                    << config.getId() << config.triggerValue.value(0);
            return false;
        }
        action = *treelandAction;
    }

    QStringList bindings;
    bool allSuccess = true;

    for (const QString &hotkey : config.hotkeys) {
        if (isLockKey(hotkey)) {
            qCDebug(logShortcut) << "Skipping lock key registration on Wayland:" << hotkey
                     << "for" << config.getId();
            continue;
        }

        // A deferred failure from an older registration must not invalidate a
        // replacement for the same config/hotkey after a session switch.
        const QString name = config.getId() + "_" + hotkey
                + "#" + QString::number(++m_bindingSerial);
        
        // Use keyEventFlags from config, default to key_release (0x2)
        int flags = config.keyEventFlags;
        if (m_wrapper->bindKey(name, hotkey, flags, action)) {
            m_nameToId.insert(name, config.getId());
            bindings.append(name);
        } else {
            allSuccess = false;
            qCWarning(logShortcut) << "Failed to bind key:" << hotkey << "for" << config.getId();
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
    m_wrapper->flushDeferred();
    return true;
}

bool WaylandKeyHandler::commitSync()
{
    bool success = m_wrapper->syncAndWait();
    if (!success) {
        qCWarning(logShortcut) << "WaylandKeyHandler::commitSync failed";
    }

    return success;
}

void WaylandKeyHandler::onActivated(const QString &name, uint32_t flags)
{
    Q_UNUSED(flags);
    // Forward the activation signal to KeybindingManager
    if (m_nameToId.contains(name)) {
        QString id = m_nameToId.value(name);
        qCDebug(logShortcut) << "WaylandKeyHandler::onActivated name:" << name << "id:" << id
                 << "flags:" << flags;
        
        emit keyActivated(id);
    }
}

void WaylandKeyHandler::onBindFailed(const QString &name, uint32_t error)
{
    Q_UNUSED(error);
    const QString id = m_nameToId.value(name);
    if (id.isEmpty())
        return;

    // A multi-hotkey config is registered as a unit by the service. V3 keeps
    // its other successful binds, so explicitly remove those on failure.
    unregisterKey(id);
    m_wrapper->flushDeferred();
    emit registrationFailed(id);
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
