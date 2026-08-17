// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "lowpowermanager.h"

#include <QVariantMap>
#include <QByteArray>
#include <QSet>
#include <DConfig>

using BatteryPercentageMap = QMap<QString, double>;
using BatteryStateMap = QMap<QString, quint32>;
using BatteryIsPresentMap = QMap<QString, bool>;

Q_DECLARE_METATYPE(BatteryPercentageMap)
Q_DECLARE_METATYPE(BatteryStateMap)
Q_DECLARE_METATYPE(BatteryIsPresentMap)

class QDBusConnection;
class QTimer;
class IdleWatcher;
class ScreenController;
class PowerHelper;
class PowerSavePlan;
class LidSwitchHandler;
class SleepInhibitor;
class SessionDBusProxy;

enum SchedState {
    SchedInit = 0,
    SchedWaitingToNotify = 1,
    SchedCountdowning = 2,
    SchedShutdown = 3,
    SchedCancel = 4,
    SchedTimeout = 5,
};

enum RepetitionMode {
    RepOnce = 0,
    RepEveryday = 1,
    RepWorkdays = 2,
    RepCustom = 3,
};

enum PrepareSuspendState {
    PS_Unknown     = 1,
    PS_LidOpen     = 2,
    PS_LidClose    = 3,
    PS_Finish      = 4,
    PS_Resume      = 5,
    PS_Prepare     = 6,
    PS_ButtonClick = 7,
};

enum PowerAction {
    PA_Shutdown     = 0, // 关机
    PA_Suspend      = 1, // 休眠
    PA_Hibernate    = 2, // 休眠
    PA_TurnOffScreen = 3, // 关闭显示器
    PA_ShowShutdownInterface = 4, // 显示关机界面
    PA_DoNothing    = 5, // 无操作
};

class PowerManager : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.deepin.dde.Power1")

    Q_PROPERTY(bool OnBattery READ onBattery NOTIFY onBatteryChanged)
    Q_PROPERTY(bool UseWayland READ useWayland CONSTANT)
    Q_PROPERTY(bool LidIsPresent READ lidIsPresent NOTIFY lidIsPresentChanged)
    Q_PROPERTY(BatteryIsPresentMap BatteryIsPresent READ batteryIsPresent NOTIFY batteryIsPresentChanged)
    Q_PROPERTY(BatteryPercentageMap BatteryPercentage READ batteryPercentage NOTIFY batteryPercentageChanged)
    Q_PROPERTY(BatteryStateMap BatteryState READ batteryState NOTIFY batteryStateChanged)
    Q_PROPERTY(bool HasAmbientLightSensor READ hasAmbientLightSensor NOTIFY hasAmbientLightSensorChanged)
    Q_PROPERTY(uint WarnLevel READ warnLevel NOTIFY warnLevelChanged)
    Q_PROPERTY(bool IsHighPerformanceSupported READ isHighPerformanceSupported NOTIFY isHighPerformanceSupportedChanged)

    Q_PROPERTY(int LinePowerScreensaverDelay READ linePowerScreensaverDelay WRITE setLinePowerScreensaverDelay NOTIFY linePowerScreensaverDelayChanged)
    Q_PROPERTY(int LinePowerScreenBlackDelay READ linePowerScreenBlackDelay WRITE setLinePowerScreenBlackDelay NOTIFY linePowerScreenBlackDelayChanged)
    Q_PROPERTY(int LinePowerSleepDelay READ linePowerSleepDelay WRITE setLinePowerSleepDelay NOTIFY linePowerSleepDelayChanged)
    Q_PROPERTY(int LinePowerLockDelay READ linePowerLockDelay WRITE setLinePowerLockDelay NOTIFY linePowerLockDelayChanged)
    Q_PROPERTY(int LinePowerShortIdleDelay READ linePowerShortIdleDelay WRITE setLinePowerShortIdleDelay NOTIFY linePowerShortIdleDelayChanged)

    Q_PROPERTY(int BatteryScreensaverDelay READ batteryScreensaverDelay WRITE setBatteryScreensaverDelay NOTIFY batteryScreensaverDelayChanged)
    Q_PROPERTY(int BatteryScreenBlackDelay READ batteryScreenBlackDelay WRITE setBatteryScreenBlackDelay NOTIFY batteryScreenBlackDelayChanged)
    Q_PROPERTY(int BatterySleepDelay READ batterySleepDelay WRITE setBatterySleepDelay NOTIFY batterySleepDelayChanged)
    Q_PROPERTY(int BatteryLockDelay READ batteryLockDelay WRITE setBatteryLockDelay NOTIFY batteryLockDelayChanged)
    Q_PROPERTY(int BatteryShortIdleDelay READ batteryShortIdleDelay WRITE setBatteryShortIdleDelay NOTIFY batteryShortIdleDelayChanged)

    Q_PROPERTY(bool ScreenBlackLock READ screenBlackLock WRITE setScreenBlackLock NOTIFY screenBlackLockChanged)
    Q_PROPERTY(bool SleepLock READ sleepLock WRITE setSleepLock NOTIFY sleepLockChanged)

    Q_PROPERTY(int LinePowerLidClosedAction READ linePowerLidClosedAction WRITE setLinePowerLidClosedAction NOTIFY linePowerLidClosedActionChanged)
    Q_PROPERTY(int BatteryLidClosedAction READ batteryLidClosedAction WRITE setBatteryLidClosedAction NOTIFY batteryLidClosedActionChanged)
    Q_PROPERTY(int LinePowerPressPowerBtnAction READ linePowerPressPowerBtnAction WRITE setLinePowerPressPowerBtnAction NOTIFY linePowerPressPowerBtnActionChanged)
    Q_PROPERTY(int BatteryPressPowerBtnAction READ batteryPressPowerBtnAction WRITE setBatteryPressPowerBtnAction NOTIFY batteryPressPowerBtnActionChanged)

    Q_PROPERTY(bool LowPowerNotifyEnable READ lowPowerNotifyEnable WRITE setLowPowerNotifyEnable NOTIFY lowPowerNotifyEnableChanged)
    Q_PROPERTY(int LowPowerNotifyThreshold READ lowPowerNotifyThreshold WRITE setLowPowerNotifyThreshold NOTIFY lowPowerNotifyThresholdChanged)
    Q_PROPERTY(int LowPowerAutoSleepThreshold READ lowPowerAutoSleepThreshold WRITE setLowPowerAutoSleepThreshold NOTIFY lowPowerAutoSleepThresholdChanged)
    Q_PROPERTY(int LowPowerAction READ lowPowerAction WRITE setLowPowerAction NOTIFY lowPowerActionChanged)

    Q_PROPERTY(bool ScheduledShutdownState READ scheduledShutdownState WRITE setScheduledShutdownState NOTIFY scheduledShutdownStateChanged)
    Q_PROPERTY(QString ShutdownTime READ shutdownTime WRITE setShutdownTime NOTIFY shutdownTimeChanged)
    Q_PROPERTY(int ShutdownRepetition READ shutdownRepetition WRITE setShutdownRepetition NOTIFY shutdownRepetitionChanged)
    Q_PROPERTY(QByteArray CustomShutdownWeekDays READ customShutdownWeekDays WRITE setCustomShutdownWeekDays NOTIFY customShutdownWeekDaysChanged)

public:
    explicit PowerManager(QDBusConnection *conn, const QString &serviceName,
                          QObject *parent = nullptr);
    ~PowerManager() override;

    bool initialize();

public Q_SLOTS:
    void Reset();
    void SetPrepareSuspend(int state);
    void TurnOffScreen();
    void TurnOnScreen();

public:
    bool onBattery() const { return m_onBattery; }
    bool lidIsPresent() const { return m_lidIsPresent; }
    BatteryIsPresentMap batteryIsPresent() const { return m_batteryIsPresent; }
    BatteryPercentageMap batteryPercentage() const { return m_batteryPercentage; }
    BatteryStateMap batteryState() const { return m_batteryState; }
    quint64 batteryTimeToEmpty() const { return m_batteryTimeToEmpty; }
    bool hasAmbientLightSensor() const { return m_hasAmbientLightSensor; }
    uint warnLevel() const { return m_warnLevel; }
    bool isHighPerformanceSupported() const { return m_isHighPerformanceSupported; }

    int linePowerScreensaverDelay() const { return m_linePowerScreensaverDelay; }
    void setLinePowerScreensaverDelay(int v);
    int linePowerScreenBlackDelay() const { return m_linePowerScreenBlackDelay; }
    void setLinePowerScreenBlackDelay(int v);
    int linePowerSleepDelay() const { return m_linePowerSleepDelay; }
    void setLinePowerSleepDelay(int v);
    int linePowerLockDelay() const { return m_linePowerLockDelay; }
    void setLinePowerLockDelay(int v);
    int linePowerShortIdleDelay() const { return m_linePowerShortIdleDelay; }
    void setLinePowerShortIdleDelay(int v);

    int batteryScreensaverDelay() const { return m_batteryScreensaverDelay; }
    void setBatteryScreensaverDelay(int v);
    int batteryScreenBlackDelay() const { return m_batteryScreenBlackDelay; }
    void setBatteryScreenBlackDelay(int v);
    int batterySleepDelay() const { return m_batterySleepDelay; }
    void setBatterySleepDelay(int v);
    int batteryLockDelay() const { return m_batteryLockDelay; }
    void setBatteryLockDelay(int v);
    int batteryShortIdleDelay() const { return m_batteryShortIdleDelay; }
    void setBatteryShortIdleDelay(int v);

    bool screenBlackLock() const { return m_screenBlackLock; }
    void setScreenBlackLock(bool v);
    bool sleepLock() const { return m_sleepLock; }
    void setSleepLock(bool v);

    int linePowerLidClosedAction() const { return m_linePowerLidClosedAction; }
    void setLinePowerLidClosedAction(int v);
    int batteryLidClosedAction() const { return m_batteryLidClosedAction; }
    void setBatteryLidClosedAction(int v);
    int linePowerPressPowerBtnAction() const { return m_linePowerPressPowerBtnAction; }
    void setLinePowerPressPowerBtnAction(int v);
    int batteryPressPowerBtnAction() const { return m_batteryPressPowerBtnAction; }
    void setBatteryPressPowerBtnAction(int v);

    bool lowPowerNotifyEnable() const { return m_lowPowerNotifyEnable; }
    void setLowPowerNotifyEnable(bool v);
    int lowPowerNotifyThreshold() const { return m_lowPowerNotifyThreshold; }
    void setLowPowerNotifyThreshold(int v);
    int lowPowerAutoSleepThreshold() const { return m_lowPowerAutoSleepThreshold; }
    void setLowPowerAutoSleepThreshold(int v);
    int lowPowerAction() const { return m_lowPowerAction; }
    void setLowPowerAction(int v);
    bool ambientLightAdjustBrightness() const;
    bool adjustBrightnessEnabled() const { return m_adjustBrightnessEnabled; }

    bool scheduledShutdownState() const { return m_scheduledShutdownState; }
    void setScheduledShutdownState(bool v);
    QString shutdownTime() const { return m_shutdownTime; }
    void setShutdownTime(const QString &v);
    int shutdownRepetition() const { return m_shutdownRepetition; }
    void setShutdownRepetition(int v);
    QByteArray customShutdownWeekDays() const { return m_customShutdownWeekDays; }
    void setCustomShutdownWeekDays(const QByteArray &v);

public:
    // ── Accessors for submodules ──────────────────────────
    IdleWatcher *idleWatcher() const { return m_idleWatcher; }
    ScreenController *screenController() const { return m_screenCtrl; }
    bool useWayland() const { return m_useWayland; }
    bool allowScreenSaver() const { return m_allowScreenSaver; }
    int idleOffDelayWhenScreenBlack() const { return m_delayHandleIdleOffIntervalWhenScreenBlack; }
    bool shouldIgnoreIdleOn() const;
    bool shouldIgnoreIdleOff() const;
    bool isSessionActive() const;
    void setShortIdleState(bool state);
    void setScreenIdleState(bool state);
    void setKernelIdleState(bool state);
    bool canEnterShortIdle() const;
    bool shouldPreventIdle() const;
    bool shortIdleEnabled() const { return m_shortIdleEnabled; }
    int prepareSuspendState() const { return static_cast<int>(m_prepareSuspendState); }

    void doSuspend();
    void doSuspendByFront();
    void doShutdown();
    void doHibernate();
    void doHibernateByFront();
    void doTurnOffScreen();
    void doLock(bool autoStartAuth = true);
    bool canSuspend() const;
    void setDPMSModeOn();
    void setDPMSModeOff();
    bool canHibernate() const;
    void handleBeforeSleep(bool beforeSleep);
    void handleWakeup();
    void sendNotify(const QString &summary, const QString &body);
    QMap<QString, double> displayBrightness() const;
    void setDisplayBrightness(const QMap<QString, double> &table);
    void setAndSaveDisplayBrightness(const QMap<QString, double> &table);
    void setBlackScreenActive(bool active);
    void initBatteryWatcher();
    void initSleepWatcher();
    void initLogindInhibit();
    void scheduledShutdown(int state);
    void closeNotify();

private Q_SLOTS:
    void onLogin1OwnerChanged(const QString &name, const QString &oldOwner,
                               const QString &newOwner);
    void onNotifyActionInvoked(uint id, const QString &actionKey);
    void onSystemTimeChanged();
    void onSessionActiveChanged();

    void onLinePowerDelayChanged();
    void onBatteryDelayChanged();

    void handleOnBatteryChanged(bool value);
    void handleHasLidSwitchChanged(bool value);
    void handleHasBatteryChanged(bool value);
    void handleBatteryPercentageChanged(double value);
    void handleBatteryStatusChanged(uint value);
    void handleBatteryTimeToEmptyChanged(quint64 value);
    void handleIsHighPerformanceSupportedChanged(bool value);
    void handlePowerSavingModeEnabledChanged(bool value);
    void handlePowerSavingModeBrightnessDropPercentChanged(uint value);
    void refreshBatteryInfo();
    void notifyPropertyChanged();

Q_SIGNALS:
    void onBatteryChanged();
    void lidIsPresentChanged();
    void batteryIsPresentChanged();
    void batteryPercentageChanged();
    void batteryStateChanged();
    void batteryTimeToEmptyChanged();
    void hasAmbientLightSensorChanged();
    void warnLevelChanged();
    void isHighPerformanceSupportedChanged();
    void linePowerScreensaverDelayChanged();
    void linePowerScreenBlackDelayChanged();
    void linePowerSleepDelayChanged();
    void linePowerShortIdleDelayChanged();
    void linePowerLockDelayChanged();
    void batteryScreensaverDelayChanged();
    void batteryScreenBlackDelayChanged();
    void batterySleepDelayChanged();
    void batteryShortIdleDelayChanged();
    void batteryLockDelayChanged();
    void screenBlackLockChanged();
    void sleepLockChanged();
    void linePowerLidClosedActionChanged();
    void batteryLidClosedActionChanged();
    void linePowerPressPowerBtnActionChanged();
    void batteryPressPowerBtnActionChanged();
    void lowPowerNotifyEnableChanged();
    void lowPowerNotifyThresholdChanged();
    void lowPowerAutoSleepThresholdChanged();
    void lowPowerActionChanged();
    void scheduledShutdownStateChanged();
    void shutdownTimeChanged();
    void shutdownRepetitionChanged();
    void customShutdownWeekDaysChanged();

private:
    // These state-machine collaborators need coordinated access without exposing
    // implementation details through the public PowerManager API.
    friend class LowPowerManager;
    friend class PowerSavePlan;
    IdleWatcher *createIdleWatcher();
    ScreenController *createScreenController();
    void initDConfig();
    void persist(const char *key, const QVariant &value);
    void resetConfig(const char *key);
    void initAmbientBrightness();
    bool screensaverProperty(const char *name) const;
    bool isInConfigOrPowerButtonInhibitors(const QString &whatOnly,
                                           QString &who,
                                           bool blockOnly = true) const;
    bool hasMultipleDisplaySessions() const;

    void initSubmodules();
    void recalculateScheduledShutdown();
    qint64 getNextShutdownTime(qint64 baseTime) const;
    bool isWorkday(const QDateTime &date) const;
    bool isCustomDay(const QDateTime &date) const;
    void shutdownCountdownNotify(int count, bool playSound);
    void doAutoShutdown();
    void initScheduledShutdown();

    QDBusConnection *m_conn = nullptr;

    SessionDBusProxy *m_proxy = nullptr;
    IdleWatcher *m_idleWatcher = nullptr;
    ScreenController *m_screenCtrl = nullptr;
    Dtk::Core::DConfig *m_config = nullptr;
    Dtk::Core::DConfig *m_configReader = nullptr;
    QSet<QString> m_writingConfigKeys;
    bool m_applyingConfig = false;
    bool m_loadingConfig = false;
    PowerSavePlan *m_powerSavePlan = nullptr;
    LidSwitchHandler *m_lidSwitch = nullptr;
    SleepInhibitor *m_sleepInhibitor = nullptr;
    LowPowerManager *m_lowPowerMgr = nullptr;

    bool m_useWayland = false;
    bool m_onBattery = false;
    bool m_lidIsPresent = false;
    BatteryIsPresentMap m_batteryIsPresent;
    BatteryPercentageMap m_batteryPercentage;
    BatteryStateMap m_batteryState;
    quint64 m_batteryTimeToEmpty = 0;
    bool m_hasAmbientLightSensor = false;
    // Shared warning state updated by LowPowerManager and consumed by shutdown logic.
    uint m_warnLevel = 0;
    bool m_isHighPerformanceSupported = false;
    int m_linePowerScreensaverDelay = 0;
    int m_linePowerScreenBlackDelay = 0;
    int m_linePowerSleepDelay = 0;
    int m_linePowerLockDelay = 0;
    int m_linePowerShortIdleDelay = 0;
    int m_batteryScreensaverDelay = 0;
    int m_batteryScreenBlackDelay = 0;
    int m_batterySleepDelay = 0;
    int m_batteryLockDelay = 0;
    int m_batteryShortIdleDelay = 0;
    bool m_screenBlackLock = false;
    bool m_sleepLock = false;
    int m_linePowerLidClosedAction = 0;
    int m_batteryLidClosedAction = 0;
    int m_linePowerPressPowerBtnAction = 0;
    int m_batteryPressPowerBtnAction = 0;
    bool m_lowPowerNotifyEnable = true;
    int m_lowPowerNotifyThreshold = 0;
    int m_lowPowerAutoSleepThreshold = 0;
    int m_lowPowerAction = 0;
    QStringList m_shortIdleBlacklistApplications;
    QStringList m_systemApplications;
    QStringList m_systemServices;
    QStringList m_fullscreenWorkaroundApplications;
    bool m_allowScreenSaver = true;
    int m_delayHandleIdleOffIntervalWhenScreenBlack = 0;
    bool m_adjustBrightnessEnabled = true;
    bool m_shortIdleEnabled = false;
    bool m_scheduledShutdownState = false;
    QString m_shutdownTime;
    int m_shutdownRepetition = 0;
    QByteArray m_customShutdownWeekDays;

    PrepareSuspendState m_prepareSuspendState = PS_Unknown;

    bool m_screensaverWasRunning = false;
    bool m_screensaverLockAtAwake = false;
    bool m_screensaverStateCaptured = false;
    bool m_delayInActive = false;
    bool m_sleepCycleHandled = false;
    int m_delayWakeupInterval = 0;
    int m_inhibitFd = -1;

    QTimer *m_shutdownTimer = nullptr;
    QTimer *m_countdownTimer = nullptr;
    int m_shutdownStatus = SchedInit;
    int m_shutdownCountdown = 60;
    uint m_shutdownNotifyId = 0;
    qint64 m_nextShutdownTime = 0;
};
