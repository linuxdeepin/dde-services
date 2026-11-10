// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QObject>
#include <QSet>

#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>

class QSocketNotifier;
class QTimer;
struct _XDisplay;

class X11ShortcutCapture : public QObject
{
    Q_OBJECT

public:
    explicit X11ShortcutCapture(QObject *parent = nullptr);
    ~X11ShortcutCapture() override;

    bool isAvailable() const;
    bool isActive() const { return m_active; }
    bool start(uint timeoutMs = 30000);
    void cancel();

Q_SIGNALS:
    void captured(const QString &shortcut);
    void failed(const QString &reason);

private:
    friend class TestX11ShortcutCapture;

    struct CapturedKey {
        QString keystroke;
        QString keyName;
        xcb_keysym_t keysym = XCB_NO_SYMBOL;
        uint16_t modifiers = 0;
    };

    void handleEvents();
    void flushPendingRelease();
    void completePendingShortcut();
    void finish(bool notifyFailure, const QString &reason = {});
    void drainEvents();
    void refreshModifierMasks();
    CapturedKey captureKey(const xcb_key_press_event_t *event) const;
    bool isCapturedKeyValid(const CapturedKey &key) const;
    bool hasAnyMask(uint16_t state, const QSet<uint16_t> &masks) const;

    xcb_connection_t *m_connection = nullptr;
    xcb_screen_t *m_screen = nullptr;
    xcb_key_symbols_t *m_keySymbols = nullptr;
    _XDisplay *m_display = nullptr;
    QSocketNotifier *m_notifier = nullptr;
    QTimer *m_timer = nullptr;
    QTimer *m_releaseTimer = nullptr;
    QSet<uint16_t> m_altMasks;
    QSet<uint16_t> m_superMasks;
    QString m_pendingShortcut;
    xcb_keycode_t m_pendingKeycode = 0;
    xcb_timestamp_t m_pendingReleaseTimestamp = 0;
    bool m_releasePending = false;
    bool m_detectableAutoRepeat = false;
    bool m_active = false;
};
