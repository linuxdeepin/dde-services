// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "wallpaperslideshow.h"
#include "wallpaperslideshowadaptor.h"
#include "commondefine.h"

#include <QDBusConnection>
#include <DGuiApplicationHelper>

DGUI_USE_NAMESPACE

static WallpaperSlideshow *service = nullptr;

extern "C" int DSMRegister(const char *name, void *data)
{
    Q_UNUSED(name)
    Q_UNUSED(data)

    service = new WallpaperSlideshow();
    new WallpaperSlideshowAdaptor(service);
    bool wallpaperSlideshowRegister = QDBusConnection::sessionBus().registerService(WALLPAPER_SLIDESHOW_SERVICE);
    bool objectRegister = QDBusConnection::sessionBus().registerObject(WALLPAPER_SLIDESHOW_PATH, WALLPAPER_SLIDESHOW_INTERFACE, service);
    if (!wallpaperSlideshowRegister || !objectRegister) {
        qWarning() << "Failed to register service: " << WALLPAPER_SLIDESHOW_SERVICE;
        return -1;
    }
    return 0;
}

// 该函数用于资源释放
// 非常驻插件必须实现该函数，以防内存泄漏
extern "C" int DSMUnRegister(const char *name, void *data)
{
    (void)name;
    (void)data;
    service->deleteLater();
    service = nullptr;
    return 0;
}
