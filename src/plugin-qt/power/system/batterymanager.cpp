// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "batterymanager.h"
#include "powermanager.h"
#include "batterydevice.h"
#include "../powerconstants.h"

#include <QDBusInterface>
#include <QDBusConnection>
#include <QDBusReply>
#include <QTimer>
#include <QSocketNotifier>
#include <QFile>
#include <QDir>
#include <QLoggingCategory>
#include <algorithm>

#include <libudev.h>

Q_DECLARE_LOGGING_CATEGORY(logPowerSystem)

using namespace PowerDBus;

BatteryManager::BatteryManager(SystemPowerManager *mgr, QObject *parent)
    : QObject(parent)
    , m_mgr(mgr)
{
    probe();
    initUdev();

    auto *batteryPollTimer = new QTimer(this);
    batteryPollTimer->setInterval(60000);
    connect(batteryPollTimer, &QTimer::timeout, this, &BatteryManager::refreshBatteries);
    batteryPollTimer->start();
}

BatteryManager::~BatteryManager()
{
    if (m_udevMon) {
        udev_monitor_unref(m_udevMon);
        m_udevMon = nullptr;
    }
    if (m_udev) {
        udev_unref(m_udev);
        m_udev = nullptr;
    }
}

void BatteryManager::probe()
{
    syncDevices();
    refreshMains();
    pollBattery();
}

void BatteryManager::syncDevices()
{
    QDir supplies("/sys/class/power_supply");
    QStringList paths;
    for (const auto &entry : supplies.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString path = supplies.filePath(entry);
        QFile type(path + "/type");
        if (!type.open(QIODevice::ReadOnly)
            || QString::fromUtf8(type.readAll()).trimmed().compare(
                QLatin1String("Battery"), Qt::CaseInsensitive) != 0) {
            continue;
        }
        QFile scope(path + "/scope");
        if (scope.open(QIODevice::ReadOnly)
            && QString::fromUtf8(scope.readAll()).trimmed().compare(
                QLatin1String("device"), Qt::CaseInsensitive) == 0) {
            continue;
        }
        paths.append(path);
    }

    for (int i = m_batteries.size() - 1; i >= 0; --i) {
        auto *battery = m_batteries.at(i);
        if (paths.contains(battery->sysfsPath()))
            continue;
        m_batteries.removeAt(i);
        m_mgr->unregisterBattery(battery);
        battery->deleteLater();
    }
    for (const auto &path : paths) {
        const auto exists = std::any_of(m_batteries.cbegin(), m_batteries.cend(),
                                        [&path](const auto *battery) { return battery->sysfsPath() == path; });
        if (!exists) {
            auto *battery = new BatteryDevice(path, this);
            if (!battery->isPresent()) {
                delete battery;
                continue;
            }
            battery->setRefreshDoneCallback([this] { refreshBatteries(); });
            m_batteries.append(battery);
            m_mgr->registerBattery(battery);
        }
    }
}

void BatteryManager::refreshBatteries()
{
    syncDevices();
    for (int i = m_batteries.size() - 1; i >= 0; --i) {
        auto *battery = m_batteries.at(i);
        battery->refresh();
        if (battery->isPresent())
            continue;
        m_batteries.removeAt(i);
        m_mgr->unregisterBattery(battery);
        battery->deleteLater();
    }
    pollBattery();
}

void BatteryManager::refreshMains()
{
    QDir supplies("/sys/class/power_supply");
    bool found = false;
    bool online = false;
    for (const auto &entry : supplies.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString path = supplies.filePath(entry);
        QFile type(path + "/type");
        if (!type.open(QIODevice::ReadOnly)
            || QString::fromUtf8(type.readAll()).trimmed().compare(
                QLatin1String("Mains"), Qt::CaseInsensitive) != 0) {
            continue;
        }
        found = true;
        QFile state(path + "/online");
        online = state.open(QIODevice::ReadOnly) && state.readAll().trimmed() == "1";
        break;
    }
    const bool onBattery = found ? !online : m_hasBattery;
    if (m_onBattery != onBattery) {
        m_onBattery = onBattery;
        Q_EMIT onBatteryChanged(onBattery);
    }
}

void BatteryManager::pollBattery()
{
    m_hasBattery = !m_batteries.isEmpty();
    m_mgr->updateHasBattery(m_hasBattery);
    if (m_batteries.isEmpty()) {
        m_mgr->updateBatteryInfo(0, 0, 0, 0, 0);
        return;
    }

    double percentage = 0;
    uint status = 0;
    quint64 timeToEmpty = 0;
    quint64 timeToFull = 0;
    double capacity = 100;

    if (m_batteries.size() == 1) {
        const auto *battery = m_batteries.first();
        percentage = battery->percentage();
        status = battery->status();
        timeToEmpty = battery->timeToEmpty();
        timeToFull = battery->timeToFull();
        capacity = battery->capacity();
    } else {
        double energy = 0;
        double energyFull = 0;
        double energyFullDesign = 0;
        double energyRate = 0;
        QList<uint> states;
        for (auto *battery : std::as_const(m_batteries)) {
            energy += battery->energy();
            energyFull += battery->energyFull();
            energyFullDesign += battery->energyFullDesign();
            energyRate += battery->energyRate();
            states.append(battery->status());
        }

        percentage = energyFull > 0
            ? qBound(0.0, energy * 100.0 / energyFull, 100.0)
            : m_batteries.first()->percentage();
        const bool allSame = std::all_of(states.cbegin(), states.cend(),
                                         [first = states.first()](uint value) { return value == first; });
        if (allSame)
            status = states.first();
        else if (states.contains(2))
            status = 2;
        else if (states.contains(1))
            status = 1;
        if (status == 2 && energyRate > 0)
            timeToEmpty = static_cast<quint64>(3600.0 * energy / energyRate);
        else if (status == 1 && energyRate > 0)
            timeToFull = static_cast<quint64>(3600.0 * qMax(0.0, energyFull - energy) / energyRate);
        if (energyFullDesign > 0)
            capacity = qBound(0.0, energyFull * 100.0 / energyFullDesign, 100.0);
    }

    if (timeToEmpty > 240ULL * 60 * 60)
        timeToEmpty = 0;
    if (timeToFull > 20ULL * 60 * 60)
        timeToFull = 0;
    if (status == 0) {
        if (m_onBattery)
            status = 2;
        else if (percentage == 100)
            status = 4;
        else
            status = 3;
    }

    m_mgr->updateBatteryInfo(percentage, status, timeToEmpty, timeToFull, capacity);
    for (auto *battery : std::as_const(m_batteries))
        battery->setStatus(status);
}

// ── udev-based AC / battery monitoring ──────────────────────────

void BatteryManager::initUdev()
{
    m_udev = udev_new();
    if (!m_udev) {
        qWarning(logPowerSystem) << "udev_new failed";
        return;
    }

    m_udevMon = udev_monitor_new_from_netlink(m_udev, "udev");
    if (!m_udevMon) {
        qWarning(logPowerSystem) << "udev_monitor_new_from_netlink failed";
        return;
    }

    if (udev_monitor_filter_add_match_subsystem_devtype(m_udevMon, "power_supply", nullptr) < 0) {
        qWarning(logPowerSystem) << "udev_monitor_filter_add_match failed";
        return;
    }

    if (udev_monitor_enable_receiving(m_udevMon) < 0) {
        qWarning(logPowerSystem) << "udev_monitor_enable_receiving failed";
        return;
    }

    int fd = udev_monitor_get_fd(m_udevMon);
    if (fd < 0) {
        qWarning(logPowerSystem) << "udev_monitor_get_fd failed";
        return;
    }

    m_udevNotifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    connect(m_udevNotifier, &QSocketNotifier::activated, this, &BatteryManager::onUdevEvent);
}

void BatteryManager::onUdevEvent()
{
    if (!m_udevMon)
        return;

    auto *dev = udev_monitor_receive_device(m_udevMon);
    if (!dev)
        return;

    const char *actionValue = udev_device_get_action(dev);
    const char *typeValue = udev_device_get_sysattr_value(dev, "type");
    if (!actionValue || !typeValue) {
        udev_device_unref(dev);
        return;
    }
    const char *scopeValue = udev_device_get_sysattr_value(dev, "scope");
    const QString action = QString::fromUtf8(actionValue);
    const QString type = QString::fromUtf8(typeValue);
    const QString scope = scopeValue ? QString::fromUtf8(scopeValue) : QString();
    const bool systemBattery = type.compare(QLatin1String("Battery"), Qt::CaseInsensitive) == 0
        && scope.compare(QLatin1String("device"), Qt::CaseInsensitive) != 0;

    if (action == QLatin1String("change")) {
        if (type.compare(QLatin1String("Mains"), Qt::CaseInsensitive) == 0) {
            refreshMains();
            scheduleBatteryRefreshAfterAC();
        } else if (systemBattery) {
            refreshBatteries();
        }
    } else if (systemBattery) {
        probe();
    }

    udev_device_unref(dev);
}



// AC 变更后, 在 1s, 3s, 5s, 10s, 15s, ... 60s 递进刷新电池
void BatteryManager::scheduleBatteryRefreshAfterAC()
{
    static const int delays[] = {1000, 3000, 5000, 10000, 15000, 20000,
                                 25000, 30000, 35000, 40000, 45000, 50000,
                                 55000, 60000};
    for (int d : delays)
        QTimer::singleShot(d, this, &BatteryManager::refreshBatteries);
}
