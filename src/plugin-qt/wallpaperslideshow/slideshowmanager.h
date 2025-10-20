// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "background/backgrounds.h"
#include "wallpaperscheduler.h"
#include "appearancedbusproxy.h"

#include <DConfig>
#include <QScopedPointer>

DCORE_USE_NAMESPACE

class SlideshowManager : public QObject
{
    Q_OBJECT
public:
    explicit SlideshowManager(QObject *parent = nullptr);
    ~SlideshowManager();

    bool doSetWallpaperSlideShow(const QString &monitorName,const QString &wallpaperSlideShow);
    QString doGetWallpaperSlideShow(QString monitorName);

    QString getWallpaperSlideShow() const { return m_wallpaperSlideShow; }
    bool setWallpaperSlideShow(const QString &value);

    void updateWSPolicy(QString policy);
    void loadWSConfig();
    bool saveWSConfig(QString monitorSpace, QDateTime date);
    void autoChangeBg(QString monitorSpace, QDateTime date);

    void setMonitorBackground(const QString& monitorName,const QString& imageGile);

    void handlePrepareForSleep(bool sleep);
    bool changeBgAfterLogin(QString monitorSpace);

private slots:
    void onWallpaperChanged();

private:
    void init();
    void loadConfig();
    bool isValidScreen(const QString &screenName);

signals:
    void propertyChanged(const QString &name, const QVariant &value);

private:
    QScopedPointer<DConfig>                          m_settingDconfig;
    QMap<QString,QSharedPointer<WallpaperScheduler>> m_wsSchedulerMap;
    QMap<QString,QSharedPointer<WallpaperLoop>>      m_wsLoopMap;
    QSharedPointer<AppearanceDBusProxy>              m_dbusProxy;
    QString                                          m_wallpaperSlideShow;
    QMap<QString, Backgrounds::BackgroundType>       m_wallpaperType;
};
