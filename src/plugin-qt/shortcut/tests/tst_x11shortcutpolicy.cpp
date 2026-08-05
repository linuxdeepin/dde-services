// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "backend/x11/x11shortcutpolicy.h"

#include <QTest>

class TestX11ShortcutPolicy : public QObject
{
    Q_OBJECT

private slots:
    void legacyGrabResilientShortcuts_data();
    void legacyGrabResilientShortcuts();
};

void TestX11ShortcutPolicy::legacyGrabResilientShortcuts_data()
{
    QTest::addColumn<QString>("shortcutId");
    QTest::addColumn<bool>("expected");

    const QString prefix = QStringLiteral("org.deepin.dde.keybinding.shortcut.app.");
    QTest::newRow("screenshot") << prefix + QStringLiteral("screenshot") << true;
    QTest::newRow("fullscreen-screenshot") << prefix + QStringLiteral("fullscreen-screenshot") << true;
    QTest::newRow("window-screenshot") << prefix + QStringLiteral("window-screenshot") << true;
    QTest::newRow("delay-screenshot") << prefix + QStringLiteral("delay-screenshot") << true;
    QTest::newRow("screenshot-ocr") << prefix + QStringLiteral("screenshot-ocr") << true;
    QTest::newRow("scroll-screenshot") << prefix + QStringLiteral("scroll-screenshot") << true;
    QTest::newRow("screen-recorder") << prefix + QStringLiteral("screen-recorder") << true;
    QTest::newRow("terminal") << prefix + QStringLiteral("terminal") << false;
    QTest::newRow("custom-similar-name")
            << QStringLiteral("org.deepin.dde.keybinding.shortcut.custom.screenshot") << false;
}

void TestX11ShortcutPolicy::legacyGrabResilientShortcuts()
{
    QFETCH(QString, shortcutId);
    QFETCH(bool, expected);
    QCOMPARE(X11ShortcutPolicy::isLegacyGrabResilientShortcut(shortcutId), expected);
}

QTEST_MAIN(TestX11ShortcutPolicy)

#include "tst_x11shortcutpolicy.moc"
