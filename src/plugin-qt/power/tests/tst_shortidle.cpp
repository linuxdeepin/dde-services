// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "../session/shortidlepolicy.h"

#include <QtTest>

// Covers the short-idle application-blocking decision behind the PMS cases
// "应用配置增减应用/打开终端/浏览器/腾讯会议…检查是否进入shortidle".

class TestShortIdle : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void blockReason_data();
    void blockReason();
};

void TestShortIdle::blockReason_data()
{
    QTest::addColumn<QString>("desktop");
    QTest::addColumn<QStringList>("blacklist");
    QTest::addColumn<QStringList>("systemApps");
    QTest::addColumn<int>("expected");

    const QStringList blacklist = {QStringLiteral("wechat.desktop")};
    const QStringList systemApps = {
        QStringLiteral("deepin-terminal.desktop"),
        QStringLiteral("deepin-browser.desktop"),
    };

    QTest::newRow("blacklisted")
        << QStringLiteral("wechat.desktop") << blacklist << systemApps
        << static_cast<int>(ShortIdleBlock::Blacklist);

    QTest::newRow("system-app")
        << QStringLiteral("deepin-terminal.desktop") << blacklist << systemApps
        << static_cast<int>(ShortIdleBlock::Allowed);

    QTest::newRow("third-party")
        << QStringLiteral("google-chrome.desktop") << blacklist << systemApps
        << static_cast<int>(ShortIdleBlock::ThirdParty);

    QTest::newRow("deepin-brand")
        << QStringLiteral("DeepinMusic.desktop") << blacklist << systemApps
        << static_cast<int>(ShortIdleBlock::Allowed);

    QTest::newRow("dde-brand")
        << QStringLiteral("dde-dock.desktop") << blacklist << systemApps
        << static_cast<int>(ShortIdleBlock::Allowed);

    QTest::newRow("uos-brand")
        << QStringLiteral("uos-browser.desktop") << blacklist << systemApps
        << static_cast<int>(ShortIdleBlock::Allowed);
}

void TestShortIdle::blockReason()
{
    QFETCH(QString, desktop);
    QFETCH(QStringList, blacklist);
    QFETCH(QStringList, systemApps);
    QFETCH(int, expected);

    QCOMPARE(static_cast<int>(shortIdleBlockReason(desktop, blacklist, systemApps)), expected);
}

QTEST_GUILESS_MAIN(TestShortIdle)
#include "tst_shortidle.moc"
