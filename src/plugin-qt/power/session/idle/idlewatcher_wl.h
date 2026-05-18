// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "idlewatcher.h"

#include <QObject>
#include <QWaylandClientExtension>
#include <QElapsedTimer>

#include <memory>
#include <unordered_map>
#include "qwayland-ext-idle-notify-v1.h"

struct wl_seat;

class IdleNotification : public QObject, public QtWayland::ext_idle_notification_v1
{
    Q_OBJECT
public:
    explicit IdleNotification(::ext_idle_notification_v1 *id);
    ~IdleNotification() override;
    void setActive(bool on) { m_active = on; }
    bool isActive() const { return m_active; }
Q_SIGNALS:
    void idled();
    void resumed();
protected:
    void ext_idle_notification_v1_idled() override;
    void ext_idle_notification_v1_resumed() override;
private:
    bool m_active = true;
};

class IdleNotifier : public QWaylandClientExtensionTemplate<IdleNotifier>
                   , public QtWayland::ext_idle_notifier_v1
{
    Q_OBJECT
public:
    IdleNotifier();
    ~IdleNotifier() override;
    void instantiate();
    std::unique_ptr<IdleNotification> getIdleNotification(uint32_t timeout, wl_seat *seat);
};

class WaylandIdleWatcher : public IdleWatcher
{
    Q_OBJECT
public:
    explicit WaylandIdleWatcher(QObject *parent = nullptr);
    ~WaylandIdleWatcher() override;

    bool isValid() const override { return m_valid; }
    void setTimeout(uint32_t timeoutSec) override;
    void simulateActivity() override;
    uint32_t idleTimeMs() const override;
    bool isIdle() const override { return m_isIdle; }

private:
    void switchToNotification(uint32_t timeoutSec);

    std::unique_ptr<IdleNotifier> m_notifier;
    std::unordered_map<uint32_t, std::unique_ptr<IdleNotification>> m_notificationCache;
    IdleNotification *m_activeNotification = nullptr;
    wl_seat *m_seat = nullptr;
    QElapsedTimer m_idleTimer;
    uint32_t m_timeoutSec = 300;
    bool m_valid = false;
    bool m_isIdle = false;
};
