// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later
#ifndef COMMON_H
#define COMMON_H
#include <QObject>

#include <variant>

#define dcKeyScaleFactor "scale-factor"
#define dcKeyWindowScale "window-scale"
#define dcKeyXftDpi "xft-dpi"
#define dcKeyGtkCursorThemeSize "gtk-cursor-theme-size"
#define cKeyIndividualScaling "individual-scaling"
#define qtThemeSection "Theme"
#define qtThemeKeyScreenScaleFactors "ScreenScaleFactors"
#define qtThemeKeyScaleFactor "ScaleFactor"
#define qtThemeKeyScaleLogicalDpi "ScaleLogicalDpi"

#define WRAPSCHEMA "com.deepin.wrap.gnome.desktop.interface"

struct ItemHeader
{
    uint8_t type;
    uint16_t length;
    QString name;
    uint32_t lastChangeSerial;
};

// struct ColorValueInfo{
//   uint16_t  red;
//   uint16_t  green;
//   uint16_t  blue;
//   uint16_t  alpha;
// };

constexpr size_t colorSize = 4;
using ColorValueInfo = std::array<uint16_t, colorSize>;
using XsValue = std::variant<int, double, QString, ColorValueInfo>;

enum DconfValueType { typeOfBool = 1, typeOfInt, typeOfString, typeOfDoublue };

struct XsSetting
{
    int8_t type;
    QString prop;
    XsValue value;
};

const int8_t HeadTypeInvalid = -1;
const int8_t HeadTypeInteger = 0;
const int8_t HeadTypeString = 1;
const int8_t HeadTypeColor = 2;

#endif // COMMON_H
