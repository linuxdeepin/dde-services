// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include <QtTest>

#include "treelandbrightnesscontroller.h"

using TreelandBrightnessPrivate::TreelandColorControl;

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
    QCOMPARE(TreelandColorControl::adjustedValue(10.0, false), 10.0);
    QCOMPARE(TreelandColorControl::adjustedValue(12.0, false), 10.0);
}

void BrightnessPolicyTest::clampsAtUpperBound()
{
    QCOMPARE(TreelandColorControl::adjustedValue(100.0, true), 100.0);
}

void BrightnessPolicyTest::keepsValueInsideRange()
{
    QCOMPARE(TreelandColorControl::adjustedValue(50.0, false), 45.0);
    QCOMPARE(TreelandColorControl::adjustedValue(50.0, true), 55.0);
}

QTEST_APPLESS_MAIN(BrightnessPolicyTest)

#include "tst_brightnesspolicy.moc"
