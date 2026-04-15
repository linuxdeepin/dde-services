// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "cachedwallpaper.h"
#include "scaleimagethread.h"
#include "imageeffectprocessor.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QDateTime>
#include <QImageReader>

CachedWallpaper::CachedWallpaper()
{
    QDir().mkpath(kBlurCacheDir);
}

CachedWallpaper::~CachedWallpaper()
{
    m_cachedImages.clear();
}

CachedWallpaper *CachedWallpaper::instance()
{
    static CachedWallpaper cachedWallpaper;
    return &cachedWallpaper;
}

QStringList CachedWallpaper::getCachedImagePaths(const QString &originalPath, const QList<QSize> &sizes, bool isMd5Path)
{
    QList<QSize> noCachedSizes;
    QStringList results;

    QString pathMd5 = ScaleImageThread::pathMd5(originalPath, isMd5Path);

    if (m_cachedImages.contains(pathMd5)) {
        const QMap<QString, QString> &map = m_cachedImages[pathMd5];
        for (const QSize &size : sizes) {
            QString strSize = ScaleImageThread::sizeToString(size);
            if (map.contains(strSize)) {
                results.append(map[strSize]);
            } else {
                noCachedSizes.append(size);
            }
        }
    } else {
        noCachedSizes = sizes;
    }

    if (!noCachedSizes.isEmpty()) {
        qDebug() << "need handle image:" << originalPath << " sizes:" << noCachedSizes;
        Q_EMIT needHandleImage(originalPath, noCachedSizes, isMd5Path);
    }

    return results;
}

void CachedWallpaper::cacheImage(const QString &originalPathMd5, const QString &size, const QString &processedPath)
{
    qDebug() << "cache Image:" << processedPath;
    m_cachedImages[originalPathMd5].insert(size, processedPath);
}

QString CachedWallpaper::getBlurImagePath(const QString &originalPath)
{
    QString pathMd5 = QCryptographicHash::hash(originalPath.toUtf8(), QCryptographicHash::Md5).toHex();

    QMutexLocker locker(&m_blurGenerateMutex);

    auto it = m_blurImageCache.constFind(pathMd5);
    if (it != m_blurImageCache.constEnd() && QFile::exists(it.value())) {
        return it.value();
    }

    QString blurPath = generateBlurImage(pathMd5, originalPath);
    if (!blurPath.isEmpty()) {
        cacheBlurImage(pathMd5, blurPath);
        return blurPath;
    }

    return QString();
}

void CachedWallpaper::cacheBlurImage(const QString &originalPathMd5, const QString &blurPath)
{
    qDebug() << "cache blur image:" << blurPath;
    m_blurImageCache[originalPathMd5] = blurPath;
}

QStringList CachedWallpaper::getProcessedImageWithBlur(const QString &originalPath, const QList<QSize> &sizes, bool needBlur)
{
    QString sourcePath = originalPath;

    if (needBlur) {
        QString blurPath = getBlurImagePath(originalPath);
        if (!blurPath.isEmpty()) {
            sourcePath = blurPath;
        }
    }

    QStringList results = getCachedImagePaths(sourcePath, sizes);
    if (!results.isEmpty()) {
        return results;
    }

    return QStringList() << sourcePath;
}

QString CachedWallpaper::blurOutputPath(const QString &pathMd5, const QString &originalPath)
{
    QFileInfo fi(originalPath);
    QString suffix = fi.suffix();
    return QString("%1/%2%3").arg(kBlurCacheDir, pathMd5, suffix.isEmpty() ? "" : "." + suffix);
}

QString CachedWallpaper::generateBlurImage(const QString &pathMd5, const QString &originalPath)
{
    QFileInfo originalFileInfo(originalPath);
    QString outputFile = blurOutputPath(pathMd5, originalPath);

    // Return cached file if up-to-date
    QFileInfo outputFileInfo(outputFile);
    if (outputFileInfo.exists()
        && outputFileInfo.lastModified() >= originalFileInfo.lastModified()
        && outputFileInfo.size() > 0) {
        qDebug() << "Blur image already exists and up-to-date:" << outputFile;
        return outputFile;
    }

    qDebug() << "Generating blur image:" << originalPath << "=>" << outputFile;

    QImage blurredImage = ImageEffectProcessor::applyPixmixEffect(originalPath);
    if (blurredImage.isNull()) {
        qWarning() << "Failed to generate blur image:" << originalPath;
        return QString();
    }

    if (!blurredImage.save(outputFile, QImageReader::imageFormat(originalPath), 100)) {
        qWarning() << "Failed to save blur image:" << outputFile;
        return QString();
    }

    // Match output mtime to input for cache freshness check
    QFile timeFile(outputFile);
    if (timeFile.open(QIODevice::ReadWrite)) {
        timeFile.setFileTime(originalFileInfo.lastModified(), QFileDevice::FileModificationTime);
        timeFile.close();
    }

    qDebug() << "Successfully generated blur image:" << outputFile;
    return outputFile;
}

QStringList CachedWallpaper::getWallpaperListForScreen(const QString &originalPath, const QList<QSize> &sizes, bool needBlur)
{
    if (needBlur) {
        QString blurPath = getBlurImagePath(originalPath);
        if (!blurPath.isEmpty()) {
            QStringList results = getCachedImagePaths(blurPath, sizes);
            if (!results.isEmpty()) {
                return results;
            }
            return QStringList() << blurPath;
        }
        qWarning() << "Blur processing failed for:" << originalPath << ", fallback to original";
    }

    QStringList results = getCachedImagePaths(originalPath, sizes);
    if (!results.isEmpty()) {
        return results;
    }

    return QStringList() << originalPath;
}

bool CachedWallpaper::deleteBlurImage(const QString &originalPath)
{
    QString pathMd5 = QCryptographicHash::hash(originalPath.toUtf8(), QCryptographicHash::Md5).toHex();

    auto it = m_blurImageCache.find(pathMd5);
    if (it != m_blurImageCache.end()) {
        QString blurPath = it.value();
        m_blurImageCache.erase(it);
        if (QFile::remove(blurPath)) {
            qDebug() << "Deleted blur image:" << blurPath;
            return true;
        }
        qWarning() << "Failed to delete blur image file:" << blurPath;
        return false;
    }

    // Try to delete on-disk file even if not in memory cache
    QString outputFile = blurOutputPath(pathMd5, originalPath);
    if (QFile::remove(outputFile)) {
        qDebug() << "Deleted blur image file:" << outputFile;
    }
    return true;
}

void CachedWallpaper::clearBlurCache()
{
    m_blurImageCache.clear();

    QDir cacheDir(kBlurCacheDir);
    if (!cacheDir.exists()) {
        return;
    }

    QFileInfoList files = cacheDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    int deletedCount = 0;
    for (const QFileInfo &fileInfo : files) {
        if (QFile::remove(fileInfo.absoluteFilePath())) {
            deletedCount++;
        } else {
            qWarning() << "Failed to delete blur cache file:" << fileInfo.absoluteFilePath();
        }
    }
    qDebug() << "Cleared blur cache, deleted" << deletedCount << "files";
}

bool CachedWallpaper::deleteEffectImage(const QString &originalPath, const QString &effect)
{
    if (effect.isEmpty() || effect == "pixmix") {
        return deleteBlurImage(originalPath);
    }
    qWarning() << "Unsupported effect for deletion:" << effect;
    return false;
}

void CachedWallpaper::clearEffectCache(const QString &effect)
{
    if (effect == "all") {
        clearBlurCache();
        return;
    }

    QString effectName = effect.isEmpty() ? "pixmix" : effect;
    if (effectName == "pixmix") {
        clearBlurCache();
    } else {
        qWarning() << "Unsupported effect for cache clearing:" << effectName;
    }
}
