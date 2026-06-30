// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "keybindingmanager.h"

#include <QStringList>

// Handles apply, persist, publish, and rollback for one custom shortcut change.
class KeybindingManager::CustomShortcutTransaction
{
public:
    CustomShortcutTransaction(KeybindingManager *manager, const CustomShortcutChange &change);

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
