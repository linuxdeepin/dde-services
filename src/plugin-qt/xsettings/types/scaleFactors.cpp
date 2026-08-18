// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "scaleFactors.h"

void registerScaleFactorsMetaType()
{
    qRegisterMetaType<ScaleFactors>("ScaleFactors");
    qDBusRegisterMetaType<ScaleFactors>();
}
