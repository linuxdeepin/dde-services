// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef WALLPAPER_CACHE_MANAGER_H
#define WALLPAPER_CACHE_MANAGER_H

#include <QObject>

/**
 * @brief Wallpaper cache manager
 *
 * Provides cache management functions for internal use and CLI tools.
 * Does not export D-Bus interfaces.
 */
class WallpaperCacheManager : public QObject
{
    Q_OBJECT

public:
    explicit WallpaperCacheManager(QObject *parent = nullptr);
    ~WallpaperCacheManager();

    static WallpaperCacheManager *instance();

    // Cache management
    bool deleteBlurImage(const QString &originalPath);
    void clearBlurCache();
    void clearEffectCache(const QString &effect = "all");

    // Cache statistics
    qint64 getCacheSize(const QString &type = "all");
    int getCacheCount(const QString &type = "all");
    QStringList getCacheList(const QString &type = "all");

    // Cache status
    bool isBlurImageCached(const QString &originalPath);
    QString getBlurImagePath(const QString &originalPath);
};

#endif // WALLPAPER_CACHE_MANAGER_H
