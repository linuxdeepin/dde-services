// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "backgrounds.h"
#include "utils.h"
#include "commondefine.h"
#include "unistd.h"
#include "appearancedbusproxy.h"
#include "format.h"

#include <QDBusInterface>
#include <pwd.h>
#include <QDBusReply>

QStringList Backgrounds::systemWallpapersDir = { "/usr/share/wallpapers/deepin", "/usr/share/wallpapers/deepin-solidwallpapers"};
QStringList Backgrounds::uiSupportedFormats = { "jpeg", "png", "bmp", "tiff", "gif" };

Backgrounds* Backgrounds::instance(QObject *parent)
{
    static Backgrounds *instance = new Backgrounds(parent);
    return instance;
}

Backgrounds::Backgrounds(QObject *parent)
    : QObject(parent)
{
    init();
}

Backgrounds::~Backgrounds()
{
}

void Backgrounds::init()
{
    refreshBackground();
}

void Backgrounds::refreshBackground()
{
    clear();
    QStringList files = getCustomBgFiles();
    for (auto file : files) {
        if (!QFile::exists(file)) {
            continue;
        }
        const QString &bg = utils::enCodeURI(file, SCHEME_FILE);
        backgrounds.push_back(bg);

        if (utils::isSolidWallpaper(file)) {
            solidBackgrounds.push_back(bg);
        } else {
            customBackgrounds.push_back(bg);
        }

    }

    files = getSysBgFIles();
    for (auto file : files) {
        if (!QFile::exists(file)) {
            continue;
        }
        const QString &bg = utils::enCodeURI(file, SCHEME_FILE);
        backgrounds.push_back(bg);

        if (utils::isSolidWallpaper(file)) {
            solidBackgrounds.push_back(bg);
        } else {
            sysBackgrounds.push_back(bg);
        }
    }
}

void Backgrounds::clear()
{
    backgrounds.clear();
    solidBackgrounds.clear();
    customBackgrounds.clear();
    sysBackgrounds.clear();
}

QStringList Backgrounds::getSysBgFIles()
{
    QStringList files;
    for (auto dir : systemWallpapersDir) {
        files.append(getBgFilesInDir(dir));
    }
    return files;
}

void Backgrounds::sortByTime(QFileInfoList listFileInfo)
{
    std::sort(listFileInfo.begin(), listFileInfo.end(), [=](const QFileInfo &f1, const QFileInfo &f2) {
        return f1.lastModified().toSecsSinceEpoch() < f2.lastModified().toSecsSinceEpoch();
    });
}

QStringList Backgrounds::getCustomBgFiles()
{
    struct passwd *user = getpwuid(getuid());
    if (user == nullptr) {
        return QStringList();
    }
    return AppearanceDBusProxy::GetCustomWallPapers(user->pw_name);
}

QStringList Backgrounds::getCustomBgFilesInDir(QString dir)
{
    QStringList wallpapers;
    QDir qdir(dir);
    if (!qdir.exists())
        return wallpapers;

    QFileInfoList fileInfoList = qdir.entryInfoList(QDir::NoSymLinks);
    sortByTime(fileInfoList);

    for (auto info : fileInfoList) {
        if (info.isDir()) {
            continue;
        }
        if (!isBackgroundFile(info.path())) {
            continue;
        }
        wallpapers.append(info.path());
    }

    return wallpapers;
}

QStringList Backgrounds::getBgFilesInDir(QString dir)
{
    QStringList walls;

    QDir qdir(dir);
    if (!qdir.exists())
        return walls;

    QFileInfoList fileInfoList = qdir.entryInfoList(QDir::NoDotAndDotDot | QDir::Dirs | QDir::Files);
    for (auto file : fileInfoList) {
        if (!isBackgroundFile(file.filePath())) {
            continue;
        }
        walls.append(file.filePath());
    }

    return walls;
}

bool Backgrounds::isFileInDirs(QString file, QStringList dirs)
{
    for (auto dir : dirs) {
        QFileInfo qfile(file);
        if (qfile.absolutePath() == dir)
            return true;
    }

    return false;
}

bool Backgrounds::isBackgroundFile(QString file)
{
    file = utils::deCodeURI(file);

    QString format = FormatPicture::getPictureType(file);
    if (format == "") {
        return false;
    }

    if (uiSupportedFormats.contains(format)) {
        return true;
    }

    return false;
}

QStringList Backgrounds::listBackground()
{
    if (backgrounds.length() == 0)
        refreshBackground();

    return backgrounds;
}

QStringList Backgrounds::getBackground(BackgroundType type)
{
    listBackground();
    switch (type) {
    case BT_Solid:
        return solidBackgrounds;
    case BT_Custom:
        return customBackgrounds;
    case BT_Sys:
        return sysBackgrounds;
    case BT_All:
        return backgrounds;
    default:
        return {};
    }
}
Backgrounds::BackgroundType Backgrounds::getBackgroundType(QString id)
{
    const QString path = utils::deCodeURI(id);
    if (utils::isSolidWallpaper(path)) {
        return BT_Solid;
    }

    for (const auto &dir : systemWallpapersDir) {
        if (path.startsWith(dir)) {
            return BT_Sys;
        }
    }

    return BT_Custom;
}
