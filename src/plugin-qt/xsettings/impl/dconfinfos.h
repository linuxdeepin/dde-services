// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later
#ifndef DCONFINFOS_H
#define DCONFINFOS_H
#include "modules/common/common.h"

#include <DConfig>

#include <QObject>

class DconfInfo : public QObject
{
    Q_OBJECT
    typedef std::function<XsValue(XsValue &)> ConverFun;

public:
    DconfInfo(QString dconfKey, QString xsKey, DconfValueType dconfType, int8_t xsType = HeadTypeInvalid);
    void setGsToXsFunc(ConverFun func);
    void setXsToGsFunc(ConverFun func);
    XsValue getValue(const DTK_CORE_NAMESPACE::DConfig &dconf);
    bool setValue(DTK_CORE_NAMESPACE::DConfig &dconf, XsValue &value);
    ConverFun getGsToXsFunc(ConverFun func);
    ConverFun getXsToGsFunc(ConverFun func);
    XsValue convertStrToDouble(XsValue &value);
    XsValue convertDoubleToStr(XsValue &value);
    XsValue convertStrToColor(XsValue &value);
    XsValue convertColorToStr(XsValue &value);
    QString getDconfKey();
    QString getXsetKey();
    int8_t getKeySType();
    int8_t getKeyDType();

private:
    QString dconfKey;
    DconfValueType dconfType;
    QString xsKey;
    int8_t xsType;
    ConverFun convertGsToXs;
    ConverFun convertXsToGs;
};

class DconfInfos : public QObject
{
    Q_OBJECT

public:
    DconfInfos();
    QSharedPointer<DconfInfo> getByDconfKey(const QString &dconfKey);
    QSharedPointer<DconfInfo> getByXSKey(const QString &xsettingKey);

private:
    QVector<QSharedPointer<DconfInfo>> dconfArray;
};

#endif // DCONFINFOS_H
