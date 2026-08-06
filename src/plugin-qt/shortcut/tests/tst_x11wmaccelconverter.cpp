// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "backend/x11/x11wmaccelconverter.h"

#include <QTest>

class TestX11WmAccelConverter : public QObject
{
    Q_OBJECT

private slots:
    void encodesReverseSameAppShortcutForKWin();
    void encodesLegacyTildeRepresentationsForKWin();
    void preservesOtherWindowManagerShortcuts();
};

void TestX11WmAccelConverter::encodesReverseSameAppShortcutForKWin()
{
    QCOMPARE(X11WmAccelConverter::toDaemonAccel(
                     QStringLiteral("switchGroupBackward"),
                     QStringLiteral("Alt+Shift+`")),
             QStringLiteral("<Shift><Alt>asciitilde"));
}

void TestX11WmAccelConverter::encodesLegacyTildeRepresentationsForKWin()
{
    QCOMPARE(X11WmAccelConverter::toDaemonAccel(
                     QStringLiteral("switchGroupBackward"),
                     QStringLiteral("Alt+Shift+~")),
             QStringLiteral("<Shift><Alt>asciitilde"));
    QCOMPARE(X11WmAccelConverter::toDaemonAccel(
                     QStringLiteral("switchGroupBackward"),
                     QStringLiteral("Alt+~")),
             QStringLiteral("<Shift><Alt>asciitilde"));
}

void TestX11WmAccelConverter::preservesOtherWindowManagerShortcuts()
{
    QCOMPARE(X11WmAccelConverter::toDaemonAccel(
                     QStringLiteral("switchGroup"),
                     QStringLiteral("Alt+`")),
             QStringLiteral("<Alt>`"));
    QCOMPARE(X11WmAccelConverter::toDaemonAccel(
                     QStringLiteral("switchApplicationsBackward"),
                     QStringLiteral("Alt+Shift+Tab")),
             QStringLiteral("<Shift><Alt>Tab"));
    QCOMPARE(X11WmAccelConverter::toDaemonAccel(
                     QStringLiteral("switchGroupBackward"),
                     QStringLiteral("Alt+`")),
             QStringLiteral("<Alt>`"));
}

QTEST_GUILESS_MAIN(TestX11WmAccelConverter)

#include "tst_x11wmaccelconverter.moc"
