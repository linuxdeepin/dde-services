// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "backend/abstractkeyhandler.h"

#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>
#include <QSocketNotifier>
#include <QList>
#include <QMap>
#include <QSet>
#include <QTimer>

// Forward declaration
class ModifierKeyMonitor;
class QDBusServiceWatcher;
typedef struct _XDisplay Display;

class X11KeyHandler : public AbstractKeyHandler
{
    Q_OBJECT
public:
    explicit X11KeyHandler(QObject *parent = nullptr);
    ~X11KeyHandler() override;

    bool registerKey(const KeyConfig &config) override;
    bool unregisterKey(const QString &appId) override;
    bool isAvailable() const override;
    bool beginCapture(uint timeoutMs, const QString &owner) override;
    bool endCapture(const QString &owner) override;

    // Lock key state operations
    bool getCapsLockState() const override;
    bool getNumLockState() const override;
    void setCapsLockState(bool on) override;
    void setNumLockState(bool on) override;

private slots:
    void handleXcbEvents();
    void onModifierKeyReleased(unsigned long keysym);
    void flushPendingReleases();
    void notifyKeymapChanged();

private:
    enum class WmSetAccelSignature {
        Unknown,
        DataOnly,
        ContextAndData,
    };

    enum class HotkeyResolution {
        Resolved,
        InvalidSpecification,
        UnavailableInKeymap,
    };

    struct ResolvedHotkey {
        QList<xcb_keycode_t> keycodes;
        QList<uint16_t> modifierCombinations;
        xcb_keysym_t keysym = XCB_NO_SYMBOL;
    };

    struct HotkeyResolutionResult {
        HotkeyResolution resolution = HotkeyResolution::InvalidSpecification;
        ResolvedHotkey hotkey;
    };

    struct CapturedKey {
        QString keystroke;
        QString keyName;
        xcb_keysym_t keysym = XCB_NO_SYMBOL;
        uint16_t modifiers = 0;
    };

    bool grabKey(xcb_keycode_t keycode, uint16_t modifiers);
    bool ungrabKey(xcb_keycode_t keycode, uint16_t modifiers);
    bool setWmShortcut(const QString &wmShortcutId, const QStringList &hotkeys);
    
    // Helpers
    HotkeyResolutionResult resolveHotkey(const QString &hotkey);
    uint16_t getConcernedMods(uint16_t state);
    QList<uint16_t> ignoredModifierCombinations() const;
    void refreshModifierMasks();
    void enableDetectableAutoRepeat();
    void handleKeyPress(const xcb_key_press_event_t *event);
    void handleKeyRelease(const xcb_key_release_event_t *event);
    CapturedKey captureKey(const xcb_key_press_event_t *event) const;
    bool isCapturedKeyValid(const CapturedKey &key) const;
    bool hasAnyMask(uint16_t state, const QList<uint16_t> &masks) const;
    void activate(const QString &shortcutId, int eventFlag);
    void clearPressedState(const QString &shortcutId);
    void finishCapture(bool notify = true);
    void scheduleKeymapChanged();
    
    // Standalone modifier helpers
    bool isStandaloneModifierKey(xcb_keysym_t keysym, uint16_t mods) const;

    // X11 connection and event dispatch.
    xcb_connection_t *m_connection = nullptr;
    Display *m_display = nullptr;
    QList<xcb_window_t> m_rootWindows;
    xcb_key_symbols_t *m_keySymbols = nullptr;
    QSocketNotifier *m_notifier = nullptr;

    // Standalone modifier shortcut monitoring.
    ModifierKeyMonitor *m_modifierMonitor = nullptr;

    // Registered shortcut lookup tables.
    QMap<uint32_t, QString> m_grabbedKeys;
    QMap<QString, QList<uint32_t>> m_shortcutKeys;
    QMap<QString, QString> m_wmShortcutIds;
    QMap<QString, int> m_shortcutFlags;
    QSet<uint32_t> m_standaloneModifierKeys;
    WmSetAccelSignature m_wmSetAccelSignature = WmSetAccelSignature::Unknown;

    // Press, release, and autorepeat tracking.
    QMap<xcb_keycode_t, QString> m_pressedBindings;
    QMap<xcb_keycode_t, xcb_timestamp_t> m_pendingReleases;
    QTimer *m_releaseTimer = nullptr;
    bool m_detectableAutoRepeat = false;

    // Interactive shortcut capture session.
    struct CaptureState {
        QTimer *timer = nullptr;
        QDBusServiceWatcher *ownerWatcher = nullptr;
        QString owner;
        QString keystroke;
        bool active = false;
    };
    CaptureState m_capture;

    // Deferred keyboard mapping notifications.
    bool m_keymapChangePending = false;
    bool m_keymapReloadAfterCapture = false;

    // Modifier masks resolved from the current X11 keyboard mapping.
    QList<uint16_t> m_altMasks{XCB_MOD_MASK_1};
    QList<uint16_t> m_superMasks{XCB_MOD_MASK_4};
    uint16_t m_capsLockMask = XCB_MOD_MASK_LOCK;
    uint16_t m_numLockMask = XCB_MOD_MASK_2;
    uint16_t m_scrollLockMask = 0;
};
