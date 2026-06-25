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

#include <algorithm>

namespace {
constexpr const char *kDefaultShortcutAppId = "org.deepin.dde.keybinding";
constexpr const char *kNoHotkeyText = "None";
}

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

static bool containsEmptyHotkey(const QStringList &hotkeys)
{
    return std::any_of(hotkeys.begin(), hotkeys.end(),
                      [](const QString &h) { return h.trimmed().isEmpty(); });
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
    if (keyword.isEmpty()) return list;

    for (auto it = m_keyConfigsMap.constBegin(); it != m_keyConfigsMap.constEnd(); ++it) {
        const KeyConfig &config = it.value();
        if (!config.isValid()) continue;

        // Match against shortcutId
        if (config.getId().contains(keyword, Qt::CaseInsensitive)) {
            list.append(toShortcutInfo(config));
            continue;
        }

        // Match against display name
        if (config.displayName.contains(keyword, Qt::CaseInsensitive)) {
            list.append(toShortcutInfo(config));
            continue;
        }

        // Match against localized display name
        QString localName = m_translationManager->translate(config.appId, config.displayName);
        if (localName.contains(keyword, Qt::CaseInsensitive)) {
            list.append(toShortcutInfo(config));
            continue;
        }

        // Match against hotkey combinations
        for (const QString &hk : config.hotkeys) {
            if (hk.contains(keyword, Qt::CaseInsensitive)) {
                list.append(toShortcutInfo(config));
                break;
            }
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
    if (normalized.isEmpty() || containsEmptyHotkey(normalized)) {
        qWarning() << "ModifyHotkeys: new hotkeys can not be empty:" << id;
        return false;
    }

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

bool KeybindingManager::SwapHotkeys(const QString &id1, const QString &id2)
{
    if (id1 == id2)
        return false;

    if (!m_keyConfigsMap.contains(id1) || !m_keyConfigsMap.contains(id2))
        return false;

    KeyConfig config1 = m_keyConfigsMap[id1];
    KeyConfig config2 = m_keyConfigsMap[id2];

    if (!config1.enabled || !config1.modifiable || !config2.enabled || !config2.modifiable) {
        qWarning() << "SwapHotkeys: both shortcuts must be enabled and modifiable:"
                    << id1 << id2;
        return false;
    }

    const QStringList hotkeys1 = config1.hotkeys;
    const QStringList hotkeys2 = config2.hotkeys;

    // Phase 1: unbind both, then rebind with swapped hotkeys.
    m_keyHandler->unregisterKey(id1);
    m_keyHandler->unregisterKey(id2);

    config1.hotkeys = hotkeys2;
    config2.hotkeys = hotkeys1;

    bool reg1 = m_keyHandler->registerKey(config1);
    bool reg2 = m_keyHandler->registerKey(config2);

    if (!reg1 || !reg2) {
        qWarning() << "SwapHotkeys: registerKey failed" << id1 << reg1 << id2 << reg2;
        rollbackRegistration(id1, id2, config1, config2, hotkeys1, hotkeys2);
        return false;
    }

    // Phase 2: commit to compositor.  Do NOT update m_keyConfigsMap yet
    // — if commit fails we want the map to still hold the originals.
    if (!m_keyHandler->commitSync()) {
        qWarning() << "SwapHotkeys: commit failed, rolling back";
        m_keyHandler->unregisterKey(id1);
        m_keyHandler->unregisterKey(id2);
        rollbackRegistration(id1, id2, config1, config2, hotkeys1, hotkeys2);
        return false;
    }

    // Phase 3: commit succeeded — update map, persist, notify.
    m_keyConfigsMap[id1] = config1;
    m_keyConfigsMap[id2] = config2;
    m_loader->updateValue(id1, "hotkeys", config1.hotkeys);
    m_loader->updateValue(id2, "hotkeys", config2.hotkeys);
    emit ShortcutChanged(id1, toShortcutInfo(config1));
    emit ShortcutChanged(id2, toShortcutInfo(config2));

    return true;
}

void KeybindingManager::rollbackRegistration(const QString &id1, const QString &id2,
                                              KeyConfig &config1, KeyConfig &config2,
                                              const QStringList &hotkeys1,
                                              const QStringList &hotkeys2)
{
    config1.hotkeys = hotkeys1;
    config2.hotkeys = hotkeys2;

    m_keyHandler->registerKey(config1);
    m_keyHandler->registerKey(config2);

    // Always restore the original config to the map — even if registerKey
    // failed, the in-memory state must not be worse than before the call.
    m_keyConfigsMap[id1] = config1;
    m_keyConfigsMap[id2] = config2;

    if (!m_keyHandler->commitSync()) {
        qCritical() << "rollbackRegistration: commitSync also failed —"
                     << "in-memory state may diverge from compositor";
        return;
    }

    emit ShortcutChanged(id1, toShortcutInfo(config1));
    emit ShortcutChanged(id2, toShortcutInfo(config2));
}

bool KeybindingManager::ReplaceHotkey(const QString &targetId, const QString &newHotkey, const QString &conflictId)
{
    if (targetId == conflictId) return false;
    if (!m_keyConfigsMap.contains(targetId) || !m_keyConfigsMap.contains(conflictId))
        return false;

    KeyConfig targetConfig = m_keyConfigsMap[targetId];
    KeyConfig conflictConfig = m_keyConfigsMap[conflictId];

    if (!targetConfig.enabled || !targetConfig.modifiable) {
        qWarning() << "ReplaceHotkey: target not modifiable or enabled:" << targetId;
        return false;
    }
    if (!conflictConfig.enabled || !conflictConfig.modifiable) {
        qWarning() << "ReplaceHotkey: conflict shortcut not modifiable or enabled:" << conflictId;
        return false;
    }

    const QString normalized = normalizeHotkey(newHotkey);
    if (normalized.trimmed().isEmpty()) {
        qWarning() << "ReplaceHotkey: new hotkey can not be empty:" << targetId;
        return false;
    }

    // Remove the hotkey from the conflict shortcut
    if (!conflictConfig.hotkeys.removeOne(normalized)) {
        qWarning() << "ReplaceHotkey: hotkey" << normalized << "not found in conflict shortcut" << conflictId;
        return false;
    }

    // Replace the target's hotkeys with the new one (not append).
    targetConfig.hotkeys.clear();
    targetConfig.hotkeys.append(normalized);

    // Save old state for rollback
    const QStringList oldTargetHotkeys = m_keyConfigsMap[targetId].hotkeys;
    const QStringList oldConflictHotkeys = m_keyConfigsMap[conflictId].hotkeys;

    // Phase 1: unregister both, then register both with new state
    m_keyHandler->unregisterKey(targetId);
    m_keyHandler->unregisterKey(conflictId);

    bool regTarget = m_keyHandler->registerKey(targetConfig);
    bool regConflict = true;
    if (!conflictConfig.hotkeys.isEmpty()) {
        regConflict = m_keyHandler->registerKey(conflictConfig);
    }

    if (!regTarget || !regConflict) {
        qWarning() << "ReplaceHotkey: registerKey failed" << targetId << regTarget << conflictId << regConflict;
        m_keyHandler->unregisterKey(targetId);
        m_keyHandler->unregisterKey(conflictId);
        rollbackRegistration(targetId, conflictId, targetConfig, conflictConfig, oldTargetHotkeys, oldConflictHotkeys);
        return false;
    }

    // Phase 2: commit to compositor
    if (!m_keyHandler->commitSync()) {
        qWarning() << "ReplaceHotkey: commit failed, rolling back";
        m_keyHandler->unregisterKey(targetId);
        m_keyHandler->unregisterKey(conflictId);
        rollbackRegistration(targetId, conflictId, targetConfig, conflictConfig, oldTargetHotkeys, oldConflictHotkeys);
        return false;
    }

    // Phase 3: commit succeeded — update map, persist, notify.
    m_keyConfigsMap[targetId] = targetConfig;
    m_keyConfigsMap[conflictId] = conflictConfig;
    m_loader->updateValue(targetId, "hotkeys", targetConfig.hotkeys);
    m_loader->updateValue(conflictId, "hotkeys", conflictConfig.hotkeys);
    emit ShortcutChanged(targetId, toShortcutInfo(targetConfig));
    emit ShortcutChanged(conflictId, toShortcutInfo(conflictConfig));

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
    const QStringList resetIds = m_loader->resettableHotkeyIds();
    if (resetIds.isEmpty()) {
        return;
    }

    // DConfig resets are delivered later as per-key valueChanged signals.
    // Release all reset targets first so one restored default does not conflict
    // with another target's stale binding.
    QStringList toRestore;
    for (const QString &id : resetIds) {
        if (!m_keyConfigsMap.contains(id)) {
            continue;
        }

        m_keyHandler->unregisterKey(id);
        m_specialKeyHandler->unregisterKey(id);
        toRestore.append(id);
    }

    if (!m_keyHandler->commitSync()) {
        // Compositor rejected the unregister; leave the local map and dconfig
        // untouched so state stays consistent with what's actually bound.
        qWarning() << "Reset: failed to commit unregistering existing hotkeys";
        return;
    }

    // The compositor confirmed the unregister. Drop the local entries so the
    // async valueChanged restore goes through onKeyConfigChanged's new-entry path
    for (const QString &id : toRestore) {
        m_keyConfigsMap.remove(id);
    }

    m_loader->resetHotkeys(resetIds);
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

        // No actual change, skip to avoid spurious ShortcutChanged signal
        if (old == config) {
            return;
        }

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

    if (config.hotkeys.isEmpty()) {
        qInfo() << "Shortcut has no hotkeys, keeping it visible without registration:"
                << config.getId();
        return true;
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
    if (config.hotkeys.isEmpty()) {
        info.hotkeys.append(localizedNoHotkeyText());
    } else {
        info.hotkeys.reserve(config.hotkeys.size());
        for (const QString &hk : config.hotkeys) {
            info.hotkeys.append(QKeySequenceConverter::qKeySequenceToXkb(hk));
        }
    }
    info.localLanguageName = m_translationManager->translate(config.appId, config.displayName);
    return info;
}

QString KeybindingManager::localizedNoHotkeyText() const
{
    return m_translationManager->translate(QString::fromLatin1(kDefaultShortcutAppId),
                                           QString::fromLatin1(kNoHotkeyText));
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
