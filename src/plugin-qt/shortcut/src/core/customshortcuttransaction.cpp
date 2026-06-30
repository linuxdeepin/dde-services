// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

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
KeybindingManager::CustomShortcutTransaction::CustomShortcutTransaction(
        KeybindingManager *manager, const CustomShortcutChange &change)
    : m_manager(manager)
    , m_change(change)
{
}

// Applies runtime bindings and rolls back if registration or commit fails.
bool KeybindingManager::CustomShortcutTransaction::applyRuntime()
{
    unregisterNewState();

    const QStringList excludeIds = changedIds();
    if (!registerIfNeeded(m_change.newTarget, excludeIds)) {
        qWarning() << "CustomShortcutTransaction: failed to register target shortcut"
                   << m_change.newTarget.getId();
        restoreRuntime();
        return false;
    }

    if (m_change.hasConflict && !registerIfNeeded(m_change.newConflict, excludeIds)) {
        qWarning() << "CustomShortcutTransaction: failed to register conflict shortcut"
                   << m_change.oldConflict.getId();
        restoreRuntime();
        return false;
    }

    if (!m_manager->m_keyHandler->commitSync()) {
        qWarning() << "CustomShortcutTransaction: runtime commit failed, rolling back"
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
        qWarning() << "AddCustomShortcut: failed to persist custom shortcut, rolling back"
                   << m_change.newTarget.getId();
        restoreRuntime();
        return false;
    }

    if (m_change.hasConflict) {
        stageConflictMap();
        if (!m_manager->m_loader->updateValue(m_change.oldConflict.getId(),
                                              "hotkeys",
                                              m_change.newConflict.hotkeys)) {
            qWarning() << "AddCustomShortcut: failed to persist conflict shortcut, rolling back"
                       << m_change.oldConflict.getId();
            restoreConflictMap();
            if (!m_manager->m_loader->removeCustomShortcut(m_change.newTarget.getId())) {
                qCritical() << "AddCustomShortcut: failed to remove persisted custom shortcut during rollback"
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
        qWarning() << "ModifyCustomShortcut: failed to persist custom shortcut, rolling back"
                   << m_change.newTarget.getId();
        if (!m_manager->m_loader->updateCustomShortcut(m_change.oldTarget)) {
            qCritical() << "ModifyCustomShortcut: failed to restore persisted custom shortcut during rollback"
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
            qWarning() << "ModifyCustomShortcut: failed to persist conflict shortcut, rolling back"
                       << m_change.oldConflict.getId();
            restoreConflictMap();
            if (!m_manager->m_loader->updateCustomShortcut(m_change.oldTarget)) {
                qCritical() << "ModifyCustomShortcut: failed to restore persisted custom shortcut during conflict rollback"
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
        qCritical() << "CustomShortcutTransaction: failed to restore shortcut registration"
                    << m_change.newTarget.getId();
    }
    if (!m_manager->m_keyHandler->commitSync()) {
        qCritical() << "CustomShortcutTransaction: rollback commit failed,"
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
