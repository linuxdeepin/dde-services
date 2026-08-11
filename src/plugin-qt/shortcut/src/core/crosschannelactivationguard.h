// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QHash>
#include <QString>

class CrossChannelActivationGuard
{
public:
    enum class Channel {
        Symbolic,
        Raw,
    };

    // The symbolic and raw backends do not expose a common event identifier.
    // Correlate only opposite-channel activations; rapid repeats reported by
    // one channel remain independent user actions.
    static constexpr qint64 CorrelationWindowMs = 200;

    bool shouldAccept(const QString &shortcutId, Channel channel, qint64 timestampMs);
    void clear(const QString &shortcutId);
    void clear();

private:
    struct Activation {
        Channel channel;
        qint64 timestampMs;
    };

    QHash<QString, Activation> m_lastAcceptedActivations;
};
