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
    QCOMPARE(TriggerActionCatalog::treelandActionId(TriggerActionId::ShowWindowMenu),
             std::optional<int>(15));
    QVERIFY(!TriggerActionCatalog::treelandActionId(TriggerActionId::ResizeWindow));
    QVERIFY(!TriggerActionCatalog::treelandActionId(TriggerActionId::MoveToRightWorkspace));
}

QTEST_GUILESS_MAIN(TestTriggerActionCatalog)
#include "tst_triggeractioncatalog.moc"
