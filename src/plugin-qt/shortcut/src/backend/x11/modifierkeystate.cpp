// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "modifierkeystate.h"

#include <algorithm>

QList<QSet<quint8>> ModifierKeyTimeline::pressedModifiersBeforePresses(
        const QList<Transition> &transitions,
        const QSet<quint8> &pressedAfterTransitions)
{
    QSet<quint8> pressedModifiers = pressedAfterTransitions;
    QList<QSet<quint8>> statesBeforePresses;

    for (auto it = transitions.crbegin(); it != transitions.crend(); ++it) {
        if (it->pressed) {
            pressedModifiers.remove(it->keycode);
            statesBeforePresses.append(pressedModifiers);
        } else {
            pressedModifiers.insert(it->keycode);
        }
    }
    std::reverse(statesBeforePresses.begin(), statesBeforePresses.end());
    return statesBeforePresses;
}

void ModifierKeyState::reset()
{
    m_pressedModifiers.clear();
    m_nonModifierActivity = false;
    m_multipleModifiersUsed = false;
}

bool ModifierKeyState::reconcileAtEventBoundary(const QSet<quint8> &pressedModifiers)
{
    if (m_pressedModifiers == pressedModifiers)
        return false;

    m_pressedModifiers = pressedModifiers;
    m_nonModifierActivity = false;
    m_multipleModifiersUsed = m_pressedModifiers.size() > 1;
    return true;
}

void ModifierKeyState::press(quint8 keycode)
{
    if (!m_pressedModifiers.isEmpty() && !m_pressedModifiers.contains(keycode))
        m_multipleModifiersUsed = true;
    m_pressedModifiers.insert(keycode);
}

bool ModifierKeyState::release(quint8 keycode)
{
    const bool standalone = m_pressedModifiers.contains(keycode)
            && m_pressedModifiers.size() == 1
            && !m_nonModifierActivity
            && !m_multipleModifiersUsed;
    m_pressedModifiers.remove(keycode);
    if (m_pressedModifiers.isEmpty()) {
        m_nonModifierActivity = false;
        m_multipleModifiersUsed = false;
    }
    return standalone;
}

void ModifierKeyState::notifyNonModifierActivity()
{
    if (!m_pressedModifiers.isEmpty())
        m_nonModifierActivity = true;
}
