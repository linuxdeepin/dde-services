// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "xsettings1.h"

XSettings1::XSettings1(QObject *parent)
    : QObject(parent)
    , xSettingsmanger(new XSettingsManager(this))
{
    registerScaleFactorsMetaType();
    registerArrayOfColorMetaType();
    connect(xSettingsmanger.get(), &XSettingsManager::SetScaleFactorStarted, this, &XSettings1::SetScaleFactorStarted);
    connect(xSettingsmanger.get(), &XSettingsManager::SetScaleFactorDone, this, &XSettings1::SetScaleFactorDone);
}

XSettings1::~XSettings1() { }

QList<quint16> XSettings1::GetColor(const QString &prop)
{
    return xSettingsmanger->getColor(prop);
}

qint32 XSettings1::GetInteger(const QString &prop)
{
    return xSettingsmanger->getInteger(prop);
}

double XSettings1::GetScaleFactor()
{
    return xSettingsmanger->getScaleFactor();
}

ScaleFactors XSettings1::GetScreenScaleFactors()
{
    return xSettingsmanger->getScreenScaleFactors();
}

QString XSettings1::GetString(const QString &prop)
{
    return xSettingsmanger->getString(prop);
}

QString XSettings1::ListProps()
{
    return xSettingsmanger->listProps();
}

void XSettings1::SetColor(const QString &prop, const QList<quint16> &v)
{
    xSettingsmanger->setColor(prop, v);
}

void XSettings1::SetInteger(const QString &prop, const qint32 &v)
{
    xSettingsmanger->setInteger(prop, v);
}

void XSettings1::SetScaleFactor(const double &scale)
{
    ScaleFactors factors;
    factors["ALL"] = scale;
    xSettingsmanger->setScreenScaleFactors(factors, true);
}

void XSettings1::SetScreenScaleFactors(const ScaleFactors &factors)
{
    xSettingsmanger->setScreenScaleFactors(factors, true);
}

void XSettings1::SetString(const QString &prop, const QString &v)
{
    xSettingsmanger->setString(prop, v);
}
