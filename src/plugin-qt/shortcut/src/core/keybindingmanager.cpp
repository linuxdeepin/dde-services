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
#include "qkeysequenceconverter.h"

#include <QDebug>
#include <QDBusConnection>

// Normalize a hotkey from XKB form ("<Control><Alt>T") to Qt PortableText
// ("Ctrl+Alt+T"). Inputs already in Qt form pass through unchanged.
// dde-services emits XKB on the wire for legacy control-center compatibility
// but stores Qt internally, so callers may send either form back to us.
static QString normalizeHotkey(const QString &hotkey)
{
    if (hotkey.contains(QLatin1Char('<')) && hotkey.contains(QLatin1Char('>')))
        return QKeySequenceConverter::xkbToQKeySequence(hotkey);
    return hotkey;
}

static QStringList normalizeHotkeys(const QStringList &hotkeys)
{
    QStringList out;
    out.reserve(hotkeys.size());
    for (const QString &h : hotkeys)
        out.append(normalizeHotkey(h));
    return out;
}

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
    qRegisterMetaType<KeyConfig>("KeyConfig");

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
        // Only expose modifiable shortcuts — control center filters out
        // non-modifiable entries to avoid showing read-only items.
        if (!config.modifiable) {
            continue;
        }
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
    const QString needle = normalizeHotkey(hotkey);
    for (const KeyConfig &config : m_keyConfigsMap) {
        if (!config.enabled) continue;
        if (config.hotkeys.contains(needle)) {
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

    // Accept either Qt PortableText ("Ctrl+Alt+T") or XKB form
    // ("<Control><Alt>T") on the wire; canonicalize to Qt internally.
    const QStringList normalized = normalizeHotkeys(newHotkeys);
    // Check for conflicts (exclude self)
    for (const QString &hotkey : normalized) {
        ShortcutInfo conflictInfo = LookupConflictShortcut(hotkey);
        if (!conflictInfo.id.isEmpty() && conflictInfo.id != id) {
            qWarning() << "Conflict detected with:" << conflictInfo.id << conflictInfo.displayName;
            return false;
        }
    }

    // Save old state so we can roll back if the Wayland commit fails.
    const QStringList oldHotkeys = config.hotkeys;

    // Phase 1: queue unbind + rebind
    m_keyHandler->unregisterKey(id);
    config.hotkeys = normalized;

    if (!m_keyHandler->registerKey(config)) {
        qWarning() << "Failed to register new hotkeys:" << id << normalized;
        // Roll back: re-register old hotkeys so we don't lose the binding.
        config.hotkeys = oldHotkeys;
        m_keyHandler->registerKey(config);
        m_keyHandler->commitSync();
        return false;
    }

    m_keyConfigsMap[id] = config;

    // Phase 2: commit to compositor (sync — must know whether to roll back).
    if (!m_keyHandler->commitSync()) {
        qWarning() << "Wayland commit failed for ModifyHotkeys:" << id << ", rolling back";
        // Unwind the in-memory change and restore the old binding.
        m_keyHandler->unregisterKey(id);
        m_keyConfigsMap.remove(id);
        config.hotkeys = oldHotkeys;
        if (m_keyHandler->registerKey(config)) {
            m_keyConfigsMap[id] = config;
            m_keyHandler->commitSync();
        }
        return false;
    }

    // Phase 3: only after a successful commit, persist to dconfig and notify.
    m_loader->updateValue(id, "hotkeys", normalized);
    emit ShortcutChanged(id, toShortcutInfo(config));

    return true;
}

bool KeybindingManager::Disable(const QString &id)
{
    if (!m_keyConfigsMap.contains(id)) {
        return false;
    }

    KeyConfig oldConfig = m_keyConfigsMap[id];

    m_keyHandler->unregisterKey(id);
    if (!m_keyHandler->commitSync()) {
        qWarning() << "Wayland commit failed for Disable:" << id << ", restoring old binding";
        // Treeland still has the old binding; keep in-memory state in sync with reality.
        if (m_keyHandler->registerKey(oldConfig)) {
            m_keyConfigsMap[id] = oldConfig;
            m_keyHandler->commitSync();
        }
        return false;
    }

    m_keyConfigsMap.remove(id);

    // Only persist to dconfig after a successful Wayland commit
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
    // Reset shortcut hotkeys to defaults; other fields (enabled, etc.) are untouched.
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
    } else if (id.contains(".shortcut")) {
        // configRemoved is shared by key/gesture; suffix says it's ours but
        // it's not in the map — registerShortcut() must have failed at init.
        // Worth a warning.
        qWarning() << "KeybindingManager: shortcut id removed but was never registered:" << id;
    } else {
        qDebug() << "KeybindingManager: ignoring removal of non-key id:" << id;
    }
}

void KeybindingManager::onKeyActivated(const QString &shortcutId)
{
    qDebug() << "Key activated:" << shortcutId;
    
    if (m_keyConfigsMap.contains(shortcutId)) {
        const auto &config = m_keyConfigsMap[shortcutId];
        m_executor->execute(config);
        emit ShortcutActivated(shortcutId, config.triggerValue);
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
    // Emit hotkeys in X11/XKB form (e.g. "<Control><Alt>T", "XF86AudioMute")
    // so the legacy control-center shortcut page can render each modifier as
    // a separate chip without doing its own format translation.
    info.hotkeys.reserve(config.hotkeys.size());
    for (const QString &hk : config.hotkeys) {
        info.hotkeys.append(QKeySequenceConverter::qKeySequenceToXkb(hk));
    }
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
