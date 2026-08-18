// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <QObject>

struct udev;
struct udev_monitor;
class QSocketNotifier;
class SystemPowerManager;

class BatteryManager : public QObject {
    Q_OBJECT
public:
    explicit BatteryManager(SystemPowerManager *mgr, QObject *parent = nullptr);
    ~BatteryManager() override;
    void probe();
    bool onBattery() const { return m_onBattery; }
    void refreshBatteries();
    void refreshMains();

Q_SIGNALS:
    void onBatteryChanged(bool onBattery);

private:
    void pollBattery();
    void initUdev();
    void onUdevEvent();
    void scheduleBatteryRefreshAfterAC();
    void syncDevices();

    SystemPowerManager *m_mgr = nullptr;
    bool m_hasBattery = false;
    bool m_onBattery = false;
    QList<class BatteryDevice *> m_batteries;

    struct udev *m_udev = nullptr;
    struct udev_monitor *m_udevMon = nullptr;
    QSocketNotifier *m_udevNotifier = nullptr;
};
