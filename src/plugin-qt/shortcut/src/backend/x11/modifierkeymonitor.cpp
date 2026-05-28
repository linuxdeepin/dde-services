// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "modifierkeymonitor.h"

#include <QDebug>

#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>

ModifierKeyMonitor::ModifierKeyMonitor(QObject *parent)
    : QObject(parent)
{
}

PollingModifierKeyMonitor::PollingModifierKeyMonitor(xcb_connection_t *conn, QObject *parent)
    : ModifierKeyMonitor(parent)
    , m_connection(conn)
    , m_display(nullptr)
    , m_timer(nullptr)
    , m_lastSuperPressed(false)
    , m_lastAltPressed(false)
    , m_lastControlPressed(false)
    , m_nonModKeyPressed(false)
{
    m_display = XOpenDisplay(nullptr);
    if (!m_display) {
        qWarning() << "ModifierMonitor: Failed to open X display";
        return;
    }
    
    m_timer = new QTimer(this);
    m_timer->setInterval(10);  // 10ms polling interval
    connect(m_timer, &QTimer::timeout, this, &PollingModifierKeyMonitor::checkModifierKeyState);
}

PollingModifierKeyMonitor::~PollingModifierKeyMonitor()
{
    if (m_timer) {
        m_timer->stop();
    }
    if (m_display) {
        XCloseDisplay(m_display);
    }
}

void PollingModifierKeyMonitor::start()
{
    if (m_timer && m_display) {
        m_timer->start();
    } else {
        qWarning() << "ModifierMonitor: Cannot start - timer or display is null";
    }
}

void PollingModifierKeyMonitor::stop()
{
    if (m_timer) {
        m_timer->stop();
    }
}

void PollingModifierKeyMonitor::notifyNonModifierKeyPressed()
{
    bool anyModifierPressed = isKeyPressed(XK_Super_L) || isKeyPressed(XK_Super_R) ||
                             isKeyPressed(XK_Alt_L) || isKeyPressed(XK_Alt_R) ||
                             isKeyPressed(XK_Control_L) || isKeyPressed(XK_Control_R);
    
    if (anyModifierPressed) {
        m_nonModKeyPressed = true;
    }
}

void PollingModifierKeyMonitor::checkModifierKeyState()
{
    if (!m_display) return;
    
    bool superPressed = isKeyPressed(XK_Super_L) || isKeyPressed(XK_Super_R);
    bool altPressed = isKeyPressed(XK_Alt_L) || isKeyPressed(XK_Alt_R);
    bool controlPressed = isKeyPressed(XK_Control_L) || isKeyPressed(XK_Control_R);
    
    // Check Super key release
    if (m_lastSuperPressed && !superPressed) {
        if (!m_nonModKeyPressed && !isKeyboardGrabbed()) {
            emit modifierKeyReleased(XK_Super_L);
        }
    }
    
    // Check Alt key release
    if (m_lastAltPressed && !altPressed) {
        if (!m_nonModKeyPressed && !isKeyboardGrabbed()) {
            emit modifierKeyReleased(XK_Alt_L);
        }
    }
    
    // Check Control key release
    if (m_lastControlPressed && !controlPressed) {
        if (!m_nonModKeyPressed && !isKeyboardGrabbed()) {
            emit modifierKeyReleased(XK_Control_L);
        }
    }
    
    // Reset combo flag when all modifiers are released
    if (!superPressed && !altPressed && !controlPressed && m_nonModKeyPressed) {
        m_nonModKeyPressed = false;
    }
    
    // Update state
    m_lastSuperPressed = superPressed;
    m_lastAltPressed = altPressed;
    m_lastControlPressed = controlPressed;
}

bool PollingModifierKeyMonitor::isKeyPressed(unsigned long keysym)
{
    if (!m_display) return false;
    
    KeyCode keycode = XKeysymToKeycode(m_display, keysym);
    if (keycode == 0) return false;
    
    char keys[32];
    XQueryKeymap(m_display, keys);
    
    return (keys[keycode / 8] & (1 << (keycode % 8))) != 0;
}

bool PollingModifierKeyMonitor::isKeyboardGrabbed()
{
    if (!m_connection) return false;
    
    const xcb_setup_t *setup = xcb_get_setup(m_connection);
    xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
    xcb_window_t rootWin = iter.data->root;
    
    xcb_grab_keyboard_cookie_t cookie = xcb_grab_keyboard(
        m_connection,
        0,  // owner_events = false
        rootWin,
        XCB_CURRENT_TIME,
        XCB_GRAB_MODE_ASYNC,
        XCB_GRAB_MODE_ASYNC
    );
    
    xcb_grab_keyboard_reply_t *reply = xcb_grab_keyboard_reply(m_connection, cookie, nullptr);
    
    if (reply) {
        uint8_t status = reply->status;
        free(reply);
        
        if (status == XCB_GRAB_STATUS_SUCCESS) {
            xcb_ungrab_keyboard(m_connection, XCB_CURRENT_TIME);
            xcb_flush(m_connection);
            return false;
        } else if (status == XCB_GRAB_STATUS_ALREADY_GRABBED) {
            return true;
        }
    }
    
    return false;
}
