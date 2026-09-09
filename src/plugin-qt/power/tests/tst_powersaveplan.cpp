// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "../session/powersaveplan.h"

#include <QtTest>

// Ports the deleted dde-daemon TestMetaTasksMin and the power-save-plan task
// coalescing rules (canAdd) to deterministic C++ tests.

class TestPowerSavePlan : public QObject
{
    Q_OBJECT

private:
    using MetaTask = PowerSavePlan::MetaTask;

    static MetaTask task(const QString &name, int delay)
    {
        MetaTask t;
        t.name = name;
        t.delay = delay;
        return t;
    }

private Q_SLOTS:
    void minDelay_data();
    void minDelay();
    void canAdd_data();
    void canAdd();
};

void TestPowerSavePlan::minDelay_data()
{
    QTest::addColumn<QVector<int>>("delays");
    QTest::addColumn<int>("expected");

    QTest::newRow("mixed") << QVector<int>{10, 30, 20} << 10;
    QTest::newRow("single") << QVector<int>{10} << 10;
    QTest::newRow("empty") << QVector<int>{} << 0;
}

void TestPowerSavePlan::minDelay()
{
    QFETCH(QVector<int>, delays);
    QFETCH(int, expected);

    QVector<MetaTask> tasks;
    for (int d : delays)
        tasks.append(task(QStringLiteral("t%1").arg(tasks.size()), d));

    QCOMPARE(minTaskDelay(tasks), expected);
}

void TestPowerSavePlan::canAdd_data()
{
    QTest::addColumn<QString>("type");
    QTest::addColumn<int>("delay");
    QTest::addColumn<QVector<MetaTask>>("existing");
    QTest::addColumn<bool>("expected");

    // Empty task list: anything can be added.
    QTest::newRow("empty") << QStringLiteral("screenBlack") << 60 << QVector<MetaTask>{} << true;

    // sleep always adds, regardless of ordering.
    const QVector<MetaTask> someTasks = {task(QStringLiteral("lock"), 60),
                                         task(QStringLiteral("screenBlack"), 120)};
    QTest::newRow("sleep-always") << QStringLiteral("sleep") << 30 << someTasks << true;

    // screenSaverStart only if not later than the earliest existing task.
    const QVector<MetaTask> early = {task(QStringLiteral("lock"), 60)};
    QTest::newRow("screensaver-before-min") << QStringLiteral("screenSaverStart") << 60 << early << true;
    QTest::newRow("screensaver-after-min") << QStringLiteral("screenSaverStart") << 61 << early << false;

    // screenBlack adds when earlier than the first task, or tied with first when last is lock.
    const QVector<MetaTask> lockThenBlack = {task(QStringLiteral("lock"), 60),
                                             task(QStringLiteral("screenBlack"), 120)};
    QTest::newRow("black-earlier") << QStringLiteral("screenBlack") << 50 << lockThenBlack << true;
    QTest::newRow("black-tied-last-lock") << QStringLiteral("screenBlack") << 60
                                          << QVector<MetaTask>{task(QStringLiteral("lock"), 60)} << true;
    QTest::newRow("black-later") << QStringLiteral("screenBlack") << 70 << lockThenBlack << false;

    // Unknown type never adds to a non-empty list.
    QTest::newRow("unknown") << QStringLiteral("bogus") << 10 << early << false;
}

void TestPowerSavePlan::canAdd()
{
    QFETCH(QString, type);
    QFETCH(int, delay);
    QFETCH(QVector<MetaTask>, existing);
    QFETCH(bool, expected);

    QCOMPARE(canAddTask(type, delay, existing), expected);
}

QTEST_GUILESS_MAIN(TestPowerSavePlan)
#include "tst_powersaveplan.moc"
