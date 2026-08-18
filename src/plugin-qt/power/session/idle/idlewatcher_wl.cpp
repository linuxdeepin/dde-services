// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "idlewatcher_wl.h"

#include <QGuiApplication>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(logPowerSession)

IdleNotification::IdleNotification(::ext_idle_notification_v1 *id)
    : QtWayland::ext_idle_notification_v1(id)
{
}

IdleNotification::~IdleNotification()
{
    destroy();
}

void IdleNotification::ext_idle_notification_v1_idled()
{
    if (!m_active)
        return;

    Q_EMIT idled();
}

void IdleNotification::ext_idle_notification_v1_resumed()
{
    if (!m_active)
        return;

    Q_EMIT resumed();
}

IdleNotifier::IdleNotifier()
    : QWaylandClientExtensionTemplate<IdleNotifier>(1)
{
}

IdleNotifier::~IdleNotifier()
{
    if (isInitialized())
        destroy();
}

void IdleNotifier::instantiate()
{
    initialize();
}

std::unique_ptr<IdleNotification>
IdleNotifier::getIdleNotification(uint32_t timeout, wl_seat *seat)
{
    if (!isInitialized() || !seat)
        return nullptr;

    auto *raw = get_idle_notification(timeout, seat);
    if (!raw) {
        qWarning(logPowerSession) << "[Power::IDLE] getIdleNotification: failed to create idle notification";
        return nullptr;
    }

    return std::make_unique<IdleNotification>(raw);
}

WaylandIdleWatcher::WaylandIdleWatcher(QObject *parent)
    : IdleWatcher(parent)
{
    auto *app = qGuiApp;
    if (!app) {
        qWarning(logPowerSession) << "[Power::WL] Idle: no QGuiApplication";
        return;
    }

    auto *wlApp =
        app->nativeInterface<QNativeInterface::QWaylandApplication>();

    if (!wlApp) {
        qWarning(logPowerSession) << "[Power::WL] Idle: no QWaylandApplication";
        return;
    }

    m_seat = wlApp->seat();
    if (!m_seat) {
        qWarning(logPowerSession) << "[Power::WL] Idle: no wl_seat";
        return;
    }

    m_notifier.reset(new IdleNotifier);
    m_notifier->instantiate();

    if (!m_notifier->isInitialized()) {
        qWarning(logPowerSession) << "[Power::WL] Idle: notifier init failed";
        return;
    }

    switchToNotification(m_timeoutSec);
    m_valid = true;
}

WaylandIdleWatcher::~WaylandIdleWatcher()
{
    if (m_activeNotification)
        m_activeNotification->disconnect(this);
    m_notificationCache.clear();
    m_notifier.reset();
}

void WaylandIdleWatcher::switchToNotification(uint32_t timeoutSec)
{
    if (!m_notifier || !m_seat || timeoutSec == 0)
        return;

    // 1. 断开旧 notification
    if (m_activeNotification) {
        m_activeNotification->setActive(false);
        m_activeNotification->disconnect(this);
        m_activeNotification = nullptr;
    }

    // 2. 缓存命中则复用, 否则从 notifier 创建新 notification 并缓存
    auto it = m_notificationCache.find(timeoutSec);
    if (it == m_notificationCache.end()) {
        auto notif = m_notifier->getIdleNotification(timeoutSec * 1000, m_seat);
        if (!notif) {
            qWarning(logPowerSession) << "[Power::IDLE] switchToNotification: failed, timeout="
                       << timeoutSec << "s";
            return;
        }
        qDebug(logPowerSession) << "[Power::IDLE] create & cache notification for timeout=" << timeoutSec << "s";
        auto [inserted, _] = m_notificationCache.emplace(timeoutSec, std::move(notif));
        it = inserted;
    }

    // 3. 激活新的 notification
    m_activeNotification = it->second.get();
    m_activeNotification->setActive(true);

    m_isIdle = false;
    m_idleTimer.invalidate();

    connect(m_activeNotification, &IdleNotification::idled, this, [this]() {
        if (m_isIdle)
            return;

        m_isIdle = true;
        m_idleTimer.start();

        Q_EMIT idled();
    });

    connect(m_activeNotification, &IdleNotification::resumed, this, [this]() {
        if (!m_isIdle)
            return;

        m_isIdle = false;
        m_idleTimer.invalidate();

        Q_EMIT resumed();
    });
}

void WaylandIdleWatcher::setTimeout(uint32_t s)
{
    if (m_timeoutSec == s)
        return;

    m_timeoutSec = s;
    switchToNotification(s);
}

void WaylandIdleWatcher::simulateActivity()
{
    // unused
}

uint32_t WaylandIdleWatcher::idleTimeMs() const
{
    if (!m_isIdle)
        return 0;

    if (!m_idleTimer.isValid())
        return 0;

    return static_cast<uint32_t>(m_idleTimer.elapsed());
}
