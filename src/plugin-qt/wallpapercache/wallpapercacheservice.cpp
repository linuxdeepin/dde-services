// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "wallpapercacheservice.h"
#include "cachedwallpaper.h"
#include "wallpapercachemanager.h"

#include <QVariant>
#include <QDBusArgument>
#include <QDebug>
#include <QFile>
#include <QImageReader>

#include <unistd.h>

WallpaperCacheService::WallpaperCacheService(QObject *parent)
    : QObject(parent)
{
}

QList<QSize> WallpaperCacheService::parseSizeArray(const QVariantList &sizeArray)
{
    QList<QSize> sizes;
    for (const QVariant &variant : sizeArray) {
        auto arg = variant.value<QDBusArgument>();
        QSize size;
        arg >> size;
        sizes.append(size);
    }
    return sizes;
}

QStringList WallpaperCacheService::GetProcessedImagePaths(const QString &originalPath, const QVariantList &sizeArray)
{
    if (!QFile::exists(originalPath)) {
        return QStringList() << originalPath;
    }

    qDebug() << "get processed image from origin path:" << originalPath;
    QList<QSize> sizes = parseSizeArray(sizeArray);

    QStringList results = CachedWallpaper::instance()->getCachedImagePaths(originalPath, sizes);
    if (results.isEmpty()) {
        results << originalPath;
    }

    return results;
}

QStringList WallpaperCacheService::GetProcessedImagePathByFd(const QDBusUnixFileDescriptor &fd, const QString &imagePathMd5, const QVariantList &sizeArray)
{
    QString destinationPath = saveImageFromFd(fd, imagePathMd5);
    if (destinationPath.isEmpty()) {
        return QStringList();
    }

    qDebug() << "get processed image from origin path:" << destinationPath;
    QList<QSize> sizes = parseSizeArray(sizeArray);

    QStringList results = CachedWallpaper::instance()->getCachedImagePaths(destinationPath, sizes, true);
    if (results.isEmpty()) {
        results << destinationPath;
    }

    return results;
}

QString WallpaperCacheService::GetBlurImagePath(const QString &originalPath)
{
    if (!QFile::exists(originalPath)) {
        qWarning() << "Original image not exists:" << originalPath;
        return QString();
    }

    return CachedWallpaper::instance()->getBlurImagePath(originalPath);
}

QStringList WallpaperCacheService::GetProcessedImageWithBlur(const QString &originalPath, const QVariantList &sizeArray, bool needBlur)
{
    if (!QFile::exists(originalPath)) {
        qWarning() << "Original image not exists:" << originalPath;
        return QStringList() << originalPath;
    }

    QList<QSize> sizes = parseSizeArray(sizeArray);
    return CachedWallpaper::instance()->getProcessedImageWithBlur(originalPath, sizes, needBlur);
}

QStringList WallpaperCacheService::GetProcessedImagePathByFdWithBlur(const QDBusUnixFileDescriptor &fd, const QString &imagePathMd5, const QVariantList &sizeArray, bool needBlur)
{
    QString destinationPath = saveImageFromFd(fd, imagePathMd5);
    if (destinationPath.isEmpty()) {
        return QStringList();
    }

    qDebug() << "get processed image from origin path:" << destinationPath;
    QList<QSize> sizes = parseSizeArray(sizeArray);

    if (needBlur) {
        return CachedWallpaper::instance()->getProcessedImageWithBlur(destinationPath, sizes, needBlur);
    } else {
        QStringList results = CachedWallpaper::instance()->getCachedImagePaths(destinationPath, sizes, true);
        if (results.isEmpty()) {
            results << destinationPath;
        }
        return results;
    }
}

QStringList WallpaperCacheService::GetWallpaperListForScreen(const QString &originalPath, const QVariantList &sizeArray, bool needBlur)
{
    if (!QFile::exists(originalPath)) {
        qWarning() << "Original image not exists:" << originalPath;
        return QStringList() << originalPath;
    }

    QList<QSize> sizes = parseSizeArray(sizeArray);
    return CachedWallpaper::instance()->getWallpaperListForScreen(originalPath, sizes, needBlur);
}

QString WallpaperCacheService::Get(const QString &effect, const QString &filename)
{
    // Compatible with dde-daemon ImageEffect Get method.
    // Currently only supports pixmix effect (blur).

    if (!QFile::exists(filename)) {
        qWarning() << "Input file not exists:" << filename;
        return QString();
    }

    QString trimmedEffect = effect.trimmed();
    if (trimmedEffect.isEmpty() || trimmedEffect == "pixmix") {
        return CachedWallpaper::instance()->getBlurImagePath(filename);
    }

    qWarning() << "Unsupported effect:" << effect << "only 'pixmix' is supported";
    return QString();
}

void WallpaperCacheService::Delete(const QString &effect, const QString &filename)
{
    // Compatible with dde-daemon ImageEffect Delete method.
    QString normalizedEffect = effect.isEmpty() ? "pixmix" : effect;

    if (normalizedEffect == "all") {
        QStringList supportedEffects = {"pixmix"};
        for (const QString &supportedEffect : supportedEffects) {
            CachedWallpaper::instance()->deleteEffectImage(filename, supportedEffect);
        }
    } else {
        bool success = CachedWallpaper::instance()->deleteEffectImage(filename, normalizedEffect);
        if (!success) {
            qWarning() << "Failed to delete effect image for effect:" << normalizedEffect
                       << "file:" << filename;
        }
    }
}

bool WallpaperCacheService::DeleteBlurImage(const QString &originalPath)
{
    return WallpaperCacheManager::instance()->deleteBlurImage(originalPath);
}

void WallpaperCacheService::ClearBlurCache()
{
    WallpaperCacheManager::instance()->clearBlurCache();
}

void WallpaperCacheService::ClearEffectCache(const QString &effect)
{
    WallpaperCacheManager::instance()->clearEffectCache(effect);
}

QString WallpaperCacheService::saveImageFromFd(const QDBusUnixFileDescriptor &fd, const QString &imagePathMd5)
{
    int fdi = fd.fileDescriptor();
    if (fdi <= 0) {
        return QString();
    }

    QString originPath = kWallpaperCacheDir + "/" + imagePathMd5;
    QFile originFile(originPath);
    if (!originFile.open(QIODevice::ReadWrite)) {
        qWarning() << "Failed to open destination file for writing:" << originPath;
        return QString();
    }

    char buffer[4096];
    ssize_t bytesRead;
    while ((bytesRead = read(fdi, buffer, sizeof(buffer))) > 0) {
        if (originFile.write(buffer, bytesRead) != bytesRead) {
            qWarning() << "Failed to write data to destination file.";
            break;
        }
    }
    if (bytesRead < 0) {
        qWarning() << "Error reading from file descriptor.";
        originFile.close();
        return QString();
    }
    originFile.close();

    // Detect format and rename with extension
    QImageReader reader(originPath);
    QString format = reader.format();
    QString destinationPath = originPath + QString(".%1").arg(format);
    QFile file(originPath);
    if (!file.rename(destinationPath)) {
        qWarning() << "originPath rename failed";
        return QString();
    }

    return destinationPath;
}
