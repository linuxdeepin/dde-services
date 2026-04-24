// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "keybindingmanager.h"
#include "backend/abstractkeyhandler.h"
#include "backend/specialkeyhandler.h"
#include "config/configloader.h"
#include "actionexecutor.h"
#include "translationmanager.h"
#include "core/shortcutconfig.h"

#include <QDebug>
#include <QDBusConnection>

KeybindingManager::KeybindingManager(ConfigLoader *loader, ActionExecutor *executor, 
                                     TranslationManager *translationManager,
                                     AbstractKeyHandler *keyHandler,
                                     QObject *parent)
    : QObject(parent)
    , m_loader(loader)
    , m_keyHandler(keyHandler)
    , m_specialKeyHandler(new SpecialKeyHandler(this))
    , m_executor(executor)
    , m_translationManager(translationManager)
{
    qRegisterMetaType<ShortcutInfo>("ShortcutInfo");
    qRegisterMetaType<QList<ShortcutInfo>>("QList<ShortcutInfo>");

    qDBusRegisterMetaType<ShortcutInfo>();
    qDBusRegisterMetaType<QList<ShortcutInfo>>();

    // Connect signals from key handler
    connect(m_keyHandler, &AbstractKeyHandler::keyActivated, this, &KeybindingManager::onKeyActivated);
    
    // Connect signals from special key handler
    connect(m_specialKeyHandler, &SpecialKeyHandler::keyActivated, this, &KeybindingManager::onKeyActivated);
    
    connect(m_loader, &ConfigLoader::keyConfigChanged, this, &KeybindingManager::onKeyConfigChanged);
    connect(m_loader, &ConfigLoader::keyConfigAdded, this, [this](const KeyConfig &newConfig){
        if (registerShortcut(newConfig)) {
            m_keyConfigsMap[newConfig.getId()] = newConfig;
            m_keyHandler->commit();
        }
    });
    connect(m_loader, &ConfigLoader::configRemoved, this, &KeybindingManager::onConfigRemoved);
}

KeybindingManager::~KeybindingManager()
{
}

void KeybindingManager::registerAllShortcuts()
{
    qDebug() << "KeybindingManager: Registering all shortcuts...";
    
    // Clear existing registrations first
    m_keyConfigsMap.clear();
    
    // Register existing configs
    for (const KeyConfig &config : m_loader->keys()) {
        if (registerShortcut(config)) {
            m_keyConfigsMap[config.getId()] = config;
        }
    }
    
    qDebug() << "KeybindingManager: Registered" << m_keyConfigsMap.size() << "shortcuts";
}

void KeybindingManager::clearState()
{
    qWarning() << "KeybindingManager: Clearing internal state due to protocol disconnection";
    m_keyConfigsMap.clear();
}

QList<ShortcutInfo> KeybindingManager::ListAllShortcuts()
{
    QList<ShortcutInfo> list;
    for (const auto &config : m_keyConfigsMap) {
        list.append(toShortcutInfo(config));
    }

    return list;
}

QList<ShortcutInfo> KeybindingManager::ListShortcutsByApp(const QString &appId)
{
    QList<ShortcutInfo> list;
    QList<KeyConfig> configs = m_keyConfigsMap.values();
    for (const KeyConfig &config : configs) {
        if (config.appId == appId) {
            list.append(toShortcutInfo(config));
        }
    }
    return list;
}

QList<ShortcutInfo> KeybindingManager::ListShortcutsByCategory(int category)
{
    QList<ShortcutInfo> list;
    QList<KeyConfig> configs = m_keyConfigsMap.values();
    for (const KeyConfig &config : configs) {
        if (config.category == category) {
            list.append(toShortcutInfo(config));
        }
    }

    return list;
}

ShortcutInfo KeybindingManager::GetShortcut(const QString &id)
{
    if (m_keyConfigsMap.contains(id)) {
        const auto &config = m_keyConfigsMap[id];
        return toShortcutInfo(config);
    }

    return ShortcutInfo();
}

ShortcutInfo KeybindingManager::LookupConflictShortcut(const QString &hotkey)
{
    for (const KeyConfig &config : m_keyConfigsMap) {
        if (!config.enabled) continue;
        if (config.hotkeys.contains(hotkey)) {
            return toShortcutInfo(config);
        }
    }
    return ShortcutInfo(); // Empty struct if no conflict
}

QList<ShortcutInfo> KeybindingManager::SearchShortcuts(const QString &keyword)
{
    QList<ShortcutInfo> list;
    QList<KeyConfig> configs = m_keyConfigsMap.values();
    for (const KeyConfig &config : configs) {
        if (config.hotkeys.contains(keyword, Qt::CaseInsensitive)) {
            list.append(toShortcutInfo(config));
        }
    }

    return list;
}

bool KeybindingManager::ModifyHotkeys(const QString &id, const QStringList &newHotkeys)
{
    if (!m_keyConfigsMap.contains(id)) return false;

    KeyConfig config = m_keyConfigsMap[id];
    if (!config.enabled || !config.modifiable) {
        qWarning() << "Shortcut is not modifiable or enabled:" << id << "enabled:" 
                    << config.enabled << "modifiable:" << config.modifiable;
        return false;
    }

    // Check for conflicts
    for (const QString &hotkey : newHotkeys) {
        ShortcutInfo conflictInfo = LookupConflictShortcut(hotkey);
        if (!conflictInfo.id.isEmpty()) {
            qWarning() << "Conflict detected with:" << conflictInfo.id << conflictInfo.displayName;
            return false;
        }
    }

    // unregister
    m_keyHandler->unregisterKey(id);
    m_keyConfigsMap.remove(id);
    
    // modify config
    config.hotkeys = newHotkeys;

    if (m_keyHandler->registerKey(config)) {
        m_keyConfigsMap[id] = config;
        m_keyHandler->commit();
    } else {
        qWarning() << "Failed to register new hotkeys:" << id << newHotkeys;
        return false;
    }

    // update dconfig
    m_loader->updateValue(id, "hotkeys", newHotkeys);
    
    return true;
}

bool KeybindingManager::Disable(const QString &id)
{
    if (!m_keyConfigsMap.contains(id)) {
        return false;
    }

    m_keyHandler->unregisterKey(id);
    m_keyHandler->commit();
    m_keyConfigsMap.remove(id);
    
    // Update DConfig
    m_loader->updateValue(id, "enabled", false);

    emit ShortcutRemoved(id);

    return true;
}

void KeybindingManager::ReloadConfigs()
{
    qInfo() << "ReloadConfigs called via DBus (delegating to Smart ConfigLoader)";
    
    // ConfigLoader will calculate diff (Add/Remove) and emit:
    // - configRemoved(id) -> Triggers unregister
    // - keyConfigChanged(config) -> Triggers register/update
    // - keyConfigAdded(config) -> Triggers register
    m_loader->reload();
    m_translationManager->reload();
}

void KeybindingManager::Reset()
{
    m_loader->resetConfig();
}

void KeybindingManager::onKeyConfigChanged(const KeyConfig &config)
{
    if (!m_keyConfigsMap.contains(config.getId())) {
        if (!config.enabled) {
            // new one, but disabled, skip
            return;
        } else {
            // new one, enable
            if (registerShortcut(config)) {
                m_keyConfigsMap[config.getId()] = config;
                m_keyHandler->commit();
            }
        }
    } else { // exist
        KeyConfig &old = m_keyConfigsMap[config.getId()];
        if (!config.enabled) {
            // enable->disable
            m_keyHandler->unregisterKey(config.getId());
            m_keyHandler->commit();
            m_keyConfigsMap.remove(config.getId());
        } else if (old.hotkeys != config.hotkeys) {
            // update
            m_keyHandler->unregisterKey(config.getId());
            m_keyConfigsMap.remove(config.getId());
            if (registerShortcut(config)) {
                m_keyConfigsMap[config.getId()] = config;
            }
            m_keyHandler->commit();
        } else {
            // other changes
            m_keyConfigsMap[config.getId()] = config;
        }
    }

    emit ShortcutChanged(config.getId(), toShortcutInfo(config));
}


void KeybindingManager::onConfigRemoved(const QString &id)
{
    if (m_keyConfigsMap.contains(id)) {
        m_keyConfigsMap.remove(id);
        m_keyHandler->unregisterKey(id);
        m_keyHandler->commit();

        emit ShortcutRemoved(id);
    } else {
        qWarning() << "KeybindingManager: Config removed but not found in map: " << id;
    }
}

void KeybindingManager::onKeyActivated(const QString &shortcutId)
{
    qDebug() << "Key activated:" << shortcutId;
    
    if (m_keyConfigsMap.contains(shortcutId)) {
        const auto &config = m_keyConfigsMap[shortcutId];
        m_executor->execute(config);
        emit ShortcutActivated(config.appId, config.triggerValue);
    }
}

bool KeybindingManager::registerShortcut(const KeyConfig &config)
{
    if (!config.isValid()) {
        qWarning() << "Shortcut is disabled or invalid, skipping registration:"
                    << "Enabled:" << config.enabled
                    << "AppId:" << config.appId
                    << "DisplayName:" << config.displayName
                    << "hotkeys:" << config.hotkeys;
        return false;
    }

    if (m_keyConfigsMap.contains(config.getId())) {
        qWarning() << "Shortcut conflict detected during init: has same appId and displayName"
                    << "hotkeys:" << config.hotkeys
                    << "Conflicts with:" << m_keyConfigsMap[config.getId()].hotkeys
                    << "- Skipping registration";
        return false;
    }

    // Separate hotkeys into normal keys and keycodes
    QStringList normalHotkeys;
    QStringList keycodeHotkeys;
    
    for (const QString &hotkey : config.hotkeys) {
        if (SpecialKeyHandler::isKeycode(hotkey)) {
            keycodeHotkeys.append(hotkey);
        } else {
            normalHotkeys.append(hotkey);
        }
    }

    bool registered = false;

    // Register normal hotkeys via AbstractKeyHandler (X11/Wayland)
    if (!normalHotkeys.isEmpty()) {
        // Check for conflicts before registering
        for (const QString &hotkey : normalHotkeys) {
            auto shortcutInfo = LookupConflictShortcut(hotkey);
            if (!shortcutInfo.id.isEmpty()) {
                qWarning() << "Shortcut conflict detected during init:"
                            << "Config appId:" << config.appId
                            << "Config displayName:" << config.displayName
                            << "Config hotkeys:" << config.hotkeys
                            << "Conflicts with:" << shortcutInfo.id << " " << shortcutInfo.displayName
                            << "- Skipping registration";
                return false;
            }
        }

        // Create a config with only normal hotkeys
        KeyConfig normalConfig = config;
        normalConfig.hotkeys = normalHotkeys;
        
        if (m_keyHandler->registerKey(normalConfig)) {
            registered = true;
        }
    }

    // Register keycode hotkeys via SpecialKeyHandler
    if (!keycodeHotkeys.isEmpty()) {
        KeyConfig keycodeConfig = config;
        keycodeConfig.hotkeys = keycodeHotkeys;
        
        if (m_specialKeyHandler->registerKey(keycodeConfig)) {
            registered = true;
        }
    }

    return registered;
}

QString KeybindingManager::checkConflictForConfig(const KeyConfig &config, const QString &excludeId)
{
    QString currentId = config.getId();
    
    // Check each hotkey in the config
    for (const QString &hotkey : config.hotkeys) {
        // Search through all registered shortcuts
        for (const KeyConfig &existingConfig : m_keyConfigsMap) {
            QString existingId = existingConfig.getId();
            
            // Skip if this is the config we're excluding (self-check)
            if (!excludeId.isEmpty() && existingId == excludeId) {
                continue;
            }
            
            // Skip if not enabled
            if (!existingConfig.enabled) {
                continue;
            }
            
            // Check if hotkey conflicts
            if (existingConfig.hotkeys.contains(hotkey)) {
                return existingId; // Return the conflicting shortcut ID
            }
        }
    }
    
    return QString(); // No conflict
}

ShortcutInfo KeybindingManager::toShortcutInfo(const KeyConfig &config)
{
    ShortcutInfo info;
    info.id = config.getId();
    info.displayName = config.displayName;
    info.category = config.category;
    info.hotkeys = config.hotkeys;
    info.localLanguageName = m_translationManager->translate(config.appId, config.displayName);
    return info;
}

uint KeybindingManager::GetNumLockState() const
{
    if (m_keyHandler) {
        return m_keyHandler->getNumLockState() ? 1 : 0;
    }
    return 0;
}

uint KeybindingManager::GetCapsLockState() const
{
    if (m_keyHandler) {
        return m_keyHandler->getCapsLockState() ? 1 : 0;
    }
    return 0;
}

void KeybindingManager::SetNumLockState(uint state)
{
    if (m_keyHandler) {
        uint oldState = GetNumLockState();
        m_keyHandler->setNumLockState(state != 0);
        if (oldState != state) {
            emit NumLockStateChanged(state);
        }
    }
}

void KeybindingManager::SetCapsLockState(uint state)
{
    if (m_keyHandler) {
        uint oldState = GetCapsLockState();
        m_keyHandler->setCapsLockState(state != 0);
        if (oldState != state) {
            emit CapsLockStateChanged(state);
        }
    }
}
