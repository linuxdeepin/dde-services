// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later
#ifndef SCALEFACTORS_H
#define SCALEFACTORS_H

#include <QDBusMetaType>
#include <QMap>

typedef QMap<QString, double> ScaleFactors;

void registerScaleFactorsMetaType();

#endif // SCALEFACTORS_H
