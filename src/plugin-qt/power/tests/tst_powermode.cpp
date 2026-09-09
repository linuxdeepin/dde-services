// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "../system/powermodepolicy.h"

#include <QtTest>

// Power-mode DSPC mapping + low-battery decision logic, extracted from
// SystemPowerManager and exercised over explicit mapping configs.

class TestPowerMode : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void mappedLookup();
    void resolveFallback_data();
    void resolveFallback();
    void logicalMode_data();
    void logicalMode();
    void batteryLow_data();
    void batteryLow();
};

void TestPowerMode::mappedLookup()
{
    const QString config = QStringLiteral(
        "{\"powersave\": {\"DSPCConfig\": \"saving\"},"
        " \"balance\": {\"DSPCConfig\": \"balance\"}}");

    QCOMPARE(mappedDspcMode(QStringLiteral("powersave"), config), QStringLiteral("saving"));
    QCOMPARE(mappedDspcMode(QStringLiteral("balance"), config), QStringLiteral("balance"));
    QCOMPARE(mappedDspcMode(QStringLiteral("missing"), config), QString());
}

void TestPowerMode::resolveFallback_data()
{
    QTest::addColumn<QString>("logicalMode");
    QTest::addColumn<QString>("config");
    QTest::addColumn<QString>("expected");

    QTest::newRow("powersave-default") << QStringLiteral("powersave") << QString() << QStringLiteral("saving");
    QTest::newRow("lowbattery-default") << QStringLiteral("lowBattery") << QString() << QStringLiteral("lowbat");
    QTest::newRow("balance-default") << QStringLiteral("balance") << QString() << QStringLiteral("balance");
    QTest::newRow("performance-default") << QStringLiteral("performance") << QString() << QStringLiteral("performance");

    QTest::newRow("valid-mapping") << QStringLiteral("powersave")
        << QStringLiteral("{\"powersave\": {\"DSPCConfig\": \"performance\"}}")
        << QStringLiteral("performance");

    // An invalid mapped value is rejected and falls back.
    QTest::newRow("invalid-mapping-fallback") << QStringLiteral("powersave")
        << QStringLiteral("{\"powersave\": {\"DSPCConfig\": \"weird\"}}")
        << QStringLiteral("saving");
}

void TestPowerMode::resolveFallback()
{
    QFETCH(QString, logicalMode);
    QFETCH(QString, config);
    QFETCH(QString, expected);

    QCOMPARE(resolveDspcMode(logicalMode, config), expected);
}

void TestPowerMode::logicalMode_data()
{
    QTest::addColumn<bool>("batteryLow");
    QTest::addColumn<QString>("mode");
    QTest::addColumn<QString>("expected");

    QTest::newRow("low-override-powersave") << true << QStringLiteral("powersave") << QStringLiteral("lowBattery");
    QTest::newRow("low-balance-untouched") << true << QStringLiteral("balance") << QStringLiteral("balance");
    QTest::newRow("not-low-powersave") << false << QStringLiteral("powersave") << QStringLiteral("powersave");
}

void TestPowerMode::logicalMode()
{
    QFETCH(bool, batteryLow);
    QFETCH(QString, mode);
    QFETCH(QString, expected);

    QCOMPARE(lowBatteryLogicalMode(batteryLow, mode), expected);
}

void TestPowerMode::batteryLow_data()
{
    QTest::addColumn<bool>("hasBattery");
    QTest::addColumn<double>("percentage");
    QTest::addColumn<double>("threshold");
    QTest::addColumn<bool>("expected");

    QTest::newRow("no-battery") << false << 10.0 << 20.0 << false;
    QTest::newRow("at-threshold") << true << 20.0 << 20.0 << true;
    QTest::newRow("below-threshold") << true << 5.0 << 20.0 << true;
    QTest::newRow("above-threshold") << true << 20.1 << 20.0 << false;
}

void TestPowerMode::batteryLow()
{
    QFETCH(bool, hasBattery);
    QFETCH(double, percentage);
    QFETCH(double, threshold);
    QFETCH(bool, expected);

    QCOMPARE(isBatteryLow(hasBattery, percentage, threshold), expected);
}

QTEST_GUILESS_MAIN(TestPowerMode)
#include "tst_powermode.moc"
