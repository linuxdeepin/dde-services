// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef TREELANDLOCKSCREENWRAPPER_H
#define TREELANDLOCKSCREENWRAPPER_H

#include <QObject>
#include <QScopedPointer>
#include <QWaylandClientExtensionTemplate>

#include <functional>

#include "qwayland-treeland-dde-shell-v1.h"

class TreelandLockScreenWorker;

/**
 * @brief Treeland dde-shell binding for the lockscreen sub-interface.
 *
 * On Wayland the legacy ShutdownFront1 / LockFront1 D-Bus services are gone;
 * the supported path goes through this Treeland protocol. The manager binds
 * to treeland_dde_shell_manager_v1, lazily acquires a treeland_lockscreen_v1
 * worker, and routes lock / shutdown / switch_user through it.
 */
class TreelandLockScreenManager
    : public QWaylandClientExtensionTemplate<TreelandLockScreenManager>,
      public QtWayland::treeland_dde_shell_manager_v1
{
    Q_OBJECT
public:
    explicit TreelandLockScreenManager(QObject *parent = nullptr);
    ~TreelandLockScreenManager() override;

    bool lock(int activateTimeoutMs = 1000);
    bool showShutdown(int activateTimeoutMs = 1000);
    bool switchUser(int activateTimeoutMs = 1000);

private:
    using WorkerAction = std::function<void(TreelandLockScreenWorker *)>;
    bool invoke(const WorkerAction &action, int activateTimeoutMs, const char *op);

    TreelandLockScreenWorker *worker();

    QScopedPointer<TreelandLockScreenWorker> m_worker;
};

class TreelandLockScreenWorker
    : public QWaylandClientExtensionTemplate<TreelandLockScreenWorker>,
      public QtWayland::treeland_lockscreen_v1
{
    Q_OBJECT
public:
    explicit TreelandLockScreenWorker(struct ::treeland_lockscreen_v1 *object);
};

#endif // TREELANDLOCKSCREENWRAPPER_H
