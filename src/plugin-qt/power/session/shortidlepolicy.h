// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QString>
#include <QStringList>

// Pure short-idle application-blocking decision, extracted from
// PowerManager::canEnterShortIdle() so the blacklist / third-party matching is
// unit-testable. `desktop` and both lists hold desktop-file basenames.

enum class ShortIdleBlock : quint8 {
    Allowed = 0,  // not blocked
    Blacklist = 1,  // explicitly blacklisted via DConfig
    ThirdParty = 2, // not a system app and not a deepin/dde/uos brand app
};

inline ShortIdleBlock shortIdleBlockReason(const QString &desktop,
                                           const QStringList &blacklistApps,
                                           const QStringList &systemApps)
{
    if (blacklistApps.contains(desktop))
        return ShortIdleBlock::Blacklist;

    const QString lower = desktop.toLower();
    if (!systemApps.contains(desktop)
        && !lower.contains(QStringLiteral("deepin"))
        && !lower.contains(QStringLiteral("dde"))
        && !lower.contains(QStringLiteral("uos")))
        return ShortIdleBlock::ThirdParty;

    return ShortIdleBlock::Allowed;
}
