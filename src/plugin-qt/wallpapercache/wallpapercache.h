// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef WALLPAPER_CACHE_H
#define WALLPAPER_CACHE_H

#include <QObject>

class ScaleImageThread;

class WallpaperCache : public QObject
{
    Q_OBJECT
public:
    explicit WallpaperCache(QObject *parent = nullptr);
    ~WallpaperCache() override;

private:
    void readCachedWallpaper();

private:
    ScaleImageThread *m_scaleImageThread;
};

#endif // WALLPAPER_CACHE_H
