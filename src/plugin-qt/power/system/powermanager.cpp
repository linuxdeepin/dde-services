// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "powermanager.h"
#include "batterymanager.h"
#include "systemdbusproxy.h"
#include "../powerconstants.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QVariantMap>
#include <QProcess>
#include <QFile>
#include <DConfig>
#include <QLoggingCategory>

using namespace PowerDBus;
using namespace PowerFS;
using namespace PowerDConfig;

Q_LOGGING_CATEGORY(logPowerSystem, "dde.power.system")

SystemPowerManager::SystemPowerManager(QDBusConnection *conn, const QString &svc,
                                       QObject *p)
    : QObject(p)
    , m_conn(conn)
{
    Q_UNUSED(svc);
}

bool SystemPowerManager::initialize()
{
    bool ok = m_conn->registerObject(kPath, this,
        QDBusConnection::ExportAllSlots |
        QDBusConnection::ExportAllSignals |
        QDBusConnection::ExportAllProperties);
    if (!ok) {
        qWarning(logPowerSystem) << "registerObject failed";
        return false;
    }

    initLidSwitch();
    initPowerSavingDConfig();
    initCpuGovernor();

    auto *battery = new BatteryManager(this, this);
    connect(battery, &BatteryManager::onBatteryChanged, this, [this](bool onBatt) {
        qDebug(logPowerSystem) << "onBatteryChanged:" << onBatt << " prev=" << m_onBattery;
        if (m_onBattery != onBatt) {
            m_onBattery = onBatt;
            Q_EMIT onBatteryChanged();
            recalcBatteryLow();
            updatePowerMode(false);
        }
    });
    connect(battery, &BatteryManager::batteryChanged, this, [battery]() {
        battery->probe();
    });

    return true;
}

void SystemPowerManager::initLidSwitch()
{
    SystemDBusProxy proxy(this);
    QString chassis = proxy.chassis();
    if (chassis != "laptop" && chassis != "convertible")
        return;

    m_hasLidSwitch = proxy.lidIsPresent();
    if (!m_hasLidSwitch) {
        // 后备: 尝试 /proc/acpi/button/lid/LID/state
        QFile f(kLidStatePath);
        if (f.exists()) {
            m_hasLidSwitch = true;
        }
    }

    if (!m_hasLidSwitch)
        return;

    Q_EMIT hasLidSwitchChanged();

    // 读取初始状态
    QDBusInterface upower(kUPowerService, kUPowerPath, "org.freedesktop.DBus.Properties",
                          QDBusConnection::systemBus());
    if (upower.isValid()) {
        QDBusReply<QVariant> reply = upower.call("Get", kUPowerService, "LidIsClosed");
        if (reply.isValid()) {
            bool closed = reply.value().toBool();
            m_lidClosed = closed;
            Q_EMIT lidClosedChanged();
            handleLidSwitchEvent(closed);
        }
    }

    // 持续监听 UPower 的 PropertiesChanged 信号，确保每次合盖/开盖都能收到通知
    QDBusConnection::systemBus().connect(
        kUPowerService, kUPowerPath,
        "org.freedesktop.DBus.Properties", "PropertiesChanged",
        this, SLOT(onUPowerPropertiesChanged(QString,QVariantMap,QStringList)));
}

void SystemPowerManager::onUPowerPropertiesChanged(const QString &interface,
                                                    const QVariantMap &changed,
                                                    const QStringList &)
{
    if (interface != QLatin1String(kUPowerService))
        return;

    if (changed.contains("LidIsClosed")) {
        bool closed = changed.value("LidIsClosed").toBool();
        if (m_lidClosed != closed) {
            m_lidClosed = closed;
            Q_EMIT lidClosedChanged();
        }
        handleLidSwitchEvent(closed);
    }
}

void SystemPowerManager::handleLidSwitchEvent(bool closed)
{
    qDebug(logPowerSystem) << "handleLidSwitchEvent: closed=" << closed;
    if (closed) {
        Q_EMIT LidClosed();
    } else {
        Q_EMIT LidOpened();
    }
}

void SystemPowerManager::initPowerSavingDConfig()
{
    m_config = Dtk::Core::DConfig::create(kAppId, kPowerName, "", this);
    if (!m_config) return;

    auto load = [this](const QString &k) {
        QVariant v = m_config->value(k);
        qDebug(logPowerSystem) << "DConfig load: key=" << k << " value=" << v;
        if (k == QLatin1String(kPowerSavingModeEnabled))
            setPowerSavingModeEnabled(v.toBool());
        else if (k == QLatin1String(kPowerSavingModeAuto))
            setPowerSavingModeAuto(v.toBool());
        else if (k == QLatin1String(kPowerSavingModeAutoWhenBatteryLow))
            setPowerSavingModeAutoWhenBatteryLow(v.toBool());
        else if (k == QLatin1String(kPowerSavingModeBrightnessDropPercent))
            setPowerSavingModeBrightnessDropPercent(v.toUInt());
        else if (k == QLatin1String(kPowerSavingModeAutoBatteryPercent))
            setPowerSavingModeAutoBatteryPercent(v.toUInt());
        else if (k == QLatin1String(kMode)) {
            QString m = v.toString();
            static const QStringList valid = {"balance", "powersave", "performance"};
            if (valid.contains(m) && !m.isEmpty())
                setMode(m);
        }
        else if (k == QLatin1String(kLastMode)) {
            QString lm = v.toString();
            if (!lm.isEmpty()) m_lastMode = lm;
        }
    };

    load(kPowerSavingModeEnabled);
    load(kMode);
    load(kPowerSavingModeAuto);
    load(kPowerSavingModeAutoWhenBatteryLow);
    load(kPowerSavingModeBrightnessDropPercent);
    load(kPowerSavingModeAutoBatteryPercent);
    load(kLastMode);

    updatePowerMode(true);

    connect(m_config, &Dtk::Core::DConfig::valueChanged, this,
            [this, load](const QString &key) {
        qDebug(logPowerSystem) << "DConfig valueChanged: key=" << key;
        load(key);
        if (key == QLatin1String(kMode)) {
            QString newMode = m_config->value(key).toString();
            setMode(newMode);
            return;
        }
        if (key == QLatin1String(kPowerSavingModeAutoBatteryPercent)) {
            recalcBatteryLow();
        }
        if (key == QLatin1String(kPowerSavingModeAutoWhenBatteryLow)
            || key == QLatin1String(kPowerSavingModeAutoBatteryPercent)) {
            recalcBatteryLow();
            updatePowerMode(false);
        }
    });
}

void SystemPowerManager::initCpuGovernor()
{
    QFile gf("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor");
    if (gf.open(QIODevice::ReadOnly)) {
        QString gov = gf.readAll().trimmed();
        gf.close();
        if (!gov.isEmpty())
            setCpuGovernor(gov);
    }

    QFile af("/sys/devices/system/cpu/cpu0/cpufreq/scaling_available_governors");
    if (af.open(QIODevice::ReadOnly)) {
        QStringList avail = QString(af.readAll()).split(' ', Qt::SkipEmptyParts);
        af.close();
        bool hp = avail.contains("performance") || avail.contains("ondemand");
        bool ps = avail.contains("powersave") || avail.contains("ondemand");
        if (m_hpSupported != hp) {
            m_hpSupported = hp;
            Q_EMIT isHighPerformanceSupportedChanged();
        }

        if (m_psSupported != ps) {
            m_psSupported = ps;
            Q_EMIT isPowerSaveSupportedChanged();
        }
    }

    QFile bf("/sys/devices/system/cpu/cpufreq/boost");
    if (bf.open(QIODevice::ReadOnly)) {
        setCpuBoost(bf.readAll().trimmed() == "1");
        bf.close();
    }
}

void SystemPowerManager::updateHasBattery(bool has)
{
    if (m_hasBattery != has) {
        m_hasBattery = has;
        Q_EMIT hasBatteryChanged();
    }
}

void SystemPowerManager::updateBatteryInfo(double pct, uint status,
                                            quint64 tte, quint64 ttf, double cap)
{
    if (m_batteryPercentage != pct) { 
        qWarning(logPowerSystem) << "batteryPercentage changed:" << m_batteryPercentage << "→" << pct;
        m_batteryPercentage = pct;
        Q_EMIT batteryPercentageChanged();
        recalcBatteryLow();
        updatePowerMode(false);
    }

    if (m_batteryStatus != status) {
        m_batteryStatus = status;
        Q_EMIT batteryStatusChanged();
    }

    if (m_batteryTimeToEmpty != tte)
    {
        m_batteryTimeToEmpty = tte;
        Q_EMIT batteryTimeToEmptyChanged();
    }

    if (m_batteryTimeToFull != ttf) { 
        m_batteryTimeToFull = ttf; 
        Q_EMIT batteryTimeToFullChanged(); 
    }
    if (m_batteryCapacity != cap) { 
        m_batteryCapacity = cap; 
        Q_EMIT batteryCapacityChanged(); 
    }
}

QList<QDBusObjectPath> SystemPowerManager::GetBatteries() {
    return {};
}

void SystemPowerManager::Refresh() 
{
    RefreshBatteries();
    RefreshMains();
}

void SystemPowerManager::RefreshBatteries()
{

}

void SystemPowerManager::RefreshMains()
{

}

void SystemPowerManager::setMode(const QString &v)
{
    static const QStringList valid = {"balance", "powersave", "performance"};
    if (!valid.contains(v)) { 
        qWarning(logPowerSystem) << "invalid mode: " << v;
        return;
    }

    if (m_mode == v) {
        qWarning(logPowerSystem) << "setMode: same mode " << v << ", skip";
        return;
    }
    m_mode = v;
    Q_EMIT modeChanged();

    QString dspc;
    if (v == "performance") {
        dspc = "performance";
    } else if (v == "powersave") {
        dspc = "saving";
    } else {
        dspc = "balance";
    }

    if (!QProcess::startDetached("/usr/sbin/deepin-power-control", {"set", dspc}))
        qWarning(logPowerSystem) << "Failed to start deepin-power-control set" << dspc;

    setPowerSavingModeEnabled(v == "powersave");

    if (m_lastMode != v && v != "powersave") {
        m_lastMode = v;
        if (m_config) m_config->setValue(kLastMode, v);
    }
}

void SystemPowerManager::SetCpuGovernor(const QString &gov)
{
    setCpuGovernor(gov);
}

void SystemPowerManager::SetCpuBoost(bool on)
{
    setCpuBoost(on);
}

void SystemPowerManager::LockCpuFreq(const QString &gov, int lockTime)
{
    Q_UNUSED(gov);
    Q_UNUSED(lockTime);
}

void SystemPowerManager::recalcBatteryLow()
{
    bool old = m_batteryLow;
    m_batteryLow = m_onBattery && m_batteryPercentage > 0
                   && m_batteryPercentage <= static_cast<double>(m_psmAutoPct);
    qDebug(logPowerSystem) << "recalcBatteryLow:" << old << "→" << m_batteryLow
                             << "(OnBattery=" << m_onBattery
                             << " pct=" << m_batteryPercentage
                             << " threshold=" << m_psmAutoPct << ")";
}

void SystemPowerManager::updatePowerMode(bool init)
{
    bool enablePowerSave = m_psmAuto && m_onBattery;
    bool enableLowPower = m_psmAutoLow && m_batteryLow;

    qDebug(logPowerSystem) << "updatePowerMode: init=" << init
                             << " PSMAuto=" << m_psmAuto
                             << " OnBattery=" << m_onBattery
                             << " PSMAutoLow=" << m_psmAutoLow
                             << " BatteryLow=" << m_batteryLow
                             << " currentMode=" << m_mode
                             << " lastMode=" << m_lastMode;

    if (!m_psmAuto && !m_psmAutoLow && !init) {
        qDebug(logPowerSystem) << "  → both auto off, restoring lastMode:" << m_lastMode;
        setMode(m_lastMode);
        return;
    }

    QString target = init ? m_mode : m_lastMode;
    if (enablePowerSave || enableLowPower)
        target = "powersave";
    qDebug(logPowerSystem) << "  → setMode(" << target << ")";
    setMode(target);
}
