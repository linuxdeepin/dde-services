// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "specialkeyhandler.h"

#include <QDebug>
#include <QDBusConnection>

SpecialKeyHandler::SpecialKeyHandler(QObject *parent)
    : QObject(parent)
    , m_connected(false)
{
    // Connect to org.deepin.dde.KeyEvent1 signal
    m_connected = QDBusConnection::systemBus().connect(
        "org.deepin.dde.KeyEvent1",           // service
        "/org/deepin/dde/KeyEvent1",          // path
        "org.deepin.dde.KeyEvent1",           // interface
        "KeyEvent",                            // signal name
        this,                                  // receiver
        SLOT(onKeyEvent(uint,bool,bool,bool,bool,bool))
    );

    if (m_connected) {
        qInfo() << "SpecialKeyHandler: Connected to org.deepin.dde.KeyEvent1";
    } else {
        qWarning() << "SpecialKeyHandler: Failed to connect to org.deepin.dde.KeyEvent1";
    }
}

SpecialKeyHandler::~SpecialKeyHandler()
{
    if (m_connected) {
        QDBusConnection::systemBus().disconnect(
            "org.deepin.dde.KeyEvent1",
            "/org/deepin/dde/KeyEvent1",
            "org.deepin.dde.KeyEvent1",
            "KeyEvent",
            this,
            SLOT(onKeyEvent(uint,bool,bool,bool,bool,bool))
        );
    }
}

bool SpecialKeyHandler::registerKey(const KeyConfig &config)
{
    if (!m_connected) {
        qWarning() << "SpecialKeyHandler: Not connected to KeyEvent1 service";
        return false;
    }

    QList<uint32_t> keycodes;
    
    for (const QString &hotkey : config.hotkeys) {
        if (!isKeycode(hotkey)) {
            continue;  // Skip non-keycode hotkeys
        }
        
        uint32_t keycode = parseKeycode(hotkey);
        if (keycode == 0) {
            qWarning() << "SpecialKeyHandler: Invalid keycode:" << hotkey;
            continue;
        }
        
        // Check for conflict
        if (m_keycodeBindings.contains(keycode)) {
            const KeyBinding &existing = m_keycodeBindings.value(keycode);
            qWarning() << "SpecialKeyHandler: Keycode conflict detected:"
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
        
        qDebug() << "SpecialKeyHandler: Registered keycode" << keycode 
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
    
    QList<uint32_t> keycodes = m_shortcutKeycodes.take(shortcutId);
    for (uint32_t keycode : keycodes) {
        m_keycodeBindings.remove(keycode);
        m_keysHeld.remove(keycode);
    }
    
    qDebug() << "SpecialKeyHandler: Unregistered" << shortcutId;
    return true;
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
    Q_UNUSED(ctrlPressed)
    Q_UNUSED(shiftPressed)
    Q_UNUSED(altPressed)
    Q_UNUSED(superPressed)
    
    if (!m_keycodeBindings.contains(keycode)) {
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
                qDebug() << "SpecialKeyHandler: Key pressed, keycode:" << keycode;
                emit keyActivated(binding.shortcutId);
            }
        } else {
            // Repeat event
            if (flags & KeyEventFlag::Repeat) {
                qDebug() << "SpecialKeyHandler: Key repeat, keycode:" << keycode;
                emit keyActivated(binding.shortcutId);
            }
        }
    } else {
        // Key release
        m_keysHeld.remove(keycode);
        
        if (flags & KeyEventFlag::Release) {
            qDebug() << "SpecialKeyHandler: Key released, keycode:" << keycode;
            emit keyActivated(binding.shortcutId);
        }
    }
}
