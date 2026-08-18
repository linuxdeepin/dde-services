// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

namespace PowerDConfig {
    inline constexpr auto kAppId           = "org.deepin.dde.daemon";
    inline constexpr auto kPowerName       = "org.deepin.dde.daemon.power";

    inline constexpr auto kLinePowerScreensaverDelay       = "linePowerScreensaverDelay";
    inline constexpr auto kLinePowerScreenBlackDelay       = "linePowerScreenBlackDelay";
    inline constexpr auto kLinePowerSleepDelay             = "linePowerSleepDelay";
    inline constexpr auto kLinePowerLockDelay              = "linePowerLockDelay";
    inline constexpr auto kLinePowerLidClosedAction        = "linePowerLidClosedAction";
    inline constexpr auto kLinePowerPressPowerButton       = "linePowerPressPowerButton";

    inline constexpr auto kBatteryScreensaverDelay         = "batteryScreensaverDelay";
    inline constexpr auto kBatteryScreenBlackDelay         = "batteryScreenBlackDelay";
    inline constexpr auto kBatterySleepDelay               = "batterySleepDelay";
    inline constexpr auto kBatteryLockDelay                = "batteryLockDelay";
    inline constexpr auto kBatteryLidClosedAction          = "batteryLidClosedAction";
    inline constexpr auto kBatteryPressPowerButton         = "batteryPressPowerButton";

    inline constexpr auto kScreenBlackLock                 = "screenBlackLock";
    inline constexpr auto kSleepLock                       = "sleepLock";
    inline constexpr auto kPowerButtonPressedExec          = "powerButtonPressedExec";

    inline constexpr auto kLowPowerNotifyEnable            = "lowPowerNotifyEnable";
    inline constexpr auto kLowPowerNotifyThreshold         = "lowPowerNotifyThreshold";
    inline constexpr auto kUsePercentageForPolicy          = "usePercentageForPolicy";
    inline constexpr auto kPercentageAction                = "percentageAction";
    inline constexpr auto kTimeToEmptyLow                  = "timeToEmptyLow";
    inline constexpr auto kTimeToEmptyDanger               = "timeToEmptyDanger";
    inline constexpr auto kTimeToEmptyCritical             = "timeToEmptyCritical";
    inline constexpr auto kTimeToEmptyAction               = "timeToEmptyAction";
    inline constexpr auto kLowPowerAction                  = "lowPowerAction";
    inline constexpr auto kPowerSavingModeBrightnessDropPercent = "powerSavingModeBrightnessDropPercent";
    inline constexpr auto kPowerSavingModeEnabled               = "powerSavingModeEnabled";
    inline constexpr auto kPowerSavingModeAuto                  = "powerSavingModeAuto";
    inline constexpr auto kPowerSavingModeAutoWhenBatteryLow    = "powerSavingModeAutoWhenBatteryLow";
    inline constexpr auto kPowerSavingModeAutoBatteryPercent    = "powerSavingModeAutoBatteryPercent";
    inline constexpr auto kLastMode                         = "lastMode";
    inline constexpr auto kMode                            = "mode";
    inline constexpr auto kAdjustBrightnessEnabled         = "adjustBrightnessEnabled";
    inline constexpr auto kHighPerformanceEnabled           = "highPerformanceEnabled";
    inline constexpr auto kAmbientLightAdjustBrightness    = "ambientLightAdjustBrightness";

    inline constexpr auto kScheduledShutdownState          = "scheduledShutdownState";
    inline constexpr auto kShutdownTime                    = "shutdownTime";
    inline constexpr auto kShutdownRepetition              = "shutdownRepetition";
    inline constexpr auto kCustomShutdownWeekDays          = "customShutdownWeekDays";
    inline constexpr auto kShutdownCountdown               = "shutdownCountdown";
    inline constexpr auto kNextShutdownTime                = "nextShutdownTime";
}

namespace PowerDBus {
    inline constexpr auto kService        = "org.deepin.dde.Power1";
    inline constexpr auto kPath           = "/org/deepin/dde/Power1";
    inline constexpr auto kInterface      = "org.deepin.dde.Power1";

    inline constexpr auto kLogin1Service  = "org.freedesktop.login1";
    inline constexpr auto kLogin1Path     = "/org/freedesktop/login1";
    inline constexpr auto kLogin1Manager  = "org.freedesktop.login1.Manager";
    inline constexpr auto kLogin1Session  = "org.freedesktop.login1.Session";

    inline constexpr auto kSessionManager = "org.deepin.dde.SessionManager1";
    inline constexpr auto kSessionPath    = "/org/deepin/dde/SessionManager1";

    inline constexpr auto kSessionWatcher     = "org.deepin.dde.SessionWatcher1";
    inline constexpr auto kSessionWatcherPath = "/org/deepin/dde/SessionWatcher1";

    inline constexpr auto kDaemonService  = "org.deepin.dde.Daemon1";
    inline constexpr auto kDaemonPath     = "/org/deepin/dde/Daemon1";

    inline constexpr auto kLockFront      = "org.deepin.dde.LockFront1";
    inline constexpr auto kLockFrontPath  = "/org/deepin/dde/LockFront1";

    inline constexpr auto kShutdownFront  = "org.deepin.dde.ShutdownFront1";
    inline constexpr auto kShutdownPath   = "/org/deepin/dde/ShutdownFront1";

    inline constexpr auto kScreensaver    = "com.deepin.ScreenSaver";
    inline constexpr auto kScreensaverPath = "/com/deepin/ScreenSaver";

    inline constexpr auto kDisplay        = "org.deepin.dde.Display1";
    inline constexpr auto kDisplayPath    = "/org/deepin/dde/Display1";

    inline constexpr auto kBlackScreen    = "org.deepin.dde.BlackScreen1";
    inline constexpr auto kBlackScreenPath = "/org/deepin/dde/BlackScreen1";

    inline constexpr auto kNotifications  = "org.freedesktop.Notifications";
    inline constexpr auto kNotificationsPath = "/org/freedesktop/Notifications";

    inline constexpr auto kFreedesktopDBus = "org.freedesktop.DBus";
    inline constexpr auto kFreedesktopPath = "/org/freedesktop/DBus";

    inline constexpr auto kUPowerService  = "org.freedesktop.UPower";
    inline constexpr auto kUPowerPath     = "/org/freedesktop/UPower";

    inline constexpr auto kCalendarService = "com.deepin.dataserver.Calendar";
    inline constexpr auto kCalendarPath    = "/com/deepin/dataserver/Calendar/HuangLi";
    inline constexpr auto kCalendarIface   = "com.deepin.dataserver.Calendar.HuangLi";
}

namespace PowerFS {
    inline constexpr auto kCpuSysfsDir       = "/sys/devices/system/cpu";
    inline constexpr auto kCpuGovernorFmt    = "/sys/devices/system/cpu/%1/cpufreq/scaling_governor";
    inline constexpr auto kCpuBoostPath      = "/sys/devices/system/cpu/cpufreq/boost";
    inline constexpr auto kLidStatePath      = "/proc/acpi/button/lid/LID/state";
    inline constexpr auto kDpmsStateFile     = "/tmp/dpms-state";
    inline constexpr auto kNoSuspendFile     = "/etc/deepin/no_suspend";
    inline constexpr auto kLowPowerCmd       = "/usr/lib/deepin-daemon/dde-lowpower";
}
