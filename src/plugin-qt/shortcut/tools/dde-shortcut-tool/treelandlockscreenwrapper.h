// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef TREELANDLOCKSCREENWRAPPER_H
#define TREELANDLOCKSCREENWRAPPER_H

#include <QObject>
#include <QWaylandClientExtensionTemplate>

#include "qwayland-treeland-compositor-action-unstable-v1.h"

/**
 * @brief Treeland compositor-action binding for session UI operations.
 *
 * On Wayland the legacy ShutdownFront1 / LockFront1 D-Bus services are gone;
 * the manager binds directly to treeland_compositor_action_v1 and triggers
 * lockscreen, shutdown_menu and show_user_switch. Successful calls indicate
 * that a request was sent; the protocol does not acknowledge the action.
 */
class TreelandLockScreenManager
    : public QWaylandClientExtensionTemplate<TreelandLockScreenManager>,
      public QtWayland::treeland_compositor_action_v1
{
    Q_OBJECT
public:
    explicit TreelandLockScreenManager(QObject *parent = nullptr);
    ~TreelandLockScreenManager() override;

    bool lock(int activateTimeoutMs = 1000);
    bool showShutdown(int activateTimeoutMs = 1000);
    bool switchUser(int activateTimeoutMs = 1000);

private:
    bool invoke(action requestedAction, int activateTimeoutMs, const char *op);
};

#endif // TREELANDLOCKSCREENWRAPPER_H
