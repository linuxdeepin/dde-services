// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "wallpapercache.h"
#include "scaleimagethread.h"
#include "cachedwallpaper.h"

#include <QDir>
#include <QDebug>
#include <QRegularExpression>

WallpaperCache::WallpaperCache(QObject *parent)
    : QObject(parent)
    , m_scaleImageThread(new ScaleImageThread(this))
{
    m_scaleImageThread->setCachePath(kWallpaperCacheDir);

    // Load existing cached wallpaper info
    readCachedWallpaper();

    connect(CachedWallpaper::instance(), &CachedWallpaper::needHandleImage,
            m_scaleImageThread, &ScaleImageThread::addTasks, Qt::QueuedConnection);
    connect(m_scaleImageThread, &ScaleImageThread::imageScaled,
            CachedWallpaper::instance(), &CachedWallpaper::cacheImage);

    // Lifecycle managed by deepin-service-manager when running as plugin.
}

WallpaperCache::~WallpaperCache()
{
    m_scaleImageThread->stopThread();
    m_scaleImageThread->wait();
}

void WallpaperCache::readCachedWallpaper()
{
    // Create cache directory if it does not exist
    QDir dir(kWallpaperCacheDir);
    if (!dir.exists()) {
        if (!dir.mkpath(kWallpaperCacheDir)) {
            qWarning() << "Failed to create directory:" << kWallpaperCacheDir;
        }
        return;
    }

    // Regular expression to match md5, sizeToString, and file extension
    QRegularExpression reg("^(\\w+)_(\\d+x\\d+)\\.(\\w+)$");

    // Scan cache directory and restore cached entries
    QFileInfoList fileList = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo &fileInfo : fileList) {
        QString fileName = fileInfo.fileName();
        QString filePath = fileInfo.filePath();

        QRegularExpressionMatch match = reg.match(fileName);
        if (!match.hasMatch()) {
            continue;
        }

        QString md5 = match.captured(1);
        QString sizeToString = match.captured(2);

        CachedWallpaper::instance()->cacheImage(md5, sizeToString, filePath);
    }
}
