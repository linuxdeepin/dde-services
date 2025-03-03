// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <slideshowmanager.h>

#include <QObject>
#include <QDBusContext>
#include <QScopedPointer>

class SlideshowManager;

class WallpaperSlideshow : public QObject, protected QDBusContext
{
    Q_OBJECT
public:
    WallpaperSlideshow(QObject *parent = nullptr);
    ~WallpaperSlideshow();

    Q_PROPERTY(QString WallpaperSlideShow READ wallpaperSlideShow WRITE setWallpaperSlideShow)
    QString wallpaperSlideShow() const;
    void setWallpaperSlideShow(const QString &value);

public slots:
    void SetWallpaperSlideShow(const QString &monitorName, const QString &slideShow);
    QString GetWallpaperSlideShow(const QString &monitorName);
    void onPropertyChanged(const QString &name, const QVariant &value);

private:
    QScopedPointer<SlideshowManager> m_manager;
};
