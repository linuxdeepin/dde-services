// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "x11keyhandler.h"
#include "modifierkeymonitor.h"
#include "core/qkeysequenceconverter.h"

#include <xcb/xtest.h>
#include <X11/keysym.h>

// Need to define XK_MISCELLANY before including keysymdef.h to get Caps_Lock, Num_Lock etc.
#define XK_MISCELLANY
#include <X11/keysymdef.h>

#include <QDebug>

X11KeyHandler::X11KeyHandler(QObject *parent)
    : AbstractKeyHandler(parent)
    , m_connection(nullptr)
    , m_screen(nullptr)
    , m_keySymbols(nullptr)
    , m_notifier(nullptr)
    , m_modifierMonitor(nullptr)
{
    m_connection = xcb_connect(nullptr, nullptr);
    if (xcb_connection_has_error(m_connection)) {
        qCritical() << "X11KeyHandler: Failed to connect to X11 server";
        return;
    }

    const xcb_setup_t *setup = xcb_get_setup(m_connection);
    xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
    m_screen = iter.data;

    m_keySymbols = xcb_key_symbols_alloc(m_connection);
    
    // Create modifier key monitor
    m_modifierMonitor = new PollingModifierKeyMonitor(m_connection, this);
    connect(m_modifierMonitor, &ModifierKeyMonitor::modifierKeyReleased,
            this, &X11KeyHandler::onModifierKeyReleased);
    m_modifierMonitor->start();

    // Setup XCB event monitoring
    int fd = xcb_get_file_descriptor(m_connection);
    m_notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    m_notifier->setEnabled(true);
    connect(m_notifier, &QSocketNotifier::activated, this, &X11KeyHandler::handleXcbEvents);
}

X11KeyHandler::~X11KeyHandler()
{
    if (m_keySymbols) xcb_key_symbols_free(m_keySymbols);
    if (m_connection) xcb_disconnect(m_connection);
}

bool X11KeyHandler::registerKey(const KeyConfig &config)
{
    if (!m_connection) return false;

    // If already registered, unregister first
    if (m_shortcutKeys.contains(config.getId())) {
        unregisterKey(config.getId());
    }

    QList<uint32_t> grabbed;
    bool allSuccess = true;

    for (const QString &hotkey : config.hotkeys) {
        // Convert Qt Standard Name (e.g., "Meta+Volume Mute") to XKB string (e.g., "<Super>XF86AudioMute")
        QString xkbHotkey = QKeySequenceConverter::qKeySequenceToXkb(hotkey);
        
        xcb_keycode_t keycode;
        uint16_t mods;
        if (parseHotkey(xkbHotkey, keycode, mods)) {
            xcb_keysym_t keysym = xcb_key_symbols_get_keysym(m_keySymbols, keycode, 0);
            bool isStandaloneModifier = isStandaloneModifierKey(keysym, mods);
            
            bool grabSuccess = true;
            if (!isStandaloneModifier) {
                grabSuccess = grabKey(keycode, mods);
                if (!grabSuccess) {
                    allSuccess = false;
                    qWarning() << "Failed to grab hotkey:" << hotkey;
                }
            }
            
            if (grabSuccess || isStandaloneModifier) {
                uint32_t key = keycode | (mods << 16);
                m_grabbedKeys.insert(key, config.getId());
                grabbed.append(key);
            }
        } else {
            allSuccess = false;
            qWarning() << "Failed to parse hotkey:" << hotkey;
        }
    }

    if (!grabbed.isEmpty()) {
        m_shortcutKeys.insert(config.getId(), grabbed);
        m_shortcutFlags.insert(config.getId(), config.keyEventFlags);
    }

    return allSuccess;
}

bool X11KeyHandler::unregisterKey(const QString &shortcutId)
{
    if (!m_shortcutKeys.contains(shortcutId)) return false;

    QList<uint32_t> keys = m_shortcutKeys.take(shortcutId);
    m_shortcutFlags.remove(shortcutId);

    for (uint32_t key : keys) {
        xcb_keycode_t keycode = key & 0xFFFF;
        uint16_t mods = key >> 16;
        ungrabKey(keycode, mods);
        m_grabbedKeys.remove(key);
        m_keysHeld.remove(key);
    }

    return true;
}

bool X11KeyHandler::grabKey(xcb_keycode_t keycode, uint16_t modifiers)
{
    const uint16_t ignoredMods[] = {
        0,
        XCB_MOD_MASK_2,
        XCB_MOD_MASK_LOCK,
        XCB_MOD_MASK_2 | XCB_MOD_MASK_LOCK
    };

    bool hasError = false;
    for (uint16_t ignored : ignoredMods) {
        xcb_void_cookie_t cookie = xcb_grab_key_checked(
            m_connection,
            1,
            m_screen->root,
            modifiers | ignored,
            keycode,
            XCB_GRAB_MODE_ASYNC,
            XCB_GRAB_MODE_ASYNC
        );
        
        xcb_generic_error_t *error = xcb_request_check(m_connection, cookie);
        if (error) {
            qWarning() << "Failed to grab key" << keycode << "with modifiers" << Qt::hex << modifiers
                      << "Error code:" << error->error_code;
            free(error);
            hasError = true;
        }
    }
    
    xcb_flush(m_connection);
    return !hasError;
}

bool X11KeyHandler::ungrabKey(xcb_keycode_t keycode, uint16_t modifiers)
{
    const uint16_t ignoredMods[] = {
        0,
        XCB_MOD_MASK_2,
        XCB_MOD_MASK_LOCK,
        XCB_MOD_MASK_2 | XCB_MOD_MASK_LOCK
    };

    for (uint16_t ignored : ignoredMods) {
        xcb_ungrab_key(m_connection, keycode, m_screen->root, modifiers | ignored);
    }
    xcb_flush(m_connection);
    return true;
}

extern "C" unsigned long x11StringToKeysym(const char* str);

bool X11KeyHandler::parseHotkey(const QString &hotkey, xcb_keycode_t &keycode, uint16_t &modifiers)
{
    modifiers = 0;
    QString keyStr = hotkey;

    // Parse modifiers
    if (keyStr.contains("<Shift>")) { modifiers |= XCB_MOD_MASK_SHIFT; keyStr.remove("<Shift>"); }
    if (keyStr.contains("<Control>")) { modifiers |= XCB_MOD_MASK_CONTROL; keyStr.remove("<Control>"); }
    if (keyStr.contains("<Alt>")) { modifiers |= XCB_MOD_MASK_1; keyStr.remove("<Alt>"); }
    if (keyStr.contains("<Super>")) { modifiers |= XCB_MOD_MASK_4; keyStr.remove("<Super>"); }
    if (keyStr.contains("<Meta>")) { modifiers |= XCB_MOD_MASK_4; keyStr.remove("<Meta>"); }

    // Get keysym and keycode
    unsigned long keysym = x11StringToKeysym(keyStr.toUtf8().constData());
    if (keysym == 0) {
        qWarning() << "Failed to parse key:" << keyStr;
        return false;
    }

    xcb_keycode_t *keycodes = xcb_key_symbols_get_keycode(m_keySymbols, keysym);
    if (keycodes && keycodes[0] != XCB_NO_SYMBOL) {
        keycode = keycodes[0];
        free(keycodes);
        return true;
    }
    
    qWarning() << "Failed to get keycode for key:" << keyStr;
    if (keycodes) free(keycodes);
    return false;
}

void X11KeyHandler::handleXcbEvents()
{
    xcb_generic_event_t *event;
    while ((event = xcb_poll_for_event(m_connection))) {
        uint8_t responseType = event->response_type & ~0x80;
        
        if (responseType == XCB_KEY_PRESS || responseType == XCB_KEY_RELEASE) {
            auto *keyEvent = reinterpret_cast<xcb_key_press_event_t*>(event);
            uint16_t mods = getConcernedMods(keyEvent->state);
            xcb_keysym_t keysym = xcb_key_symbols_get_keysym(m_keySymbols, keyEvent->detail, 0);
            
            // Notify modifier monitor for combo detection
            if (responseType == XCB_KEY_PRESS) {
                m_modifierMonitor->notifyNonModifierKeyPressed();
            }
            
            // Clear modifier bit for standalone modifier key release
            if (responseType == XCB_KEY_RELEASE) {
                mods = clearModifierBit(keysym, mods);
            }
            
            uint32_t key = keyEvent->detail | (mods << 16);
            
            if (m_grabbedKeys.contains(key)) {
                QString shortcutId = m_grabbedKeys.value(key);
                int flags = m_shortcutFlags.value(shortcutId, KeyEventFlag::Release);
                bool isPress = (responseType == XCB_KEY_PRESS);
                
                if (isPress) {
                    // Check if this is a repeat event (key already held down)
                    bool isRepeat = m_keysHeld.contains(key);
                    
                    if (!isRepeat) {
                        // First press - mark key as held
                        m_keysHeld.insert(key);
                        if (flags & KeyEventFlag::Press) {
                            emit keyActivated(shortcutId);
                        }
                    } else {
                        // Repeat event (autorepeat from X11)
                        if (flags & KeyEventFlag::Repeat) {
                            emit keyActivated(shortcutId);
                        }
                    }
                } else {
                    // Key release - mark key as no longer held
                    m_keysHeld.remove(key);
                    if (flags & KeyEventFlag::Release) {
                        emit keyActivated(shortcutId);
                    }
                }
            }
        }

        free(event);
    }
}

uint16_t X11KeyHandler::getConcernedMods(uint16_t state)
{
    return state & ~(XCB_MOD_MASK_2 | XCB_MOD_MASK_LOCK);
}

bool X11KeyHandler::getCapsLockState() const
{
    if (!m_connection) return false;
    
    // Query pointer to get modifier state (CapsLock is XCB_MOD_MASK_LOCK)
    xcb_query_pointer_cookie_t cookie = xcb_query_pointer(m_connection, m_screen->root);
    xcb_query_pointer_reply_t *reply = xcb_query_pointer_reply(m_connection, cookie, nullptr);
    
    if (!reply) return false;
    
    bool state = (reply->mask & XCB_MOD_MASK_LOCK) != 0;
    free(reply);
    
    return state;
}

bool X11KeyHandler::getNumLockState() const
{
    if (!m_connection) return false;
    
    // Query pointer to get modifier state
    xcb_query_pointer_cookie_t cookie = xcb_query_pointer(m_connection, m_screen->root);
    xcb_query_pointer_reply_t *reply = xcb_query_pointer_reply(m_connection, cookie, nullptr);
    
    if (!reply) return false;
    
    // NumLock is typically Mod2
    bool state = (reply->mask & XCB_MOD_MASK_2) != 0;
    free(reply);
    
    return state;
}

void X11KeyHandler::setCapsLockState(bool on)
{
    if (!m_connection) return;
    
    bool currentState = getCapsLockState();
    if (currentState == on) return;
    
    // Get CapsLock keycode
    xcb_keycode_t *keycodes = xcb_key_symbols_get_keycode(m_keySymbols, XK_Caps_Lock);
    if (!keycodes || keycodes[0] == XCB_NO_SYMBOL) {
        if (keycodes) free(keycodes);
        qWarning() << "Failed to get CapsLock keycode";
        return;
    }
    
    xcb_keycode_t keycode = keycodes[0];
    free(keycodes);
    
    // Simulate key press and release
    xcb_test_fake_input(m_connection, XCB_KEY_PRESS, keycode, XCB_CURRENT_TIME, m_screen->root, 0, 0, 0);
    xcb_test_fake_input(m_connection, XCB_KEY_RELEASE, keycode, XCB_CURRENT_TIME, m_screen->root, 0, 0, 0);
    xcb_flush(m_connection);
}

void X11KeyHandler::setNumLockState(bool on)
{
    if (!m_connection) return;
    
    bool currentState = getNumLockState();
    if (currentState == on) return;
    
    // Get NumLock keycode
    xcb_keycode_t *keycodes = xcb_key_symbols_get_keycode(m_keySymbols, XK_Num_Lock);
    if (!keycodes || keycodes[0] == XCB_NO_SYMBOL) {
        if (keycodes) free(keycodes);
        qWarning() << "Failed to get NumLock keycode";
        return;
    }
    
    xcb_keycode_t keycode = keycodes[0];
    free(keycodes);
    
    // Simulate key press and release
    xcb_test_fake_input(m_connection, XCB_KEY_PRESS, keycode, XCB_CURRENT_TIME, m_screen->root, 0, 0, 0);
    xcb_test_fake_input(m_connection, XCB_KEY_RELEASE, keycode, XCB_CURRENT_TIME, m_screen->root, 0, 0, 0);
    xcb_flush(m_connection);
}

// ==================== Modifier Key Handler ====================
void X11KeyHandler::onModifierKeyReleased(unsigned long keysym)
{
    xcb_keycode_t *keycodes = xcb_key_symbols_get_keycode(m_keySymbols, keysym);
    if (!keycodes || keycodes[0] == XCB_NO_SYMBOL) {
        if (keycodes) free(keycodes);
        return;
    }
    
    xcb_keycode_t keycode = keycodes[0];
    free(keycodes);
    
    uint32_t key = keycode | (0 << 16);  // Standalone modifier has mods = 0
    
    if (m_grabbedKeys.contains(key)) {
        QString shortcutId = m_grabbedKeys.value(key);
        emit keyActivated(shortcutId);
    }
}

// ==================== Helper Functions ====================
bool X11KeyHandler::isStandaloneModifierKey(xcb_keysym_t keysym, uint16_t mods) const
{
    if (mods != 0) return false;
    
    return (keysym == XK_Super_L || keysym == XK_Super_R ||
            keysym == XK_Alt_L || keysym == XK_Alt_R ||
            keysym == XK_Control_L || keysym == XK_Control_R ||
            keysym == XK_Shift_L || keysym == XK_Shift_R);
}

uint16_t X11KeyHandler::clearModifierBit(xcb_keysym_t keysym, uint16_t mods) const
{
    if (keysym == XK_Super_L || keysym == XK_Super_R) {
        return mods & ~XCB_MOD_MASK_4;
    } else if (keysym == XK_Alt_L || keysym == XK_Alt_R) {
        return mods & ~XCB_MOD_MASK_1;
    } else if (keysym == XK_Control_L || keysym == XK_Control_R) {
        return mods & ~XCB_MOD_MASK_CONTROL;
    } else if (keysym == XK_Shift_L || keysym == XK_Shift_R) {
        return mods & ~XCB_MOD_MASK_SHIFT;
    }
    return mods;
}
