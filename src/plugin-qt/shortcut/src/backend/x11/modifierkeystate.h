// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QList>
#include <QSet>
#include <QtGlobal>

namespace ModifierKeyTimeline {

struct Transition
{
    quint8 keycode;
    bool pressed;
};

// Reconstruct the modifier state before each press from a snapshot taken
// after all transitions in the same event batch.
QList<QSet<quint8>> pressedModifiersBeforePresses(
        const QList<Transition> &transitions,
        const QSet<quint8> &pressedAfterTransitions);

} // namespace ModifierKeyTimeline

class ModifierKeyState
{
public:
    void reset();
    // The snapshot must describe the same event boundary as the state machine.
    bool reconcileAtEventBoundary(const QSet<quint8> &pressedModifiers);
    void press(quint8 keycode);
    bool release(quint8 keycode);
    void notifyNonModifierActivity();

private:
    QSet<quint8> m_pressedModifiers;
    bool m_nonModifierActivity = false;
    bool m_multipleModifiersUsed = false;
};
