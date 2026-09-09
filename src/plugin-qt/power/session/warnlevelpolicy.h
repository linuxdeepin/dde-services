// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QtTypes>

// Pure low-battery warning-level policy, extracted from LowPowerManager so the
// threshold math is testable in isolation. Values mirror the deleted
// dde-daemon session/power1/warn_level.go (WarnLevel) ordering and the
// LowPowerManager::Level enum.

enum class WarnLevel : quint8 {
    None = 0,
    Remind = 1,
    Low = 2,
    Danger = 3,
    Critical = 4,
    Action = 5,
};

// Maps (onBattery, percentage | timeToEmpty) to a warning level using the
// configured thresholds. Kept in lockstep with the legacy dde-daemon
// getWarnLevel(): percentage policy is gated by the notify threshold and uses
// fixed 10/15/20/25 breakpoints; time policy uses strictly decreasing
// time-to-empty thresholds.
inline WarnLevel computeWarnLevel(bool usePercentageForPolicy,
                                  bool onBattery,
                                  int lowPowerNotifyThreshold,
                                  int percentageAction,
                                  quint64 timeToEmptyLow,
                                  quint64 timeToEmptyDanger,
                                  quint64 timeToEmptyCritical,
                                  quint64 timeToEmptyAction,
                                  double percentage,
                                  quint64 timeToEmpty)
{
    if (!onBattery)
        return WarnLevel::None;

    if (usePercentageForPolicy) {
        // A missing/0% reading from firmware is untrustworthy.
        if (percentage == 0.0)
            return WarnLevel::None;

        if (percentage <= lowPowerNotifyThreshold) {
            if (percentageAction > 0 && percentage <= percentageAction)
                return WarnLevel::Action;
            if (percentage <= 10.0)
                return WarnLevel::Critical;
            if (percentage <= 15.0)
                return WarnLevel::Danger;
            if (percentage <= 20.0)
                return WarnLevel::Low;
            if (percentage <= 25.0)
                return WarnLevel::Remind;
            return WarnLevel::None;
        }

        return WarnLevel::None;
    }

    if (timeToEmpty > timeToEmptyLow || timeToEmpty == 0)
        return WarnLevel::None;
    if (timeToEmpty > timeToEmptyDanger)
        return WarnLevel::Low;
    if (timeToEmpty > timeToEmptyCritical)
        return WarnLevel::Danger;
    if (timeToEmpty > timeToEmptyAction)
        return WarnLevel::Critical;
    return WarnLevel::Action;
}

// Validation for the time-to-empty thresholds. Legacy dde-daemon only accepted
// 1%-9% action thresholds; 10% is the separate critical threshold, so equality
// would collapse two warning levels.
inline bool warnLevelConfigValid(quint64 timeToEmptyLow,
                                 quint64 timeToEmptyDanger,
                                 quint64 timeToEmptyCritical,
                                 quint64 timeToEmptyAction,
                                 int percentageAction)
{
    return timeToEmptyLow > timeToEmptyDanger
        && timeToEmptyDanger > timeToEmptyCritical
        && timeToEmptyCritical > timeToEmptyAction
        && percentageAction < 10;
}
