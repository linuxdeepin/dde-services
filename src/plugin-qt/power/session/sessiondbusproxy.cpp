// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "sessiondbusproxy.h"
#include "../powerconstants.h"

#include <QDBusConnection>
#include <QDBusArgument>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDBusUnixFileDescriptor>

using namespace PowerDBus;

namespace {
constexpr auto kAmbientBrightnessService = "org.deepin.dde.AmbientBrightness1";
constexpr auto kAmbientBrightnessPath = "/org/deepin/dde/AmbientBrightness1";
constexpr auto kAmbientBrightnessInterface = "org.deepin.dde.AmbientBrightness1";
}

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
          QDBusConnection::sessionBus(), nullptr))
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
    , m_ambientBrightnessInter(new DDBusInterface(
          kAmbientBrightnessService, kAmbientBrightnessPath, kAmbientBrightnessInterface,
          QDBusConnection::sessionBus(), this))
{
    m_displayInter->setParent(this);
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

    QDBusConnection::sessionBus().connect(
        kAmbientBrightnessService, kAmbientBrightnessPath,
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("PropertiesChanged"), this,
        SIGNAL(ambientBrightnessPropertiesChanged(QString,QVariantMap,QStringList)));

    QDBusConnection::sessionBus().connect(
        kDisplay, kDisplayPath, QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("PropertiesChanged"), this,
        SLOT(handleDisplayPropertiesChanged(QString,QVariantMap,QStringList)));

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

QString SessionDBusProxy::powerSavingModeBrightnessData() const
{
    return m_powerInter->property("PowerSavingModeBrightnessData").toString();
}

void SessionDBusProxy::setPowerSavingModeBrightnessData(const QString &value)
{
    m_powerInter->setProperty("PowerSavingModeBrightnessData", value);
}
void SessionDBusProxy::handleDisplayPropertiesChanged(const QString &interface,
                                                       const QVariantMap &changed,
                                                       const QStringList &)
{
    if (interface == QLatin1String(kDisplay)
        && changed.contains(QStringLiteral("Brightness")))
        Q_EMIT BrightnessChanged(
            qdbus_cast<QMap<QString, double>>(changed.value(QStringLiteral("Brightness"))));
}



void SessionDBusProxy::setShortIdleState(bool state)
{
    m_powerInter->asyncCall("SetShortIdleState", state);
}

void SessionDBusProxy::setIdleState(bool state)
{
    QDBusInterface daemon(kDaemonService, kDaemonPath, kDaemonService,
                          QDBusConnection::systemBus());
    daemon.asyncCall("SetIdleState", state);
}

void SessionDBusProxy::setScreenState(bool state)
{
    QDBusInterface daemon(kDaemonService, kDaemonPath, kDaemonService,
                          QDBusConnection::systemBus());
    daemon.asyncCall("SetScreenState", state);
}

void SessionDBusProxy::refreshBatteries()
{
    m_powerInter->asyncCall("RefreshBatteries");
}

void SessionDBusProxy::refreshMains()
{
    m_powerInter->asyncCall("RefreshMains");
}

bool SessionDBusProxy::sessionActive() const
{
    return m_sessionWatcherInter->property("IsActive").toBool();
}

bool SessionDBusProxy::sessionLocked() const
{
    return m_sessionManagerInter->property("Locked").toBool();
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

void SessionDBusProxy::requestHibernateByFront()
{
    m_shutdownFrontInter->asyncCall("Hibernate");
}

void SessionDBusProxy::requestShutdownByFront()
{
    m_shutdownFrontInter->asyncCall("Shutdown");
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

QDBusMessage SessionDBusProxy::inhibitors() const
{
    return m_login1Inter->call(QStringLiteral("ListInhibitors"));
}

QDBusMessage SessionDBusProxy::listSessions() const
{
    return m_login1Inter->call(QStringLiteral("ListSessions"));
}

void SessionDBusProxy::setBlackScreenActive(bool active)
{
    m_blackScreenInter->asyncCall("setActive", active);
    QDBusInterface kwin(QStringLiteral("org.kde.KWin"), QStringLiteral("/BlackScreen"),
                        QStringLiteral("org.kde.kwin.BlackScreen"),
                        QDBusConnection::sessionBus());
    kwin.asyncCall(QStringLiteral("setActive"), active);
}

QMap<QString, double> SessionDBusProxy::brightness() const
{
    QDBusInterface properties(kDisplay, kDisplayPath,
                              QStringLiteral("org.freedesktop.DBus.Properties"),
                              QDBusConnection::sessionBus());
    const QDBusReply<QDBusVariant> reply = properties.call(
        QStringLiteral("Get"), QString::fromLatin1(kDisplay),
        QStringLiteral("Brightness"));
    return reply.isValid()
        ? qdbus_cast<QMap<QString, double>>(reply.value().variant())
        : QMap<QString, double>();
}

void SessionDBusProxy::setBrightness(const QString &monitor, double value)
{
    m_displayInter->asyncCall("SetBrightness", monitor, value);
}

void SessionDBusProxy::setAndSaveBrightness(const QString &monitor, double value)
{
    m_displayInter->asyncCall("SetAndSaveBrightness", monitor, value);
}

void SessionDBusProxy::refreshBrightness()
{
    m_displayInter->asyncCall("RefreshBrightness");
}

bool SessionDBusProxy::ambientBrightnessSupported() const
{
    return m_ambientBrightnessInter->property("Supported").toBool();
}

bool SessionDBusProxy::ambientBrightnessEnabled() const
{
    return m_ambientBrightnessInter->property("Enabled").toBool();
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
