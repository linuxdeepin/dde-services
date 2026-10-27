// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QObject>
#include <QSet>
#include <QWaylandClientExtension>

#include "qwayland-treeland-shortcut-manager-unstable-v3.h"

class TreelandShortcutWrapper : public QWaylandClientExtensionTemplate<TreelandShortcutWrapper>,
                        public QtWayland::treeland_shortcut_manager_v3
{
    Q_OBJECT
public:
    explicit TreelandShortcutWrapper(QObject *parent = nullptr);
    ~TreelandShortcutWrapper() override;

    bool bindKey(const QString &name, const QString &key, uint32_t flags, int action);
    bool bindSwipeGesture(const QString &name, int finger, int direction, int action);
    bool bindHoldGesture(const QString &name, int finger, int action);
    bool unbind(const QString &name);
    void flush();
    bool syncAndWait(int timeoutMs = 1000);

    // Coalesce outgoing flushes. V3 applies requests individually and has no
    // commit or success event. syncAndWait checks failures up to a display sync;
    // inactive sessions may still report a deferred bind failure afterwards.
    void flushDeferred();

    /**
     * @brief Initialize protocol binding (deferred initialization)
     * Connect ready and protocolInactive signals before calling this method
     */
    void initProtocol();

signals:
    void ready();              // Protocol ready, can start registering shortcuts
    void protocolInactive();   // Protocol disconnected, need to clear state
    void activated(const QString &name, uint32_t flags);
    void bindFailed(const QString &name, uint32_t error);

protected:
    void treeland_shortcut_manager_v3_activated(const QString &name, uint32_t flags) override;
    void treeland_shortcut_manager_v3_bind_failure(const QString &name, uint32_t error) override;

private:
    struct PendingSync;
    struct ::treeland_shortcut_manager_v3 *m_boundObject = nullptr;
    QSet<QString> m_pendingBindings;
    QList<PendingSync *> m_syncs;
    bool m_pendingBindFailed = false;
    bool m_flushPending = false;
};
