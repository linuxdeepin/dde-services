// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "waylandkeyhandler.h"
#include "core/shortcutconfig.h"

#include <QDebug>
#include <QGuiApplication>
#include <QtWaylandClient/QWaylandClientExtension>
#include <wayland-client.h>

extern "C" {
#include "wayland-kde-keystate-client-protocol.h"
}

// KeyStateManager implementation
static const struct org_kde_kwin_keystate_listener keystate_listener = {
    KeyStateManager::handleStateChanged
};

KeyStateManager::KeyStateManager(QObject *parent)
    : QObject(parent)
{
}

KeyStateManager::~KeyStateManager()
{
    if (m_keystate) {
        org_kde_kwin_keystate_destroy(m_keystate);
        m_keystate = nullptr;
    }
}

bool KeyStateManager::initialize()
{
    // Get Wayland display from Qt
    auto *app = qGuiApp;
    if (!app) {
        qWarning() << "KeyStateManager: No QGuiApplication instance";
        return false;
    }

    // In Qt 6, we need to use native interface differently
    auto *nativeInterface = app->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (!nativeInterface) {
        qWarning() << "KeyStateManager: Not running on Wayland";
        return false;
    }

    struct wl_display *display = nativeInterface->display();
    if (!display) {
        qWarning() << "KeyStateManager: Failed to get Wayland display";
        return false;
    }

    struct wl_registry *registry = wl_display_get_registry(display);
    if (!registry) {
        qWarning() << "KeyStateManager: Failed to get Wayland registry";
        return false;
    }

    // Registry listener to bind keystate interface
    static const struct wl_registry_listener registry_listener = {
        [](void *data, struct wl_registry *registry, uint32_t name,
           const char *interface, uint32_t version) {
            KeyStateManager *self = static_cast<KeyStateManager *>(data);
            if (strcmp(interface, org_kde_kwin_keystate_interface.name) == 0) {
                self->m_keystate = static_cast<struct org_kde_kwin_keystate *>(
                    wl_registry_bind(registry, name, &org_kde_kwin_keystate_interface,
                                    qMin(version, 5u)));
                if (self->m_keystate) {
                    // 1. Add event listener
                    // Return value: 0 = success, -1 = failure
                    int ret = org_kde_kwin_keystate_add_listener(self->m_keystate, &keystate_listener, self);
                    if (ret != 0) {
                        qWarning() << "KeyStateManager: Failed to add listener, ret:" << ret;
                        org_kde_kwin_keystate_destroy(self->m_keystate);
                        self->m_keystate = nullptr;
                        return;
                    }
                    
                    // 2. Request current state of all keys (called only once during initialization)
                    // Compositor will return all key states through multiple stateChanged events
                    // After that, compositor will automatically send stateChanged events when key state changes
                    // No need to call fetchStates() again
                    org_kde_kwin_keystate_fetchStates(self->m_keystate);
                    qDebug() << "KeyStateManager: Successfully bound to keystate interface, fetching initial states";
                }
            }
        },
        [](void *, struct wl_registry *, uint32_t) {}
    };

    // Add registry listener
    int ret = wl_registry_add_listener(registry, &registry_listener, this);
    if (ret != 0) {
        qWarning() << "KeyStateManager: Failed to add registry listener, ret:" << ret;
        wl_registry_destroy(registry);
        return false;
    }
    
    wl_display_roundtrip(display);
    wl_registry_destroy(registry);

    if (!m_keystate) {
        qWarning() << "KeyStateManager: org_kde_kwin_keystate interface not available";
    }

    return m_keystate != nullptr;
}

void KeyStateManager::handleStateChanged(void *data, struct org_kde_kwin_keystate *,
                                         uint32_t key, uint32_t state)
{
    KeyStateManager *self = static_cast<KeyStateManager *>(data);
    if (!self || !self->m_keystate) {
        qWarning() << "KeyStateManager: Keystate interface not available";
        return;
    }
    
    // Key enum: 0=capslock, 1=numlock, 2=scrolllock, ...
    // State enum: 0=unlocked, 1=latched, 2=locked, 3=pressed
    
    // Note: compositor sends state of all keys (full state)
    // Each time fetchStates() is called or key state changes, multiple stateChanged events will be received
    // qDebug() << "KeyStateManager: stateChanged - key:" << key << "state:" << state;
    
    bool locked = (state == 2); // ORG_KDE_KWIN_KEYSTATE_STATE_LOCKED
    
    switch (key) {
    case 0: // ORG_KDE_KWIN_KEYSTATE_KEY_CAPSLOCK
        if (self->m_capsLockState != locked) {
            self->m_capsLockState = locked;
            emit self->capsLockStateChanged(locked);
        }
        break;
    case 1: // ORG_KDE_KWIN_KEYSTATE_KEY_NUMLOCK
        if (self->m_numLockState != locked) {
            self->m_numLockState = locked;
            emit self->numLockStateChanged(locked);
        }
        break;
    default:
        // Ignore other keys (ScrollLock, Alt, Control, etc.)
        break;
    }
    
    // When state is first received, mark as ready and emit signal
    if (!self->m_ready) {
        self->m_ready = true;
        qDebug() << "KeyStateManager: Ready - CapsLock:" << self->m_capsLockState 
                 << "NumLock:" << self->m_numLockState;
        emit self->ready();
    }
}

// WaylandKeyHandler implementation
WaylandKeyHandler::WaylandKeyHandler(TreelandShortcutWrapper *wrapper, QObject *parent)
    : AbstractKeyHandler(parent)
    , m_wrapper(wrapper)
    , m_keyStateManager(new KeyStateManager(this))
{
    connect(m_wrapper, &TreelandShortcutWrapper::activated, this, &WaylandKeyHandler::onActivated);
    connect(m_wrapper, &TreelandShortcutWrapper::protocolInactive, this, [this]() {
        qWarning() << "WaylandKeyHandler: Protocol inactive, clearing all bindings";
        m_nameToId.clear();
        m_idToNames.clear();
    });

    // Initialize KeyStateManager
    if (!m_keyStateManager->initialize()) {
        qWarning() << "WaylandKeyHandler: Failed to initialize KeyStateManager";
    }
}

WaylandKeyHandler::~WaylandKeyHandler()
{
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
    bool success = m_wrapper->commitAndWait();
    if (!success) {
        qWarning() << "WaylandKeyHandler::commit failed";
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

bool WaylandKeyHandler::getCapsLockState() const
{
    if (m_keyStateManager) {
        return m_keyStateManager->getCapsLockState();
    }
    return false;
}

bool WaylandKeyHandler::getNumLockState() const
{
    if (m_keyStateManager) {
        return m_keyStateManager->getNumLockState();
    }
    return false;
}

void WaylandKeyHandler::setCapsLockState(bool on)
{
    // TODO: Implement Wayland-specific CapsLock state setting
    // Need to use compositor DBus interface or Wayland protocol to simulate key press
    // X11 uses xcb_test_fake_input, but this is not available on Wayland
    Q_UNUSED(on);
    qWarning() << "WaylandKeyHandler::setCapsLockState() not implemented for Wayland";
}

void WaylandKeyHandler::setNumLockState(bool on)
{
    // TODO: Implement Wayland-specific NumLock state setting
    // Need to use compositor DBus interface or Wayland protocol to simulate key press
    // X11 uses xcb_test_fake_input, but this is not available on Wayland
    Q_UNUSED(on);
    qWarning() << "WaylandKeyHandler::setNumLockState() not implemented for Wayland";
}
