// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QObject>
#include <QWaylandClientExtension>

#include "qwayland-treeland-shortcut-manager-v2.h"

class TreelandShortcutWrapper : public QWaylandClientExtensionTemplate<TreelandShortcutWrapper>,
                        public QtWayland::treeland_shortcut_manager_v2
{
    Q_OBJECT
public:
    explicit TreelandShortcutWrapper(QObject *parent = nullptr);
    ~TreelandShortcutWrapper() override;

    bool bindKey(const QString &name, const QString &key, uint32_t flags, int action);
    bool bindSwipeGesture(const QString &name, int finger, int direction, int action);
    bool bindHoldGesture(const QString &name, int finger, int action);
    bool unbind(const QString &name);
    void commit();
    bool commitAndWait(int timeoutMs = 3000);

    /**
     * @brief Initialize protocol binding (deferred initialization)
     * Connect ready and protocolInactive signals before calling this method
     */
    void initProtocol();

signals:
    void ready();              // Protocol ready, can start registering shortcuts
    void protocolInactive();   // Protocol disconnected, need to clear state
    void activated(const QString &name, uint32_t flags);
    void commitStatus(bool success);

protected:
    void treeland_shortcut_manager_v2_activated(const QString &name, uint32_t flags) override;
    void treeland_shortcut_manager_v2_commit_success() override;
    void treeland_shortcut_manager_v2_commit_failure(const QString &name, uint32_t error) override;

private:
    struct ::treeland_shortcut_manager_v2 *m_boundObject = nullptr;  // Track bound object for session recovery
};
