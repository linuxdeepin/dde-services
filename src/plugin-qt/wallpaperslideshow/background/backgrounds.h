// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef BACKGROUNDPRIVATE_H
#define BACKGROUNDPRIVATE_H
#include <QDir>
#include <QList>
#include <QDebug>
#include <QDateTime>
#include <QMutex>
#include <QVector>

#include "background.h"
#include <qvector.h>

class Backgrounds: public QObject
{
    Q_OBJECT
public:

    enum BackgroundType {
        BT_Solid = 0,
        BT_Custom,
        BT_Sys,
        BT_All
    };

    Backgrounds(QObject *parent = nullptr);
    ~Backgrounds();

    void refreshBackground();
    void sortByTime(QFileInfoList listFileInfo);
    QStringList getCustomBgFilesInDir(QString dir);
    QStringList getBgFilesInDir(QString dir);
    bool isFileInDirs(QString file, QStringList dirs);
    bool isBackgroundFile(QString file);
    QVector<Background> listBackground();
    QVector<Background> getBackground(BackgroundType type);

    static BackgroundType getBackgroundType(QString id);

private:
    void init();
    QStringList getSysBgFIles();
    QStringList getCustomBgFiles();

private:
    QVector<Background> backgrounds;
    QVector<Background> solidBackgrounds;
    QVector<Background> customBackgrounds;
    QVector<Background> sysBackgrounds;

    static QStringList systemWallpapersDir;
    static QStringList uiSupportedFormats;
};

#endif // BACKGROUNDPRIVATE_H
