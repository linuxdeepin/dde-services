// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QDBusAbstractAdaptor>
#include <QTimer>
#include <DConfig>

class PowerManager;

class LowPowerManager : public QDBusAbstractAdaptor {
    // PowerManager owns adaptor initialization; keep those hooks out of the D-Bus API.
    friend class PowerManager;

    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.deepin.dde.Power1.WarnLevelConfig")
    Q_PROPERTY(bool UsePercentageForPolicy READ usePercentageForPolicy WRITE setUsePercentageForPolicy NOTIFY usePercentageForPolicyChanged)
    Q_PROPERTY(qint64 LowTime READ lowTime WRITE setLowTime NOTIFY lowTimeChanged)
    Q_PROPERTY(qint64 DangerTime READ dangerTime WRITE setDangerTime NOTIFY dangerTimeChanged)
    Q_PROPERTY(qint64 CriticalTime READ criticalTime WRITE setCriticalTime NOTIFY criticalTimeChanged)
    Q_PROPERTY(qint64 ActionTime READ actionTime WRITE setActionTime NOTIFY actionTimeChanged)
    Q_PROPERTY(qint64 LowPowerNotifyThreshold READ lowPowerNotifyThreshold WRITE setLowPowerNotifyThreshold NOTIFY lowPowerNotifyThresholdChanged)
    Q_PROPERTY(qint64 ActionPercentage READ actionPercentage WRITE setActionPercentage NOTIFY actionPercentageChanged)

public:
    explicit LowPowerManager(PowerManager *powerManager);
    enum Level { None = 0, Remind, Low, Danger, Critical, Action };

    void initConfig(Dtk::Core::DConfig *config);
    void applyConfigValue(const QString &key, const QVariant &value);

    bool usePercentageForPolicy() const { return m_usePercentageForPolicy; }
    void setUsePercentageForPolicy(bool value);
    qint64 lowTime() const { return static_cast<qint64>(m_timeToEmptyLow); }
    void setLowTime(qint64 value);
    qint64 dangerTime() const { return static_cast<qint64>(m_timeToEmptyDanger); }
    void setDangerTime(qint64 value);
    qint64 criticalTime() const { return static_cast<qint64>(m_timeToEmptyCritical); }
    void setCriticalTime(qint64 value);
    qint64 actionTime() const { return static_cast<qint64>(m_timeToEmptyAction); }
    void setActionTime(qint64 value);
    qint64 lowPowerNotifyThreshold() const { return m_lowPowerNotifyThreshold; }
    void setLowPowerNotifyThreshold(qint64 value);
    qint64 actionPercentage() const { return m_percentageAction; }
    void setActionPercentage(qint64 value);

public Q_SLOTS:
    void Reset();

Q_SIGNALS:
    void usePercentageForPolicyChanged();
    void lowTimeChanged();
    void dangerTimeChanged();
    void criticalTimeChanged();
    void actionTimeChanged();
    void lowPowerNotifyThresholdChanged();
    void actionPercentageChanged();

private:
    void updateWarnLevel();
    void handleLevelChanged(uint level);
    void disableTicker();
    uint getWarnLevel(double percentage, quint64 timeToEmpty);
    void startCountTicker();
    void sendNotify(const QString &body);
    void showLowPower();
    void closeLowPower();
    void lockWaitShow(int timeoutMs, bool autoStartAuth);
    void playBatterySound();
    void scheduleValidation();
    bool configValid() const;
    void notifyPropertyChanged(const char *name, const QVariant &value);

    QTimer *m_countTicker = nullptr;
    QTimer *m_validationTimer = nullptr;
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
