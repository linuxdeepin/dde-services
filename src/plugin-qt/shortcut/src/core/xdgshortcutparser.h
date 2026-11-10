// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "portalshortcuttypes.h"

#include <optional>

class XdgShortcutParser
{
public:
    static constexpr qsizetype MaximumTriggerLength = 128;

    static std::optional<QString> normalize(const QString &trigger,
                                            QString *errorMessage = nullptr);
    static std::optional<PortalTriggerList> normalizeBatch(const PortalTriggerList &requested,
                                                           QString *errorMessage = nullptr);
};
