// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "wallpapercacheservice.h"
#include "wallpapercache.h"

#include <QDBusConnection>
#include <DLog>

#include <memory>

DCORE_USE_NAMESPACE

static std::unique_ptr<WallpaperCacheService> service;
static std::unique_ptr<ImageEffect1Service> imageEffectService;
static std::unique_ptr<ImageBlur1Service> imageBlurService;
static std::unique_ptr<WallpaperCache> wallpaperCache;

extern "C" int DSMRegister(const char *name, void *data)
{
    (void)name;

    DLogManager::registerJournalAppender();

    auto connection = reinterpret_cast<QDBusConnection *>(data);

    const QString path = "/org/deepin/dde/WallpaperCache";
    const QString imageEffectInterface = "org.deepin.dde.ImageEffect1";
    const QString imageEffectPath = "/org/deepin/dde/ImageEffect1";
    const QString imageBlurInterface = "org.deepin.dde.ImageBlur1";
    const QString imageBlurPath = "/org/deepin/dde/ImageBlur1";

    // Register WallpaperCache primary service object
    service = std::make_unique<WallpaperCacheService>();
    if (!connection->registerObject(path, service.get(),
            QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals)) {
        qWarning() << "Failed to register WallpaperCache dbus object";
    }

    // Register ImageEffect1 compatibility service
    if (connection->registerService(imageEffectInterface)) {
        imageEffectService = std::make_unique<ImageEffect1Service>(service.get());
        if (!connection->registerObject(imageEffectPath, imageEffectService.get(),
                QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals)) {
            qWarning() << "Failed to register ImageEffect1 dbus object";
        } else {
            qInfo() << "Successfully registered ImageEffect1 compatibility service";
        }
    } else {
        qWarning() << "Failed to register ImageEffect1 dbus service";
    }

    // Register ImageBlur1 compatibility service
    if (connection->registerService(imageBlurInterface)) {
        imageBlurService = std::make_unique<ImageBlur1Service>(service.get());
        if (!connection->registerObject(imageBlurPath, imageBlurService.get(),
                QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals)) {
            qWarning() << "Failed to register ImageBlur1 dbus object";
        } else {
            qInfo() << "Successfully registered ImageBlur1 compatibility service";
        }
    } else {
        qWarning() << "Failed to register ImageBlur1 dbus service";
    }

    // Initialize wallpaper cache (load existing cache, start scale thread)
    wallpaperCache = std::make_unique<WallpaperCache>();

    return 0;
}

extern "C" int DSMUnRegister(const char *name, void *data)
{
    (void)name;
    (void)data;

    wallpaperCache.reset();
    imageBlurService.reset();
    imageEffectService.reset();
    service.reset();

    return 0;
}
