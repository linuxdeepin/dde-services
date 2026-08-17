// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include <QtTest>

#include "modules/common/common.h"

class XftDpiUpdateTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void persistsChangedValue();
    void republishesUnchangedValue();
};

void XftDpiUpdateTest::persistsChangedValue()
{
    const auto update = makeXftDpiUpdate(96 * 1024, 192 * 1024);

    QVERIFY(update.needsPersist);
    QCOMPARE(update.setting.prop, QStringLiteral("Xft/DPI"));
    QCOMPARE(update.setting.type, HeadTypeInteger);
    QCOMPARE(std::get<int>(update.setting.value), 192 * 1024);
}

void XftDpiUpdateTest::republishesUnchangedValue()
{
    const auto update = makeXftDpiUpdate(192 * 1024, 192 * 1024);

    QVERIFY(!update.needsPersist);
    QCOMPARE(update.setting.prop, QStringLiteral("Xft/DPI"));
    QCOMPARE(std::get<int>(update.setting.value), 192 * 1024);
}

QTEST_APPLESS_MAIN(XftDpiUpdateTest)

#include "tst_xftdpiupdate.moc"
