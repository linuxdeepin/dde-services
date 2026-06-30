// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "keybindingmanager.h"
#include "customshortcuttransaction.h"
#include "backend/abstractkeyhandler.h"
#include "backend/specialkeyhandler.h"
#include "config/configloader.h"
#include "actionexecutor.h"
#include "translationmanager.h"
#include "core/shortcutconfig.h"
#include "qkeysequenceconverter.h"

#include <QDebug>
#include <QDBusConnection>
#include <QHash>
#include <QUuid>
#include <algorithm>

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

constexpr int MaxCustomShortcutCount = 200;
constexpr int MaxCustomShortcutNameLength = 128;
constexpr int MaxCustomShortcutCommandLength = 4096;
constexpr int MaxCustomShortcutHotkeyLength = 256;

static bool containsControlCharacter(const QString &text)
{
    for (const QChar ch : text) {
        const QChar::Category category = ch.category();
        if (category == QChar::Other_Control
            || category == QChar::Other_Format
            || category == QChar::Other_Surrogate
            || category == QChar::Other_PrivateUse
            || category == QChar::Other_NotAssigned) {
            return true;
        }
    }
    return false;
}

static bool isValidCustomShortcutName(const QString &name)
{
    return !name.isEmpty()
            && name.size() <= MaxCustomShortcutNameLength
            && !containsControlCharacter(name);
}

static bool isValidCustomShortcutCommand(const QString &command)
{
    return !command.isEmpty()
            && command.size() <= MaxCustomShortcutCommandLength
            && !containsControlCharacter(command);
}

static bool isValidCustomShortcutHotkey(const QString &hotkey, bool allowEmpty)
{
    if (hotkey.isEmpty())
        return allowEmpty;
    return hotkey.size() <= MaxCustomShortcutHotkeyLength
            && !containsControlCharacter(hotkey);
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

    qRegisterMetaType<CategoryInfo>("CategoryInfo");
    qRegisterMetaType<QList<CategoryInfo>>("QList<CategoryInfo>");
    qDBusRegisterMetaType<CategoryInfo>();
    qDBusRegisterMetaType<QList<CategoryInfo>>();

    // Connect signals from key handler
    connect(m_keyHandler, &AbstractKeyHandler::keyActivated, this, &KeybindingManager::onKeyActivated);
    
    // Connect signals from special key handler
    connect(m_specialKeyHandler, &SpecialKeyHandler::keyActivated, this, &KeybindingManager::onKeyActivated);
    
    connect(m_loader, &ConfigLoader::keyConfigChanged, this, &KeybindingManager::onKeyConfigChanged);
    connect(m_loader, &ConfigLoader::keyConfigAdded, this, [this](const KeyConfig &newConfig){
        if (newConfig.isDisplayOnly()) {
            m_keyConfigsMap[newConfig.getId()] = newConfig;
            emit ShortcutChanged(newConfig.getId(), toShortcutInfo(newConfig));
        } else if (registerShortcut(newConfig)) {
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
        if (config.isDisplayOnly()) {
            m_keyConfigsMap[config.getId()] = config;
        } else if (registerShortcut(config)) {
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
        // Empty hotkeys are still exposed so the control center can show
        // the existing row as "None" after another shortcut takes its binding.
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

QList<ShortcutInfo> KeybindingManager::ListShortcutsByCategory(const QString &category)
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

// Fixed display order for framework-reserved categories. The service owns
// these, so it is entitled to define their canonical sequence. App-defined
// categories slot in after Workspace and before Custom (which is always last).
static const QHash<QString, int> &reservedCategoryOrder()
{
    static const QHash<QString, int> order{
        {QStringLiteral("System"),     0},
        {QStringLiteral("Window"),     1},
        {QStringLiteral("Workspace"),  2},
        {QStringLiteral("Custom"),     99},   // always last
    };
    return order;
}

QList<CategoryInfo> KeybindingManager::ListCategories()
{
    // Collect distinct categories from the user-visible (modifiable, with
    // hotkeys) configs — mirrors ListAllShortcuts' filter so the category
    // set matches what the control center actually renders.
    QHash<QString, CategoryInfo> seen;
    int appOrder = 10;  // app-defined categories land after Workspace(2), before Custom(99)
    for (const auto &config : m_keyConfigsMap) {
        if (!config.modifiable || config.category.isEmpty())
            continue;
        if (!seen.contains(config.category)) {
            CategoryInfo ci;
            ci.key = config.category;
            ci.displayName = m_translationManager->translate(config.appId, config.category);
            ci.isCustom = (config.category == CategoryKey::Custom);
            const auto &reserved = reservedCategoryOrder();
            if (reserved.contains(config.category)) {
                ci.order = reserved.value(config.category);
            } else {
                ci.order = appOrder++;   // first-seen order for app-defined
            }
            seen.insert(config.category, ci);
        }
    }

    QList<CategoryInfo> result = seen.values();
    std::sort(result.begin(), result.end(),
              [](const CategoryInfo &a, const CategoryInfo &b) {
        return a.order < b.order;
    });
    return result;
}

ShortcutInfo KeybindingManager::GetShortcut(const QString &id)
{
    if (m_keyConfigsMap.contains(id)) {
        const auto &config = m_keyConfigsMap[id];
        return toShortcutInfo(config);
    }

    return ShortcutInfo();
}

QString KeybindingManager::GetShortcutCommand(const QString &id)
{
    const KeyConfig config = m_keyConfigsMap.value(id);
    if (config.category == QLatin1String(CategoryKey::Custom)
        && config.triggerType == static_cast<int>(TriggerType::Command)
        && !config.triggerValue.isEmpty()) {
        return config.triggerValue.first();
    }
    return QString();
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

QString KeybindingManager::AddCustomShortcut(const QString &name, const QString &action, const QString &hotkey)
{
    const QString displayName = name.trimmed();
    const QString actionText = action.trimmed();
    const QString normalizedHotkey = normalizeHotkey(hotkey);

    if (runtimeCustomShortcutCount() >= MaxCustomShortcutCount) {
        qWarning() << "AddCustomShortcut: custom shortcut count limit reached";
        return QString();
    }

    if (!isValidCustomShortcutName(displayName)
        || !isValidCustomShortcutCommand(actionText)
        || !isValidCustomShortcutHotkey(normalizedHotkey, false)) {
        qWarning() << "AddCustomShortcut: invalid input";
        return QString();
    }

    KeyConfig config;
    config.appId = QStringLiteral("org.deepin.dde.keybinding");
    config.subPath = createCustomShortcutId();
    config.keyEventFlags = KeyEventFlag::Release;
    updateCustomShortcutConfigFields(config, displayName, actionText, normalizedHotkey);

    CustomShortcutChange change;
    change.newTarget = config;
    if (!prepareConflictShortcutChange(normalizedHotkey, change))
        return QString();

    CustomShortcutTransaction transaction(this, change);
    if (!transaction.applyRuntime()) {
        qWarning() << "AddCustomShortcut: failed to apply runtime change" << config.getId();
        return QString();
    }

    if (!transaction.persistAdd())
        return QString();

    transaction.publish();
    return config.getId();
}

bool KeybindingManager::ModifyCustomShortcut(const QString &id, const QString &name,
                                             const QString &action, const QString &hotkey)
{
    if (!m_keyConfigsMap.contains(id)) {
        return false;
    }

    KeyConfig oldConfig = m_keyConfigsMap[id];
    if (!isRuntimeCustomShortcut(oldConfig)) {
        qWarning() << "ModifyCustomShortcut: shortcut is not a runtime custom shortcut:" << id;
        return false;
    }

    const QString displayName = name.trimmed();
    const QString actionText = action.trimmed();
    const QString normalizedHotkey = normalizeHotkey(hotkey);
    if (!isValidCustomShortcutName(displayName)
        || !isValidCustomShortcutCommand(actionText)
        || !isValidCustomShortcutHotkey(normalizedHotkey, true)) {
        qWarning() << "ModifyCustomShortcut: invalid input" << id;
        return false;
    }

    KeyConfig newConfig = oldConfig;
    updateCustomShortcutConfigFields(newConfig, displayName, actionText, normalizedHotkey);

    const bool hotkeysChanged = oldConfig.hotkeys != newConfig.hotkeys;
    if (!hotkeysChanged) {
        if (oldConfig == newConfig)
            return true;

        if (!m_loader->updateCustomShortcut(newConfig)) {
            qWarning() << "ModifyCustomShortcut: failed to persist custom shortcut:" << id;
            return false;
        }

        m_keyConfigsMap[id] = newConfig;
        emit ShortcutChanged(id, toShortcutInfo(newConfig));
        return true;
    }

    CustomShortcutChange change;
    change.hasOldTarget = true;
    change.oldTarget = oldConfig;
    change.newTarget = newConfig;
    if (!prepareConflictShortcutChange(normalizedHotkey, change, id))
        return false;

    CustomShortcutTransaction transaction(this, change);
    if (!transaction.applyRuntime()) {
        qWarning() << "ModifyCustomShortcut: failed to apply runtime change" << id;
        return false;
    }

    if (!transaction.persistModify())
        return false;

    transaction.publish();
    return true;
}

bool KeybindingManager::DeleteCustomShortcut(const QString &id)
{
    if (!m_keyConfigsMap.contains(id)) {
        return false;
    }

    KeyConfig oldConfig = m_keyConfigsMap[id];
    if (!isRuntimeCustomShortcut(oldConfig)) {
        qWarning() << "DeleteCustomShortcut: shortcut is not a runtime custom shortcut:" << id;
        return false;
    }

    unregisterShortcut(id);
    if (!m_keyHandler->commitSync()) {
        qWarning() << "DeleteCustomShortcut: commit failed, restoring" << id;
        if (registerShortcut(oldConfig, QStringList{id})) {
            m_keyHandler->commitSync();
        }
        return false;
    }

    if (!m_loader->removeCustomShortcut(id)) {
        qWarning() << "DeleteCustomShortcut: failed to remove persisted custom shortcut, restoring" << id;
        if (registerShortcut(oldConfig, QStringList{id})) {
            m_keyHandler->commitSync();
        }
        return false;
    }

    m_keyConfigsMap.remove(id);
    emit ShortcutRemoved(id);
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
    if (!canPersistShortcutHotkeys(config1) || !canPersistShortcutHotkeys(config2)) {
        qWarning() << "SwapHotkeys: both shortcuts must have writable configs:"
                    << id1 << m_loader->canUpdateValue(id1)
                    << id2 << m_loader->canUpdateValue(id2);
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
    if (!canPersistShortcutHotkeys(targetConfig) || !canPersistShortcutHotkeys(conflictConfig)) {
        qWarning() << "ReplaceHotkey: target and conflict shortcuts must have writable configs:"
                    << targetId << m_loader->canUpdateValue(targetId)
                    << conflictId << m_loader->canUpdateValue(conflictId);
        return false;
    }

    const QString normalized = normalizeHotkey(newHotkey);

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
    m_loader->resetConfig();
}

void KeybindingManager::onKeyConfigChanged(const KeyConfig &config)
{
    if (!m_keyConfigsMap.contains(config.getId())) {
        if (!config.enabled) {
            // new one, but disabled, skip
            return;
        } else if (config.isDisplayOnly()) {
            m_keyConfigsMap[config.getId()] = config;
        } else {
            // new one, enable
            if (registerShortcut(config)) {
                m_keyConfigsMap[config.getId()] = config;
                m_keyHandler->commit();
            }
        }
    } else { // exist
        KeyConfig &old = m_keyConfigsMap[config.getId()];
        if (old == config)
            return;

        if (!config.enabled) {
            // enable->disable
            m_keyHandler->unregisterKey(config.getId());
            m_keyHandler->commit();
            m_keyConfigsMap.remove(config.getId());
        } else if (old.hotkeys != config.hotkeys) {
            // update
            m_keyHandler->unregisterKey(config.getId());
            m_keyConfigsMap.remove(config.getId());
            if (config.isDisplayOnly()) {
                m_keyConfigsMap[config.getId()] = config;
            } else if (registerShortcut(config)) {
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

bool KeybindingManager::registerShortcut(const KeyConfig &config, const QStringList &excludeIds)
{
    if (!config.canRegister()) {
        qWarning() << "Shortcut can not be registered, skipping:"
                   << "Enabled:" << config.enabled
                   << "AppId:" << config.appId
                   << "DisplayName:" << config.displayName
                   << "hotkeys:" << config.hotkeys;
        return false;
    }

    if (m_keyConfigsMap.contains(config.getId()) && !excludeIds.contains(config.getId())) {
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
            if (!shortcutInfo.id.isEmpty() && !excludeIds.contains(shortcutInfo.id)) {
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

void KeybindingManager::unregisterShortcut(const QString &id)
{
    m_keyHandler->unregisterKey(id);
    m_specialKeyHandler->unregisterKey(id);
}

bool KeybindingManager::isRuntimeCustomShortcut(const KeyConfig &config) const
{
    return config.category == QLatin1String(CategoryKey::Custom)
            && config.modifiable
            && config.subPath.startsWith(QStringLiteral("org.deepin.dde.keybinding.shortcut.custom."));
}

bool KeybindingManager::canPersistShortcutHotkeys(const KeyConfig &config) const
{
    return config.modifiable && m_loader->canUpdateValue(config.getId());
}

int KeybindingManager::runtimeCustomShortcutCount() const
{
    int count = 0;
    for (const KeyConfig &config : m_keyConfigsMap) {
        if (isRuntimeCustomShortcut(config))
            ++count;
    }
    return count;
}

QString KeybindingManager::createCustomShortcutId() const
{
    QString id;
    do {
        id = QStringLiteral("org.deepin.dde.keybinding.shortcut.custom.")
                + QUuid::createUuid().toString(QUuid::WithoutBraces);
    } while (m_keyConfigsMap.contains(id));
    return id;
}

void KeybindingManager::updateCustomShortcutConfigFields(KeyConfig &config, const QString &displayName,
                                                         const QString &commandText,
                                                         const QString &normalizedHotkey) const
{
    config.displayName = displayName;
    config.category = QString::fromLatin1(CategoryKey::Custom);
    config.enabled = true;
    config.modifiable = true;
    config.triggerType = static_cast<int>(TriggerType::Command);
    config.triggerValue = QStringList{commandText};
    config.hotkeys = normalizedHotkey.isEmpty() ? QStringList() : QStringList{normalizedHotkey};
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
    info.isCustom = (config.category == CategoryKey::Custom);
    info.localLanguageCategory = m_translationManager->translate(config.appId, config.category);
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

bool KeybindingManager::prepareConflictShortcutChange(const QString &hotkey, CustomShortcutChange &change,
                                                      const QString &selfId)
{
    change.hasConflict = false;
    change.oldConflict = KeyConfig();
    change.newConflict = KeyConfig();

    const QString normalizedHotkey = normalizeHotkey(hotkey);
    if (normalizedHotkey.isEmpty())
        return true;

    ShortcutInfo conflictInfo = LookupConflictShortcut(normalizedHotkey);
    if (conflictInfo.id.isEmpty() || conflictInfo.id == selfId) {
        return true;
    }

    if (!m_keyConfigsMap.contains(conflictInfo.id)) {
        qWarning() << "prepareConflictShortcutChange: conflict shortcut is missing:" << conflictInfo.id;
        return false;
    }

    KeyConfig conflictConfig = m_keyConfigsMap.value(conflictInfo.id);
    if (!canPersistShortcutHotkeys(conflictConfig)) {
        qWarning() << "prepareConflictShortcutChange: conflict shortcut can not be replaced:"
                    << conflictInfo.id
                    << "modifiable:" << conflictConfig.modifiable
                    << "has writable config:" << m_loader->canUpdateValue(conflictInfo.id);
        return false;
    }

    KeyConfig newConflictConfig = conflictConfig;
    if (!newConflictConfig.hotkeys.removeOne(normalizedHotkey)) {
        qWarning() << "prepareConflictShortcutChange: hotkey not found in conflict shortcut:"
                   << normalizedHotkey << conflictInfo.id;
        return false;
    }

    change.hasConflict = true;
    change.oldConflict = conflictConfig;
    change.newConflict = newConflictConfig;

    return true;
}
