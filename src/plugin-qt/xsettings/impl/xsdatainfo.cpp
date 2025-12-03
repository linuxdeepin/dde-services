// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "xsdatainfo.h"

#include "modules/api/utils.h"

#include <QDebug>

XSItemInfo::XSItemInfo(QByteArray &datas)
{
    unMarshalXSItemInfoData(datas);
}

XSItemInfo::XSItemInfo(const QString &prop, const XsValue &xsValue)
{
    if (std::get_if<int>(&xsValue) != nullptr) {
        initXSItemInfoInteger(prop, std::get<int>(xsValue));
    } else if (std::get_if<QString>(&xsValue) != nullptr) {
        initXSItemInfoString(prop, std::get<QString>(xsValue));
    } else if (std::get_if<ColorValueInfo>(&xsValue) != nullptr) {
        initXSItemInfoColor(prop, std::get<ColorValueInfo>(xsValue));
    } else {
        qDebug() << "";
    }
}

void XSItemInfo::unMarshalXSItemInfoData(QByteArray &datas)
{

    readXSItemHeader(datas);

    int tempInt = 0;
    QString tempString = 0;
    ColorValueInfo tempColor;
    switch (head.type) {
    case HeadTypeInteger:
        Utils::readInteger(datas, tempInt);
        value = tempInt;
        break;
    case HeadTypeString:
        int length;
        Utils::readInteger(datas, length);
        Utils::readString(datas, tempString, length);
        Utils::readSkip(datas, Utils::getPad(length));
        value = tempString;
        break;
    case HeadTypeColor:
        for (int i = 0; i < colorSize; i++) {
            Utils::readInteger(datas, tempColor[i]);
        }
        value = tempColor;
        break;
    default:
            // todo

            ;
    }
}

QString xsValueToString(XsValue &value, uint type)
{
    QString retStr;
    int *iVal;
    QString *strVal;
    ColorValueInfo *color;
    switch (type) {
    case HeadTypeInteger:
        iVal = std::get_if<int>(&value);
        if (iVal == nullptr) {
            break;
        }
        retStr = QString::number(*iVal);
        break;
    case HeadTypeString:
        strVal = std::get_if<QString>(&value);
        if (strVal == nullptr) {
            return "nullptr";
        }
        retStr = *strVal;
        break;
    case HeadTypeColor:
        color = std::get_if<ColorValueInfo>(&value);
        if (color == nullptr) {
            return "nullptr";
        }
        retStr = QString::asprintf("%d,%d,%d,%d", (*color)[0], (*color)[1], (*color)[2], (*color)[3]);
        break;
    default:
        retStr = "unknown";
        break;
    }
    return retStr;
}

bool XSItemInfo::marshalXSItemInfoData(QByteArray &datas)
{
    bool ret = false;
    int length = 0;
    QString *str = nullptr;
    QByteArray strArray;
    writeXSItemHeader(datas);
    switch (head.type) {
    case HeadTypeInteger:
        if (std::get_if<int>(&value) == nullptr) {
            break;
        }
        Utils::writeInteger(datas, std::get<int>(value));
        ret = true;
        break;
    case HeadTypeString:
        str = std::get_if<QString>(&value);
        if (str == nullptr) {
            break;
        }
        strArray = str->toUtf8();
        length = strArray.length();
        Utils::writeInteger(datas, length);

        Utils::writeString(datas, strArray);
        Utils::writeSkip(datas, Utils::getPad(length));
        ret = true;
        break;
    case HeadTypeColor:
        ColorValueInfo *colorValue;
        colorValue = std::get_if<ColorValueInfo>(&value);
        if (colorValue == nullptr) {
            break;
        }
        for (int i = 0; i < colorSize; i++) {
            Utils::writeInteger(datas, (*colorValue)[i]);
        }
        ret = true;
        break;
    default:
            // todo
            ;
    }
    return ret;
}

void XSItemInfo::readXSItemHeader(QByteArray &datas)
{
    Utils::readInteger(datas, head.type);
    Utils::readSkip(datas, 1);
    Utils::readInteger(datas, head.length);
    Utils::readString(datas, head.name, static_cast<int>(head.length));
    Utils::readSkip(datas, Utils::getPad(static_cast<int>(head.length)));
    Utils::readInteger(datas, head.lastChangeSerial);
}

void XSItemInfo::writeXSItemHeader(QByteArray &datas)
{
    Utils::writeInteger(datas, head.type);
    Utils::writeSkip(datas, 1);
    Utils::writeInteger(datas, head.length);
    Utils::writeString(datas, head.name.toUtf8());
    Utils::writeSkip(datas, Utils::getPad(static_cast<int>(head.length)));
    Utils::writeInteger(datas, head.lastChangeSerial);
}

QString XSItemInfo::getHeadName()
{
    return head.name;
}

XsValue XSItemInfo::getValue()
{
    return value;
}

void XSItemInfo::modifyProperty(const XsSetting &setting)
{
    head.lastChangeSerial++;
    value = setting.value;
}

void XSItemInfo::initXSItemInfoInteger(const QString &prop, int value)
{
    head.name = prop;
    head.length = prop.toUtf8().length();
    head.lastChangeSerial = 1;
    head.type = HeadTypeInteger;

    this->value = value;
}

void XSItemInfo::initXSItemInfoString(const QString &prop, QString value)
{
    head.name = prop;
    head.length = prop.toUtf8().length();
    head.lastChangeSerial = 1;
    head.type = HeadTypeString;

    this->value = value;
}

void XSItemInfo::initXSItemInfoColor(const QString &prop, ColorValueInfo value)
{
    head.name = prop;
    head.length = prop.toUtf8().length();
    head.lastChangeSerial = 1;
    head.type = HeadTypeColor;

    this->value = value;
}

XSDataInfo::XSDataInfo(QByteArray &datas)
{
    unMarshalSettingData(datas);
}

void XSDataInfo::unMarshalSettingData(QByteArray &datas)
{
    if (datas.isEmpty()) {
        byteOrder = 0;
        serial = 0;
        numSettings = 0;
        return;
    }

    Utils::readInteger(datas, byteOrder);
    Utils::readSkip(datas, 3);
    Utils::readInteger(datas, serial);
    Utils::readInteger(datas, numSettings);
    for (uint32_t i = 0; i < numSettings; i++) {
        QSharedPointer<XSItemInfo> info(new XSItemInfo(datas));
        items.push_back(info);
    }
}

QByteArray XSDataInfo::marshalSettingData()
{
    QByteArray array;

    Utils::writeInteger(array, byteOrder);
    Utils::writeSkip(array, 3);
    Utils::writeInteger(array, serial);
    Utils::writeInteger(array, numSettings);
    for (auto item : items) {
        if (!item->marshalXSItemInfoData(array)) {
            qWarning() << "marshal xsetting info failed";
            return {};
        }
    }
    return array;
}

QString XSDataInfo::listProps()
{
    QStringList content;

    for (int i = 0; i < items.length(); i++) {
        content.append("\"" + items[i]->getHeadName() + "\"");
    }

    return "[" + content.join(",") + "]";
}

QSharedPointer<XSItemInfo> XSDataInfo::getPropItem(QString prop)
{
    for (auto item : items) {
        if (item->getHeadName() == prop) {
            return item;
        }
    }

    return QSharedPointer<XSItemInfo>();
}

void XSDataInfo::inserItem(QSharedPointer<XSItemInfo> itemInfo)
{
    items.push_back(itemInfo);
}

void XSDataInfo::increaseSerial()
{
    serial++;
}

void XSDataInfo::increaseNumSettings()
{
    numSettings++;
}
