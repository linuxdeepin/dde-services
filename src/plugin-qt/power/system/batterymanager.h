// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <QObject>

struct udev;
struct udev_monitor;
class QSocketNotifier;
class QTimer;
class SystemPowerManager;

class BatteryManager : public QObject {
    Q_OBJECT
public:
    explicit BatteryManager(SystemPowerManager *mgr, QObject *parent = nullptr);
    ~BatteryManager() override;
    void probe();
    bool hasBattery() const { return m_hasBattery; }

Q_SIGNALS:
    void batteryChanged();
    void onBatteryChanged(bool onBattery);

private:
    void pollBattery();
    void initUdev();
    void onUdevEvent();
    void refreshACFromUdev(struct udev_device *dev);
    void refreshBatteryFromUdev(struct udev_device *dev);
    void scheduleBatteryRefreshAfterAC();

    SystemPowerManager *m_mgr = nullptr;
    bool m_hasBattery = false;
    bool m_onBattery = false;
    double m_percentage = 100.0;
    uint m_status = 0;
    quint64 m_timeToEmpty = 0;
    quint64 m_timeToFull = 0;
    double m_capacity = 100.0;

    struct udev *m_udev = nullptr;
    struct udev_monitor *m_udevMon = nullptr;
    QSocketNotifier *m_udevNotifier = nullptr;
    QTimer *m_batteryPollTimer = nullptr;
};
