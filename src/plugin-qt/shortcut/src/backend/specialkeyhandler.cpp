// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "shortcutlogging.h"

#include "specialkeyhandler.h"

#include <QDebug>
#include <QDBusConnection>
#include <QDBusServiceWatcher>

namespace {

constexpr auto KeyEventService = "org.deepin.dde.KeyEvent1";
constexpr auto KeyEventPath = "/org/deepin/dde/KeyEvent1";
constexpr auto KeyEventInterface = "org.deepin.dde.KeyEvent1";

}

SpecialKeyHandler::SpecialKeyHandler(QObject *parent)
    : QObject(parent)
    , m_connected(false)
{
    // Connect to org.deepin.dde.KeyEvent1 signal
    m_connected = QDBusConnection::systemBus().connect(
        KeyEventService,    // service
        KeyEventPath,       // path
        KeyEventInterface,  // interface
        "KeyEvent",         // signal name
        this,               // receiver
        SLOT(onKeyEvent(uint,bool,bool,bool,bool,bool))
    );

    if (m_connected) {
        qCInfo(logShortcut) << "SpecialKeyHandler: Subscribed to org.deepin.dde.KeyEvent1";
    } else {
        qCWarning(logShortcut) << "SpecialKeyHandler: Failed to connect to org.deepin.dde.KeyEvent1";
    }

    auto *serviceWatcher = new QDBusServiceWatcher(QLatin1String(KeyEventService),
                                                   QDBusConnection::systemBus(),
                                                   QDBusServiceWatcher::WatchForUnregistration,
                                                   this);
    connect(serviceWatcher, &QDBusServiceWatcher::serviceUnregistered,
            this, [this]() {
                qCCritical(logShortcut) << "SpecialKeyHandler: org.deepin.dde.KeyEvent1 stopped;"
                                        << "raw hardware shortcuts are unavailable.";
                m_keysHeld.clear();
                m_suppressedKeys.clear();
            });
}

SpecialKeyHandler::~SpecialKeyHandler()
{
    if (m_connected) {
        QDBusConnection::systemBus().disconnect(
            KeyEventService,
            KeyEventPath,
            KeyEventInterface,
            "KeyEvent",
            this,
            SLOT(onKeyEvent(uint,bool,bool,bool,bool,bool))
        );
    }
}

bool SpecialKeyHandler::registerKey(const KeyConfig &config)
{
    if (!m_connected) {
        qCWarning(logShortcut) << "SpecialKeyHandler: Not connected to KeyEvent1 service";
        return false;
    }

    // Keep local registrations even when KeyEvent1 has no current owner. The
    // D-Bus signal subscription survives service startup and restarts.

    if (m_shortcutKeycodes.contains(config.getId()))
        unregisterKey(config.getId());

    QList<uint32_t> keycodes;
    for (const QString &hotkey : config.hotkeys) {
        if (!isKeycode(hotkey)) {
            continue;  // Skip non-keycode hotkeys
        }

        uint32_t keycode = parseKeycode(hotkey);
        if (keycode == 0) {
            qCWarning(logShortcut) << "SpecialKeyHandler: Invalid keycode:" << hotkey;
            continue;
        }

        if (keycodes.contains(keycode))
            continue;
        
        // Check for conflict
        if (m_keycodeBindings.contains(keycode)) {
            const KeyBinding &existing = m_keycodeBindings.value(keycode);
            qCWarning(logShortcut) << "SpecialKeyHandler: Keycode conflict detected:"
                       << keycode << "(" << QString("0x%1").arg(keycode, 0, 16) << ")"
                       << "already registered by" << existing.shortcutId
                       << "- skipping" << config.getId();
            continue;
        }
        
        KeyBinding binding;
        binding.shortcutId = config.getId();
        binding.keyEventFlags = config.keyEventFlags;
        
        m_keycodeBindings.insert(keycode, binding);
        keycodes.append(keycode);
        
        qCDebug(logShortcut) << "SpecialKeyHandler: Registered keycode" << keycode
                 << "(" << QString("0x%1").arg(keycode, 0, 16) << ")"
                 << "for" << config.getId();
    }
    
    if (!keycodes.isEmpty()) {
        m_shortcutKeycodes.insert(config.getId(), keycodes);
        return true;
    }
    
    return false;
}

bool SpecialKeyHandler::unregisterKey(const QString &shortcutId)
{
    if (!m_shortcutKeycodes.contains(shortcutId)) {
        return false;
    }
    
    const QList<uint32_t> keycodes = m_shortcutKeycodes.take(shortcutId);
    for (uint32_t keycode : keycodes) {
        m_keycodeBindings.remove(keycode);
        m_keysHeld.remove(keycode);
        m_suppressedKeys.remove(keycode);
    }
    
    qCDebug(logShortcut) << "SpecialKeyHandler: Unregistered" << shortcutId;
    return true;
}

void SpecialKeyHandler::clear()
{
    m_keycodeBindings.clear();
    m_shortcutKeycodes.clear();
    m_keysHeld.clear();
    m_suppressedKeys.clear();
}

void SpecialKeyHandler::setEnabled(bool enabled)
{
    if (m_inputEnabled == enabled)
        return;

    m_inputEnabled = enabled;
    updateDispatchEnabled();
}

void SpecialKeyHandler::setSessionActive(bool active)
{
    if (m_sessionActive == active)
        return;

    m_sessionActive = active;
    updateDispatchEnabled();
}

void SpecialKeyHandler::updateDispatchEnabled()
{
    const bool enabled = m_inputEnabled && m_sessionActive;
    if (m_dispatchEnabled == enabled)
        return;

    if (!enabled) {
        m_suppressedKeys.unite(m_keysHeld);
        m_keysHeld.clear();
    }
    m_dispatchEnabled = enabled;
}

QString SpecialKeyHandler::lookupConflict(uint32_t keycode) const
{
    if (m_keycodeBindings.contains(keycode)) {
        return m_keycodeBindings.value(keycode).shortcutId;
    }
    return QString();
}

bool SpecialKeyHandler::isKeycode(const QString &hotkey)
{
    if (hotkey.isEmpty()) {
        return false;
    }
    
    // Check if starts with digit or "0x"
    if (hotkey[0].isDigit()) {
        return true;
    }
    
    if (hotkey.startsWith("0x", Qt::CaseInsensitive) && hotkey.length() > 2) {
        return true;
    }
    
    return false;
}

uint32_t SpecialKeyHandler::parseKeycode(const QString &hotkey)
{
    bool ok = false;
    uint32_t keycode = 0;
    
    if (hotkey.startsWith("0x", Qt::CaseInsensitive)) {
        // Hexadecimal
        keycode = hotkey.toUInt(&ok, 16);
    } else {
        // Decimal
        keycode = hotkey.toUInt(&ok, 10);
    }
    
    return ok ? keycode : 0;
}

void SpecialKeyHandler::onKeyEvent(uint keycode, bool pressed, 
                                    bool ctrlPressed, bool shiftPressed, 
                                    bool altPressed, bool superPressed)
{
    if (!m_dispatchEnabled) {
        if (pressed)
            m_suppressedKeys.insert(keycode);
        else
            m_suppressedKeys.remove(keycode);
        return;
    }

    if (m_suppressedKeys.contains(keycode)) {
        if (!pressed)
            m_suppressedKeys.remove(keycode);
        return;
    }
    
    if (!m_keycodeBindings.contains(keycode)) {
        return;
    }

    // Numeric hotkeys represent bare Linux keycodes.  Once any modifier is
    // observed, suppress the sequence through release so a modified shortcut
    // cannot also trigger its bare raw alias.
    if (ctrlPressed || shiftPressed || altPressed || superPressed) {
        m_keysHeld.remove(keycode);
        if (pressed)
            m_suppressedKeys.insert(keycode);
        qCDebug(logShortcut) << "SpecialKeyHandler: Ignoring modified raw key sequence:"
                             << keycode;
        return;
    }
    
    const KeyBinding &binding = m_keycodeBindings.value(keycode);
    int flags = binding.keyEventFlags;
    
    if (pressed) {
        // Check if this is a repeat event
        bool isRepeat = m_keysHeld.contains(keycode);
        
        if (!isRepeat) {
            // First press
            m_keysHeld.insert(keycode);
            
            if (flags & KeyEventFlag::Press) {
                qCDebug(logShortcut) << "SpecialKeyHandler: Key pressed, keycode:" << keycode;
                emit keyActivated(binding.shortcutId);
            }
        } else {
            // Repeat event
            if (flags & KeyEventFlag::Repeat) {
                qCDebug(logShortcut) << "SpecialKeyHandler: Key repeat, keycode:" << keycode;
                emit keyActivated(binding.shortcutId);
            }
        }
    } else {
        const bool wasHeld = m_keysHeld.remove(keycode);
        if (!wasHeld)
            return;
        
        if (flags & KeyEventFlag::Release) {
            qCDebug(logShortcut) << "SpecialKeyHandler: Key released, keycode:" << keycode;
            emit keyActivated(binding.shortcutId);
        }
    }
}
