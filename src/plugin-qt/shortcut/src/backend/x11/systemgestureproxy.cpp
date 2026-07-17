// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "systemgestureproxy.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QDebug>

namespace {

constexpr auto Service = "org.deepin.dde.Gesture1";
constexpr auto Path = "/org/deepin/dde/Gesture1";
constexpr auto Interface = "org.deepin.dde.Gesture1";

}

SystemGestureProxy::SystemGestureProxy(QObject *parent)
    : QObject(parent)
    , m_watcher(new QDBusServiceWatcher(QLatin1String(Service),
                                        QDBusConnection::systemBus(),
                                        QDBusServiceWatcher::WatchForRegistration
                                            | QDBusServiceWatcher::WatchForUnregistration,
                                        this))
{
    connect(m_watcher, &QDBusServiceWatcher::serviceRegistered,
            this, &SystemGestureProxy::onServiceRegistered);
    connect(m_watcher, &QDBusServiceWatcher::serviceUnregistered,
            this, &SystemGestureProxy::onServiceUnregistered);

    QDBusConnectionInterface *busInterface = QDBusConnection::systemBus().interface();
    if (!busInterface)
        return;

    const QDBusReply<bool> registered = busInterface->isServiceRegistered(QLatin1String(Service));
    if (registered.isValid() && registered.value())
        onServiceRegistered();
}

void SystemGestureProxy::onServiceRegistered()
{
    setAvailable(connectEventSignal());
}

void SystemGestureProxy::onServiceUnregistered()
{
    disconnectEventSignal();
    setAvailable(false);
}

void SystemGestureProxy::onEvent(const QString &name, const QString &direction, int fingers)
{
    emit eventReceived(name, direction, fingers);
}

void SystemGestureProxy::onDoubleClickDown(int fingers)
{
    emit doubleClickDown(fingers);
}

void SystemGestureProxy::onSwipeMoving(int fingers, double accelX, double accelY)
{
    emit swipeMoving(fingers, accelX, accelY);
}

void SystemGestureProxy::onSwipeStopped(int fingers)
{
    emit swipeStopped(fingers);
}

bool SystemGestureProxy::connectEventSignal()
{
    if (m_connected)
        return true;

    m_connected = QDBusConnection::systemBus().connect(QLatin1String(Service),
                                                       QLatin1String(Path),
                                                       QLatin1String(Interface),
                                                       QStringLiteral("Event"),
                                                       this,
                                                       SLOT(onEvent(QString,QString,int)));
    const bool doubleClickConnected = QDBusConnection::systemBus().connect(
            QLatin1String(Service), QLatin1String(Path), QLatin1String(Interface),
            QStringLiteral("DbclickDown"), this, SLOT(onDoubleClickDown(int)));
    const bool movingConnected = QDBusConnection::systemBus().connect(
            QLatin1String(Service), QLatin1String(Path), QLatin1String(Interface),
            QStringLiteral("SwipeMoving"), this, SLOT(onSwipeMoving(int,double,double)));
    const bool stoppedConnected = QDBusConnection::systemBus().connect(
            QLatin1String(Service), QLatin1String(Path), QLatin1String(Interface),
            QStringLiteral("SwipeStop"), this, SLOT(onSwipeStopped(int)));
    if (!m_connected)
        qWarning() << "SystemGestureProxy: failed to subscribe to system gesture events";
    if (!doubleClickConnected || !movingConnected || !stoppedConnected)
        qWarning() << "SystemGestureProxy: failed to subscribe to window-move gesture events";
    return m_connected;
}

void SystemGestureProxy::disconnectEventSignal()
{
    if (!m_connected)
        return;
    QDBusConnection::systemBus().disconnect(QLatin1String(Service),
                                            QLatin1String(Path),
                                            QLatin1String(Interface),
                                            QStringLiteral("Event"),
                                            this,
                                            SLOT(onEvent(QString,QString,int)));
    QDBusConnection::systemBus().disconnect(QLatin1String(Service), QLatin1String(Path),
                                            QLatin1String(Interface), QStringLiteral("DbclickDown"),
                                            this, SLOT(onDoubleClickDown(int)));
    QDBusConnection::systemBus().disconnect(QLatin1String(Service), QLatin1String(Path),
                                            QLatin1String(Interface), QStringLiteral("SwipeMoving"),
                                            this, SLOT(onSwipeMoving(int,double,double)));
    QDBusConnection::systemBus().disconnect(QLatin1String(Service), QLatin1String(Path),
                                            QLatin1String(Interface), QStringLiteral("SwipeStop"),
                                            this, SLOT(onSwipeStopped(int)));
    m_connected = false;
}

void SystemGestureProxy::setAvailable(bool available)
{
    if (m_available == available)
        return;
    m_available = available;
    emit availabilityChanged(available);
}
