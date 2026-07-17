// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "core/gestureactioncatalog.h"

#include <QTest>

class TestGestureActionCatalog : public QObject
{
    Q_OBJECT

private slots:
    void swipeActionsFollowFingerClassification();
    void holdsOnlyExposeServiceActions();
    void routesCommonTreelandActionsThroughNotify();
    void preservesHideDesktopConfigWhenRegisteringTreeland();
    void hidesUnsupportedTreelandActions();
    void keepsX11OnlyRestrictions();
};

void TestGestureActionCatalog::swipeActionsFollowFingerClassification()
{
    const QList<GestureActionId> x11ThreeFinger = {
        GestureActionId::MaximizeWindow,
        GestureActionId::RestoreWindow,
        GestureActionId::SplitWindowLeft,
        GestureActionId::SplitWindowRight,
        GestureActionId::Disable,
    };
    const QList<GestureActionId> treelandThreeFinger = {
        GestureActionId::MaximizeWindow,
        GestureActionId::RestoreWindow,
        GestureActionId::Disable,
    };
    const QList<GestureActionId> x11FourFinger = {
        GestureActionId::ShowMultitask,
        GestureActionId::HideMultitask,
        GestureActionId::PreviousWorkspace,
        GestureActionId::NextWorkspace,
        GestureActionId::ShowDesktop,
        GestureActionId::HideDesktop,
        GestureActionId::Disable,
    };
    const QList<GestureActionId> treelandFourFinger = {
        GestureActionId::ShowMultitask,
        GestureActionId::HideMultitask,
        GestureActionId::PreviousWorkspace,
        GestureActionId::NextWorkspace,
        GestureActionId::ShowDesktop,
        GestureActionId::HideDesktop,
        GestureActionId::Disable,
    };

    const auto verifyActions = [](GestureBackend backend, int fingerCount,
                                  const QList<GestureActionId> &expected) {
        GestureConfig swipe;
        swipe.gestureType = int(GestureType::Swipe);
        swipe.fingerCount = fingerCount;
        const QList<GestureActionMetadata> actions =
                GestureActionCatalog::actionsFor(swipe, backend);
        QCOMPARE(actions.size(), expected.size());
        for (int i = 0; i < expected.size(); ++i)
            QCOMPARE(actions.at(i).id, expected.at(i));
    };

    verifyActions(GestureBackend::X11, 3, x11ThreeFinger);
    verifyActions(GestureBackend::Treeland, 3, treelandThreeFinger);
    verifyActions(GestureBackend::X11, 4, x11FourFinger);
    verifyActions(GestureBackend::Treeland, 4, treelandFourFinger);
}

void TestGestureActionCatalog::holdsOnlyExposeServiceActions()
{
    const QList<GestureActionId> expected = {
        GestureActionId::ToggleGrandSearch,
        GestureActionId::ToggleLauncher,
        GestureActionId::ToggleClipboard,
        GestureActionId::ToggleNotifications,
        GestureActionId::Disable,
    };

    for (GestureBackend backend : {GestureBackend::X11, GestureBackend::Treeland}) {
        for (int fingerCount : {3, 4}) {
            GestureConfig hold;
            hold.gestureType = int(GestureType::Hold);
            hold.fingerCount = fingerCount;

            const QList<GestureActionMetadata> actions =
                    GestureActionCatalog::actionsFor(hold, backend);
            QCOMPARE(actions.size(), expected.size());
            for (int i = 0; i < expected.size(); ++i)
                QCOMPARE(actions.at(i).id, expected.at(i));
        }
    }
}

void TestGestureActionCatalog::routesCommonTreelandActionsThroughNotify()
{
    GestureConfig hold;
    hold.gestureType = int(GestureType::Hold);
    hold.fingerCount = 3;

    for (GestureActionId actionId : {
                 GestureActionId::ToggleGrandSearch,
                 GestureActionId::ToggleLauncher,
                 GestureActionId::ToggleClipboard,
                 GestureActionId::ToggleNotifications,
         }) {
        QVERIFY(GestureActionCatalog::find(hold, actionId, GestureBackend::Treeland));
        QCOMPARE(GestureActionCatalog::targetFor(actionId, GestureBackend::Treeland),
                 GestureActionTarget::Service);
        QCOMPARE(GestureActionCatalog::registrationActionId(actionId, GestureBackend::Treeland),
                 GestureActionId::Notify);
    }

    QCOMPARE(GestureActionCatalog::targetFor(
                     GestureActionId::LockScreen, GestureBackend::Treeland),
             GestureActionTarget::Backend);
    QCOMPARE(GestureActionCatalog::registrationActionId(
                     GestureActionId::LockScreen, GestureBackend::Treeland),
             GestureActionId::LockScreen);
    QCOMPARE(GestureActionCatalog::targetFor(
                     GestureActionId::LockScreen, GestureBackend::X11),
             GestureActionTarget::Service);
}

void TestGestureActionCatalog::preservesHideDesktopConfigWhenRegisteringTreeland()
{
    GestureConfig fourFingerSwipe;
    fourFingerSwipe.gestureType = int(GestureType::Swipe);
    fourFingerSwipe.fingerCount = 4;

    QVERIFY(GestureActionCatalog::find(
            fourFingerSwipe, GestureActionId::HideDesktop, GestureBackend::Treeland));
    QCOMPARE(GestureActionCatalog::idString(GestureActionId::HideDesktop),
             QStringLiteral("102"));
    QCOMPARE(GestureActionCatalog::targetFor(
                     GestureActionId::HideDesktop, GestureBackend::Treeland),
             GestureActionTarget::Backend);
    QCOMPARE(GestureActionCatalog::registrationActionId(
                     GestureActionId::HideDesktop, GestureBackend::Treeland),
             GestureActionId::ShowDesktop);
}

void TestGestureActionCatalog::hidesUnsupportedTreelandActions()
{
    GestureConfig threeFingerSwipe;
    threeFingerSwipe.gestureType = int(GestureType::Swipe);
    threeFingerSwipe.fingerCount = 3;

    QVERIFY(!GestureActionCatalog::find(
            threeFingerSwipe, GestureActionId::SplitWindowLeft, GestureBackend::Treeland));
    QVERIFY(!GestureActionCatalog::find(
            threeFingerSwipe, GestureActionId::SplitWindowRight, GestureBackend::Treeland));
    QVERIFY(!GestureActionCatalog::find(
            threeFingerSwipe, GestureActionId::ToggleLauncher, GestureBackend::Treeland));

}

void TestGestureActionCatalog::keepsX11OnlyRestrictions()
{
    GestureConfig threeFingerHold;
    threeFingerHold.gestureType = int(GestureType::Hold);
    threeFingerHold.fingerCount = 3;

    QCOMPARE(GestureActionCatalog::resolveActionId(
                     threeFingerHold,
                     GestureActionCatalog::idString(GestureActionId::SplitWindowLeft),
                     GestureBackend::X11),
             GestureActionId::Invalid);

    GestureConfig fourFingerSwipe;
    fourFingerSwipe.gestureType = int(GestureType::Swipe);
    fourFingerSwipe.fingerCount = 4;

    QCOMPARE(GestureActionCatalog::resolveActionId(
                     fourFingerSwipe,
                     GestureActionCatalog::idString(GestureActionId::LockScreen),
                     GestureBackend::X11),
             GestureActionId::Invalid);
}

QTEST_GUILESS_MAIN(TestGestureActionCatalog)
#include "tst_gestureactioncatalog.moc"
