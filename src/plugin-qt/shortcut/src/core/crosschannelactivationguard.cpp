// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "crosschannelactivationguard.h"

bool CrossChannelActivationGuard::shouldAccept(const QString &shortcutId,
                                               Channel channel,
                                               qint64 timestampMs)
{
    const auto previous = m_lastAcceptedActivations.constFind(shortcutId);
    if (previous != m_lastAcceptedActivations.constEnd()
            && previous->channel != channel
            && timestampMs >= previous->timestampMs
            && timestampMs - previous->timestampMs <= CorrelationWindowMs) {
        return false;
    }

    m_lastAcceptedActivations.insert(shortcutId, {channel, timestampMs});
    return true;
}

void CrossChannelActivationGuard::clear(const QString &shortcutId)
{
    m_lastAcceptedActivations.remove(shortcutId);
}

void CrossChannelActivationGuard::clear()
{
    m_lastAcceptedActivations.clear();
}
