// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "backend/x11/modifierkeystate.h"

#include <QTest>

class TestModifierKeyState : public QObject
{
    Q_OBJECT

private slots:
    void standaloneModifier();
    void modifierCombination();
    void multipleModifiers();
    void queuedMultipleModifiersWithEmptySnapshot();
    void recoverMissingReleaseBeforeDifferentModifier();
    void recoverMissingReleaseBeforeSameModifier();
};

void TestModifierKeyState::standaloneModifier()
{
    ModifierKeyState state;
    state.press(133);

    QVERIFY(state.release(133));
}

void TestModifierKeyState::modifierCombination()
{
    ModifierKeyState state;
    state.press(133);
    state.notifyNonModifierActivity();

    QVERIFY(!state.release(133));
}

void TestModifierKeyState::multipleModifiers()
{
    ModifierKeyState state;
    state.press(37);
    QVERIFY(!state.reconcileAtEventBoundary({37}));
    state.press(133);

    QVERIFY(!state.release(133));
    QVERIFY(!state.release(37));
}

void TestModifierKeyState::queuedMultipleModifiersWithEmptySnapshot()
{
    const QList<ModifierKeyTimeline::Transition> transitions = {
        {37, true},
        {133, true},
        {37, false},
        {133, false},
    };
    const QList<QSet<quint8>> statesBeforePresses =
            ModifierKeyTimeline::pressedModifiersBeforePresses(transitions, {});

    QCOMPARE(statesBeforePresses.size(), 2);
    QVERIFY(statesBeforePresses.at(0).isEmpty());
    QVERIFY(statesBeforePresses.at(1) == QSet<quint8>{37});

    ModifierKeyState state;
    QVERIFY(!state.reconcileAtEventBoundary(statesBeforePresses.at(0)));
    state.press(37);
    QVERIFY(!state.reconcileAtEventBoundary(statesBeforePresses.at(1)));
    state.press(133);

    QVERIFY(!state.release(37));
    QVERIFY(!state.release(133));
    QVERIFY(!state.reconcileAtEventBoundary({}));
}

void TestModifierKeyState::recoverMissingReleaseBeforeDifferentModifier()
{
    const QList<ModifierKeyTimeline::Transition> transitions = {{133, true}};
    const QList<QSet<quint8>> statesBeforePresses =
            ModifierKeyTimeline::pressedModifiersBeforePresses(transitions, {133});

    ModifierKeyState state;
    state.press(37);
    state.notifyNonModifierActivity();

    QVERIFY(state.reconcileAtEventBoundary(statesBeforePresses.constFirst()));
    state.press(133);
    QVERIFY(state.release(133));
}

void TestModifierKeyState::recoverMissingReleaseBeforeSameModifier()
{
    const QList<ModifierKeyTimeline::Transition> transitions = {{133, true}};
    const QList<QSet<quint8>> statesBeforePresses =
            ModifierKeyTimeline::pressedModifiersBeforePresses(transitions, {133});

    ModifierKeyState state;
    state.press(133);
    state.notifyNonModifierActivity();

    QVERIFY(state.reconcileAtEventBoundary(statesBeforePresses.constFirst()));
    state.press(133);
    QVERIFY(state.release(133));
}

QTEST_MAIN(TestModifierKeyState)

#include "tst_modifierkeystate.moc"
