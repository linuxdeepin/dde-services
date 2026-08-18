// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef WALLPAPERSCHEDULER_H
#define WALLPAPERSCHEDULER_H

#include "background/backgrounds.h"

#include <QDateTime>
#include <QString>
#include <functional>
#include <QTimer>
#include <QRandomGenerator>
#include <QSharedPointer>

class WallpaperScheduler : public QObject
{
    Q_OBJECT
    typedef std::function<void(QString, QDateTime)> BGCHANGEFUNC;

public:
    WallpaperScheduler(BGCHANGEFUNC func);
    void setInterval(QString monitorSpace, qint64 interval);
    void setLastChangeTime(QDateTime date);
    void start(int interval);
    void stop();

public Q_SLOT:
    void handleChangeTimeOut();

private:
    qint64          interval;
    QDateTime       lastSetBgTime;
    QString         monitorSpace;
    QTimer         *changeTimer = nullptr;
    BGCHANGEFUNC    bgChangeFunc;
    bool            stopScheduler;
};

class WallpaperLoop : public QObject
{
    Q_OBJECT

public:
    WallpaperLoop(Backgrounds::BackgroundType wallpaperType);
    QStringList getShowed();
    QString     getNext();
    void        addToShow(QString file);
    void        updateLoopList();
    void        updateWallpaperType(Backgrounds::BackgroundType type);

private:
    QStringList getNotShowed();
    QString     getNextShow();
    void        reset();

private:
    QStringList         showedList;
    QStringList         allList;
    QSharedPointer<QRandomGenerator>    rander;
    Backgrounds::BackgroundType m_wallpaperType;
};

class WallpaperLoopConfigManger : public QObject
{
    Q_OBJECT
    struct WallpaperLoopConfig{
        QDateTime       lastChange;
        QStringList     showedList;
    };

public:
    typedef  QMap<QString,WallpaperLoopConfig> WallpaperLoopConfigMap;

public:
    WallpaperLoopConfigManger();
    WallpaperLoopConfigMap loadWSConfig(QString fileName);
    void setShowed(QString monitorSpace, QStringList showedList);
    void setLastChange(QString monitorSpace, QDateTime date);
    bool save(QString fileName);
    static bool isValidWSPolicy(QString policy);
private:
    WallpaperLoopConfigMap  wallpaperLoopConfigMap;
};

#endif // WALLPAPERSCHEDULER_H
