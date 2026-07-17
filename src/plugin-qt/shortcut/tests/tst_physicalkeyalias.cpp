// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "core/physicalkeyalias.h"

#include <QTest>

class TestPhysicalKeyAlias : public QObject
{
    Q_OBJECT

private slots:
    void canonicalize_data();
    void canonicalize();
    void expandX11Candidates_data();
    void expandX11Candidates();
};

void TestPhysicalKeyAlias::canonicalize_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("expected");

    QTest::newRow("bare-alias") << "KP_Delete" << "Delete";
    QTest::newRow("xkb-alias") << "<Control><Alt>KP_Delete" << "<Control><Alt>Delete";
    QTest::newRow("qt-alias") << "Ctrl+Alt+KP_Delete" << "Ctrl+Alt+Delete";
    QTest::newRow("canonical") << "<Control><Alt>Delete" << "<Control><Alt>Delete";
    QTest::newRow("not-a-key-boundary") << "Ctrl+Alt+MyKP_Delete" << "Ctrl+Alt+MyKP_Delete";
    QTest::newRow("not-trailing") << "KP_Delete+X" << "KP_Delete+X";
}

void TestPhysicalKeyAlias::canonicalize()
{
    QFETCH(QString, input);
    QFETCH(QString, expected);

    QCOMPARE(PhysicalKeyAlias::canonicalize(input), expected);
}

void TestPhysicalKeyAlias::expandX11Candidates_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("canonical");
    QTest::addColumn<QString>("alias");

    QTest::newRow("canonical-input")
            << "<Control><Alt>Delete" << "<Control><Alt>Delete" << "<Control><Alt>KP_Delete";
    QTest::newRow("alias-input")
            << "<Control><Alt>KP_Delete" << "<Control><Alt>Delete" << "<Control><Alt>KP_Delete";
}

void TestPhysicalKeyAlias::expandX11Candidates()
{
    QFETCH(QString, input);
    QFETCH(QString, canonical);
    QFETCH(QString, alias);

    const auto candidates = PhysicalKeyAlias::expandX11Candidates(input);
    QCOMPARE(candidates.size(), 2);
    QCOMPARE(candidates.at(0).hotkey, canonical);
    QCOMPARE(candidates.at(0).requirement,
             PhysicalKeyAlias::X11CandidateRequirement::Required);
    QCOMPARE(candidates.at(1).hotkey, alias);
    QCOMPARE(candidates.at(1).requirement,
             PhysicalKeyAlias::X11CandidateRequirement::IfAvailable);

    const auto unrelated = PhysicalKeyAlias::expandX11Candidates("<Control><Alt>T");
    QCOMPARE(unrelated.size(), 1);
    QCOMPARE(unrelated.constFirst().hotkey, QStringLiteral("<Control><Alt>T"));
    QCOMPARE(unrelated.constFirst().requirement,
             PhysicalKeyAlias::X11CandidateRequirement::Required);
}

QTEST_MAIN(TestPhysicalKeyAlias)

#include "tst_physicalkeyalias.moc"
