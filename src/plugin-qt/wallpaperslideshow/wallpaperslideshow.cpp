// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "wallpaperslideshow.h"

WallpaperSlideshow::WallpaperSlideshow(QObject *parent)
    : QObject(parent)
    , m_manager(new SlideshowManager(this))
{

}

WallpaperSlideshow::~WallpaperSlideshow()
{

}

QString WallpaperSlideshow::wallpaperSlideShow() const
{
    return m_manager->getWallpaperSlideShow();
}

void WallpaperSlideshow::setWallpaperSlideShow(const QString &value)
{
    m_manager->setWallpaperSlideShow(value);
}

void WallpaperSlideshow::SetWallpaperSlideShow(const QString &monitorName, const QString &slideShow)
{
    m_manager->doSetWallpaperSlideShow(monitorName, slideShow);
}

QString WallpaperSlideshow::GetWallpaperSlideShow(const QString &monitorName)
{
    return m_manager->doGetWallpaperSlideShow(monitorName);
}
