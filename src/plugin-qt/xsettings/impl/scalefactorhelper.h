// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later
#ifndef SCALEFACTORHELPER_H
#define SCALEFACTORHELPER_H

#include <QDBusInterface>
#include <QMap>
#include <QObject>
#include <QSharedPointer>

#include <functional>

class ScaleFactorHelper : public QObject
{
    typedef std::function<void(QMap<QString, float>)> CHANGECALLBACK;

public:
    ScaleFactorHelper();

private:
    QSharedPointer<QDBusInterface> sysDisplay;
    CHANGECALLBACK callback;
};

#endif // SCALEFACTORHELPER_H
