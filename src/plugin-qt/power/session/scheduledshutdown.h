// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QTime>
#include <functional>

// Pure scheduled-shutdown time computation, extracted from PowerManager so the
// next-shutdown math is testable in isolation. Mirrors the deleted dde-daemon
// session/power1 scheduled-shutdown logic (getNextShutdownTime / isCustomDay).

enum class ShutdownRepetition : int {
    Once = 0,     // RepOnce
    Everyday = 1, // RepEveryday
    Workdays = 2, // RepWorkdays
    Custom = 3,   // RepCustom
};

// Whether a weekday (Qt day-of-week, Monday=1 .. Sunday=7) is among the custom
// shutdown days. Custom days are stored as numeric byte values (legacy daemon used
// []byte); a stored 0 also means Sunday.
inline bool isCustomShutdownDay(int dayOfWeek, const QByteArray &customWeekDays)
{
    for (char configured : customWeekDays) {
        const auto value = static_cast<quint8>(configured);
        if (value == dayOfWeek || (dayOfWeek == Qt::Sunday && value == 0))
            return true;
    }
    return false;
}

// Computes the next shutdown QDateTime, or an invalid QDateTime when the workday/
// custom scan is exhausted. `isWorkday` is the holiday-aware predicate (DBus-backed
// in production, a weekday check in tests). `baseTime` is the previously scheduled
// epoch (0 for the first computation).
inline QDateTime nextShutdownDateTime(qint64 baseTime,
                                      const QDateTime &now,
                                      const QString &shutdownTime,
                                      ShutdownRepetition repetition,
                                      const QByteArray &customWeekDays,
                                      const std::function<bool(const QDateTime &)> &isWorkday)
{
    const auto getNextTime = [&](qint64 bt) -> QDateTime {
        const QDateTime baseDate = QDateTime::fromSecsSinceEpoch(bt);
        const QTime targetTime = QTime::fromString(shutdownTime, QStringLiteral("hh:mm"));
        QDateTime target(now.date(), targetTime);

        if (now.secsTo(target) / 60 < 0) // already passed today
            target = target.addDays(1);

        if (baseDate.secsTo(target) / 60 <= 0)
            target = target.addDays(1);
        return target;
    };

    QDateTime target;
    switch (repetition) {
    case ShutdownRepetition::Once:
    case ShutdownRepetition::Everyday:
        target = getNextTime(baseTime);
        break;
    case ShutdownRepetition::Workdays: {
        target = getNextTime(baseTime);
        for (int i = 0; i <= 366; ++i) {
            if (i == 366)
                return QDateTime();
            if (isWorkday(target))
                break;
            target = target.addDays(1);
        }
        break;
    }
    case ShutdownRepetition::Custom: {
        target = getNextTime(baseTime);
        for (int i = 0; i <= 7; ++i) {
            if (i == 7)
                return QDateTime();
            if (isCustomShutdownDay(target.date().dayOfWeek(), customWeekDays))
                break;
            target = target.addDays(1);
        }
        break;
    }
    default:
        target = getNextTime(baseTime);
        break;
    }
    return target;
}
