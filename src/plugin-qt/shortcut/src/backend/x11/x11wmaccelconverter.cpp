// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "x11wmaccelconverter.h"

#include "core/qkeysequenceconverter.h"

#include <QKeySequence>

namespace X11WmAccelConverter {

QString toDaemonAccel(const QString &wmShortcutId, const QString &hotkey)
{
    QString accel = QKeySequenceConverter::qKeySequenceToXkb(hotkey);
    if (wmShortcutId != QLatin1String("switchGroupBackward"))
        return accel;

    const QKeySequence sequence = QKeySequence::fromString(hotkey, QKeySequence::PortableText);
    if (sequence.isEmpty() || sequence.count() != 1)
        return accel;

    const QKeyCombination combination = sequence[0];
    const bool shiftedGrave = combination.key() == Qt::Key_QuoteLeft
            && combination.keyboardModifiers().testFlag(Qt::ShiftModifier);
    const bool tilde = combination.key() == Qt::Key_AsciiTilde;
    if (!shiftedGrave && !tilde)
        return accel;

    const QString keyName = QKeySequence(combination.key()).toString(QKeySequence::PortableText);
    if (!keyName.isEmpty() && accel.endsWith(keyName)) {
        accel.chop(keyName.size());
        accel += QStringLiteral("asciitilde");
    }
    if (!accel.contains(QLatin1String("<Shift>")))
        accel.prepend(QLatin1String("<Shift>"));
    return accel;
}

}
