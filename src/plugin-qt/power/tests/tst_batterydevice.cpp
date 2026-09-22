// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "../system/batterydevice.h"

#include <QDir>
#include <QTemporaryDir>
#include <QtTest>

// BatteryDevice reads Linux power_supply sysfs entries. These tests drive the
// parsing/math through a fake sysfs tree, covering the critical battery-info
// path that feeds warn levels and the UI.

class TestBatteryDevice : public QObject
{
    Q_OBJECT

private:
    // Builds a fake sysfs directory; each QPair is (filename, contents).
    static void makeSysfs(QTemporaryDir &dir,
                          std::initializer_list<QPair<QString, QString>> entries)
    {
        for (const auto &e : entries) {
            QFile f(dir.path() + '/' + e.first);
            if (!f.open(QIODevice::WriteOnly)) {
                QFAIL(qPrintable(QStringLiteral("failed to create ") + e.first));
            }
            f.write(e.second.toUtf8());
        }
    }

private Q_SLOTS:
    void absentWithoutTypeFile();
    void absentWhenPresentIsZero();
    void energyBasedBattery();
    void dischargingTimeToEmpty();
    void chargingTimeToFull();
    void chargeBasedFallback();
    void percentageFromCapacityFile();
    void energyFullFallsBackToDesign();
    void energyAboveFullClampsFull();
    void statusMapping_data();
    void statusMapping();
    void objectPath_data();
    void objectPath();
    void setStatusEmitsChange();
};

// sysfs numeric files are µ-units; readScaled divides by 1e6.
static QString uValue(double units) { return QString::number(units * 1'000'000.0, 'f', 0); }

void TestBatteryDevice::absentWithoutTypeFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    BatteryDevice device(dir.path());
    QVERIFY(!device.isPresent());
}

void TestBatteryDevice::absentWhenPresentIsZero()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    makeSysfs(dir, {{QStringLiteral("type"), QStringLiteral("Battery")},
                          {QStringLiteral("present"), QStringLiteral("0")}});
    BatteryDevice device(dir.path());
    QVERIFY(!device.isPresent());
}

void TestBatteryDevice::energyBasedBattery()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    makeSysfs(dir, {{QStringLiteral("type"), QStringLiteral("Battery")},
                          {QStringLiteral("present"), QStringLiteral("1")},
                          {QStringLiteral("voltage_max_design"), uValue(10)},
                          {QStringLiteral("energy_now"), uValue(50)},
                          {QStringLiteral("energy_full"), uValue(100)},
                          {QStringLiteral("energy_full_design"), uValue(100)}});
    BatteryDevice device(dir.path());

    QVERIFY(device.isPresent());
    QCOMPARE(device.energy(), 50.0);
    QCOMPARE(device.energyFull(), 100.0);
    QCOMPARE(device.energyFullDesign(), 100.0);
    QCOMPARE(device.percentage(), 50.0);
    QCOMPARE(device.capacity(), 100.0);
    QCOMPARE(device.status(), uint(0));
    QCOMPARE(device.timeToEmpty(), quint64(0));
    QCOMPARE(device.timeToFull(), quint64(0));
}

void TestBatteryDevice::dischargingTimeToEmpty()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    makeSysfs(dir, {{QStringLiteral("type"), QStringLiteral("Battery")},
                          {QStringLiteral("present"), QStringLiteral("1")},
                          {QStringLiteral("voltage_max_design"), uValue(10)},
                          {QStringLiteral("energy_now"), uValue(50)},
                          {QStringLiteral("energy_full"), uValue(100)},
                          {QStringLiteral("power_now"), uValue(10)},
                          {QStringLiteral("status"), QStringLiteral("Discharging")}});
    BatteryDevice device(dir.path());

    QCOMPARE(device.status(), uint(2));
    QCOMPARE(device.energyRate(), 10.0);
    // 3600 * 50 Wh / 10 W = 18000 s
    QCOMPARE(device.timeToEmpty(), quint64(18000));
}

void TestBatteryDevice::chargingTimeToFull()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    makeSysfs(dir, {{QStringLiteral("type"), QStringLiteral("Battery")},
                          {QStringLiteral("present"), QStringLiteral("1")},
                          {QStringLiteral("voltage_max_design"), uValue(10)},
                          {QStringLiteral("energy_now"), uValue(50)},
                          {QStringLiteral("energy_full"), uValue(100)},
                          {QStringLiteral("power_now"), uValue(10)},
                          {QStringLiteral("status"), QStringLiteral("Charging")}});
    BatteryDevice device(dir.path());

    QCOMPARE(device.status(), uint(1));
    // 3600 * (100 - 50) Wh / 10 W = 18000 s
    QCOMPARE(device.timeToFull(), quint64(18000));
}

void TestBatteryDevice::chargeBasedFallback()
{
    // No energy_* files; energy is derived from charge_now * voltage_design and
    // energy_full from charge_full * voltage_design (µAh × V = Wh).
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    makeSysfs(dir, {{QStringLiteral("type"), QStringLiteral("Battery")},
                          {QStringLiteral("present"), QStringLiteral("1")},
                          {QStringLiteral("voltage_max_design"), uValue(10)},
                          {QStringLiteral("charge_now"), uValue(5)},
                          {QStringLiteral("charge_full"), uValue(10)},
                          {QStringLiteral("charge_full_design"), uValue(10)}});
    BatteryDevice device(dir.path());

    QCOMPARE(device.energy(), 50.0);
    QCOMPARE(device.energyFull(), 100.0);
    QCOMPARE(device.percentage(), 50.0);
}

void TestBatteryDevice::percentageFromCapacityFile()
{
    // A hardware-provided `capacity` percentage wins over energy math, and
    // missing energy_now is back-filled from energyFull × percentage.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    makeSysfs(dir, {{QStringLiteral("type"), QStringLiteral("Battery")},
                          {QStringLiteral("present"), QStringLiteral("1")},
                          {QStringLiteral("voltage_max_design"), uValue(10)},
                          {QStringLiteral("energy_full"), uValue(100)},
                          {QStringLiteral("capacity"), QStringLiteral("80")}});
    BatteryDevice device(dir.path());

    QCOMPARE(device.percentage(), 80.0);
    QCOMPARE(device.energy(), 80.0);
}

void TestBatteryDevice::energyFullFallsBackToDesign()
{
    // energy_full missing and charge_full missing, but charge_full_design present:
    // energy_full is back-filled from charge_full_design × voltage_design (µAh × V = Wh).
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    makeSysfs(dir, {{QStringLiteral("type"), QStringLiteral("Battery")},
                          {QStringLiteral("present"), QStringLiteral("1")},
                          {QStringLiteral("voltage_max_design"), uValue(10)},
                          {QStringLiteral("energy_now"), uValue(40)},
                          {QStringLiteral("charge_full_design"), uValue(10)}});
    BatteryDevice device(dir.path());

    QCOMPARE(device.energyFull(), 100.0);
    QCOMPARE(device.energyFullDesign(), 100.0);
    QCOMPARE(device.percentage(), 40.0);
}

void TestBatteryDevice::energyAboveFullClampsFull()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    makeSysfs(dir, {{QStringLiteral("type"), QStringLiteral("Battery")},
                          {QStringLiteral("present"), QStringLiteral("1")},
                          {QStringLiteral("voltage_max_design"), uValue(10)},
                          {QStringLiteral("energy_now"), uValue(120)},
                          {QStringLiteral("energy_full"), uValue(100)},
                          {QStringLiteral("energy_full_design"), uValue(100)}});
    BatteryDevice device(dir.path());

    // energy_full is raised to energy, so percentage clamps to 100.
    QCOMPARE(device.energyFull(), 120.0);
    QCOMPARE(device.percentage(), 100.0);
}

void TestBatteryDevice::statusMapping_data()
{
    QTest::addColumn<QString>("state");
    QTest::addColumn<uint>("expected");

    QTest::newRow("Charging")      << QStringLiteral("Charging")      << uint(1);
    QTest::newRow("Discharging")   << QStringLiteral("Discharging")   << uint(2);
    QTest::newRow("Not charging")  << QStringLiteral("Not charging")  << uint(3);
    QTest::newRow("Full")          << QStringLiteral("Full")          << uint(4);
    QTest::newRow("FullCharging")  << QStringLiteral("FullCharging")  << uint(5);
    QTest::newRow("Unknown")       << QStringLiteral("Unknown")       << uint(0);
}

void TestBatteryDevice::statusMapping()
{
    QFETCH(QString, state);
    QFETCH(uint, expected);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    makeSysfs(dir, {{QStringLiteral("type"), QStringLiteral("Battery")},
                          {QStringLiteral("present"), QStringLiteral("1")},
                          {QStringLiteral("status"), state}});
    BatteryDevice device(dir.path());
    QCOMPARE(device.status(), expected);
}

void TestBatteryDevice::objectPath_data()
{
    QTest::addColumn<QString>("dirName");
    QTest::addColumn<QString>("expectedPath");

    QTest::newRow("plain")     << QStringLiteral("BAT0")      << QStringLiteral("/org/deepin/dde/Power1/battery_BAT0");
    QTest::newRow("underscore")<< QStringLiteral("AC_ADAPTER") << QStringLiteral("/org/deepin/dde/Power1/battery_AC_ADAPTER");
    QTest::newRow("dash")      << QStringLiteral("BAT-1")     << QStringLiteral("/org/deepin/dde/Power1/battery_BAT_x01");
    QTest::newRow("dot")       << QStringLiteral("psu.0")     << QStringLiteral("/org/deepin/dde/Power1/battery_psu_x10");
    QTest::newRow("colon")     << QStringLiteral("battery:1") << QStringLiteral("/org/deepin/dde/Power1/battery_battery_x21");
    QTest::newRow("space")     << QStringLiteral("A B")       << QStringLiteral("/org/deepin/dde/Power1/battery_A_x20B");
}

void TestBatteryDevice::objectPath()
{
    QFETCH(QString, dirName);
    QFETCH(QString, expectedPath);

    QTemporaryDir parent;
    QVERIFY(parent.isValid());
    const QString fullPath = parent.path() + '/' + dirName;
    QVERIFY(QDir().mkpath(fullPath));

    BatteryDevice device(fullPath);
    QCOMPARE(device.objectPath().path(), expectedPath);
}

void TestBatteryDevice::setStatusEmitsChange()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    makeSysfs(dir, {{QStringLiteral("type"), QStringLiteral("Battery")},
                          {QStringLiteral("present"), QStringLiteral("1")}});
    BatteryDevice device(dir.path());

    QSignalSpy spy(&device, &BatteryDevice::statusChanged);
    device.setStatus(1);
    device.setStatus(1); // no-op, no second emission
    QCOMPARE(spy.count(), 1);
    QCOMPARE(device.status(), uint(1));
}

QTEST_GUILESS_MAIN(TestBatteryDevice)
#include "tst_batterydevice.moc"
