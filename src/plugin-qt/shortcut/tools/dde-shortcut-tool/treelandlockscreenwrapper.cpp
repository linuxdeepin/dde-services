// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "treelandlockscreenwrapper.h"

#include <QDebug>
#include <QEventLoop>
#include <QGuiApplication>
#include <QTimer>
#include <QtCore/qnativeinterface.h>

extern "C" {
#include <wayland-client-core.h>
}

TreelandLockScreenManager::TreelandLockScreenManager(QObject *parent)
    : QWaylandClientExtensionTemplate<TreelandLockScreenManager>(
          treeland_compositor_action_v1_interface.version)
{
    setParent(parent);
    initialize();
}

TreelandLockScreenManager::~TreelandLockScreenManager()
{
    if (isInitialized())
        destroy();
}

bool TreelandLockScreenManager::lock(int activateTimeoutMs)
{
    return invoke(action_lockscreen, activateTimeoutMs, "lock");
}

bool TreelandLockScreenManager::showShutdown(int activateTimeoutMs)
{
    return invoke(action_shutdown_menu, activateTimeoutMs, "shutdown");
}

bool TreelandLockScreenManager::switchUser(int activateTimeoutMs)
{
    return invoke(action_show_user_switch, activateTimeoutMs, "switch_user");
}

bool TreelandLockScreenManager::invoke(action requestedAction,
                                       int activateTimeoutMs, const char *op)
{
    // The compositor binds the global asynchronously, so a freshly-constructed
    // wrapper is usually still inactive. Spin the event loop briefly until
    // activeChanged fires.
    if (!isActive()) {
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        connect(this, &TreelandLockScreenManager::activeChanged,
                &loop, &QEventLoop::quit);
        connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        timer.start(activateTimeoutMs);
        loop.exec();
    }

    if (!isActive()) {
        qWarning() << "TreelandLockScreenManager: protocol not active, cannot" << op;
        return false;
    }

    trigger(requestedAction);

    // Without a window, libwayland's outgoing buffer can sit unflushed for a
    // long time. Force the flush so the compositor sees the request before
    // this short-lived tool exits.
    if (auto *wlApp = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>()) {
        wl_display_flush(wlApp->display());
    }
    return true;
}
