// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "core/qkeysequenceconverter.h"

#include <QTest>

class TestQKeySequenceConverter : public QObject
{
    Q_OBJECT

private slots:
    void symbolKeysUseXkbNames_data();
    void symbolKeysUseXkbNames();
};

void TestQKeySequenceConverter::symbolKeysUseXkbNames_data()
{
    QTest::addColumn<QString>("qtSequence");
    QTest::addColumn<QString>("xkbSequence");

    QTest::newRow("zoom-in") << QStringLiteral("Meta++")
                              << QStringLiteral("<Super>plus");
    QTest::newRow("zoom-out") << QStringLiteral("Meta+-")
                               << QStringLiteral("<Super>minus");
    QTest::newRow("equal") << QStringLiteral("Meta+=")
                            << QStringLiteral("<Super>equal");
}

void TestQKeySequenceConverter::symbolKeysUseXkbNames()
{
    QFETCH(QString, qtSequence);
    QFETCH(QString, xkbSequence);

    QCOMPARE(QKeySequenceConverter::qKeySequenceToXkb(qtSequence), xkbSequence);
    QCOMPARE(QKeySequenceConverter::xkbToQKeySequence(xkbSequence), qtSequence);
}

QTEST_GUILESS_MAIN(TestQKeySequenceConverter)

#include "tst_qkeysequenceconverter.moc"
