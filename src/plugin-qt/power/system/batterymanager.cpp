// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "batterymanager.h"
#include "powermanager.h"
#include "../powerconstants.h"

#include <QDBusInterface>
#include <QDBusConnection>
#include <QDBusReply>
#include <QTimer>
#include <QSocketNotifier>
#include <QFile>
#include <QDir>
#include <QLoggingCategory>

#include <libudev.h>

Q_DECLARE_LOGGING_CATEGORY(logPowerSystem)

using namespace PowerDBus;

BatteryManager::BatteryManager(SystemPowerManager *mgr, QObject *parent)
    : QObject(parent)
    , m_mgr(mgr)
{
    probe();
    initUdev();

    m_batteryPollTimer = new QTimer(this);
    m_batteryPollTimer->setInterval(60000);
    connect(m_batteryPollTimer, &QTimer::timeout, this, &BatteryManager::pollBattery);
    m_batteryPollTimer->start();
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
    QDir ps("/sys/class/power_supply");
    for (const auto &entry : ps.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QFile tf("/sys/class/power_supply/" + entry + "/type");
        if (tf.open(QIODevice::ReadOnly)) {
            if (tf.readAll().trimmed() == "Battery") { m_hasBattery = true; break; }
        }
    }
    if (m_hasBattery) { m_mgr->updateHasBattery(true); pollBattery(); }
    // AC initial state will be picked up by udev on next event,
    // or by the fallback poll if no udev events arrive.
}

void BatteryManager::pollBattery()
{
    if (!m_hasBattery) {
        QDir ps("/sys/class/power_supply");
        for (const auto &entry : ps.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            QFile tf("/sys/class/power_supply/" + entry + "/type");
            if (tf.open(QIODevice::ReadOnly) && tf.readAll().trimmed() == "Battery")
                { m_hasBattery = true; m_mgr->updateHasBattery(true); break; }
        }
    }
    if (!m_hasBattery) return;

    double pct = 100.0; uint status = 0; quint64 tte = 0; double cap = 100.0;
    QDir ps("/sys/class/power_supply");
    for (const auto &entry : ps.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QFile tf("/sys/class/power_supply/" + entry + "/type");
        if (!tf.open(QIODevice::ReadOnly) || tf.readAll().trimmed() != "Battery") continue;
        auto readInt = [&](const QString &f) -> int {
            QFile ff("/sys/class/power_supply/" + entry + "/" + f);
            if (ff.open(QIODevice::ReadOnly)) return ff.readAll().trimmed().toInt();
            return 0;
        };
        QFile cf("/sys/class/power_supply/" + entry + "/capacity");
        if (cf.open(QIODevice::ReadOnly)) { pct = cf.readAll().trimmed().toInt(); cf.close(); }
        QFile sf("/sys/class/power_supply/" + entry + "/status");
        if (sf.open(QIODevice::ReadOnly)) {
            QString s = sf.readAll().trimmed(); sf.close();
            if (s == "Charging") status = 1;
            else if (s == "Discharging") { status = 2; int pw = readInt("power_now");
                tte = pw > 0 ? static_cast<quint64>(readInt("energy_now")) * 3600 / pw : 0; }
            else if (s == "Full") status = 4;
        }
        int ef = readInt("energy_full"), efd = readInt("energy_full_design");
        if (efd > 0) cap = ef * 100.0 / efd;
        break;
    }
    bool chg = (pct != m_percentage || status != m_status || tte != m_timeToEmpty || cap != m_capacity);
    m_percentage = pct; m_status = status; m_timeToEmpty = tte; m_capacity = cap;
    if (chg) { m_mgr->updateBatteryInfo(pct, status, tte, m_timeToFull, cap); Q_EMIT batteryChanged(); }
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
    if (!m_udevMon) return;

    auto *dev = udev_monitor_receive_device(m_udevMon);
    if (!dev) return;

    const char *action = udev_device_get_action(dev);
    const char *type = udev_device_get_sysattr_value(dev, "type");

    if (!action || !type) {
        udev_device_unref(dev);
        return;
    }

    if (strcmp(action, "change") == 0) {
        if (strcmp(type, "Mains") == 0 || strcmp(type, "USB") == 0) {
            refreshACFromUdev(dev);
            scheduleBatteryRefreshAfterAC();
        } else if (strcmp(type, "Battery") == 0) {
            refreshBatteryFromUdev(dev);
        }
    } else if (strcmp(action, "add") == 0) {
        if (strcmp(type, "Battery") == 0) {
            m_hasBattery = true;
            m_mgr->updateHasBattery(true);
            pollBattery();
        }
    } else if (strcmp(action, "remove") == 0) {
        if (strcmp(type, "Battery") == 0) {
            // re-probe to see if any batteries remain
            m_hasBattery = false;
            QDir ps("/sys/class/power_supply");
            for (const auto &entry : ps.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
                QFile tf("/sys/class/power_supply/" + entry + "/type");
                if (tf.open(QIODevice::ReadOnly) && tf.readAll().trimmed() == "Battery") {
                    m_hasBattery = true;
                    break;
                }
            }
            if (!m_hasBattery)
                m_mgr->updateHasBattery(false);
        }
    }

    udev_device_unref(dev);
}

void BatteryManager::refreshACFromUdev(struct udev_device *dev)
{
    const char *online = udev_device_get_sysattr_value(dev, "online");
    bool onBatt = !(online && strcmp(online, "1") == 0);
    if (onBatt != m_onBattery) {
        m_onBattery = onBatt;
        Q_EMIT onBatteryChanged(onBatt);
    }
}

void BatteryManager::refreshBatteryFromUdev(struct udev_device *)
{
    // udev notified us of a battery change; full poll picks up all values
    pollBattery();
}

// AC 变更后, 在 1s, 3s, 5s, 10s, 15s, ... 60s 递进刷新电池
void BatteryManager::scheduleBatteryRefreshAfterAC()
{
    static const int delays[] = {1000, 3000, 5000, 10000, 15000, 20000,
                                 25000, 30000, 35000, 40000, 45000, 50000,
                                 55000, 60000};
    for (int d : delays)
        QTimer::singleShot(d, this, &BatteryManager::pollBattery);
}
