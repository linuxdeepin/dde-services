// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later
#ifndef ARRAYOFCOLOR_H
#define ARRAYOFCOLOR_H

#include <QDBusMetaType>

typedef QList<quint16> ArrayOfColor;

void registerArrayOfColorMetaType();

#endif // ARRAYOFCOLOR_H
