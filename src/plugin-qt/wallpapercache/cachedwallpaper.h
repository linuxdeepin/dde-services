// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef CACHED_WALLPAPER_H
#define CACHED_WALLPAPER_H

#include <QObject>
#include <QString>
#include <QMap>
#include <QSize>
#include <QMutex>

// Shared cache path constants
inline const QString kWallpaperCacheDir = QStringLiteral("/var/cache/dde-wallpaper-cache");
inline const QString kBlurCacheDir = kWallpaperCacheDir + QStringLiteral("/blur");

class CachedWallpaper : public QObject
{
    Q_OBJECT

private:
    explicit CachedWallpaper();
    ~CachedWallpaper();

signals:
    void needHandleImage(const QString &originalPath, const QList<QSize> &size, bool isMd5Path);

public:
    static CachedWallpaper *instance();
    QStringList getCachedImagePaths(const QString &originalPath, const QList<QSize> &sizes, bool isMd5Path = false);
    void cacheImage(const QString &originalPathMd5, const QString &size, const QString &processedPath);

    // Blur wallpaper interfaces
    QString getBlurImagePath(const QString &originalPath);
    QStringList getProcessedImageWithBlur(const QString &originalPath, const QList<QSize> &sizes, bool needBlur = false);

    // Unified wallpaper processing interface
    QStringList getWallpaperListForScreen(const QString &originalPath, const QList<QSize> &sizes, bool needBlur = true);

    // Blur image management interfaces
    bool deleteBlurImage(const QString &originalPath);
    bool deleteEffectImage(const QString &originalPath, const QString &effect);
    void clearBlurCache();
    void clearEffectCache(const QString &effect);

    static QString blurOutputPath(const QString &pathMd5, const QString &originalPath);

private:
    QString generateBlurImage(const QString &pathMd5, const QString &originalPath);
    void cacheBlurImage(const QString &originalPathMd5, const QString &blurPath);

private:
    // originalPath's md5; sizeToString;processedWallpaperPath
    QMap<QString, QMap<QString, QString>> m_cachedImages;
    // 模糊壁纸缓存: originalPath's md5; blurImagePath
    QMap<QString, QString> m_blurImageCache;
    QMutex m_blurGenerateMutex;
};

#endif // CACHED_WALLPAPER_H
