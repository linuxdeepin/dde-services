// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "modifierkeystate.h"

#include <QObject>
#include <QSocketNotifier>
#include <QList>

#include <optional>

#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>

typedef struct _XDisplay Display;

class ModifierKeyMonitor : public QObject
{
    Q_OBJECT
public:
    explicit ModifierKeyMonitor(QObject *parent = nullptr, bool allowRecord = true);
    ~ModifierKeyMonitor() override;

    void start();
    bool isAvailable() const;
    bool isRunning() const;
    bool supportsGrabResilientEvents() const;
    void stop();
    void refreshKeyboardMapping();
    void notifyNonModifierKeyPressed();

signals:
    void modifierKeyReleased(unsigned long keysym);
    void keyEventRecorded(bool pressed, quint8 keycode, quint16 state, quint32 time);

private slots:
    void handleEvents();

private:
    enum class RecordState {
        Disabled,
        Enabling,
        Enabled,
        Disabling,
    };

    enum class Transport {
        Unavailable,
        Record,
        XInput2,
    };

    bool initializeRecord();
    bool initializeRawEvents();
    void fallbackToXInput2();
    void cleanupRecordResources();
    void discardPendingInputEvents();
    bool createContext();
    void freeContext();
    bool openDataConnection();
    void closeDataConnection();
    void enableContext();
    void disableContext();
    void handleRecordedData(void *recordedData);
    void handleRawEvents();
    void handleRawEvent(xcb_generic_event_t *event);
    bool parseModifierKeyEvent(const xcb_generic_event_t *event,
                               bool &pressed, xcb_keycode_t &keycode) const;
    std::optional<QSet<quint8>> queryPressedModifiers() const;
    void handleRawKey(bool pressed, xcb_keycode_t keycode);
    bool isModifierKey(xcb_keysym_t keysym) const;
    void handleKey(bool pressed, xcb_keycode_t keycode, uint16_t state, uint32_t time);

    Display *m_controlDisplay = nullptr;
    Display *m_dataDisplay = nullptr;
    xcb_connection_t *m_keyConnection = nullptr;
    xcb_key_symbols_t *m_keySymbols = nullptr;
    QSocketNotifier *m_notifier = nullptr;
    unsigned long m_recordContext = 0;
    xcb_connection_t *m_eventConnection = nullptr;
    QList<xcb_window_t> m_rootWindows;
    uint8_t m_inputOpcode = 0;
    ModifierKeyState m_state;
    Transport m_transport = Transport::Unavailable;
    RecordState m_recordState = RecordState::Disabled;
    bool m_available = false;
    bool m_startRequested = false;
    bool m_running = false;
};
