// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "core/crosschannelactivationguard.h"

#include <QtTest>

class TestCrossChannelActivationGuard : public QObject
{
    Q_OBJECT

private slots:
    void suppressesOnlyCorrelatedOtherChannelActivation();
    void acceptsSameChannelRapidActivations();
    void suppressedActivationDoesNotReplaceAcceptedSource();
    void acceptsOtherChannelOutsideCorrelationWindow();
};

void TestCrossChannelActivationGuard::suppressesOnlyCorrelatedOtherChannelActivation()
{
    CrossChannelActivationGuard guard;
    const QString id = QStringLiteral("micmute");

    QVERIFY(guard.shouldAccept(id, CrossChannelActivationGuard::Channel::Symbolic, 1000));
    QVERIFY(!guard.shouldAccept(id, CrossChannelActivationGuard::Channel::Raw, 1010));
}

void TestCrossChannelActivationGuard::acceptsSameChannelRapidActivations()
{
    CrossChannelActivationGuard guard;
    const QString id = QStringLiteral("micmute");

    QVERIFY(guard.shouldAccept(id, CrossChannelActivationGuard::Channel::Raw, 1000));
    QVERIFY(guard.shouldAccept(id, CrossChannelActivationGuard::Channel::Raw, 1010));
}

void TestCrossChannelActivationGuard::suppressedActivationDoesNotReplaceAcceptedSource()
{
    CrossChannelActivationGuard guard;
    const QString id = QStringLiteral("micmute");

    QVERIFY(guard.shouldAccept(id, CrossChannelActivationGuard::Channel::Symbolic, 1000));
    QVERIFY(!guard.shouldAccept(id, CrossChannelActivationGuard::Channel::Raw, 1010));
    QVERIFY(guard.shouldAccept(id, CrossChannelActivationGuard::Channel::Symbolic, 1020));
    QVERIFY(!guard.shouldAccept(id, CrossChannelActivationGuard::Channel::Raw, 1030));
}

void TestCrossChannelActivationGuard::acceptsOtherChannelOutsideCorrelationWindow()
{
    CrossChannelActivationGuard guard;
    const QString id = QStringLiteral("micmute");

    QVERIFY(guard.shouldAccept(id, CrossChannelActivationGuard::Channel::Symbolic, 1000));
    QVERIFY(guard.shouldAccept(id, CrossChannelActivationGuard::Channel::Raw,
                               1000 + CrossChannelActivationGuard::CorrelationWindowMs + 1));
}

QTEST_GUILESS_MAIN(TestCrossChannelActivationGuard)

#include "tst_crosschannelactivationguard.moc"
