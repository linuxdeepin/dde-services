// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef COMMONDEFINE_H
#define COMMONDEFINE_H
#include <QString>

const QString APPEARANCE_SERVICE = "org.deepin.dde.Appearance1";
const QString APPEARANCE_PATH = "/org/deepin/dde/Appearance1";
const QString APPEARANCE_INTERFACE = "org.deepin.dde.Appearance1";

#define APPEARANCEAPPID     "org.deepin.dde.appearance"
#define APPEARANCESCHEMA    "org.deepin.dde.appearance"
#define STARTCDDEAPPID      "org.deepin.startdde"
#define GSKEYWALLPAPERSLIDESHOW  "Wallpaper_Slideshow"

#define WALLPAPER_SLIDESHOW_SERVICE "org.deepin.dde.WallpaperSlideshow"
#define WALLPAPER_SLIDESHOW_PATH "/org/deepin/dde/WallpaperSlideshow"
#define WALLPAPER_SLIDESHOW_INTERFACE "org.deepin.dde.WallpaperSlideshow"

#define WS_CONFIG_PATH  utils::GetUserConfigDir() + "/deepin/dde-daemon/appearance/wallpaper-slideshow.json"

#define SCHEME_FILE             "file://"

#define WSPOLICYLOGIN           "login"
#define	WSPOLICYWAKEUP          "wakeup"

#define WALLPAPERSLIDESHOWNAME "WallpaperSlideShow"

#endif // COMMONDEFINE_H
