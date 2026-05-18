// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QObject>
#include <QDBusUnixFileDescriptor>
#include <DDBusInterface>

using Dtk::Core::DDBusInterface;

class SessionDBusProxy : public QObject {
    Q_OBJECT

public:
    explicit SessionDBusProxy(QObject *parent = nullptr);

    // ── Power (system bus) — DDBusInterface 自动转发 PropertiesChanged ──
    Q_PROPERTY(bool OnBattery READ onBattery NOTIFY OnBatteryChanged)
    bool onBattery() const;

    Q_PROPERTY(bool HasLidSwitch READ hasLidSwitch NOTIFY HasLidSwitchChanged)
    bool hasLidSwitch() const;

    Q_PROPERTY(bool HasBattery READ hasBattery NOTIFY HasBatteryChanged)
    bool hasBattery() const;

    Q_PROPERTY(double batteryPercentage READ batteryPercentage NOTIFY BatteryPercentageChanged)
    double batteryPercentage() const;

    Q_PROPERTY(uint batteryStatus READ batteryStatus NOTIFY BatteryStatusChanged)
    uint batteryStatus() const;

    Q_PROPERTY(quint64 batteryTimeToEmpty READ batteryTimeToEmpty NOTIFY BatteryTimeToEmptyChanged)
    quint64 batteryTimeToEmpty() const;

    Q_PROPERTY(bool IsHighPerformanceSupported READ isHighPerformanceSupported NOTIFY IsHighPerformanceSupportedChanged)
    bool isHighPerformanceSupported() const;

    Q_PROPERTY(bool PowerSavingModeEnabled READ powerSavingModeEnabled NOTIFY PowerSavingModeEnabledChanged)
    bool powerSavingModeEnabled() const;

    Q_PROPERTY(uint PowerSavingModeBrightnessDropPercent READ powerSavingModeBrightnessDropPercent NOTIFY PowerSavingModeBrightnessDropPercentChanged)
    uint powerSavingModeBrightnessDropPercent() const;

    // ── SessionWatcher ──
    Q_PROPERTY(bool SessionActive READ sessionActive NOTIFY SessionActiveChanged)
    bool sessionActive() const;

    // ── SessionManager ──
    void requestSuspend();
    void requestShutdown();
    void requestHibernate();
    bool canSuspend();
    bool canHibernate();

    // ── ShutdownFront ──
    void requestSuspendByFront();

    // ── LockFront ──
    void showLockAuth(bool autoStart);

    // ── Login1 ──
    void lockSession(const QString &sessionId);
    QDBusUnixFileDescriptor inhibit(const QString &what, const QString &who,
                                    const QString &why, const QString &mode);

    // ── BlackScreen ──
    void setBlackScreenActive(bool active);

    // ── Display ──
    void setBrightness(const QString &monitor, double value);
    void setAndSaveBrightness(const QString &monitor, double value);

    // ── Notifications ──
    uint notify(uint replaceId, const QString &appName, const QString &icon,
                const QString &title, const QString &body,
                const QStringList &actions, const QVariantMap &hints, int timeout);
    void closeNotification(uint id);

    // ── Calendar ──
    QString getFestivalMonth(int year, int month);

signals:
    // DDBusInterface 自动转发：属性名 + Changed
    void OnBatteryChanged(bool value);
    void HasLidSwitchChanged(bool value);
    void HasBatteryChanged(bool value);
    void BatteryPercentageChanged(double value);
    void BatteryStatusChanged(uint value);
    void BatteryTimeToEmptyChanged(quint64 value);
    void IsHighPerformanceSupportedChanged(bool value);
    void PowerSavingModeEnabledChanged(bool value);
    void PowerSavingModeBrightnessDropPercentChanged(uint value);
    void SessionActiveChanged(bool value);

    void notifyActionInvoked(uint id, const QString &actionKey);
    void timeUpdate();
    void login1OwnerChanged(const QString &name, const QString &oldOwner, const QString &newOwner);

private:
    DDBusInterface *m_powerInter;
    DDBusInterface *m_sessionManagerInter;
    DDBusInterface *m_shutdownFrontInter;
    DDBusInterface *m_login1Inter;
    DDBusInterface *m_lockFrontInter;
    DDBusInterface *m_blackScreenInter;
    DDBusInterface *m_displayInter;
    DDBusInterface *m_notificationsInter;
    DDBusInterface *m_sessionWatcherInter;
    DDBusInterface *m_calendarInter;
    DDBusInterface *m_timedateInter;
    DDBusInterface *m_freedesktopDBusInter;
};
