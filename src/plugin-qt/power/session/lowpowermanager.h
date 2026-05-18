// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QObject>
#include <QTimer>
#include <DConfig>

class PowerManager;

class LowPowerManager : public QObject {
    Q_OBJECT
public:
    LowPowerManager(PowerManager *powerManager, QObject *parent = nullptr);
    enum Level { None = 0, Remind, Low, Danger, Critical, Action };

    void initConfig(Dtk::Core::DConfig *config);

private Q_SLOTS:
    void updateWarnLevel();
    void onConfigChanged(const QString &key);

private:
    void handleLevelChanged(uint level);
    void disableTicker();
    uint getWarnLevel(double percentage, quint64 timeToEmpty);
    void startCountTicker();
    void sendNotify(const QString &body);
    void showLowPower();
    void closeLowPower();
    void lockWaitShow(int timeoutMs, bool autoStartAuth);
    void playBatterySound();

    QTimer *m_countTicker = nullptr;
    int m_count = 0;
    uint m_currentLevel = 0;

    Dtk::Core::DConfig *m_config = nullptr;
    bool m_usePercentageForPolicy = true;
    quint64 m_timeToEmptyLow = 0;
    quint64 m_timeToEmptyDanger = 0;
    quint64 m_timeToEmptyCritical = 0;
    quint64 m_timeToEmptyAction = 0;
    int m_percentageAction = 0;
    int m_lowPowerNotifyThreshold = 0;
    PowerManager *m_powerManager = nullptr;
};
