// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include <QtTest>

#include "constant.h"

class BrightnessPolicyTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void clampsAtLowerBound();
    void clampsAtUpperBound();
    void keepsValueInsideRange();
};

void BrightnessPolicyTest::clampsAtLowerBound()
{
    QCOMPARE(Brightness::adjustedValue(10.0, false), 10.0);
    QCOMPARE(Brightness::adjustedValue(12.0, false), 10.0);
}

void BrightnessPolicyTest::clampsAtUpperBound()
{
    QCOMPARE(Brightness::adjustedValue(100.0, true), 100.0);
}

void BrightnessPolicyTest::keepsValueInsideRange()
{
    QCOMPARE(Brightness::adjustedValue(50.0, false), 45.0);
    QCOMPARE(Brightness::adjustedValue(50.0, true), 55.0);
}

QTEST_APPLESS_MAIN(BrightnessPolicyTest)

#include "tst_brightnesspolicy.moc"
