// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "wallpapercachemanager.h"
#include "cachedwallpaper.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QDebug>

WallpaperCacheManager::WallpaperCacheManager(QObject *parent)
    : QObject(parent)
{
}

WallpaperCacheManager::~WallpaperCacheManager()
{
}

WallpaperCacheManager *WallpaperCacheManager::instance()
{
    static WallpaperCacheManager instance;
    return &instance;
}

bool WallpaperCacheManager::deleteBlurImage(const QString &originalPath)
{
    return CachedWallpaper::instance()->deleteBlurImage(originalPath);
}

void WallpaperCacheManager::clearBlurCache()
{
    CachedWallpaper::instance()->clearBlurCache();
}

void WallpaperCacheManager::clearEffectCache(const QString &effect)
{
    CachedWallpaper::instance()->clearEffectCache(effect);
}

qint64 WallpaperCacheManager::getCacheSize(const QString &type)
{
    qint64 totalSize = 0;
    
    if (type == "all" || type == "blur" || type == "pixmix") {
        QDir blurDir(kBlurCacheDir);
        if (blurDir.exists()) {
            QFileInfoList files = blurDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
            for (const QFileInfo &fileInfo : files) {
                totalSize += fileInfo.size();
            }
        }
    }
    
    // Extensible: add other cache type size calculation here
    
    return totalSize;
}

int WallpaperCacheManager::getCacheCount(const QString &type)
{
    int totalCount = 0;
    
    if (type == "all" || type == "blur" || type == "pixmix") {
        QDir blurDir(kBlurCacheDir);
        if (blurDir.exists()) {
            QFileInfoList files = blurDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
            totalCount += files.count();
        }
    }
    
    return totalCount;
}

QStringList WallpaperCacheManager::getCacheList(const QString &type)
{
    QStringList cacheList;
    
    if (type == "all" || type == "blur" || type == "pixmix") {
        QDir blurDir(kBlurCacheDir);
        if (blurDir.exists()) {
            QFileInfoList files = blurDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
            for (const QFileInfo &fileInfo : files) {
                cacheList << fileInfo.absoluteFilePath();
            }
        }
    }
    
    return cacheList;
}

bool WallpaperCacheManager::isBlurImageCached(const QString &originalPath)
{
    // Check disk only — do not trigger generation via getBlurImagePath
    QString pathMd5 = QCryptographicHash::hash(originalPath.toUtf8(), QCryptographicHash::Md5).toHex();
    return QFile::exists(CachedWallpaper::blurOutputPath(pathMd5, originalPath));
}

QString WallpaperCacheManager::getBlurImagePath(const QString &originalPath)
{
    return CachedWallpaper::instance()->getBlurImagePath(originalPath);
}
