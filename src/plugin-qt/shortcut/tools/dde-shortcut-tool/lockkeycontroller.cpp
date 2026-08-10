// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "lockkeycontroller.h"

#include <DGuiApplicationHelper>

#include <QDebug>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingReply>

#include <DConfig>

// X11 headers
#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>

#define XK_MISCELLANY
#include <X11/keysymdef.h>

DCORE_USE_NAMESPACE
DGUI_USE_NAMESPACE

namespace {
constexpr const char *kKeyboardConfigAppId = "org.deepin.dde.keybinding";
constexpr const char *kKeyboardConfigName = "org.deepin.dde.keybinding.keyboard";
constexpr const char *kLegacyKeyboardConfigAppId = "org.deepin.dde.daemon";
constexpr const char *kLegacyKeyboardConfigName = "org.deepin.dde.daemon.keyboard";
constexpr const char *kCapsLockToggleKey = "capslockToggle";
constexpr const char *kSaveNumLockStateKey = "saveNumlockState";
constexpr const char *kNumLockStateKey = "numlockState";
constexpr uint kNumLockUnknown = 2;
}

LockKeyController::LockKeyController(QObject *parent) 
    : BaseController(parent)
    , m_keyboardConfig(nullptr)
    , m_connection(nullptr)
    , m_keySymbols(nullptr)
    , m_screen(nullptr)
    , m_isWayland(false)
{
    // Detect session type
    m_isWayland = isWayland();
    
    // Lock-key settings are owned and installed by the shortcut service.
    // X11NumLockStateController normally owns NumLock persistence; this tool
    // retains a compatibility write for mixed-version live upgrades.
    m_keyboardConfig = DConfig::create(kKeyboardConfigAppId,
                                        kKeyboardConfigName,
                                        "", this);
    if (!m_keyboardConfig || !m_keyboardConfig->isValid()) {
        qWarning() << "Keyboard config not available";
    }
    
    // Initialize X11 connection (if not Wayland)
    if (!m_isWayland) {
        initX11();
    }
}

LockKeyController::~LockKeyController()
{
    cleanupX11();
}

bool LockKeyController::isWayland() const
{
    return DGuiApplicationHelper::testAttribute(DGuiApplicationHelper::IsWaylandPlatform);
}

void LockKeyController::initX11()
{
    m_connection = xcb_connect(nullptr, nullptr);
    if (xcb_connection_has_error(m_connection)) {
        qWarning() << "Failed to connect to X11 server";
        m_connection = nullptr;
        return;
    }
    
    const xcb_setup_t *setup = xcb_get_setup(m_connection);
    xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
    m_screen = iter.data;
    
    m_keySymbols = xcb_key_symbols_alloc(m_connection);
}

void LockKeyController::cleanupX11()
{
    if (m_keySymbols) {
        xcb_key_symbols_free(m_keySymbols);
        m_keySymbols = nullptr;
    }
    if (m_connection) {
        xcb_disconnect(m_connection);
        m_connection = nullptr;
    }
}

QStringList LockKeyController::commandActions()
{
    return QStringList{
        "capslock",
        "numlock"
    };
}

QMap<QString, QString> LockKeyController::commandActionHelp()
{
    return {
        {"capslock", "Show CapsLock OSD based on current state"},
        {"numlock", "Show NumLock OSD based on current state"}
    };
}

QStringList LockKeyController::supportedActions() const
{
    return commandActions();
}

bool LockKeyController::execute(const QString &action, const QStringList &args)
{
    Q_UNUSED(args);
    
    if (action == "capslock") {
        handleCapsLockOSD();
        return true;
    } else if (action == "numlock") {
        handleNumLockOSD();
        return true;
    }
    
    qWarning() << "Unknown lockkey action:" << action;
    return false;
}

QString LockKeyController::actionHelp(const QString &action) const
{
    return commandActionHelp().value(action);
}

void LockKeyController::handleCapsLockOSD()
{
    if (!shouldShowCapsLockOSD()) {
        qDebug() << "CapsLock OSD disabled";
        return;
    }
    
    int state = queryCapsLockState();
    if (state < 0) {
        qWarning() << "Failed to query CapsLock state";
        return;
    }
    
    if (state == 1) { // CapsLock On
        showOSD("CapsLockOn");
    } else { // CapsLock Off
        showOSD("CapsLockOff");
    }
}

void LockKeyController::handleNumLockOSD()
{
    int state = queryNumLockState();
    if (state < 0) {
        qWarning() << "Failed to query NumLock state";
        return;
    }

    saveNumLockStateForUpgradeCompatibility(state);

    if (state == 1) { // NumLock On
        showOSD("NumLockOn");
    } else { // NumLock Off
        showOSD("NumLockOff");
    }
}

void LockKeyController::saveNumLockStateForUpgradeCompatibility(int state)
{
    if (m_isWayland || !m_keyboardConfig || !m_keyboardConfig->isValid())
        return;

    bool ok = false;
    const uint configuredState = m_keyboardConfig->value(kNumLockStateKey).toUInt(&ok);
    if (ok && configuredState == kNumLockUnknown) {
        // During a live upgrade the old plugin can remain loaded while this
        // helper has already been replaced. Preserve the legacy save switch
        // before the new controller gets a chance to run its full migration.
        auto *legacyConfig = DConfig::create(kLegacyKeyboardConfigAppId,
                                             kLegacyKeyboardConfigName,
                                             "", this);
        if (legacyConfig && legacyConfig->isValid()) {
            const QVariant legacySaveState = legacyConfig->value(kSaveNumLockStateKey);
            if (legacySaveState.isValid())
                m_keyboardConfig->setValue(kSaveNumLockStateKey,
                                           legacySaveState.toBool());

            bool legacyStateOk = false;
            const uint legacyState = legacyConfig->value(kNumLockStateKey)
                                             .toUInt(&legacyStateOk);
            if (legacyStateOk && legacyState <= 1)
                m_keyboardConfig->setValue(kNumLockStateKey,
                                           static_cast<int>(legacyState));
        }
        if (legacyConfig)
            legacyConfig->deleteLater();
    }

    if (!m_keyboardConfig->value(kSaveNumLockStateKey).toBool())
        return;

    if (!ok || configuredState != static_cast<uint>(state))
        m_keyboardConfig->setValue(kNumLockStateKey, state);
}

int LockKeyController::queryCapsLockState()
{
    if (!m_isWayland) {
        return queryCapsLockStateX11();
    } else {
        return queryCapsLockStateWayland();
    }
}

int LockKeyController::queryNumLockState()
{
    if (!m_isWayland) {
        return queryNumLockStateX11();
    } else {
        return queryNumLockStateWayland();
    }
}

bool LockKeyController::shouldShowCapsLockOSD()
{
    if (!m_keyboardConfig || !m_keyboardConfig->isValid()) {
        return true; // Show by default
    }
    
    QVariant showValue = m_keyboardConfig->value(kCapsLockToggleKey);
    return showValue.toBool();
}

void LockKeyController::showOSD(const QString &signal)
{
    QDBusConnection sessionBus = QDBusConnection::sessionBus();
    if (!sessionBus.isConnected()) {
        qWarning() << "Failed to connect to session bus for OSD";
        return;
    }
    
    QDBusMessage call = QDBusMessage::createMethodCall(
        "org.deepin.dde.Osd1",
        "/org/deepin/dde/shell/osd",
        "org.deepin.dde.shell.osd",
        "ShowOSD"
    );
    call.setArguments(QVariantList() << signal);
    
    // Async call to avoid blocking
    sessionBus.asyncCall(call);
    
    qDebug() << "ShowOSD:" << signal;
}

int LockKeyController::queryCapsLockStateX11()
{
    if (!m_connection) {
        qWarning() << "X11 connection not available";
        return -1;
    }
    
    // Query pointer to get modifier state
    xcb_query_pointer_cookie_t cookie = xcb_query_pointer(m_connection, m_screen->root);
    xcb_query_pointer_reply_t *reply = xcb_query_pointer_reply(m_connection, cookie, nullptr);
    
    if (!reply) {
        qWarning() << "Failed to query pointer for modifier state";
        return -1;
    }

    // CapsLock is Lock modifier
    bool state = (reply->mask & XCB_MOD_MASK_LOCK) != 0;
    
    free(reply);
    return state ? 1 : 0;
}


int LockKeyController::queryNumLockStateX11()
{
    if (!m_connection) {
        qWarning() << "X11 connection not available";
        return -1;
    }
    
    // Query pointer to get modifier state
    xcb_query_pointer_cookie_t cookie = xcb_query_pointer(m_connection, m_screen->root);
    xcb_query_pointer_reply_t *reply = xcb_query_pointer_reply(m_connection, cookie, nullptr);
    
    if (!reply) {
        qWarning() << "Failed to query pointer for modifier state";
        return -1;
    }

    // NumLock is typically Mod2
    bool state = (reply->mask & XCB_MOD_MASK_2) != 0;
    
    free(reply);
    return state ? 1 : 0;
}

int LockKeyController::queryCapsLockStateWayland()
{
    QDBusMessage call = QDBusMessage::createMethodCall("org.deepin.dde.Keybinding1",
         "/org/deepin/dde/Keybinding1", "org.deepin.dde.Keybinding1", "GetCapsLockState");
    QDBusPendingReply<uint> pendingReply = QDBusConnection::sessionBus().asyncCall(call);
    pendingReply.waitForFinished();
    if (pendingReply.isError()) {
        qWarning() << "Failed to get lock state via D-Bus:" << pendingReply.error().message();
        return -1;
    }

    return pendingReply.value() ? 1 : 0;
}

int LockKeyController::queryNumLockStateWayland()
{
    QDBusMessage call = QDBusMessage::createMethodCall("org.deepin.dde.Keybinding1",
         "/org/deepin/dde/Keybinding1", "org.deepin.dde.Keybinding1", "GetNumLockState");
    QDBusPendingReply<uint> pendingReply = QDBusConnection::sessionBus().asyncCall(call);
    pendingReply.waitForFinished();
    if (pendingReply.isError()) {
        qWarning() << "Failed to get lock state via D-Bus:" << pendingReply.error().message();
        return -1;
    }

    return pendingReply.value() ? 1 : 0;
}
