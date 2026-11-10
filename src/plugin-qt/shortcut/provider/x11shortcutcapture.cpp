// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "x11shortcutcapture.h"

#include <QDebug>
#include <QSocketNotifier>
#include <QTimer>

#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/Xlib-xcb.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <cstdlib>

X11ShortcutCapture::X11ShortcutCapture(QObject *parent)
    : QObject(parent)
    , m_timer(new QTimer(this))
    , m_releaseTimer(new QTimer(this))
{
    m_display = XOpenDisplay(nullptr);
    if (!m_display) {
        qWarning() << "GlobalShortcuts provider: could not connect to X11";
        return;
    }
    XSetEventQueueOwner(m_display, XCBOwnsEventQueue);
    m_connection = XGetXCBConnection(m_display);
    if (!m_connection || xcb_connection_has_error(m_connection)) {
        qWarning() << "GlobalShortcuts provider: could not connect to X11 through XCB";
        return;
    }

    const xcb_screen_iterator_t screens = xcb_setup_roots_iterator(xcb_get_setup(m_connection));
    m_screen = screens.data;
    m_keySymbols = xcb_key_symbols_alloc(m_connection);
    if (!m_screen || !m_keySymbols) {
        qWarning() << "GlobalShortcuts provider: incomplete X11 capture setup";
        return;
    }

    Bool supported = False;
    m_detectableAutoRepeat = XkbSetDetectableAutoRepeat(m_display, True, &supported)
            && supported;
    refreshModifierMasks();
    m_notifier = new QSocketNotifier(xcb_get_file_descriptor(m_connection),
                                     QSocketNotifier::Read,
                                     this);
    m_notifier->setEnabled(false);
    connect(m_notifier, &QSocketNotifier::activated, this, &X11ShortcutCapture::handleEvents);

    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, [this] {
        finish(true, tr("Shortcut capture timed out"));
    });
    m_releaseTimer->setSingleShot(true);
    m_releaseTimer->setInterval(0);
    connect(m_releaseTimer, &QTimer::timeout, this, &X11ShortcutCapture::flushPendingRelease);
}

X11ShortcutCapture::~X11ShortcutCapture()
{
    finish(false);
    if (m_keySymbols)
        xcb_key_symbols_free(m_keySymbols);
    if (m_display)
        XCloseDisplay(m_display);
}

bool X11ShortcutCapture::isAvailable() const
{
    return m_connection && m_screen && m_keySymbols && m_display
            && !xcb_connection_has_error(m_connection);
}

bool X11ShortcutCapture::start(uint timeoutMs)
{
    if (m_active)
        return false;
    if (!isAvailable())
        return false;

    drainEvents();
    const auto keyboardCookie = xcb_grab_keyboard(m_connection,
                                                   false,
                                                   m_screen->root,
                                                   XCB_CURRENT_TIME,
                                                   XCB_GRAB_MODE_ASYNC,
                                                   XCB_GRAB_MODE_ASYNC);
    xcb_grab_keyboard_reply_t *keyboardReply =
            xcb_grab_keyboard_reply(m_connection, keyboardCookie, nullptr);
    if (!keyboardReply || keyboardReply->status != XCB_GRAB_STATUS_SUCCESS) {
        free(keyboardReply);
        return false;
    }
    free(keyboardReply);

    const auto pointerCookie = xcb_grab_pointer(m_connection,
                                                false,
                                                m_screen->root,
                                                XCB_EVENT_MASK_BUTTON_PRESS
                                                        | XCB_EVENT_MASK_BUTTON_RELEASE,
                                                XCB_GRAB_MODE_ASYNC,
                                                XCB_GRAB_MODE_ASYNC,
                                                XCB_WINDOW_NONE,
                                                XCB_CURSOR_NONE,
                                                XCB_CURRENT_TIME);
    xcb_grab_pointer_reply_t *pointerReply =
            xcb_grab_pointer_reply(m_connection, pointerCookie, nullptr);
    if (!pointerReply || pointerReply->status != XCB_GRAB_STATUS_SUCCESS) {
        free(pointerReply);
        xcb_ungrab_keyboard(m_connection, XCB_CURRENT_TIME);
        xcb_flush(m_connection);
        return false;
    }
    free(pointerReply);

    m_pendingShortcut.clear();
    m_pendingKeycode = 0;
    m_releasePending = false;
    m_active = true;
    m_notifier->setEnabled(true);
    m_timer->start(qBound(1000u, timeoutMs, 60000u));
    xcb_flush(m_connection);
    return true;
}

void X11ShortcutCapture::cancel()
{
    finish(false);
}

void X11ShortcutCapture::handleEvents()
{
    xcb_generic_event_t *event = nullptr;
    while ((event = xcb_poll_for_event(m_connection))) {
        const uint8_t responseType = event->response_type & ~0x80;
        if (!m_active) {
            free(event);
            continue;
        }

        if (responseType == XCB_KEY_PRESS) {
            const auto *keyEvent = reinterpret_cast<xcb_key_press_event_t *>(event);
            if (m_releasePending && keyEvent->detail == m_pendingKeycode
                    && keyEvent->time == m_pendingReleaseTimestamp) {
                m_releasePending = false;
                m_releaseTimer->stop();
                free(event);
                continue;
            }
            const CapturedKey key = captureKey(keyEvent);
            if (m_pendingKeycode == 0 && key.keysym != XCB_NO_SYMBOL
                    && !IsModifierKey(KeySym(key.keysym))) {
                m_pendingKeycode = keyEvent->detail;
                m_pendingShortcut = isCapturedKeyValid(key) ? key.keystroke : QString();
            }
        } else if (responseType == XCB_KEY_RELEASE) {
            const auto *keyEvent = reinterpret_cast<xcb_key_release_event_t *>(event);
            if (m_pendingKeycode == 0 || keyEvent->detail != m_pendingKeycode) {
                free(event);
                continue;
            }
            if (!m_detectableAutoRepeat) {
                m_pendingReleaseTimestamp = keyEvent->time;
                m_releasePending = true;
                m_releaseTimer->start();
            } else {
                completePendingShortcut();
            }
        } else if (responseType == XCB_BUTTON_RELEASE) {
            finish(true, tr("Shortcut capture was cancelled"));
        } else if (responseType == XCB_MAPPING_NOTIFY) {
            auto *mappingEvent = reinterpret_cast<xcb_mapping_notify_event_t *>(event);
            if (mappingEvent->request == XCB_MAPPING_KEYBOARD)
                xcb_refresh_keyboard_mapping(m_keySymbols, mappingEvent);
            if (mappingEvent->request == XCB_MAPPING_KEYBOARD
                    || mappingEvent->request == XCB_MAPPING_MODIFIER) {
                refreshModifierMasks();
            }
        }
        free(event);
    }
}

void X11ShortcutCapture::flushPendingRelease()
{
    if (!m_active || !m_releasePending)
        return;

    // A round trip ensures a repeat Press split across socket reads is queued
    // before deciding that the deferred Release is a physical key release.
    free(xcb_get_input_focus_reply(m_connection, xcb_get_input_focus(m_connection), nullptr));
    handleEvents();
    if (m_active && m_releasePending)
        completePendingShortcut();
}

void X11ShortcutCapture::completePendingShortcut()
{
    const QString shortcut = m_pendingShortcut;
    finish(shortcut.isEmpty(), tr("This key combination cannot be used"));
    if (!shortcut.isEmpty())
        Q_EMIT captured(shortcut);
}

void X11ShortcutCapture::finish(bool notifyFailure, const QString &reason)
{
    if (!m_active)
        return;

    m_active = false;
    m_pendingShortcut.clear();
    m_pendingKeycode = 0;
    m_releasePending = false;
    m_timer->stop();
    m_releaseTimer->stop();
    m_notifier->setEnabled(false);
    xcb_ungrab_keyboard(m_connection, XCB_CURRENT_TIME);
    xcb_ungrab_pointer(m_connection, XCB_CURRENT_TIME);
    xcb_flush(m_connection);
    drainEvents();
    if (notifyFailure)
        Q_EMIT failed(reason);
}

void X11ShortcutCapture::drainEvents()
{
    if (!m_connection)
        return;
    xcb_generic_event_t *event = nullptr;
    while ((event = xcb_poll_for_event(m_connection)))
        free(event);
}

void X11ShortcutCapture::refreshModifierMasks()
{
    m_altMasks.clear();
    m_superMasks.clear();
    if (!m_display)
        return;

    XModifierKeymap *mapping = XGetModifierMapping(m_display);
    if (!mapping)
        return;

    for (int modifier = 0; modifier < 8; ++modifier) {
        const uint16_t mask = uint16_t(1u << modifier);
        for (int offset = 0; offset < mapping->max_keypermod; ++offset) {
            const KeyCode keycode = mapping->modifiermap[modifier * mapping->max_keypermod + offset];
            if (!keycode)
                continue;
            const KeySym keysym = XkbKeycodeToKeysym(m_display, keycode, 0, 0);
            if (keysym == XK_Alt_L || keysym == XK_Alt_R
                    || keysym == XK_Meta_L || keysym == XK_Meta_R) {
                m_altMasks.insert(mask);
            } else if (keysym == XK_Super_L || keysym == XK_Super_R
                       || keysym == XK_Hyper_L || keysym == XK_Hyper_R) {
                m_superMasks.insert(mask);
            }
        }
    }
    XFreeModifiermap(mapping);

    if (m_altMasks.isEmpty())
        m_altMasks.insert(XCB_MOD_MASK_1);
    if (m_superMasks.isEmpty())
        m_superMasks.insert(XCB_MOD_MASK_4);
}

X11ShortcutCapture::CapturedKey X11ShortcutCapture::captureKey(
        const xcb_key_press_event_t *event) const
{
    CapturedKey captured;
    if (!event || !m_display)
        return captured;

    unsigned int group = 0;
    XkbStateRec state{};
    if (XkbGetState(m_display, XkbUseCoreKbd, &state) == Success)
        group = state.group;

    xcb_keysym_t keysym = xcb_keysym_t(
            XkbKeycodeToKeysym(m_display, KeyCode(event->detail), int(group), 0));
    if (keysym == XCB_NO_SYMBOL)
        return captured;

    KeySym lower = NoSymbol;
    KeySym upper = NoSymbol;
    XConvertCase(KeySym(keysym), &lower, &upper);
    keysym = upper == NoSymbol ? keysym : xcb_keysym_t(upper);

    const char *rawName = XKeysymToString(KeySym(keysym));
    if (!rawName)
        return captured;
    QString name = QString::fromLatin1(rawName);
    if (name == QLatin1String("KP_Prior"))
        name = QStringLiteral("KP_Page_Up");
    else if (name == QLatin1String("KP_Next"))
        name = QStringLiteral("KP_Page_Down");
    else if (name == QLatin1String("Prior"))
        name = QStringLiteral("Page_Up");
    else if (name == QLatin1String("Next"))
        name = QStringLiteral("Page_Down");
    else if (name == QLatin1String("ISO_Left_Tab"))
        name = QStringLiteral("Tab");

    uint16_t modifiers = 0;
    if (event->state & XCB_MOD_MASK_SHIFT)
        modifiers |= XCB_MOD_MASK_SHIFT;
    if (event->state & XCB_MOD_MASK_CONTROL)
        modifiers |= XCB_MOD_MASK_CONTROL;
    if (hasAnyMask(event->state, m_altMasks))
        modifiers |= XCB_MOD_MASK_1;
    if (hasAnyMask(event->state, m_superMasks))
        modifiers |= XCB_MOD_MASK_4;
    if ((keysym == XK_Super_L || keysym == XK_Super_R)
            && modifiers != 0 && (modifiers & ~XCB_MOD_MASK_4) == 0) {
        modifiers = 0;
    }

    QString keystroke;
    if (modifiers & XCB_MOD_MASK_SHIFT)
        keystroke += QStringLiteral("<Shift>");
    if (modifiers & XCB_MOD_MASK_CONTROL)
        keystroke += QStringLiteral("<Control>");
    if (modifiers & XCB_MOD_MASK_1)
        keystroke += QStringLiteral("<Alt>");
    if (modifiers & XCB_MOD_MASK_4)
        keystroke += QStringLiteral("<Super>");
    keystroke += name;

    captured.keystroke = keystroke;
    captured.keyName = name;
    captured.keysym = keysym;
    captured.modifiers = modifiers;
    return captured;
}

bool X11ShortcutCapture::isCapturedKeyValid(const CapturedKey &key) const
{
    if (key.keysym == XCB_NO_SYMBOL || key.keyName.isEmpty())
        return false;
    if (key.modifiers == 0) {
        if (IsFunctionKey(KeySym(key.keysym)) || IsMiscFunctionKey(KeySym(key.keysym)))
            return true;
        if (key.keysym == XK_BackSpace || key.keysym == XK_Delete
                || key.keysym == XK_Super_L || key.keysym == XK_Super_R
                || key.keysym == XK_Pause) {
            return true;
        }
        return key.keyName.startsWith(QLatin1String("XF86"));
    }
    if (IsModifierKey(KeySym(key.keysym)))
        return false;
    if (key.modifiers == XCB_MOD_MASK_SHIFT) {
        if (IsFunctionKey(KeySym(key.keysym)) || IsMiscFunctionKey(KeySym(key.keysym))
                || IsCursorKey(KeySym(key.keysym))) {
            return true;
        }
        if (key.keysym == XK_BackSpace || key.keysym == XK_space
                || key.keysym == XK_Delete || key.keysym == XK_Sys_Req
                || key.keysym == XK_Escape || key.keysym == XK_Tab) {
            return true;
        }
        return key.keyName.startsWith(QLatin1String("XF86"));
    }
    return true;
}

bool X11ShortcutCapture::hasAnyMask(uint16_t state, const QSet<uint16_t> &masks) const
{
    for (uint16_t mask : masks) {
        if (mask && (state & mask))
            return true;
    }
    return false;
}
