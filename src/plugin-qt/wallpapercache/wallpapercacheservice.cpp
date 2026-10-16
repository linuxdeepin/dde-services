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
#include <QDir>
#include <QRegularExpression>
#include <QTemporaryFile>

#include <cerrno>
#include <cstdio>

#include <unistd.h>

WallpaperCacheService::WallpaperCacheService(QObject *parent)
    : QObject(parent)
{
}

QList<QSize> WallpaperCacheService::parseSizeArray(const QVariantList &sizeArray)
{
    QList<QSize> sizes;
    for (const QVariant &variant : sizeArray) {
        if (variant.canConvert<QSize>()) {
            sizes.append(variant.toSize());
            continue;
        }
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
    const QList<QSize> sizes = parseSizeArray(sizeArray);
    if (fd.fileDescriptor() < 0)
        return {};

    const QStringList cached = CachedWallpaper::instance()->findCachedImagePaths(imagePathMd5, sizes);
    if (!sizes.isEmpty() && cached.size() == sizes.size())
        return cached;

    QString destinationPath = saveImageFromFd(fd, imagePathMd5);
    if (destinationPath.isEmpty()) {
        return QStringList();
    }

    qDebug() << "get processed image from origin path:" << destinationPath;

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
    if (!needBlur)
        return GetProcessedImagePathByFd(fd, imagePathMd5, sizeArray);

    QString destinationPath = saveImageFromFd(fd, imagePathMd5);
    if (destinationPath.isEmpty()) {
        return QStringList();
    }

    qDebug() << "get processed image from origin path:" << destinationPath;
    QList<QSize> sizes = parseSizeArray(sizeArray);

    return CachedWallpaper::instance()->getProcessedImageWithBlur(destinationPath, sizes, true);
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
    const int fdi = fd.fileDescriptor();
    static const QRegularExpression md5Pattern(QStringLiteral("\\A[0-9a-fA-F]{32}\\z"));
    if (fdi < 0 || !md5Pattern.match(imagePathMd5).hasMatch())
        return {};

    if (!QDir().mkpath(kWallpaperCacheDir))
        return {};

    // Import privately: failed or concurrent copies must never expose a partial
    // image under the shared source name.
    QTemporaryFile originFile(kWallpaperCacheDir + "/.fd-import-XXXXXX");
    if (!originFile.open()) {
        qWarning() << "Failed to create imported image:" << originFile.errorString();
        return {};
    }
    // Imported sources are returned over D-Bus and read by other clients.
    if (!originFile.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther))
        return {};

    char buffer[64 * 1024];
    for (;;) {
        const ssize_t bytesRead = read(fdi, buffer, sizeof(buffer));
        if (bytesRead < 0) {
            if (errno == EINTR)
                continue;
            qWarning() << "Error reading image descriptor:" << errno;
            return {};
        }
        if (bytesRead == 0)
            break;
        if (originFile.write(buffer, bytesRead) != bytesRead) {
            qWarning() << "Failed to write imported image:" << originFile.errorString();
            return {};
        }
    }
    if (!originFile.flush())
        return {};

    const QByteArray format = QImageReader::imageFormat(originFile.fileName());
    if (format.isEmpty()) {
        qWarning() << "Failed to detect imported image format";
        return {};
    }
    const QString destinationPath = kWallpaperCacheDir + "/" + imagePathMd5 + "." + QString::fromLatin1(format);
    // POSIX rename atomically replaces an earlier import, including a longer
    // image. Existing readers retain their opened file until they finish.
    if (::rename(QFile::encodeName(originFile.fileName()).constData(),
                 QFile::encodeName(destinationPath).constData()) != 0) {
        qWarning() << "Failed to publish imported image:" << errno;
        return {};
    }
    originFile.setAutoRemove(false);
    return destinationPath;
}
