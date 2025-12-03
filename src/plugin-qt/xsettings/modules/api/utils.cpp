// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "utils.h"

#include <QDir>
#include <QStandardPaths>

#include <pwd.h>
#include <unistd.h>

Utils::Utils() { }

bool Utils::readString(QByteArray &array, QString &value, int length)
{
    if (array.length() < length) {
        return false;
    }

    value = array.left(length);

    array.remove(0, length);

    return true;
}

bool Utils::readSkip(QByteArray &array, int length)
{
    if (array.length() < length) {
        return false;
    }

    array.remove(0, length);

    return true;
}

bool Utils::writeString(QByteArray &array, const QByteArray &value)
{
    array.append(value);
    return true;
}

bool Utils::writeSkip(QByteArray &array, int length)
{
    for (int i = 0; i < length; i++) {
        array.push_back('\0');
    }
    return true;
}

int Utils::getPad(int e)
{
    return (4 - (e % 4)) % 4;
}

QString Utils::GetUserConfigDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
}

QString Utils::getUserHomeDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
}

bool Utils::hasXsValue(const XsValue &value)
{
    if (!std::get_if<int>(&value) && !std::get_if<ColorValueInfo>(&value) && !std::get_if<double>(&value) && !std::get_if<QString>(&value)) {
        return false;
    }

    return true;
}
