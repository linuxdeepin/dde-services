// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QObject>
#include <QVector>
#include <QTimer>
#include <QMap>
#include <functional>

class PowerManager;
class ScreenController;

class PowerSavePlan : public QObject {
    Q_OBJECT
public:
    struct MetaTask {
        int delay = 0;
        int realDelay = 0;
        QString name;
        std::function<void()> fn;
    };

    PowerSavePlan(PowerManager *powerManager, QObject *parent = nullptr);

    void Start();
    void Reset();
    void Update(int screenSaverStartDelay, int lockDelay,
                int screenBlackDelay, int sleepDelay);
    void HandleIdleOn();
    void HandleIdleOff();
    void OnBattery();
    void OnLinePower();

    void onPowerSavingModeEnabledChanged(bool enabled);
    void onBrightnessDropPercentChanged(uint value);

private:
    void startScreensaver();
    void stopScreensaver();
    void screenBlack();
    void interruptTasks();
    void setScreenSaverTimeout(int seconds);
    void saveCurrentBrightness();
    void resetBrightness();
    void applyBrightnessDrop();
    void scheduleTask(const MetaTask &t);

    QVector<MetaTask> m_metaTasks;
    QVector<QTimer *> m_timers;
    QMap<QString, double> m_oldBrightness;
    bool m_screensaverRunning = false;
    bool m_allowScreenSaver = true;
    bool m_psmEnabled = false;
    uint m_psmDrop = 0;
    bool m_lockFired = false;
    PowerManager *m_powerManager = nullptr;
};
