// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later
#ifndef UTILS_H
#define UTILS_H

#include "../common/common.h"

#include <QByteArray>
#include <QDataStream>
#include <QString>
#include <QtEndian>

class Utils
{
public:
    Utils();

    template<typename Value>
    static bool readInteger(QByteArray &array, Value &value)
    {
        int size = sizeof(Value);
        if (array.size() < size) {
            return false;
        }
        // 使用小端格式
        value = qFromLittleEndian<Value>(array.data());
        array = array.remove(0, size);

        return true;
    }

    template<typename Value>
    static bool writeInteger(QByteArray &array, const Value &value)
    {
        int size = sizeof(Value);
        char tmp[size];
        qToLittleEndian<Value>(value, tmp);
        array.append(tmp, size);
        return true;
    }

    static bool readString(QByteArray &array, QString &value, int length);
    static bool readSkip(QByteArray &array, int length);
    static bool writeString(QByteArray &array, const QByteArray &value);
    static bool writeSkip(QByteArray &array, int length);
    static int getPad(int e);
    static QString GetUserConfigDir();
    static QString getUserHomeDir();
    static bool hasXsValue(const XsValue &value);
};

#endif // UTILS_H
