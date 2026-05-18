// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "screencontroller.h"

void ScreenController::setAllModes(Mode m)
{
    for (int i = 0; i < outputCount(); ++i)
        setMode(i, m);
}

bool ScreenController::isAllOff() const
{
    for (int i = 0; i < outputCount(); ++i) {
        if (mode(i) != Off)
            return false;
    }
    return outputCount() > 0;
}
