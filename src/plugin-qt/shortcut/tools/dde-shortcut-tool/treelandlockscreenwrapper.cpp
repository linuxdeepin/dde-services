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
          treeland_dde_shell_manager_v1_interface.version)
{
    setParent(parent);
    initialize();
}

TreelandLockScreenManager::~TreelandLockScreenManager() = default;

bool TreelandLockScreenManager::lock(int activateTimeoutMs)
{
    return invoke([](TreelandLockScreenWorker *w) { w->lock(); },
                  activateTimeoutMs, "lock");
}

bool TreelandLockScreenManager::showShutdown(int activateTimeoutMs)
{
    return invoke([](TreelandLockScreenWorker *w) { w->shutdown(); },
                  activateTimeoutMs, "shutdown");
}

bool TreelandLockScreenManager::switchUser(int activateTimeoutMs)
{
    return invoke([](TreelandLockScreenWorker *w) { w->switch_user(); },
                  activateTimeoutMs, "switch_user");
}

bool TreelandLockScreenManager::invoke(const WorkerAction &action,
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

    auto *impl = worker();
    if (!impl) {
        qWarning() << "TreelandLockScreenManager: protocol not active, cannot" << op;
        return false;
    }

    action(impl);

    // Without a window, libwayland's outgoing buffer can sit unflushed for a
    // long time. Force the flush so the compositor sees the request before
    // this short-lived tool exits.
    if (auto *wlApp = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>()) {
        wl_display_flush(wlApp->display());
    }
    return true;
}

TreelandLockScreenWorker *TreelandLockScreenManager::worker()
{
    if (!isActive())
        return nullptr;

    if (m_worker.isNull())
        m_worker.reset(new TreelandLockScreenWorker(get_treeland_lockscreen()));

    return m_worker.get();
}

TreelandLockScreenWorker::TreelandLockScreenWorker(struct ::treeland_lockscreen_v1 *object)
    : QWaylandClientExtensionTemplate<TreelandLockScreenWorker>(
          treeland_lockscreen_v1_interface.version)
    , QtWayland::treeland_lockscreen_v1(object)
{
}
