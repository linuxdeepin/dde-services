// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "sessiondbusproxy.h"
#include "../powerconstants.h"

#include <QDBusConnection>
#include <QDBusReply>
#include <QDBusUnixFileDescriptor>

using namespace PowerDBus;

SessionDBusProxy::SessionDBusProxy(QObject *parent)
    : QObject(parent)
    , m_powerInter(new DDBusInterface(
          PowerDBus::kService, PowerDBus::kPath, PowerDBus::kInterface,
          QDBusConnection::systemBus(), this))
    , m_sessionManagerInter(new DDBusInterface(
          kSessionManager, kSessionPath, kSessionManager,
          QDBusConnection::sessionBus(), this))
    , m_shutdownFrontInter(new DDBusInterface(
          kShutdownFront, kShutdownPath, kShutdownFront,
          QDBusConnection::sessionBus(), this))
    , m_login1Inter(new DDBusInterface(
          kLogin1Service, kLogin1Path, kLogin1Manager,
          QDBusConnection::systemBus(), this))
    , m_lockFrontInter(new DDBusInterface(
          kLockFront, kLockFrontPath, kLockFront,
          QDBusConnection::sessionBus(), this))
    , m_blackScreenInter(new DDBusInterface(
          kBlackScreen, kBlackScreenPath, kBlackScreen,
          QDBusConnection::sessionBus(), this))
    , m_displayInter(new DDBusInterface(
          kDisplay, kDisplayPath, kDisplay,
          QDBusConnection::sessionBus(), this))
    , m_notificationsInter(new DDBusInterface(
          kNotifications, kNotificationsPath, kNotifications,
          QDBusConnection::sessionBus(), this))
    , m_sessionWatcherInter(new DDBusInterface(
          kSessionWatcher, kSessionWatcherPath, kSessionWatcher,
          QDBusConnection::sessionBus(), this))
    , m_calendarInter(new DDBusInterface(
          kCalendarService, kCalendarPath, kCalendarIface,
          QDBusConnection::sessionBus(), this))
    , m_timedateInter(new DDBusInterface(
          "org.deepin.dde.Timedate1", "/org/deepin/dde/Timedate1",
          "org.deepin.dde.Timedate1", QDBusConnection::sessionBus(), this))
    , m_freedesktopDBusInter(new DDBusInterface(
          kFreedesktopDBus, kFreedesktopPath, kFreedesktopDBus,
          QDBusConnection::systemBus(), this))
{
    QDBusConnection::sessionBus().connect(
        m_notificationsInter->service(), m_notificationsInter->path(),
        m_notificationsInter->interface(),
        "ActionInvoked", this, SIGNAL(notifyActionInvoked(uint,QString)));

    QDBusConnection::sessionBus().connect(
        m_timedateInter->service(), m_timedateInter->path(),
        m_timedateInter->interface(),
        "TimeUpdate", this, SIGNAL(timeUpdate()));

    QDBusConnection::systemBus().connect(
        m_freedesktopDBusInter->service(), m_freedesktopDBusInter->path(),
        m_freedesktopDBusInter->interface(),
        "NameOwnerChanged", this, SIGNAL(login1OwnerChanged(QString,QString,QString)));
}

bool SessionDBusProxy::onBattery() const
{
    return m_powerInter->property("OnBattery").toBool();
}

bool SessionDBusProxy::hasLidSwitch() const
{
    return m_powerInter->property("HasLidSwitch").toBool();
}

bool SessionDBusProxy::hasBattery() const
{
    return m_powerInter->property("HasBattery").toBool();
}

double SessionDBusProxy::batteryPercentage() const
{
    return m_powerInter->property("BatteryPercentage").toDouble();
}

uint SessionDBusProxy::batteryStatus() const
{
    return m_powerInter->property("BatteryStatus").toUInt();
}

quint64 SessionDBusProxy::batteryTimeToEmpty() const
{
    return m_powerInter->property("BatteryTimeToEmpty").toULongLong();
}

bool SessionDBusProxy::isHighPerformanceSupported() const
{
    return m_powerInter->property("IsHighPerformanceSupported").toBool();
}

bool SessionDBusProxy::powerSavingModeEnabled() const
{
    return m_powerInter->property("PowerSavingModeEnabled").toBool();
}

uint SessionDBusProxy::powerSavingModeBrightnessDropPercent() const
{
    return m_powerInter->property("PowerSavingModeBrightnessDropPercent").toUInt();
}

bool SessionDBusProxy::sessionActive() const
{
    return m_sessionWatcherInter->property("IsActive").toBool();
}

void SessionDBusProxy::requestSuspend()
{
    m_sessionManagerInter->asyncCall("RequestSuspend");
}

void SessionDBusProxy::requestShutdown()
{
    m_sessionManagerInter->asyncCall("RequestShutdown");
}

void SessionDBusProxy::requestHibernate()
{
    m_sessionManagerInter->asyncCall("RequestHibernate");
}

bool SessionDBusProxy::canSuspend()
{
    QDBusReply<bool> r = m_sessionManagerInter->call("CanSuspend");
    return r.isValid() && r.value();
}

bool SessionDBusProxy::canHibernate()
{
    QDBusReply<bool> r = m_sessionManagerInter->call("CanHibernate");
    return r.isValid() && r.value();
}

void SessionDBusProxy::requestSuspendByFront()
{
    m_shutdownFrontInter->asyncCall("Suspend");
}

void SessionDBusProxy::showLockAuth(bool autoStart)
{
    m_lockFrontInter->call("ShowAuth", autoStart);
}

void SessionDBusProxy::lockSession(const QString &sessionId)
{
    QDBusReply<void> r = m_login1Inter->call("LockSession", sessionId);
    if (!r.isValid())
        qWarning("[Proxy] LockSession(%s) failed: %s", qPrintable(sessionId), qPrintable(r.error().message()));
}

QDBusUnixFileDescriptor SessionDBusProxy::inhibit(const QString &what, const QString &who,
                                                   const QString &why, const QString &mode)
{
    QDBusReply<QDBusUnixFileDescriptor> r = m_login1Inter->call("Inhibit", what, who, why, mode);
    if (r.isValid())
        return r.value();
    qWarning("[Proxy] Inhibit failed: %s", qPrintable(r.error().message()));
    return {};
}

void SessionDBusProxy::setBlackScreenActive(bool active)
{
    m_blackScreenInter->asyncCall("setActive", active);
}

void SessionDBusProxy::setBrightness(const QString &monitor, double value)
{
    m_displayInter->asyncCall("SetBrightness", monitor, value);
}

void SessionDBusProxy::setAndSaveBrightness(const QString &monitor, double value)
{
    m_displayInter->asyncCall("SetAndSaveBrightness", monitor, value);
}

uint SessionDBusProxy::notify(uint replaceId, const QString &appName, const QString &icon,
                              const QString &title, const QString &body,
                              const QStringList &actions, const QVariantMap &hints, int timeout)
{
    QDBusReply<uint> r = m_notificationsInter->call(
        "Notify", appName, replaceId, icon, title, body, actions, hints, timeout);
    return r.isValid() ? r.value() : 0;
}

void SessionDBusProxy::closeNotification(uint id)
{
    m_notificationsInter->asyncCall("CloseNotification", id);
}

QString SessionDBusProxy::getFestivalMonth(int year, int month)
{
    QDBusReply<QString> r = m_calendarInter->call("getFestivalMonth", year, month);
    return r.isValid() ? r.value() : QString();
}
