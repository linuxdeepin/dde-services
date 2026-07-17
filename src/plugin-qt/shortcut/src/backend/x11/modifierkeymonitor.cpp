// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "modifierkeymonitor.h"

#include <QDebug>

#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xcb/xinput.h>
#include <X11/keysym.h>

ModifierKeyMonitor::ModifierKeyMonitor(QObject *parent)
    : QObject(parent)
{
    m_available = initializeRawEvents();
}

ModifierKeyMonitor::~ModifierKeyMonitor()
{
    stop();
    if (m_keySymbols)
        xcb_key_symbols_free(m_keySymbols);
    if (m_eventConnection)
        xcb_disconnect(m_eventConnection);
}

void ModifierKeyMonitor::start()
{
    if (m_available && m_notifier) {
        discardPendingInputEvents();
        m_notifier->setEnabled(true);
    }
}

bool ModifierKeyMonitor::isAvailable() const
{
    return m_available;
}

void ModifierKeyMonitor::stop()
{
    if (m_notifier)
        m_notifier->setEnabled(false);
    discardPendingInputEvents();
    m_state.reset();
}

void ModifierKeyMonitor::notifyNonModifierKeyPressed()
{
    m_state.notifyNonModifierActivity();
}

bool ModifierKeyMonitor::initializeRawEvents()
{
    m_eventConnection = xcb_connect(nullptr, nullptr);
    if (!m_eventConnection || xcb_connection_has_error(m_eventConnection)) {
        qWarning() << "ModifierMonitor: Failed to connect to X server";
        return false;
    }

    xcb_screen_iterator_t screenIterator = xcb_setup_roots_iterator(xcb_get_setup(m_eventConnection));
    m_screen = screenIterator.data;
    while (screenIterator.rem) {
        m_rootWindows.append(screenIterator.data->root);
        xcb_screen_next(&screenIterator);
    }
    m_keySymbols = xcb_key_symbols_alloc(m_eventConnection);
    const xcb_query_extension_reply_t *extension = xcb_get_extension_data(m_eventConnection, &xcb_input_id);
    if (!m_screen || !m_keySymbols || !extension || !extension->present) {
        qWarning() << "ModifierMonitor: XInput2 is unavailable";
        return false;
    }
    m_inputOpcode = extension->major_opcode;

    xcb_generic_error_t *versionError = nullptr;
    const auto versionCookie = xcb_input_xi_query_version(m_eventConnection, 2, 0);
    xcb_input_xi_query_version_reply_t *versionReply =
            xcb_input_xi_query_version_reply(m_eventConnection, versionCookie, &versionError);
    if (!versionReply || versionError) {
        qWarning() << "ModifierMonitor: XInput2 version negotiation failed";
        free(versionReply);
        free(versionError);
        return false;
    }
    free(versionReply);

    struct EventMask {
        xcb_input_event_mask_t header;
        uint32_t mask;
    } eventMask{};
    eventMask.header.deviceid = XCB_INPUT_DEVICE_ALL_MASTER;
    eventMask.header.mask_len = 1;
    eventMask.mask = XCB_INPUT_XI_EVENT_MASK_RAW_KEY_PRESS
            | XCB_INPUT_XI_EVENT_MASK_RAW_KEY_RELEASE
            | XCB_INPUT_XI_EVENT_MASK_RAW_BUTTON_PRESS
            | XCB_INPUT_XI_EVENT_MASK_RAW_BUTTON_RELEASE;

    for (xcb_window_t rootWindow : std::as_const(m_rootWindows)) {
        const xcb_void_cookie_t selectCookie = xcb_input_xi_select_events_checked(
                m_eventConnection, rootWindow, 1, &eventMask.header);
        xcb_generic_error_t *selectError = xcb_request_check(m_eventConnection, selectCookie);
        if (selectError) {
            qWarning() << "ModifierMonitor: Failed to select XInput2 raw events:"
                       << selectError->error_code << "on root" << rootWindow;
            free(selectError);
            return false;
        }
    }

    m_notifier = new QSocketNotifier(xcb_get_file_descriptor(m_eventConnection),
                                     QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated,
            this, &ModifierKeyMonitor::handleEvents);
    m_notifier->setEnabled(false);
    return xcb_flush(m_eventConnection) > 0
            && !xcb_connection_has_error(m_eventConnection);
}

void ModifierKeyMonitor::discardPendingInputEvents()
{
    if (!m_eventConnection)
        return;

    xcb_generic_event_t *event = nullptr;
    while ((event = xcb_poll_for_event(m_eventConnection))) {
        const uint8_t responseType = event->response_type & ~0x80;
        if (responseType == XCB_MAPPING_NOTIFY) {
            auto *mappingEvent = reinterpret_cast<xcb_mapping_notify_event_t *>(event);
            if (mappingEvent->request == XCB_MAPPING_KEYBOARD && m_keySymbols)
                xcb_refresh_keyboard_mapping(m_keySymbols, mappingEvent);
        }
        free(event);
    }
}

void ModifierKeyMonitor::handleEvents()
{
    QList<xcb_generic_event_t *> events;
    bool needsStateSnapshot = false;
    xcb_generic_event_t *event = nullptr;
    while ((event = xcb_poll_for_event(m_eventConnection))) {
        events.append(event);
        bool pressed = false;
        xcb_keycode_t keycode = XCB_NO_SYMBOL;
        if (parseModifierKeyEvent(event, pressed, keycode) && pressed)
            needsStateSnapshot = true;
    }

    std::optional<QSet<quint8>> physicallyPressed;
    if (needsStateSnapshot) {
        physicallyPressed = queryPressedModifiers();
        if (physicallyPressed) {
            // Waiting for QueryKeymap queues every event preceding its reply.
            // Drain only that queue so the snapshot and events share a boundary.
            while ((event = xcb_poll_for_queued_event(m_eventConnection)))
                events.append(event);
        }
    }

    bool mappingChanged = false;
    QList<ModifierKeyTimeline::Transition> transitions;
    for (const xcb_generic_event_t *pendingEvent : std::as_const(events)) {
        const uint8_t responseType = pendingEvent->response_type & ~0x80;
        if (responseType == XCB_MAPPING_NOTIFY) {
            const auto *mappingEvent =
                    reinterpret_cast<const xcb_mapping_notify_event_t *>(pendingEvent);
            mappingChanged |= mappingEvent->request == XCB_MAPPING_KEYBOARD
                    || mappingEvent->request == XCB_MAPPING_MODIFIER;
        }

        bool pressed = false;
        xcb_keycode_t keycode = XCB_NO_SYMBOL;
        if (parseModifierKeyEvent(pendingEvent, pressed, keycode))
            transitions.append({quint8(keycode), pressed});
    }

    QList<QSet<quint8>> statesBeforePresses;
    if (physicallyPressed && !mappingChanged) {
        statesBeforePresses = ModifierKeyTimeline::pressedModifiersBeforePresses(
                transitions, physicallyPressed.value());
    }

    qsizetype pressIndex = 0;
    for (xcb_generic_event_t *pendingEvent : std::as_const(events)) {
        bool pressed = false;
        xcb_keycode_t keycode = XCB_NO_SYMBOL;
        if (parseModifierKeyEvent(pendingEvent, pressed, keycode) && pressed
                && pressIndex < statesBeforePresses.size()) {
            if (m_state.reconcileAtEventBoundary(statesBeforePresses.at(pressIndex))) {
                qInfo() << "ModifierMonitor: recovered stale state before key press"
                        << int(keycode);
            }
            ++pressIndex;
        }
        handleEvent(pendingEvent);
        free(pendingEvent);
    }

    if (physicallyPressed && !mappingChanged
            && m_state.reconcileAtEventBoundary(physicallyPressed.value())) {
        qInfo() << "ModifierMonitor: recovered stale state after event batch";
    }
}

void ModifierKeyMonitor::handleEvent(xcb_generic_event_t *event)
{
    const uint8_t responseType = event->response_type & ~0x80;
    if (responseType == XCB_GE_GENERIC) {
        const auto *genericEvent = reinterpret_cast<const xcb_ge_generic_event_t *>(event);
        if (genericEvent->extension != m_inputOpcode)
            return;

        if (genericEvent->event_type == XCB_INPUT_RAW_KEY_PRESS
                || genericEvent->event_type == XCB_INPUT_RAW_KEY_RELEASE) {
            const auto *keyEvent =
                    reinterpret_cast<const xcb_input_raw_key_press_event_t *>(event);
            handleRawKey(genericEvent->event_type == XCB_INPUT_RAW_KEY_PRESS,
                         xcb_keycode_t(keyEvent->detail));
        } else if (genericEvent->event_type == XCB_INPUT_RAW_BUTTON_PRESS) {
            m_state.notifyNonModifierActivity();
        }
        return;
    }

    if (responseType != XCB_MAPPING_NOTIFY)
        return;

    auto *mappingEvent = reinterpret_cast<xcb_mapping_notify_event_t *>(event);
    if (mappingEvent->request == XCB_MAPPING_KEYBOARD)
        xcb_refresh_keyboard_mapping(m_keySymbols, mappingEvent);
    if (mappingEvent->request == XCB_MAPPING_KEYBOARD
            || mappingEvent->request == XCB_MAPPING_MODIFIER) {
        m_state.reset();
    }
}

void ModifierKeyMonitor::handleRawKey(bool pressed, xcb_keycode_t keycode)
{
    const xcb_keysym_t keysym = xcb_key_symbols_get_keysym(m_keySymbols, keycode, 0);
    if (!isModifierKey(keysym)) {
        if (pressed)
            m_state.notifyNonModifierActivity();
        return;
    }

    if (pressed) {
        m_state.press(keycode);
        return;
    }

    if (m_state.release(keycode))
        emit modifierKeyReleased(keysym);
}

bool ModifierKeyMonitor::isModifierKey(xcb_keysym_t keysym) const
{
    return keysym == XK_Super_L || keysym == XK_Super_R
            || keysym == XK_Meta_L || keysym == XK_Meta_R
            || keysym == XK_Alt_L || keysym == XK_Alt_R
            || keysym == XK_Control_L || keysym == XK_Control_R
            || keysym == XK_Shift_L || keysym == XK_Shift_R;
}

bool ModifierKeyMonitor::parseModifierKeyEvent(const xcb_generic_event_t *event,
                                               bool &pressed, xcb_keycode_t &keycode) const
{
    const uint8_t responseType = event->response_type & ~0x80;
    if (responseType != XCB_GE_GENERIC)
        return false;

    const auto *genericEvent = reinterpret_cast<const xcb_ge_generic_event_t *>(event);
    if (genericEvent->extension != m_inputOpcode
            || (genericEvent->event_type != XCB_INPUT_RAW_KEY_PRESS
                && genericEvent->event_type != XCB_INPUT_RAW_KEY_RELEASE)) {
        return false;
    }

    const auto *keyEvent = reinterpret_cast<const xcb_input_raw_key_press_event_t *>(event);
    keycode = xcb_keycode_t(keyEvent->detail);
    if (!isModifierKey(xcb_key_symbols_get_keysym(m_keySymbols, keycode, 0)))
        return false;

    pressed = genericEvent->event_type == XCB_INPUT_RAW_KEY_PRESS;
    return true;
}

std::optional<QSet<quint8>> ModifierKeyMonitor::queryPressedModifiers() const
{
    if (!m_eventConnection || xcb_connection_has_error(m_eventConnection))
        return std::nullopt;

    xcb_generic_error_t *error = nullptr;
    const xcb_query_keymap_cookie_t cookie = xcb_query_keymap(m_eventConnection);
    xcb_query_keymap_reply_t *reply = xcb_query_keymap_reply(m_eventConnection, cookie, &error);
    if (!reply || error) {
        free(reply);
        free(error);
        return std::nullopt;
    }

    QSet<quint8> pressedModifiers;
    for (int keycode = 8; keycode < 256; ++keycode) {
        const bool pressed = reply->keys[keycode / 8] & (1U << (keycode % 8));
        if (pressed && isModifierKey(xcb_key_symbols_get_keysym(m_keySymbols, keycode, 0)))
            pressedModifiers.insert(quint8(keycode));
    }
    free(reply);
    return pressedModifiers;
}
