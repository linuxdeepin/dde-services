// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QObject>
#include <QElapsedTimer>
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
    void ResetFromNow();
    void Update(int screenSaverStartDelay, int lockDelay,
                int screenBlackDelay, int sleepDelay, int shortIdleDelay,
                bool resetFromNow = false);
    void HandleIdleOn();
    void HandleIdleOff();
    void OnBattery();
    void OnLinePower();

    void onPowerSavingModeEnabledChanged(bool enabled);
    void onBrightnessDropPercentChanged(uint value);
    void syncPowerSavingMode(bool enabled, uint brightnessDropPercent);
    void setAllowScreenSaver(bool allow) { m_allowScreenSaver = allow; }

private:
    void startScreensaver();
    void stopScreensaver();
    void handleIdleOff();
    void screenBlack();
    void sleep();
    void setShortIdle(bool state);
    void interruptTasks();
    void setScreenSaverTimeout(int seconds);
    void saveCurrentBrightness();
    void resetBrightness();
    void applyBrightnessDrop();
    void restorePowerSavingBrightness();
    void scheduleTask(const MetaTask &t);
    void initializePowerSavingBrightness();
    void handleBrightnessChanged(const QMap<QString, double> &brightness);
    void loadPowerSavingBrightness(const QString &data);
    void savePowerSavingBrightness();

    struct BrightnessState {
        double saved = 0;
        double latest = 0;
        bool manuallyModified = false;
    };

    QVector<MetaTask> m_metaTasks;
    QVector<QTimer *> m_timers;
    QMap<QString, double> m_oldBrightness;
    QMap<QString, double> m_powerSavingBrightness;
    QMap<QString, BrightnessState> m_brightnessState;
    QMap<QString, double> m_lastBrightness;
    bool m_screensaverRunning = false;
    bool m_isIdle = false;
    bool m_allowScreenSaver = true;
    bool m_psmEnabled = false;
    uint m_psmDrop = 0;
    bool m_lockFired = false;
    QElapsedTimer m_psmEnabledGrace;
    QElapsedTimer m_psmPercentGrace;
    QElapsedTimer m_sessionActiveGrace;
    PowerManager *m_powerManager = nullptr;
};
