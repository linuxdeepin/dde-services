// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "wallpaperslideshow.h"
#include "commondefine.h"

WallpaperSlideshow::WallpaperSlideshow(QObject *parent)
    : QObject(parent)
    , m_manager(new SlideshowManager(this))
{
    connect(m_manager.get(), &SlideshowManager::propertyChanged, this, &WallpaperSlideshow::onPropertyChanged);
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

void WallpaperSlideshow::onPropertyChanged(const QString &name, const QVariant &value)
{
    QVariantMap properties;
    properties.insert(name, value);

    QList<QVariant> arguments;
    arguments.push_back(WALLPAPER_SLIDESHOW_INTERFACE);
    arguments.push_back(properties);
    arguments.push_back(QStringList());

    QDBusMessage msg = QDBusMessage::createSignal(WALLPAPER_SLIDESHOW_PATH, "org.freedesktop.DBus.Properties", "PropertiesChanged");
    msg.setArguments(arguments);
    QDBusConnection::sessionBus().send(msg);
}
