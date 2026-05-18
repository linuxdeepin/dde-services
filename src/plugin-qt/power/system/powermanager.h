// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <QObject>
#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QString>
#include <DConfig>

class SystemPowerManager : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.deepin.dde.Power1")

    Q_PROPERTY(bool OnBattery READ onBattery NOTIFY onBatteryChanged)
    Q_PROPERTY(bool HasBattery READ hasBattery NOTIFY hasBatteryChanged)
    Q_PROPERTY(bool HasLidSwitch READ hasLidSwitch NOTIFY hasLidSwitchChanged)
    Q_PROPERTY(bool LidClosed READ lidClosed NOTIFY lidClosedChanged)
    Q_PROPERTY(double BatteryPercentage READ batteryPercentage NOTIFY batteryPercentageChanged)
    Q_PROPERTY(uint BatteryStatus READ batteryStatus NOTIFY batteryStatusChanged)
    Q_PROPERTY(quint64 BatteryTimeToEmpty READ batteryTimeToEmpty NOTIFY batteryTimeToEmptyChanged)
    Q_PROPERTY(quint64 BatteryTimeToFull READ batteryTimeToFull NOTIFY batteryTimeToFullChanged)
    Q_PROPERTY(double BatteryCapacity READ batteryCapacity NOTIFY batteryCapacityChanged)
    Q_PROPERTY(bool PowerSavingModeEnabled READ powerSavingModeEnabled
               WRITE setPowerSavingModeEnabled NOTIFY powerSavingModeEnabledChanged)
    Q_PROPERTY(bool PowerSavingModeAuto READ powerSavingModeAuto
               WRITE setPowerSavingModeAuto NOTIFY powerSavingModeAutoChanged)
    Q_PROPERTY(bool PowerSavingModeAutoWhenBatteryLow READ powerSavingModeAutoWhenBatteryLow
               WRITE setPowerSavingModeAutoWhenBatteryLow NOTIFY powerSavingModeAutoWhenBatteryLowChanged)
    Q_PROPERTY(uint PowerSavingModeBrightnessDropPercent READ powerSavingModeBrightnessDropPercent
               WRITE setPowerSavingModeBrightnessDropPercent NOTIFY powerSavingModeBrightnessDropPercentChanged)
    Q_PROPERTY(uint PowerSavingModeAutoBatteryPercent READ powerSavingModeAutoBatteryPercent
               WRITE setPowerSavingModeAutoBatteryPercent NOTIFY powerSavingModeAutoBatteryPercentChanged)
    Q_PROPERTY(QString Mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(bool IsHighPerformanceSupported READ isHighPerformanceSupported NOTIFY isHighPerformanceSupportedChanged)
    Q_PROPERTY(bool IsPowerSaveSupported READ isPowerSaveSupported NOTIFY isPowerSaveSupportedChanged)
    Q_PROPERTY(QString CpuGovernor READ cpuGovernor WRITE setCpuGovernor NOTIFY cpuGovernorChanged)
    Q_PROPERTY(bool CpuBoost READ cpuBoost WRITE setCpuBoost NOTIFY cpuBoostChanged)

public:
    explicit SystemPowerManager(QDBusConnection *conn, const QString &svc, QObject *p = nullptr);
    bool initialize();

    void updateHasBattery(bool has);
    void updateBatteryInfo(double pct, uint status, quint64 tte, quint64 ttf, double cap);
    void initLidSwitch();
    void initPowerSavingDConfig();
    void initCpuGovernor();

private Q_SLOTS:
    void onUPowerPropertiesChanged(const QString &interface,
                                   const QVariantMap &changed,
                                   const QStringList &invalidated);

    bool onBattery() const { return m_onBattery; }
    bool hasBattery() const { return m_hasBattery; }
    bool hasLidSwitch() const { return m_hasLidSwitch; }
    bool lidClosed() const { return m_lidClosed; }
    double batteryPercentage() const { return m_batteryPercentage; }
    uint batteryStatus() const { return m_batteryStatus; }
    quint64 batteryTimeToEmpty() const { return m_batteryTimeToEmpty; }
    quint64 batteryTimeToFull() const { return m_batteryTimeToFull; }
    double batteryCapacity() const { return m_batteryCapacity; }
    bool powerSavingModeEnabled() const { return m_psmEnabled; }
    void setPowerSavingModeEnabled(bool v) { if (m_psmEnabled != v) { m_psmEnabled = v; Q_EMIT powerSavingModeEnabledChanged(); } }
    bool powerSavingModeAuto() const { return m_psmAuto; }
    void setPowerSavingModeAuto(bool v) { if (m_psmAuto != v) { m_psmAuto = v; Q_EMIT powerSavingModeAutoChanged(); } }
    bool powerSavingModeAutoWhenBatteryLow() const { return m_psmAutoLow; }
    void setPowerSavingModeAutoWhenBatteryLow(bool v) { if (m_psmAutoLow != v) { m_psmAutoLow = v; Q_EMIT powerSavingModeAutoWhenBatteryLowChanged(); } }
    uint powerSavingModeBrightnessDropPercent() const { return m_psmDrop; }
    void setPowerSavingModeBrightnessDropPercent(uint v) { if (m_psmDrop != v) { m_psmDrop = v; Q_EMIT powerSavingModeBrightnessDropPercentChanged(); } }
    uint powerSavingModeAutoBatteryPercent() const { return m_psmAutoPct; }
    void setPowerSavingModeAutoBatteryPercent(uint v) { if (m_psmAutoPct != v) { m_psmAutoPct = v; Q_EMIT powerSavingModeAutoBatteryPercentChanged(); } }
    QString mode() const { return m_mode; }
    void setMode(const QString &v);
    QString lastMode() const { return m_lastMode; }
    bool batteryLow() const { return m_batteryLow; }
    bool isHighPerformanceSupported() const { return m_hpSupported; }
    bool isPowerSaveSupported() const { return m_psSupported; }
    QString cpuGovernor() const { return m_governor; }
    void setCpuGovernor(const QString &v) { if (m_governor != v) { m_governor = v; Q_EMIT cpuGovernorChanged(); } }
    bool cpuBoost() const { return m_boost; }
    void setCpuBoost(bool v) { if (m_boost != v) { m_boost = v; Q_EMIT cpuBoostChanged(); } }

public Q_SLOTS:
    QList<QDBusObjectPath> GetBatteries();
    void Refresh();
    void RefreshBatteries();
    void RefreshMains();
    void SetCpuGovernor(const QString &gov);
    void SetCpuBoost(bool on);
    void LockCpuFreq(const QString &gov, int lockTime);

Q_SIGNALS:
    void onBatteryChanged();
    void hasBatteryChanged();
    void hasLidSwitchChanged();
    void lidClosedChanged();
    void LidClosed();
    void LidOpened();
    void batteryPercentageChanged();
    void batteryStatusChanged();
    void batteryTimeToEmptyChanged();
    void batteryTimeToFullChanged();
    void batteryCapacityChanged();
    void powerSavingModeEnabledChanged();
    void powerSavingModeAutoChanged();
    void powerSavingModeAutoWhenBatteryLowChanged();
    void powerSavingModeBrightnessDropPercentChanged();
    void powerSavingModeAutoBatteryPercentChanged();
    void modeChanged();
    void isHighPerformanceSupportedChanged();
    void isPowerSaveSupportedChanged();
    void cpuGovernorChanged();
    void cpuBoostChanged();

private:
    void handleLidSwitchEvent(bool closed);
    void updatePowerMode(bool init = false);
    void recalcBatteryLow();

    QDBusConnection *m_conn;

    bool m_onBattery = false;
    bool m_hasBattery = false;
    bool m_hasLidSwitch = false;
    bool m_lidClosed = false;
    double m_batteryPercentage = 100.0;
    uint m_batteryStatus = 0;
    quint64 m_batteryTimeToEmpty = 0;
    quint64 m_batteryTimeToFull = 0;
    double m_batteryCapacity = 100.0;
    bool m_psmEnabled = false;
    bool m_psmAuto = false;
    bool m_psmAutoLow = false;
    uint m_psmDrop = 0;
    uint m_psmAutoPct = 20;
    QString m_mode = "balance";
    QString m_lastMode = "balance";
    bool m_batteryLow = false;
    QString m_governor = "powersave";
    bool m_boost = true;
    bool m_hpSupported = true;
    bool m_psSupported = true;
    Dtk::Core::DConfig *m_config = nullptr;
};
