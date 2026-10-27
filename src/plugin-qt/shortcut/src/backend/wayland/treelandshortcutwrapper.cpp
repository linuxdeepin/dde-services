// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "shortcutlogging.h"

#include "treelandshortcutwrapper.h"

#include <QDebug>
#include <QEventLoop>
#include <QGuiApplication>
#include <QPointer>
#include <QTimer>
#include <QtCore/qnativeinterface.h>

#include <utility>
#include <wayland-client.h>

namespace {
wl_display *waylandDisplay()
{
    if (qGuiApp) {
        if (auto *wlApp = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>())
            return wlApp->display();
    }
    return nullptr;
}

const char *bindErrorName(uint32_t error)
{
    switch (error) {
    case QtWayland::treeland_shortcut_manager_v3::bind_error_name_conflict:
        return "name_conflict";
    case QtWayland::treeland_shortcut_manager_v3::bind_error_duplicate_binding:
        return "duplicate_binding";
    case QtWayland::treeland_shortcut_manager_v3::bind_error_invalid_argument:
        return "invalid_argument";
    case QtWayland::treeland_shortcut_manager_v3::bind_error_internal_error:
        return "internal_error";
    default:
        return "unknown";
    }
}
}  // namespace

struct TreelandShortcutWrapper::PendingSync {
    QEventLoop loop;
    QSet<QString> bindings;
    bool failed = false;
    bool done = false;
};

TreelandShortcutWrapper::TreelandShortcutWrapper(QObject *parent)
    : QWaylandClientExtensionTemplate<TreelandShortcutWrapper>(1)
    , QtWayland::treeland_shortcut_manager_v3()
{
    setParent(parent);

    connect(this, &TreelandShortcutWrapper::activeChanged, this, [this]() {
        if (isActive()) {
            qCDebug(logShortcut) << "TreelandShortcutManager protocol is now active, acquiring...";
            acquire();
            m_boundObject = object();  // Save bound object for session recovery check
            emit ready();  // Emit signal directly since signal is connected before init() is called
        } else {
            qCWarning(logShortcut) << "TreelandShortcutManager protocol is now inactive!";
            m_boundObject = nullptr;
            m_pendingBindings.clear();
            m_pendingBindFailed = false;
            emit protocolInactive();
        }
    });

    // Don't call initialize() in constructor; let external code call init() to trigger it
}

void TreelandShortcutWrapper::initProtocol()
{
    initialize();
}

TreelandShortcutWrapper::~TreelandShortcutWrapper()
{
    if (isActive()) {
        destroy();
    }
}

bool TreelandShortcutWrapper::bindKey(const QString &name, const QString &key, uint32_t flags, int action)
{
    if (!isActive()) {
        return false;
    }

    m_pendingBindings.insert(name);
    bind_key(name, key, flags, action);
    return true;
}

bool TreelandShortcutWrapper::bindSwipeGesture(const QString &name, int finger, int direction, int action)
{
    if (!isActive()) {
        return false;
    }

    m_pendingBindings.insert(name);
    bind_swipe_gesture(name, finger, direction, action);
    return true;
}

bool TreelandShortcutWrapper::bindHoldGesture(const QString &name, int finger, int action)
{
    if (!isActive()) {
        return false;
    }

    m_pendingBindings.insert(name);
    bind_hold_gesture(name, finger, action);
    return true;
}

bool TreelandShortcutWrapper::unbind(const QString &name)
{
    if (!isActive()) {
        return false;
    }

    // Check if object changed (session recovery scenario)
    if (object() != m_boundObject) {
        qCWarning(logShortcut) << "TreelandShortcutWrapper::unbind: object changed, skip unbind for" << name;
        return false;
    }

    QtWayland::treeland_shortcut_manager_v3::unbind(name);
    return true;
}

void TreelandShortcutWrapper::flush()
{
    if (!isActive()) {
        return;
    }

    // The service has no window traffic to flush queued Wayland requests.
    if (auto *display = waylandDisplay())
        wl_display_flush(display);
}

void TreelandShortcutWrapper::flushDeferred()
{
    if (m_flushPending) {
        return;
    }
    m_flushPending = true;
    QTimer::singleShot(0, this, [this]() {
        m_flushPending = false;
        flush();
    });
}

bool TreelandShortcutWrapper::syncAndWait(int timeoutMs)
{
    auto *display = waylandDisplay();
    if (!isActive() || !display) {
        return false;
    }

    auto *callback = wl_display_sync(display);
    if (!callback)
        return false;

    PendingSync sync;
    sync.bindings = std::exchange(m_pendingBindings, {});
    sync.failed = std::exchange(m_pendingBindFailed, false);
    m_syncs.append(&sync);

    static const wl_callback_listener listener = {
        [](void *data, wl_callback *, uint32_t) {
            auto *pending = static_cast<PendingSync *>(data);
            pending->done = true;
            pending->loop.quit();
        }
    };
    wl_callback_add_listener(callback, &listener, &sync);

    QPointer<TreelandShortcutWrapper> guard(this);
    const auto *boundObject = object();
    connect(this, &TreelandShortcutWrapper::protocolInactive, &sync.loop, &QEventLoop::quit);
    connect(this, &QObject::destroyed, &sync.loop, &QEventLoop::quit);
    QTimer timer;
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, &sync.loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    flush();
    sync.loop.exec();
    wl_callback_destroy(callback);

    if (!guard)
        return false;
    m_syncs.removeOne(&sync);
    if (!sync.done)
        qCWarning(logShortcut) << "Treeland shortcut sync did not complete within" << timeoutMs << "ms";
    return sync.done && !sync.failed && isActive() && object() == boundObject;
}

void TreelandShortcutWrapper::treeland_shortcut_manager_v3_activated(const QString &name, uint32_t flags)
{
    qCDebug(logShortcut) << "Treeland Shortcut Manager Activated:" << name << "flags:" << flags;
    emit activated(name, flags);
}

void TreelandShortcutWrapper::treeland_shortcut_manager_v3_bind_failure(const QString &name, uint32_t error)
{
    qCWarning(logShortcut) << "Treeland shortcut binding failed:" << name
               << "Error:" << error << bindErrorName(error);
    // Keep failures received before syncAndWait as well as those received while
    // waiting. A late failure from an inactive session must not fail a later,
    // unrelated batch, but it must still invalidate the affected binding.
    if (m_pendingBindings.contains(name))
        m_pendingBindFailed = true;
    for (PendingSync *sync : std::as_const(m_syncs)) {
        if (sync->bindings.contains(name))
            sync->failed = true;
    }
    emit bindFailed(name, error);
}
