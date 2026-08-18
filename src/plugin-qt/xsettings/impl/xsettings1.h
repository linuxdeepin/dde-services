// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later
#ifndef XSETTINGS_H
#define XSETTINGS_H

#include "types/arrayOfColor.h"
#include "types/scaleFactors.h"
#include "xsettingsmanager.h"

#include <QObject>
#include <QString>

class XSettings1 : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.deepin.dde.XSettings1")
public:
    XSettings1(QObject *parent = nullptr);
    ~XSettings1();

public Q_SLOTS: // METHODS
    ArrayOfColor GetColor(const QString &prop);
    qint32 GetInteger(const QString &prop);
    double GetScaleFactor();
    ScaleFactors GetScreenScaleFactors();
    QString GetString(const QString &prop);
    QString ListProps();
    void SetColor(const QString &prop, const ArrayOfColor &v);
    void SetInteger(const QString &prop, const qint32 &v);
    void SetScaleFactor(const double &scale);
    void SetScreenScaleFactors(const ScaleFactors &factors);
    void SetString(const QString &prop, const QString &v);

Q_SIGNALS: // SIGNALS
    void SetScaleFactorDone();
    void SetScaleFactorStarted();

private:
    QSharedPointer<XSettingsManager> xSettingsmanger;
};

#endif
