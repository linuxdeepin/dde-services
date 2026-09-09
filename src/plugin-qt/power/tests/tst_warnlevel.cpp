// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "../session/warnlevelpolicy.h"

#include <QtTest>

// Ports the deleted dde-daemon session/power1/power_test.go Test_getWarnLevel and
// TestWarnLevelConfig expectations to the extracted C++ policy.

class TestWarnLevel : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void percentagePolicy_data();
    void percentagePolicy();
    void timePolicy_data();
    void timePolicy();
    void offBatteryIsAlwaysNone();
    void configValid_data();
    void configValid();
};

static constexpr quint64 LowTime = 1200;
static constexpr quint64 DangerTime = 900;
static constexpr quint64 CriticalTime = 600;
static constexpr quint64 ActionTime = 300;

void TestWarnLevel::percentagePolicy_data()
{
    QTest::addColumn<double>("percentage");
    QTest::addColumn<int>("expected");

    // notify threshold = 20, action threshold = 5
    QTest::newRow("below-action")    << 1.1 << static_cast<int>(WarnLevel::Action);
    QTest::newRow("at-action")       << 5.0 << static_cast<int>(WarnLevel::Action);
    QTest::newRow("above-action")    << 5.1 << static_cast<int>(WarnLevel::Critical);
    QTest::newRow("at-critical")     << 10.0 << static_cast<int>(WarnLevel::Critical);
    QTest::newRow("above-critical")  << 10.1 << static_cast<int>(WarnLevel::Danger);
    QTest::newRow("at-danger")       << 15.0 << static_cast<int>(WarnLevel::Danger);
    QTest::newRow("above-danger")    << 15.1 << static_cast<int>(WarnLevel::Low);
    QTest::newRow("at-low")          << 20.0 << static_cast<int>(WarnLevel::Low);
    QTest::newRow("above-notify")    << 20.1 << static_cast<int>(WarnLevel::None);
    QTest::newRow("healthy")         << 50.0 << static_cast<int>(WarnLevel::None);
    QTest::newRow("zero-untrusted")  << 0.0 << static_cast<int>(WarnLevel::None);
}

void TestWarnLevel::percentagePolicy()
{
    QFETCH(double, percentage);
    QFETCH(int, expected);

    const auto level = computeWarnLevel(true /*percentage policy*/, true /*onBattery*/,
                                        /*notifyThreshold*/ 20, /*actionPct*/ 5,
                                        LowTime, DangerTime, CriticalTime, ActionTime,
                                        percentage, /*timeToEmpty*/ 0);
    QCOMPARE(static_cast<int>(level), expected);
}

void TestWarnLevel::timePolicy_data()
{
    QTest::addColumn<quint64>("timeToEmpty");
    QTest::addColumn<int>("expected");

    QTest::newRow("action")          << quint64(61)    << static_cast<int>(WarnLevel::Action);
    QTest::newRow("at-action")       << quint64(300)   << static_cast<int>(WarnLevel::Action);
    QTest::newRow("above-action")    << quint64(301)   << static_cast<int>(WarnLevel::Critical);
    QTest::newRow("at-critical")     << quint64(600)   << static_cast<int>(WarnLevel::Critical);
    QTest::newRow("above-critical")  << quint64(601)   << static_cast<int>(WarnLevel::Danger);
    QTest::newRow("at-danger")       << quint64(900)   << static_cast<int>(WarnLevel::Danger);
    QTest::newRow("above-danger")    << quint64(901)   << static_cast<int>(WarnLevel::Low);
    QTest::newRow("at-low")          << quint64(1200)  << static_cast<int>(WarnLevel::Low);
    QTest::newRow("above-low")       << quint64(12001) << static_cast<int>(WarnLevel::None);
    QTest::newRow("zero")            << quint64(0)     << static_cast<int>(WarnLevel::None);
}

void TestWarnLevel::timePolicy()
{
    QFETCH(quint64, timeToEmpty);
    QFETCH(int, expected);

    const auto level = computeWarnLevel(false /*time policy*/, true /*onBattery*/,
                                        /*notifyThreshold*/ 20, /*actionPct*/ 5,
                                        LowTime, DangerTime, CriticalTime, ActionTime,
                                        /*percentage*/ 100.0, timeToEmpty);
    QCOMPARE(static_cast<int>(level), expected);
}

void TestWarnLevel::offBatteryIsAlwaysNone()
{
    QCOMPARE(static_cast<int>(computeWarnLevel(true, false, 20, 5, LowTime, DangerTime,
                                              CriticalTime, ActionTime, 1.0, 0)),
             static_cast<int>(WarnLevel::None));
    QCOMPARE(static_cast<int>(computeWarnLevel(false, false, 20, 5, LowTime, DangerTime,
                                              CriticalTime, ActionTime, 0.0, 1)),
             static_cast<int>(WarnLevel::None));
}

void TestWarnLevel::configValid_data()
{
    QTest::addColumn<quint64>("low");
    QTest::addColumn<quint64>("danger");
    QTest::addColumn<quint64>("critical");
    QTest::addColumn<quint64>("action");
    QTest::addColumn<int>("actionPct");
    QTest::addColumn<bool>("expected");

    QTest::newRow("valid")       << quint64(1200) << quint64(900) << quint64(600)
                                 << quint64(300) << 5 << true;
    QTest::newRow("low-below-danger") << quint64(599) << quint64(900) << quint64(600)
                                      << quint64(300) << 5 << false;
    QTest::newRow("equal-thresholds") << quint64(900) << quint64(900) << quint64(600)
                                      << quint64(300) << 5 << false;
    QTest::newRow("action-pct-10") << quint64(1200) << quint64(900) << quint64(600)
                                   << quint64(300) << 10 << false;
}

void TestWarnLevel::configValid()
{
    QFETCH(quint64, low);
    QFETCH(quint64, danger);
    QFETCH(quint64, critical);
    QFETCH(quint64, action);
    QFETCH(int, actionPct);
    QFETCH(bool, expected);

    QCOMPARE(warnLevelConfigValid(low, danger, critical, action, actionPct), expected);
}

QTEST_GUILESS_MAIN(TestWarnLevel)
#include "tst_warnlevel.moc"
