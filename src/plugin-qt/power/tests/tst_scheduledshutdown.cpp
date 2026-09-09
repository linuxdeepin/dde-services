// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "../session/scheduledshutdown.h"

#include <QtTest>

// Ports the scheduled-shutdown next-time computation from the deleted dde-daemon
// session/power1 logic into deterministic tests over explicit dates.

class TestScheduledShutdown : public QObject
{
    Q_OBJECT

private:
    static QDateTime dt(int year, int month, int day, int hour, int minute)
    {
        return QDateTime(QDate(year, month, day), QTime(hour, minute));
    }

private Q_SLOTS:
    void customDay_data();
    void customDay();
    void onceInFuture();
    void onceAlreadyPassed();
    void everydayRollsPastBase();
    void customNextOccurrence();
    void customSundayMapping();
    void workdaysSkipsWeekend();
    void customExhaustedIsInvalid();
};

void TestScheduledShutdown::customDay_data()
{
    QTest::addColumn<int>("dayOfWeek");
    QTest::addColumn<QByteArray>("customDays");
    QTest::addColumn<bool>("expected");

    QByteArray monWed;
    monWed.append(char(1));
    monWed.append(char(3));

    QTest::newRow("monday")   << 1 << monWed << true;
    QTest::newRow("wednesday")<< 3 << monWed << true;
    QTest::newRow("tuesday")  << 2 << monWed << false;
    QTest::newRow("sunday-not-set") << 7 << monWed << false;

    QByteArray zero;
    zero.append(char(0));
    QTest::newRow("sunday-as-zero") << 7 << zero << true;
    QTest::newRow("monday-not-zero") << 1 << zero << false;

    QByteArray sunday;
    sunday.append(char(7));
    QTest::newRow("sunday-as-seven") << 7 << sunday << true;
}

void TestScheduledShutdown::customDay()
{
    QFETCH(int, dayOfWeek);
    QFETCH(QByteArray, customDays);
    QFETCH(bool, expected);

    QCOMPARE(isCustomShutdownDay(dayOfWeek, customDays), expected);
}

void TestScheduledShutdown::onceInFuture()
{
    const QDateTime now = dt(2026, 9, 9, 10, 0); // Wednesday
    const QDateTime target = nextShutdownDateTime(
        0, now, QStringLiteral("22:00"), ShutdownRepetition::Once, {},
        [](const QDateTime &) { return true; });

    QCOMPARE(target, dt(2026, 9, 9, 22, 0));
}

void TestScheduledShutdown::onceAlreadyPassed()
{
    const QDateTime now = dt(2026, 9, 9, 23, 0);
    const QDateTime target = nextShutdownDateTime(
        0, now, QStringLiteral("22:00"), ShutdownRepetition::Once, {},
        [](const QDateTime &) { return true; });

    QCOMPARE(target, dt(2026, 9, 10, 22, 0));
}

void TestScheduledShutdown::everydayRollsPastBase()
{
    // baseTime is the previously scheduled epoch (today 22:00); the next one is
    // tomorrow 22:00.
    const QDateTime now = dt(2026, 9, 9, 10, 0);
    const qint64 base = dt(2026, 9, 9, 22, 0).toSecsSinceEpoch();
    const QDateTime target = nextShutdownDateTime(
        base, now, QStringLiteral("22:00"), ShutdownRepetition::Everyday, {},
        [](const QDateTime &) { return true; });

    QCOMPARE(target, dt(2026, 9, 10, 22, 0));
}

void TestScheduledShutdown::customNextOccurrence()
{
    // Custom Friday only; from Wednesday the next match is Friday.
    QByteArray friday;
    friday.append(char(5));
    const QDateTime now = dt(2026, 9, 9, 10, 0);
    const QDateTime target = nextShutdownDateTime(
        0, now, QStringLiteral("22:00"), ShutdownRepetition::Custom, friday,
        [](const QDateTime &) { return true; });

    QCOMPARE(target, dt(2026, 9, 11, 22, 0));
}

void TestScheduledShutdown::customSundayMapping()
{
    // A stored 0 means Sunday; from Wednesday the next Sunday is 2026-09-13.
    QByteArray sunday;
    sunday.append(char(0));
    const QDateTime now = dt(2026, 9, 9, 10, 0);
    const QDateTime target = nextShutdownDateTime(
        0, now, QStringLiteral("22:00"), ShutdownRepetition::Custom, sunday,
        [](const QDateTime &) { return true; });

    QCOMPARE(target, dt(2026, 9, 13, 22, 0));
}

void TestScheduledShutdown::workdaysSkipsWeekend()
{
    const QDateTime now = dt(2026, 9, 12, 10, 0); // Saturday
    const auto isWorkday = [](const QDateTime &d) {
        const int dow = d.date().dayOfWeek();
        return dow != Qt::Saturday && dow != Qt::Sunday;
    };
    const QDateTime target = nextShutdownDateTime(
        0, now, QStringLiteral("22:00"), ShutdownRepetition::Workdays, {}, isWorkday);

    QCOMPARE(target, dt(2026, 9, 14, 22, 0)); // Monday
}

void TestScheduledShutdown::customExhaustedIsInvalid()
{
    // No custom days match → the 7-day scan is exhausted and no target exists.
    const QDateTime now = dt(2026, 9, 9, 10, 0);
    const QDateTime target = nextShutdownDateTime(
        0, now, QStringLiteral("22:00"), ShutdownRepetition::Custom, {},
        [](const QDateTime &) { return true; });

    QVERIFY(!target.isValid());
}

QTEST_GUILESS_MAIN(TestScheduledShutdown)
#include "tst_scheduledshutdown.moc"
