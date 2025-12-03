// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "dconfinfos.h"

#include <QDebug>

DconfInfo::DconfInfo(QString dconfKey, QString xsKey, DconfValueType dconfType, int8_t xsType)
    : dconfKey(dconfKey)
    , dconfType(dconfType)
    , xsKey(xsKey)
    , xsType(xsType)
{
}

void DconfInfo::setGsToXsFunc(ConverFun func)
{
    convertGsToXs = func;
}

void DconfInfo::setXsToGsFunc(ConverFun func)
{
    convertXsToGs = func;
}

XsValue DconfInfo::getValue(const DTK_CORE_NAMESPACE::DConfig &dconf)
{
    XsValue retVal;
    if (!dconf.isValid()) {
        return retVal;
    }

    bool bOk = false;
    switch (dconfType) {
    case typeOfBool:
    case typeOfInt:
        int valInt;
        valInt = dconf.value(dconfKey).toInt(&bOk);
        if (!bOk) {
            return retVal;
        }
        retVal = valInt;
        break;
    case typeOfString:
        retVal = dconf.value(dconfKey).toString();
        break;
    case typeOfDoublue:
        double valDouble;
        valDouble = dconf.value(dconfKey).toDouble(&bOk);
        if (!bOk) {
            return retVal;
        }
        retVal = valDouble;
        break;
    default:
        return retVal;
    }

    if (convertGsToXs) {
        retVal = convertGsToXs(retVal);
    }

    return retVal;
}

bool DconfInfo::setValue(DTK_CORE_NAMESPACE::DConfig &dconf, XsValue &value)
{
    XsValue converValue = value;
    int *valInt;
    QString *valString;
    double *valDouble;
    if (convertXsToGs) {
        converValue = convertXsToGs(value);
    }

    switch (dconfType) {
    case typeOfBool:
        valInt = std::get_if<int>(&converValue);
        if (valInt == nullptr) {
            return false;
        }

        if (*valInt == 1) {
            dconf.setValue(dconfKey, true);
        } else {
            dconf.setValue(dconfKey, false);
        }
        break;
    case typeOfInt:
        valInt = std::get_if<int>(&converValue);
        if (valInt == nullptr) {
            return false;
        }
        dconf.setValue(dconfKey, *valInt);
        break;
    case typeOfString:
        valString = std::get_if<QString>(&converValue);
        if (valString == nullptr) {
            return false;
        }

        dconf.setValue(dconfKey, *valString);
        break;
    case typeOfDoublue:
        valDouble = std::get_if<double>(&converValue);
        if (valDouble == nullptr) {
            return false;
        }

        dconf.setValue(dconfKey, *valDouble);
        break;
    }

    return true;
}

DconfInfo::ConverFun DconfInfo::getGsToXsFunc(ConverFun func)
{
    return convertGsToXs;
}

DconfInfo::ConverFun DconfInfo::getXsToGsFunc(ConverFun func)
{
    return convertXsToGs;
}

XsValue DconfInfo::convertStrToDouble(XsValue &value)
{
    QString *tempValue = std::get_if<QString>(&value);
    if (tempValue == nullptr) {
        return XsValue();
    }

    return tempValue->toDouble();
}

XsValue DconfInfo::convertDoubleToStr(XsValue &value)
{
    double *tempValue = std::get_if<double>(&value);
    if (tempValue == nullptr) {
        return XsValue();
    }
    return QString::number(*tempValue);
}

XsValue DconfInfo::convertStrToColor(XsValue &value)
{
    ColorValueInfo valueInfo;
    QString *tempValue = std::get_if<QString>(&value);
    if (tempValue == nullptr) {
        return XsValue();
    }
    QStringList valueArray = tempValue->split(",");
    if (valueArray.length() != 4) {
        return valueInfo;
    }

    for (int i = 0; i < colorSize; i++) {
        valueInfo[i] = (uint16_t)((valueArray[i].toDouble()) / double(UINT16_MAX) * double(UINT8_MAX));
    }
    return valueInfo;
}

XsValue DconfInfo::convertColorToStr(XsValue &value)
{
    ColorValueInfo *tempValue = std::get_if<ColorValueInfo>(&value);
    if (tempValue == nullptr) {
        return XsValue();
    }

    uint16_t arr[4];
    for (int i = 0; i < colorSize; i++) {
        arr[i] = (uint16_t)((double)((*tempValue)[i]) / (double)(UINT8_MAX) * (double)(UINT16_MAX));
    }
    return QString::asprintf("%d,%d,%d,%d", arr[0], arr[1], arr[2], arr[3]);
}

QString DconfInfo::getDconfKey()
{
    return dconfKey;
}

QString DconfInfo::getXsetKey()
{
    return xsKey;
}

int8_t DconfInfo::getKeyDType()
{
    return dconfType;
}

int8_t DconfInfo::getKeySType()
{
    if (xsType != HeadTypeInvalid) {
        return xsType;
    }

    if (dconfType == typeOfString || dconfType == typeOfDoublue) {
        return HeadTypeString;
    } else {
        return HeadTypeInteger;
    }
}

DconfInfos::DconfInfos()
    : dconfArray{ QSharedPointer<DconfInfo>(new DconfInfo("theme-name", "Net/ThemeName", typeOfString)),
                  QSharedPointer<DconfInfo>(new DconfInfo("icon-theme-name", "Net/IconThemeName", typeOfString)),
                  QSharedPointer<DconfInfo>(new DconfInfo("fallback-icon-theme", "Net/FallbackIconTheme", typeOfString)),
                  QSharedPointer<DconfInfo>(new DconfInfo("sound-theme-name", "Net/SoundThemeName", typeOfString)),
                  QSharedPointer<DconfInfo>(new DconfInfo("gtk-theme-name", "Gtk/ThemeName", typeOfString)),
                  QSharedPointer<DconfInfo>(new DconfInfo("gtk-cursor-theme-name", "Gtk/CursorThemeName", typeOfString)),
                  QSharedPointer<DconfInfo>(new DconfInfo("gtk-font-name", "Gtk/FontName", typeOfString)),
                  QSharedPointer<DconfInfo>(new DconfInfo("gtk-key-theme-name", "Gtk/KeyThemeName", typeOfString)),
                  QSharedPointer<DconfInfo>(new DconfInfo("gtk-color-palette", "Gtk/ColorPalette", typeOfString)),
                  QSharedPointer<DconfInfo>(new DconfInfo("gtk-toolbar-style", "Gtk/ToolbarStyle", typeOfString)),
                  QSharedPointer<DconfInfo>(new DconfInfo("gtk-toolbar-icon-size", "Gtk/ToolbarIconSize", typeOfString)),
                  QSharedPointer<DconfInfo>(new DconfInfo("gtk-color-scheme", "Gtk/ColorScheme", typeOfString)),
                  QSharedPointer<DconfInfo>(new DconfInfo("gtk-im-preedit-style", "Gtk/IMPreeditStyle", typeOfString)),
                  QSharedPointer<DconfInfo>(new DconfInfo("gtk-im-status-style", "Gtk/IMStatusStyle", typeOfString)),
                  QSharedPointer<DconfInfo>(new DconfInfo("gtk-im-module", "Gtk/IMModule", typeOfString)),
                  QSharedPointer<DconfInfo>(new DconfInfo("gtk-modules", "Gtk/Modules", typeOfString)),
                  QSharedPointer<DconfInfo>(new DconfInfo("gtk-menubar-accel", "Gtk/MenuBarAccel", typeOfString)),
                  QSharedPointer<DconfInfo>(new DconfInfo("xft-hintstyle", "Xft/HintStyle", typeOfString)),
                  QSharedPointer<DconfInfo>(new DconfInfo("xft-rgba", "Xft/RGBA", typeOfString)),
                  QSharedPointer<DconfInfo>(new DconfInfo("cursor-blink-time", "Net/CursorBlinkTime", typeOfInt)),
                  QSharedPointer<DconfInfo>(new DconfInfo("gtk-cursor-blink-timeout", "Net/CursorBlinkTimeout", typeOfInt)),
                  QSharedPointer<DconfInfo>(new DconfInfo("double-click-time", "Net/DoubleClickTime", typeOfInt)),
                  QSharedPointer<DconfInfo>(new DconfInfo("double-click-distance", "Net/DoubleClickDistance", typeOfInt)),
                  QSharedPointer<DconfInfo>(new DconfInfo("dnd-drag-threshold", "Net/DndDragThreshold", typeOfInt)),
                  QSharedPointer<DconfInfo>(new DconfInfo("gtk-cursor-theme-size", "Gtk/CursorThemeSize", typeOfInt)),
                  QSharedPointer<DconfInfo>(new DconfInfo("gtk-timeout-initial", "Gtk/TimeoutInitial", typeOfInt)),
                  QSharedPointer<DconfInfo>(new DconfInfo("gtk-timeout-repeat", "Gtk/TimeoutRepeat", typeOfInt)),
                  QSharedPointer<DconfInfo>(new DconfInfo("gtk-recent-files-max-age", "Gtk/RecentFilesMaxAge", typeOfInt)),
                  QSharedPointer<DconfInfo>(new DconfInfo("xft-dpi", "Xft/DPI", typeOfInt)),
                  QSharedPointer<DconfInfo>(new DconfInfo("cursor-blink", "Net/CursorBlink", typeOfBool)),
                  QSharedPointer<DconfInfo>(new DconfInfo("enable-event-sounds", "Net/EnableEventSounds", typeOfBool)),
                  QSharedPointer<DconfInfo>(new DconfInfo("enable-input-feedback-sounds", "Net/EnableInputFeedbackSounds", typeOfBool)),
                  QSharedPointer<DconfInfo>(new DconfInfo("gtk-can-change-accels", "Gtk/CanChangeAccels", typeOfBool)),
                  QSharedPointer<DconfInfo>(new DconfInfo("gtk-menu-images", "Gtk/MenuImages", typeOfBool)),
                  QSharedPointer<DconfInfo>(new DconfInfo("gtk-button-images", "Gtk/ButtonImages", typeOfBool)),
                  QSharedPointer<DconfInfo>(new DconfInfo("gtk-enable-animations", "Gtk/EnableAnimations", typeOfBool)),
                  QSharedPointer<DconfInfo>(new DconfInfo("gtk-show-input-method-menu", "Gtk/ShowInputMethodMenu", typeOfBool)),
                  QSharedPointer<DconfInfo>(new DconfInfo("gtk-show-unicode-menu", "Gtk/ShowUnicodeMenu", typeOfBool)),
                  QSharedPointer<DconfInfo>(new DconfInfo("gtk-auto-mnemonics", "Gtk/AutoMnemonics", typeOfBool)),
                  QSharedPointer<DconfInfo>(new DconfInfo("gtk-recent-files-enabled", "Gtk/RecentFilesEnabled", typeOfBool)),
                  QSharedPointer<DconfInfo>(new DconfInfo("gtk-shell-shows-app-menu", "Gtk/ShellShowsAppMenu", typeOfBool)),
                  QSharedPointer<DconfInfo>(new DconfInfo("xft-antialias", "Xft/Antialias", typeOfBool)),
                  QSharedPointer<DconfInfo>(new DconfInfo("xft-hinting", "Xft/Hinting", typeOfBool)),
                  QSharedPointer<DconfInfo>(new DconfInfo("qt-font-name", "Qt/FontName", typeOfString)),
                  QSharedPointer<DconfInfo>(new DconfInfo("qt-mono-font-name", "Qt/MonoFontName", typeOfString)),
                  QSharedPointer<DconfInfo>(new DconfInfo("dtk-window-radius", "DTK/WindowRadius", typeOfInt)),
                  QSharedPointer<DconfInfo>(new DconfInfo("dtk-size-mode", "DTK/SizeMode", typeOfInt)),
                  QSharedPointer<DconfInfo>(new DconfInfo("qt-scrollbar-policy", "Qt/ScrollBarPolicy", typeOfInt)),
                  QSharedPointer<DconfInfo>(new DconfInfo("primary-monitor-name", "Gdk/PrimaryMonitorName", typeOfString)) }

{
    QSharedPointer<DconfInfo> qtActiveColor(new DconfInfo("qt-active-color", "Qt/ActiveColor", typeOfString, HeadTypeColor));
    qtActiveColor->setGsToXsFunc(std::bind(&DconfInfo::convertStrToColor, qtActiveColor.get(), std::placeholders::_1));
    qtActiveColor->setXsToGsFunc(std::bind(&DconfInfo::convertColorToStr, qtActiveColor.get(), std::placeholders::_1));
    dconfArray.push_back(qtActiveColor);

    QSharedPointer<DconfInfo> qtDarkActiveColor(new DconfInfo("qt-dark-active-color", "Qt/DarkActiveColor", typeOfString, HeadTypeColor));
    qtDarkActiveColor->setGsToXsFunc(std::bind(&DconfInfo::convertStrToColor, qtActiveColor.get(), std::placeholders::_1));
    qtDarkActiveColor->setXsToGsFunc(std::bind(&DconfInfo::convertColorToStr, qtActiveColor.get(), std::placeholders::_1));
    dconfArray.push_back(qtDarkActiveColor);

    QSharedPointer<DconfInfo> qtFontPoint(new DconfInfo("qt-font-point-size", "Qt/FontPointSize", typeOfDoublue));
    qtFontPoint->setGsToXsFunc(std::bind(&DconfInfo::convertDoubleToStr, qtFontPoint.get(), std::placeholders::_1));
    qtFontPoint->setXsToGsFunc(std::bind(&DconfInfo::convertStrToDouble, qtFontPoint.get(), std::placeholders::_1));
    dconfArray.push_back(qtFontPoint);
}

QSharedPointer<DconfInfo> DconfInfos::getByDconfKey(const QString &dconfKey)
{
    for (auto item : dconfArray) {
        if (item->getDconfKey() == dconfKey) {
            return item;
        }
    }

    return nullptr;
}

QSharedPointer<DconfInfo> DconfInfos::getByXSKey(const QString &xsettingKey)
{
    for (auto item : dconfArray) {
        if (item->getXsetKey() == xsettingKey) {
            return item;
        }
    }

    return nullptr;
}
