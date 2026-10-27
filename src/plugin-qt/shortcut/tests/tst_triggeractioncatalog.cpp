// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "core/triggeractioncatalog.h"

#include <QTest>

class TestTriggerActionCatalog : public QObject
{
    Q_OBJECT

private slots:
    void resolvesStableActionIds();
    void mapsX11ActionsByLogicalId();
    void mapsStableIdsToTreelandV3_data();
    void mapsStableIdsToTreelandV3();
    void rejectsUnsupportedTreelandExtensions();
};

void TestTriggerActionCatalog::resolvesStableActionIds()
{
    QCOMPARE(TriggerActionCatalog::resolve(QStringLiteral("15")),
             TriggerActionId::ShowWindowMenu);
    QCOMPARE(TriggerActionCatalog::resolve(QStringLiteral("111")),
             TriggerActionId::ResizeWindow);
    QCOMPARE(TriggerActionCatalog::resolve(QStringLiteral("unknown")),
             TriggerActionId::Invalid);
}

void TestTriggerActionCatalog::mapsX11ActionsByLogicalId()
{
    QCOMPARE(TriggerActionCatalog::x11WmShortcutId(TriggerActionId::ShowWindowMenu),
             QStringLiteral("activateWindowMenu"));
    QCOMPARE(TriggerActionCatalog::x11WmShortcutId(TriggerActionId::MoveToLeftWorkspace),
             QStringLiteral("moveToWorkspaceLeft"));
    QCOMPARE(TriggerActionCatalog::x11WmShortcutId(TriggerActionId::ZoomIn),
             QStringLiteral("viewZoomIn"));
    QVERIFY(TriggerActionCatalog::x11WmShortcutId(TriggerActionId::ToggleFpsDisplay).isEmpty());
}

void TestTriggerActionCatalog::rejectsUnsupportedTreelandExtensions()
{
    QVERIFY(!TriggerActionCatalog::treelandActionId(TriggerActionId::Disable));
    QVERIFY(!TriggerActionCatalog::treelandActionId(TriggerActionId::Quit));
    QVERIFY(!TriggerActionCatalog::treelandActionId(TriggerActionId::TaskSwitchEnter));
    QVERIFY(!TriggerActionCatalog::treelandActionId(TriggerActionId::ResizeWindow));
    QVERIFY(!TriggerActionCatalog::treelandActionId(TriggerActionId::ZoomIn));
    QVERIFY(!TriggerActionCatalog::treelandActionId(TriggerActionId::ZoomOut));
    QVERIFY(!TriggerActionCatalog::treelandActionId(TriggerActionId::ZoomReset));
    QVERIFY(!TriggerActionCatalog::treelandActionId(TriggerActionId::ToggleLauncher));
    QVERIFY(!TriggerActionCatalog::treelandActionId(TriggerActionId::Invalid));
}

void TestTriggerActionCatalog::mapsStableIdsToTreelandV3_data()
{
    QTest::addColumn<QString>("configuredId");
    QTest::addColumn<int>("wireValue");
    QTest::newRow("notify-is-zero") << QStringLiteral("1") << 0;
    QTest::newRow("workspace-1") << QStringLiteral("2") << 1;
    QTest::newRow("workspace-6") << QStringLiteral("7") << 6;
    QTest::newRow("previous-workspace") << QStringLiteral("8") << 13;
    QTest::newRow("next-workspace") << QStringLiteral("9") << 14;
    QTest::newRow("desktop-is-not-workspace-10") << QStringLiteral("10") << 24;
    QTest::newRow("maximize") << QStringLiteral("11") << 15;
    QTest::newRow("restore") << QStringLiteral("12") << 16;
    QTest::newRow("move") << QStringLiteral("13") << 18;
    QTest::newRow("close") << QStringLiteral("14") << 20;
    QTest::newRow("window-menu") << QStringLiteral("15") << 21;
    QTest::newRow("multitask-open") << QStringLiteral("16") << 25;
    QTest::newRow("multitask-close") << QStringLiteral("17") << 26;
    QTest::newRow("multitask-toggle") << QStringLiteral("18") << 27;
    QTest::newRow("fps") << QStringLiteral("19") << 43;
    QTest::newRow("lock") << QStringLiteral("20") << 44;
    QTest::newRow("shutdown-menu") << QStringLiteral("21") << 45;
    QTest::newRow("task-next") << QStringLiteral("24") << 28;
    QTest::newRow("task-previous") << QStringLiteral("25") << 29;
    QTest::newRow("same-app-next") << QStringLiteral("26") << 30;
    QTest::newRow("same-app-previous") << QStringLiteral("27") << 31;
    QTest::newRow("tile-left") << QStringLiteral("100") << 32;
    QTest::newRow("tile-right") << QStringLiteral("101") << 33;
    QTest::newRow("minimize") << QStringLiteral("110") << 17;
    QTest::newRow("move-to-left-workspace") << QStringLiteral("120") << 22;
    QTest::newRow("move-to-right-workspace") << QStringLiteral("121") << 23;
}

void TestTriggerActionCatalog::mapsStableIdsToTreelandV3()
{
    QFETCH(QString, configuredId);
    QFETCH(int, wireValue);
    const auto action = TriggerActionCatalog::resolve(configuredId);
    QCOMPARE(TriggerActionCatalog::idString(action), configuredId);
    QCOMPARE(TriggerActionCatalog::treelandActionId(action), std::optional<int>(wireValue));
}

QTEST_GUILESS_MAIN(TestTriggerActionCatalog)
#include "tst_triggeractioncatalog.moc"
