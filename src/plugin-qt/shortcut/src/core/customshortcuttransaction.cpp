// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "shortcutlogging.h"

#include "customshortcuttransaction.h"

#include "backend/abstractkeyhandler.h"
#include "config/configloader.h"

#include <QDebug>

namespace {

void appendUniqueId(QStringList &ids, const QString &id)
{
    if (!id.isEmpty() && !ids.contains(id))
        ids.append(id);
}

} // namespace

// Creates a transaction for one prepared custom shortcut change.
KeybindingManager::CustomShortcutTransaction::CustomShortcutTransaction(KeybindingManager *manager, const CustomShortcutChange &change)
    : m_manager(manager)
    , m_change(change)
{
}

// Each transaction restores its own state on failure. Reset only reports the
// failure and does not add cross-component recovery.
void KeybindingManager::CustomShortcutTransaction::clearConflictingHotkeys(KeybindingManager *manager, const QSet<QString> &reservedHotkeys)
{
    const QList<KeyConfig> configs = manager->m_keyConfigsMap.values();
    for (const KeyConfig &oldConfig : configs) {
        if (!manager->isRuntimeCustomShortcut(oldConfig))
            continue;

        KeyConfig newConfig = oldConfig;
        newConfig.hotkeys.removeIf([&reservedHotkeys](const QString &hotkey) {
            return reservedHotkeys.contains(hotkey);
        });
        if (newConfig.hotkeys == oldConfig.hotkeys)
            continue;

        CustomShortcutChange change;
        change.hasOldTarget = true;
        change.oldTarget = oldConfig;
        change.newTarget = newConfig;

        CustomShortcutTransaction transaction(manager, change);
        if (!transaction.applyRuntime()) {
            qCWarning(logShortcut) << "Reset: failed to clear custom shortcut conflict at runtime:"
                                   << "id:" << oldConfig.getId()
                                   << "old hotkeys:" << oldConfig.hotkeys
                                   << "target hotkeys:" << newConfig.hotkeys;
            return;
        }
        if (!transaction.persistModify()) {
            qCWarning(logShortcut) << "Reset: failed to persist custom shortcut conflict cleanup:"
                                   << "id:" << oldConfig.getId()
                                   << "old hotkeys:" << oldConfig.hotkeys
                                   << "target hotkeys:" << newConfig.hotkeys;
            return;
        }

        transaction.publish();
    }
}

// Applies runtime bindings and rolls back if registration or commit fails.
bool KeybindingManager::CustomShortcutTransaction::applyRuntime()
{
    unregisterNewState();

    const QStringList excludeIds = changedIds();
    if (!registerIfNeeded(m_change.newTarget, excludeIds)) {
        qCWarning(logShortcut) << "CustomShortcutTransaction: failed to register target shortcut"
                   << m_change.newTarget.getId();
        restoreRuntime();
        return false;
    }

    if (m_change.hasConflict && !registerIfNeeded(m_change.newConflict, excludeIds)) {
        qCWarning(logShortcut) << "CustomShortcutTransaction: failed to register conflict shortcut"
                   << m_change.oldConflict.getId();
        restoreRuntime();
        return false;
    }

    if (!m_manager->m_keyHandler->commitSync()) {
        qCWarning(logShortcut) << "CustomShortcutTransaction: runtime commit failed, rolling back"
                   << m_change.newTarget.getId();
        restoreRuntime();
        return false;
    }

    return true;
}

// Persists a newly added custom shortcut and rolls back on failure.
bool KeybindingManager::CustomShortcutTransaction::persistAdd()
{
    if (!m_manager->m_loader->saveCustomShortcut(m_change.newTarget)) {
        qCWarning(logShortcut) << "AddCustomShortcut: failed to persist custom shortcut, rolling back"
                   << m_change.newTarget.getId();
        restoreRuntime();
        return false;
    }

    if (m_change.hasConflict) {
        stageConflictMap();
        if (!m_manager->m_loader->updateValue(m_change.oldConflict.getId(),
                                              "hotkeys",
                                              m_change.newConflict.hotkeys)) {
            qCWarning(logShortcut) << "AddCustomShortcut: failed to persist conflict shortcut, rolling back"
                       << m_change.oldConflict.getId();
            restoreConflictMap();
            if (!m_manager->m_loader->removeCustomShortcut(m_change.newTarget.getId())) {
                qCCritical(logShortcut) << "AddCustomShortcut: failed to remove persisted custom shortcut during rollback"
                            << m_change.newTarget.getId();
            }
            restoreRuntime();
            return false;
        }
    }

    return true;
}

// Persists a modified custom shortcut and rolls back on failure.
bool KeybindingManager::CustomShortcutTransaction::persistModify()
{
    if (!m_manager->m_loader->updateCustomShortcut(m_change.newTarget)) {
        qCWarning(logShortcut) << "ModifyCustomShortcut: failed to persist custom shortcut, rolling back"
                   << m_change.newTarget.getId();
        if (!m_manager->m_loader->updateCustomShortcut(m_change.oldTarget)) {
            qCCritical(logShortcut) << "ModifyCustomShortcut: failed to restore persisted custom shortcut during rollback"
                        << m_change.oldTarget.getId();
        }
        restoreRuntime();
        return false;
    }

    if (m_change.hasConflict) {
        stageConflictMap();
        if (!m_manager->m_loader->updateValue(m_change.oldConflict.getId(),
                                              "hotkeys",
                                              m_change.newConflict.hotkeys)) {
            qCWarning(logShortcut) << "ModifyCustomShortcut: failed to persist conflict shortcut, rolling back"
                       << m_change.oldConflict.getId();
            restoreConflictMap();
            if (!m_manager->m_loader->updateCustomShortcut(m_change.oldTarget)) {
                qCCritical(logShortcut) << "ModifyCustomShortcut: failed to restore persisted custom shortcut during conflict rollback"
                            << m_change.oldTarget.getId();
            }
            restoreRuntime();
            return false;
        }
    }

    return true;
}

// Publishes the final in-memory state and change signals.
void KeybindingManager::CustomShortcutTransaction::publish()
{
    if (m_change.hasConflict) {
        m_manager->m_keyConfigsMap[m_change.oldConflict.getId()] = m_change.newConflict;
        emit m_manager->ShortcutChanged(m_change.oldConflict.getId(),
                                        m_manager->toShortcutInfo(m_change.newConflict));
    }

    m_manager->m_keyConfigsMap[m_change.newTarget.getId()] = m_change.newTarget;
    emit m_manager->ShortcutChanged(m_change.newTarget.getId(),
                                    m_manager->toShortcutInfo(m_change.newTarget));
}

// Returns ids that may conflict while this change is being applied.
QStringList KeybindingManager::CustomShortcutTransaction::changedIds() const
{
    QStringList ids;
    appendUniqueId(ids, m_change.newTarget.getId());
    if (m_change.hasOldTarget)
        appendUniqueId(ids, m_change.oldTarget.getId());
    if (m_change.hasConflict)
        appendUniqueId(ids, m_change.oldConflict.getId());
    return ids;
}

// Registers a shortcut only when it has hotkeys to bind.
bool KeybindingManager::CustomShortcutTransaction::registerIfNeeded(
        const KeyConfig &config, const QStringList &excludeIds) const
{
    if (config.hotkeys.isEmpty())
        return true;
    return m_manager->registerShortcut(config, excludeIds);
}

// Removes runtime bindings that will be replaced by this change.
void KeybindingManager::CustomShortcutTransaction::unregisterNewState()
{
    if (m_change.hasOldTarget)
        m_manager->unregisterShortcut(m_change.oldTarget.getId());
    if (m_change.hasConflict)
        m_manager->unregisterShortcut(m_change.oldConflict.getId());
}

// Restores the old runtime bindings after a failed step.
void KeybindingManager::CustomShortcutTransaction::restoreRuntime()
{
    m_manager->unregisterShortcut(m_change.newTarget.getId());
    if (m_change.hasConflict)
        m_manager->unregisterShortcut(m_change.oldConflict.getId());

    const QStringList excludeIds = changedIds();
    bool restored = true;
    if (m_change.hasConflict)
        restored = registerIfNeeded(m_change.oldConflict, excludeIds) && restored;
    if (m_change.hasOldTarget)
        restored = registerIfNeeded(m_change.oldTarget, excludeIds) && restored;

    if (!restored) {
        qCCritical(logShortcut) << "CustomShortcutTransaction: failed to restore shortcut registration"
                    << m_change.newTarget.getId();
    }
    if (!m_manager->m_keyHandler->commitSync()) {
        qCCritical(logShortcut) << "CustomShortcutTransaction: rollback commit failed,"
                    << "runtime state may diverge from compositor";
    }
}

// Mirrors the conflict change while ConfigLoader writes it.
void KeybindingManager::CustomShortcutTransaction::stageConflictMap()
{
    if (m_change.hasConflict)
        m_manager->m_keyConfigsMap[m_change.oldConflict.getId()] = m_change.newConflict;
}

// Restores the conflict shortcut in memory after a failed write.
void KeybindingManager::CustomShortcutTransaction::restoreConflictMap()
{
    if (m_change.hasConflict)
        m_manager->m_keyConfigsMap[m_change.oldConflict.getId()] = m_change.oldConflict;
}
