// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "keybindingmanager.h"

#include <QSet>
#include <QStringList>

// Handles transactional runtime and persistence changes for custom shortcuts.
class KeybindingManager::CustomShortcutTransaction
{
public:
    CustomShortcutTransaction(KeybindingManager *manager, const CustomShortcutChange &change);

    static void clearConflictingHotkeys(KeybindingManager *manager, const QSet<QString> &reservedHotkeys);

    bool applyRuntime();
    bool persistAdd();
    bool persistModify();
    void publish();

private:
    QStringList changedIds() const;
    bool registerIfNeeded(const KeyConfig &config, const QStringList &excludeIds) const;
    void unregisterNewState();
    void restoreRuntime();
    void stageConflictMap();
    void restoreConflictMap();

    KeybindingManager *m_manager = nullptr;
    CustomShortcutChange m_change;
};
