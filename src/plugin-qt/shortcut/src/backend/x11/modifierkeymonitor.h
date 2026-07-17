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

class ModifierKeyMonitor : public QObject
{
    Q_OBJECT
public:
    explicit ModifierKeyMonitor(QObject *parent = nullptr);
    ~ModifierKeyMonitor() override;

    void start();
    bool isAvailable() const;
    void stop();
    void notifyNonModifierKeyPressed();

signals:
    /**
     * @brief 单独修饰键被释放
     * @param keysym X11 keysym (如 XK_Super_L, XK_Alt_L等)
     */
    void modifierKeyReleased(unsigned long keysym);
private slots:
    void handleEvents();

private:
    bool initializeRawEvents();
    void discardPendingInputEvents();
    bool isModifierKey(xcb_keysym_t keysym) const;
    bool parseModifierKeyEvent(const xcb_generic_event_t *event,
                               bool &pressed, xcb_keycode_t &keycode) const;
    std::optional<QSet<quint8>> queryPressedModifiers() const;
    void handleEvent(xcb_generic_event_t *event);
    void handleRawKey(bool pressed, xcb_keycode_t keycode);

    xcb_connection_t *m_eventConnection = nullptr;
    xcb_screen_t *m_screen = nullptr;
    QList<xcb_window_t> m_rootWindows;
    xcb_key_symbols_t *m_keySymbols = nullptr;
    QSocketNotifier *m_notifier = nullptr;
    uint8_t m_inputOpcode = 0;
    ModifierKeyState m_state;
    bool m_available = false;
};
