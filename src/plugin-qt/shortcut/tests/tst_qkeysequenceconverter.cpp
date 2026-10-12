// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "core/qkeysequenceconverter.h"

#include <QTest>

class TestQKeySequenceConverter : public QObject
{
    Q_OBJECT

private slots:
    void symbolKeysUseXkbNames_data();
    void symbolKeysUseXkbNames();
    void multimediaKeysAreRecognized_data();
    void multimediaKeysAreRecognized();
};

void TestQKeySequenceConverter::symbolKeysUseXkbNames_data()
{
    QTest::addColumn<QString>("qtSequence");
    QTest::addColumn<QString>("xkbSequence");

    QTest::newRow("zoom-in") << QStringLiteral("Meta++")
                              << QStringLiteral("<Super>plus");
    QTest::newRow("zoom-out") << QStringLiteral("Meta+-")
                               << QStringLiteral("<Super>minus");
    QTest::newRow("equal") << QStringLiteral("Meta+=")
                            << QStringLiteral("<Super>equal");
    QTest::newRow("display") << QStringLiteral("Display")
                              << QStringLiteral("XF86Display");
    QTest::newRow("webcam") << QStringLiteral("WebCam")
                              << QStringLiteral("XF86WebCam");
    QTest::newRow("sleep") << QStringLiteral("Sleep")
                            << QStringLiteral("XF86Sleep");
    QTest::newRow("suspend") << QStringLiteral("Suspend")
                              << QStringLiteral("XF86Suspend");
}

void TestQKeySequenceConverter::symbolKeysUseXkbNames()
{
    QFETCH(QString, qtSequence);
    QFETCH(QString, xkbSequence);

    QCOMPARE(QKeySequenceConverter::qKeySequenceToXkb(qtSequence), xkbSequence);
    QCOMPARE(QKeySequenceConverter::xkbToQKeySequence(xkbSequence), qtSequence);
}

void TestQKeySequenceConverter::multimediaKeysAreRecognized_data()
{
    QTest::addColumn<int>("qtKey");
    QTest::addColumn<bool>("expected");

    // Regression: Qt::Key_Sleep (0x01020004) sits outside the classic
    // multimedia ranges but hardware sleep keys (XF86Sleep) must validate,
    // otherwise KeybindingManager::registerShortcut() drops the binding.
    QTest::newRow("sleep") << static_cast<int>(Qt::Key_Sleep) << true;
    QTest::newRow("suspend") << static_cast<int>(Qt::Key_Suspend) << true;
    QTest::newRow("launch-mail") << static_cast<int>(Qt::Key_LaunchMail) << true;
    QTest::newRow("mic-mute") << static_cast<int>(Qt::Key_MicMute) << true;
    QTest::newRow("plain-a-is-not-multimedia") << static_cast<int>(Qt::Key_A) << false;
}

void TestQKeySequenceConverter::multimediaKeysAreRecognized()
{
    QFETCH(int, qtKey);
    QFETCH(bool, expected);

    QCOMPARE(QKeySequenceConverter::isMultimediaKey(static_cast<Qt::Key>(qtKey)), expected);
}

QTEST_GUILESS_MAIN(TestQKeySequenceConverter)

#include "tst_qkeysequenceconverter.moc"
