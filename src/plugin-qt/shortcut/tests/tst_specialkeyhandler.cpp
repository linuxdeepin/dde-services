// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "backend/specialkeyhandler.h"

#include <QDBusConnection>
#include <QSignalSpy>
#include <QtTest>

class TestSpecialKeyHandler : public QObject
{
    Q_OBJECT

private slots:
    void registrationAndPressActivation();
};

void TestSpecialKeyHandler::registrationAndPressActivation()
{
    if (!QDBusConnection::systemBus().isConnected())
        QSKIP("A system bus connection is required");

    SpecialKeyHandler handler;
    KeyConfig config;
    config.subPath = QStringLiteral("hardware.cyclewindows");
    config.hotkeys = {QStringLiteral("154")};
    config.keyEventFlags = KeyEventFlag::Press;

    QVERIFY(handler.registerKey(config));
    QCOMPARE(handler.lookupConflict(154), config.getId());

    QSignalSpy activationSpy(&handler, &SpecialKeyHandler::keyActivated);
    handler.setSessionActive(true);
    QVERIFY(QMetaObject::invokeMethod(&handler, "onKeyEvent", Qt::DirectConnection,
                                      Q_ARG(uint, 154U), Q_ARG(bool, true),
                                      Q_ARG(bool, false), Q_ARG(bool, false),
                                      Q_ARG(bool, false), Q_ARG(bool, false)));
    QCOMPARE(activationSpy.count(), 1);
    QVERIFY(QMetaObject::invokeMethod(&handler, "onKeyEvent", Qt::DirectConnection,
                                      Q_ARG(uint, 154U), Q_ARG(bool, false),
                                      Q_ARG(bool, false), Q_ARG(bool, false),
                                      Q_ARG(bool, false), Q_ARG(bool, false)));
    QCOMPARE(activationSpy.count(), 1);
    QCOMPARE(activationSpy.first().first().toString(), config.getId());
}

QTEST_GUILESS_MAIN(TestSpecialKeyHandler)

#include "tst_specialkeyhandler.moc"
