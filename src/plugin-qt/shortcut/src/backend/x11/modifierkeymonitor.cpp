// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "shortcutlogging.h"

#include "modifierkeymonitor.h"

#include <QDebug>
#include <QMetaObject>

#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xcb/xinput.h>
#include <X11/Xlib.h>
#include <X11/Xlib-xcb.h>
#include <X11/extensions/record.h>
#include <X11/keysym.h>

ModifierKeyMonitor::ModifierKeyMonitor(QObject *parent, bool allowRecord)
    : QObject(parent)
{
    if (allowRecord && initializeRecord()) {
        m_transport = Transport::Record;
        m_available = true;
    } else {
        cleanupRecordResources();
        if (initializeRawEvents()) {
            m_transport = Transport::XInput2;
            m_available = true;
            qCInfo(logShortcut) << "ModifierMonitor: using XInput2 fallback";
        }
    }
}

ModifierKeyMonitor::~ModifierKeyMonitor()
{
    stop();
    if (m_notifier)
        m_notifier->setEnabled(false);
    if (m_recordContext && m_controlDisplay) {
        XRecordDisableContext(m_controlDisplay, m_recordContext);
        XSync(m_controlDisplay, False);
    }
    if (m_keySymbols)
        xcb_key_symbols_free(m_keySymbols);
    if (m_keyConnection)
        xcb_disconnect(m_keyConnection);
    closeDataConnection();
    if (m_eventConnection)
        xcb_disconnect(m_eventConnection);
    if (m_recordContext && m_controlDisplay) {
        freeContext();
    }
    if (m_controlDisplay)
        XCloseDisplay(m_controlDisplay);
}

void ModifierKeyMonitor::start()
{
    if (!m_available)
        return;

    m_startRequested = true;
    m_state.reset();
    if (m_transport == Transport::XInput2) {
        discardPendingInputEvents();
        m_running = true;
        if (m_notifier)
            m_notifier->setEnabled(true);
        return;
    }
    if (m_recordState == RecordState::Disabled) {
        enableContext();
    } else if (m_recordState == RecordState::Enabled) {
        m_running = true;
    }
}

bool ModifierKeyMonitor::isAvailable() const
{
    return m_available;
}

bool ModifierKeyMonitor::isRunning() const
{
    return m_running;
}

bool ModifierKeyMonitor::supportsGrabResilientEvents() const
{
    return m_transport == Transport::Record && m_available;
}

void ModifierKeyMonitor::refreshKeyboardMapping()
{
    if (m_transport == Transport::XInput2) {
        if (m_keySymbols)
            xcb_key_symbols_free(m_keySymbols);
        m_keySymbols = m_eventConnection
                ? xcb_key_symbols_alloc(m_eventConnection) : nullptr;
        m_state.reset();
        return;
    }
    if (!m_keyConnection)
        return;

    if (m_keySymbols)
        xcb_key_symbols_free(m_keySymbols);
    m_keySymbols = xcb_key_symbols_alloc(m_keyConnection);
    if (!m_keySymbols)
        qCWarning(logShortcut) << "ModifierMonitor: failed to refresh RECORD key symbols";
    m_state.reset();
}

void ModifierKeyMonitor::stop()
{
    m_startRequested = false;
    m_running = false;
    m_state.reset();

    if (m_transport == Transport::XInput2) {
        if (m_notifier)
            m_notifier->setEnabled(false);
        discardPendingInputEvents();
        return;
    }

    if (m_recordState == RecordState::Enabled)
        disableContext();
    else if (m_recordState == RecordState::Disabled && m_notifier)
        m_notifier->setEnabled(false);
}

void ModifierKeyMonitor::notifyNonModifierKeyPressed()
{
    m_state.notifyNonModifierActivity();
}

bool ModifierKeyMonitor::initializeRecord()
{
    m_controlDisplay = XOpenDisplay(nullptr);
    if (!m_controlDisplay) {
        qCWarning(logShortcut) << "ModifierMonitor: failed to connect to X server for RECORD";
        return false;
    }

    int major = 0;
    int minor = 0;
    if (!XRecordQueryVersion(m_controlDisplay, &major, &minor)) {
        qCWarning(logShortcut) << "ModifierMonitor: X RECORD extension is unavailable";
        return false;
    }

    m_keyConnection = xcb_connect(nullptr, nullptr);
    if (!m_keyConnection || xcb_connection_has_error(m_keyConnection)) {
        qCWarning(logShortcut) << "ModifierMonitor: failed to open keymap connection";
        return false;
    }
    m_keySymbols = xcb_key_symbols_alloc(m_keyConnection);
    if (!m_keySymbols) {
        qCWarning(logShortcut) << "ModifierMonitor: failed to initialize RECORD key symbols";
        return false;
    }

    if (!createContext())
        return false;

    qCInfo(logShortcut) << "ModifierMonitor: X RECORD" << major << minor << "available";
    return true;
}

void ModifierKeyMonitor::cleanupRecordResources()
{
    closeDataConnection();
    if (m_keySymbols) {
        xcb_key_symbols_free(m_keySymbols);
        m_keySymbols = nullptr;
    }
    if (m_keyConnection) {
        xcb_disconnect(m_keyConnection);
        m_keyConnection = nullptr;
    }
    if (m_recordContext && m_controlDisplay)
        freeContext();
    if (m_controlDisplay) {
        XCloseDisplay(m_controlDisplay);
        m_controlDisplay = nullptr;
    }
}

void ModifierKeyMonitor::fallbackToXInput2()
{
    const bool shouldRun = m_startRequested;
    m_running = false;
    m_recordState = RecordState::Disabled;
    m_available = false;
    m_transport = Transport::Unavailable;
    cleanupRecordResources();

    if (!initializeRawEvents()) {
        qCWarning(logShortcut) << "ModifierMonitor: XInput2 fallback failed";
        return;
    }

    m_transport = Transport::XInput2;
    m_available = true;
    qCInfo(logShortcut) << "ModifierMonitor: switched to XInput2 fallback";
    if (shouldRun) {
        discardPendingInputEvents();
        m_running = true;
        m_notifier->setEnabled(true);
    }
}

bool ModifierKeyMonitor::initializeRawEvents()
{
    m_eventConnection = xcb_connect(nullptr, nullptr);
    if (!m_eventConnection || xcb_connection_has_error(m_eventConnection)) {
        qCWarning(logShortcut) << "ModifierMonitor: failed to connect for XInput2 fallback";
        return false;
    }

    xcb_screen_iterator_t screenIterator =
            xcb_setup_roots_iterator(xcb_get_setup(m_eventConnection));
    while (screenIterator.rem) {
        m_rootWindows.append(screenIterator.data->root);
        xcb_screen_next(&screenIterator);
    }
    m_keySymbols = xcb_key_symbols_alloc(m_eventConnection);
    const xcb_query_extension_reply_t *extension =
            xcb_get_extension_data(m_eventConnection, &xcb_input_id);
    if (m_rootWindows.isEmpty() || !m_keySymbols || !extension || !extension->present) {
        qCWarning(logShortcut) << "ModifierMonitor: XInput2 fallback is unavailable";
        return false;
    }
    m_inputOpcode = extension->major_opcode;

    xcb_generic_error_t *versionError = nullptr;
    const auto versionCookie = xcb_input_xi_query_version(m_eventConnection, 2, 0);
    xcb_input_xi_query_version_reply_t *versionReply =
            xcb_input_xi_query_version_reply(m_eventConnection, versionCookie, &versionError);
    if (!versionReply || versionError) {
        qCWarning(logShortcut) << "ModifierMonitor: XInput2 version negotiation failed";
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
        const xcb_void_cookie_t cookie = xcb_input_xi_select_events_checked(
                m_eventConnection, rootWindow, 1, &eventMask.header);
        xcb_generic_error_t *error = xcb_request_check(m_eventConnection, cookie);
        if (error) {
            qCWarning(logShortcut) << "ModifierMonitor: failed to select XInput2 raw events:"
                                   << error->error_code;
            free(error);
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

void ModifierKeyMonitor::handleRawEvents()
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
                qCInfo(logShortcut) << "ModifierMonitor: recovered stale XI2 state before key press"
                                    << int(keycode);
            }
            ++pressIndex;
        }
        handleRawEvent(pendingEvent);
        free(pendingEvent);
    }

    if (physicallyPressed && !mappingChanged
            && m_state.reconcileAtEventBoundary(physicallyPressed.value())) {
        qCInfo(logShortcut) << "ModifierMonitor: recovered stale XI2 state after event batch";
    }
}

void ModifierKeyMonitor::handleRawEvent(xcb_generic_event_t *event)
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

bool ModifierKeyMonitor::parseModifierKeyEvent(const xcb_generic_event_t *event,
                                               bool &pressed,
                                               xcb_keycode_t &keycode) const
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
    if (!m_keySymbols
            || !isModifierKey(xcb_key_symbols_get_keysym(m_keySymbols, keycode, 0))) {
        return false;
    }

    pressed = genericEvent->event_type == XCB_INPUT_RAW_KEY_PRESS;
    return true;
}

std::optional<QSet<quint8>> ModifierKeyMonitor::queryPressedModifiers() const
{
    if (!m_eventConnection || xcb_connection_has_error(m_eventConnection))
        return std::nullopt;

    xcb_generic_error_t *error = nullptr;
    const xcb_query_keymap_cookie_t cookie = xcb_query_keymap(m_eventConnection);
    xcb_query_keymap_reply_t *reply =
            xcb_query_keymap_reply(m_eventConnection, cookie, &error);
    if (!reply || error) {
        free(reply);
        free(error);
        return std::nullopt;
    }

    QSet<quint8> pressedModifiers;
    for (int keycode = 8; keycode < 256; ++keycode) {
        const bool pressed = reply->keys[keycode / 8] & (1U << (keycode % 8));
        if (pressed && m_keySymbols
                && isModifierKey(xcb_key_symbols_get_keysym(m_keySymbols, keycode, 0))) {
            pressedModifiers.insert(quint8(keycode));
        }
    }
    free(reply);
    return pressedModifiers;
}

void ModifierKeyMonitor::handleRawKey(bool pressed, xcb_keycode_t keycode)
{
    if (!m_keySymbols)
        return;
    const xcb_keysym_t keysym = xcb_key_symbols_get_keysym(m_keySymbols, keycode, 0);
    if (!isModifierKey(keysym)) {
        if (pressed)
            m_state.notifyNonModifierActivity();
        return;
    }

    if (pressed) {
        m_state.press(keycode);
    } else if (m_state.release(keycode)) {
        emit modifierKeyReleased(keysym);
    }
}

bool ModifierKeyMonitor::createContext()
{
    XRecordRange *range = XRecordAllocRange();
    if (!range) {
        qCWarning(logShortcut) << "ModifierMonitor: failed to allocate RECORD range";
        return false;
    }
    range->device_events.first = KeyPress;
    range->device_events.last = ButtonRelease;

    XRecordClientSpec clients[] = {XRecordAllClients};
    m_recordContext = XRecordCreateContext(m_controlDisplay, 0, clients, 1, &range, 1);
    XFree(range);
    if (!m_recordContext) {
        qCWarning(logShortcut) << "ModifierMonitor: failed to create RECORD context";
        return false;
    }
    // The context is created on the control connection and enabled on the
    // data connection. Ensure the server has processed the create request
    // before the second connection references that XID.
    XSync(m_controlDisplay, False);

    return true;
}

void ModifierKeyMonitor::freeContext()
{
    if (!m_recordContext || !m_controlDisplay)
        return;
    XRecordFreeContext(m_controlDisplay, m_recordContext);
    XSync(m_controlDisplay, False);
    m_recordContext = 0;
}

bool ModifierKeyMonitor::openDataConnection()
{
    closeDataConnection();
    m_dataDisplay = XOpenDisplay(nullptr);
    if (!m_dataDisplay) {
        qCWarning(logShortcut) << "ModifierMonitor: failed to open RECORD data connection";
        return false;
    }

    m_notifier = new QSocketNotifier(ConnectionNumber(m_dataDisplay),
                                     QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated,
            this, &ModifierKeyMonitor::handleEvents);
    return true;
}

void ModifierKeyMonitor::closeDataConnection()
{
    if (m_notifier) {
        m_notifier->setEnabled(false);
        delete m_notifier;
        m_notifier = nullptr;
    }
    if (m_dataDisplay) {
        XCloseDisplay(m_dataDisplay);
        m_dataDisplay = nullptr;
    }
}

void ModifierKeyMonitor::enableContext()
{
    if (!m_available || !m_startRequested || !m_recordContext
            || m_recordState != RecordState::Disabled) {
        return;
    }

    if (!openDataConnection()) {
        fallbackToXInput2();
        return;
    }

    // The callback can be invoked synchronously by EnableContextAsync, so the
    // state must be visible before entering Xlib.
    m_recordState = RecordState::Enabling;
    if (!XRecordEnableContextAsync(m_dataDisplay, m_recordContext,
                                   [](XPointer closure, XRecordInterceptData *data) {
                                       auto *monitor = reinterpret_cast<ModifierKeyMonitor *>(closure);
                                       if (monitor)
                                           monitor->handleRecordedData(data);
                                       else
                                           XRecordFreeData(data);
                                   },
                                   reinterpret_cast<XPointer>(this))) {
        qCWarning(logShortcut) << "ModifierMonitor: failed to enable RECORD context";
        m_recordState = RecordState::Disabled;
        fallbackToXInput2();
        return;
    }

    m_notifier->setEnabled(true);
    XFlush(m_dataDisplay);
    // EnableContextAsync may already have pulled StartOfData into Xlib's
    // userspace buffer. Process it now because the fd would no longer become
    // readable and therefore would not wake the socket notifier.
    XRecordProcessReplies(m_dataDisplay);
}

void ModifierKeyMonitor::disableContext()
{
    if (!m_controlDisplay || !m_recordContext
            || m_recordState != RecordState::Enabled) {
        return;
    }

    m_running = false;
    m_recordState = RecordState::Disabling;
    if (!XRecordDisableContext(m_controlDisplay, m_recordContext)) {
        qCWarning(logShortcut) << "ModifierMonitor: failed to disable RECORD context";
        fallbackToXInput2();
        return;
    }
    XFlush(m_controlDisplay);
}

void ModifierKeyMonitor::handleEvents()
{
    if (m_transport == Transport::XInput2) {
        handleRawEvents();
        return;
    }
    if (m_recordState != RecordState::Disabled && m_dataDisplay)
        XRecordProcessReplies(m_dataDisplay);
}

void ModifierKeyMonitor::handleRecordedData(void *data)
{
    auto *recordedData = static_cast<XRecordInterceptData *>(data);
    if (!recordedData)
        return;

    if (recordedData->category == XRecordStartOfData) {
        if (m_recordState == RecordState::Enabling)
            m_recordState = RecordState::Enabled;
        if (m_startRequested) {
            m_running = true;
        } else {
            QMetaObject::invokeMethod(this, &ModifierKeyMonitor::disableContext,
                                      Qt::QueuedConnection);
        }
    } else if (recordedData->category == XRecordEndOfData) {
        m_running = false;
        m_recordState = RecordState::Disabled;
        m_state.reset();
        // The data connection belongs to exactly one RECORD epoch. Recreate
        // it before the next enable so delayed bytes from a stopped capture
        // can never be interpreted as live input in the next epoch.
        QMetaObject::invokeMethod(this, [this] {
            closeDataConnection();
            freeContext();
            if (!createContext()) {
                fallbackToXInput2();
                return;
            }
            if (m_startRequested)
                enableContext();
        }, Qt::QueuedConnection);
    } else if (m_running && m_recordState == RecordState::Enabled
            && recordedData->category == XRecordFromServer
            && !recordedData->client_swapped) {
        const unsigned long byteCount = recordedData->data_len * 4;
        for (unsigned long offset = 0; offset + 32 <= byteCount; offset += 32) {
            const auto *event = reinterpret_cast<const xcb_generic_event_t *>(
                    recordedData->data + offset);
            const uint8_t type = event->response_type & 0x7f;
            if (type == XCB_KEY_PRESS || type == XCB_KEY_RELEASE) {
                const auto *keyEvent = reinterpret_cast<const xcb_key_press_event_t *>(event);
                handleKey(type == XCB_KEY_PRESS, keyEvent->detail,
                          keyEvent->state, keyEvent->time);
            } else if (type == XCB_BUTTON_PRESS) {
                m_state.notifyNonModifierActivity();
            }
        }
    }
    XRecordFreeData(recordedData);
}

void ModifierKeyMonitor::handleKey(bool pressed, xcb_keycode_t keycode,
                                   uint16_t state, uint32_t time)
{
    if (!m_keySymbols)
        return;

    const xcb_keysym_t keysym = xcb_key_symbols_get_keysym(m_keySymbols, keycode, 0);
    if (!isModifierKey(keysym)) {
        if (pressed)
            m_state.notifyNonModifierActivity();
    } else if (pressed) {
        m_state.press(keycode);
    } else if (m_state.release(keycode)) {
        emit modifierKeyReleased(keysym);
    }

    emit keyEventRecorded(pressed, quint8(keycode), quint16(state), quint32(time));
}

bool ModifierKeyMonitor::isModifierKey(xcb_keysym_t keysym) const
{
    return keysym == XK_Super_L || keysym == XK_Super_R
            || keysym == XK_Meta_L || keysym == XK_Meta_R
            || keysym == XK_Alt_L || keysym == XK_Alt_R
            || keysym == XK_Control_L || keysym == XK_Control_R
            || keysym == XK_Shift_L || keysym == XK_Shift_R
            || keysym == XK_Caps_Lock || keysym == XK_Num_Lock;
}
