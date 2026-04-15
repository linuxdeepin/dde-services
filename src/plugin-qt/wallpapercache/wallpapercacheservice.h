// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef WALLPAPER_CACHE_SERVICE_H
#define WALLPAPER_CACHE_SERVICE_H

#include <QObject>
#include <QDBusUnixFileDescriptor>

class WallpaperCacheService : public QObject
{
    Q_OBJECT
    // Primary D-Bus interface name (for introspection)
    Q_CLASSINFO("D-Bus Interface", "org.deepin.dde.WallpaperCache")
    // Note: org.deepin.dde.ImageEffect1 compatibility interface is mounted
    // on a separate object path via plugin.cpp; not declared here to avoid
    // overriding the primary interface name.

public:
    explicit WallpaperCacheService(QObject *parent = nullptr);

public Q_SLOTS:
    // Scale wallpaper to multiple screen sizes; returns cached paths if available,
    // otherwise triggers async processing and immediately returns original path.
    QStringList GetProcessedImagePaths(const QString &originalPath, const QVariantList &sizeArray);
    // Same as above, but reads source image from a file descriptor.
    QStringList GetProcessedImagePathByFd(const QDBusUnixFileDescriptor &fd, const QString &imagePathMd5, const QVariantList &sizeArray);

    // Synchronously generates and returns the blurred wallpaper path (no scaling).
    QString GetBlurImagePath(const QString &originalPath);

    // Sync blur + async scaling; returns cached scaled paths if available,
    // otherwise immediately returns the blurred (or original) image path.
    QStringList GetProcessedImageWithBlur(const QString &originalPath, const QVariantList &sizeArray, bool needBlur);
    // Same as above, but reads source image from a file descriptor; returns list.
    QStringList GetProcessedImagePathByFdWithBlur(const QDBusUnixFileDescriptor &fd, const QString &imagePathMd5, const QVariantList &sizeArray, bool needBlur);

    // Sync blur + async multi-screen scaling; returns cached paths per size if available,
    // otherwise immediately returns the blurred (or original) image path.
    QStringList GetWallpaperListForScreen(const QString &originalPath, const QVariantList &sizeArray, bool needBlur = true);

    // ImageEffect compatibility interfaces (replaces dde-daemon ImageEffect service)
    // ImageEffect1 compat: get processed image path for given effect (only "pixmix"/blur supported).
    QString Get(const QString &effect, const QString &filename);
    // ImageEffect1 compat: delete cached effect image; effect "all" removes all caches.
    void Delete(const QString &effect, const QString &filename);

public:
    // Management interfaces (not exported via D-Bus, for internal/CLI use)
    bool DeleteBlurImage(const QString &originalPath);
    void ClearBlurCache();
    void ClearEffectCache(const QString &effect = "all");

private:
    QString saveImageFromFd(const QDBusUnixFileDescriptor &fd, const QString &imagePathMd5);
    static QList<QSize> parseSizeArray(const QVariantList &sizeArray);
};

class ImageEffect1Service : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.deepin.dde.ImageEffect1")
public:
    explicit ImageEffect1Service(WallpaperCacheService *target, QObject *parent = nullptr)
        : QObject(parent), m_target(target) {}

public Q_SLOTS:
    QString Get(const QString &effect, const QString &filename) {
        return m_target->Get(effect, filename);
    }
    void Delete(const QString &effect, const QString &filename) {
        m_target->Delete(effect, filename);
    }
private:
    WallpaperCacheService *m_target;
};

class ImageBlur1Service : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.deepin.dde.ImageBlur1")
public:
    explicit ImageBlur1Service(WallpaperCacheService *target, QObject *parent = nullptr)
        : QObject(parent), m_target(target) {}

public Q_SLOTS:
    QString Get(const QString &filename) {
        return m_target->GetBlurImagePath(filename);
    }
    void Delete(const QString &filename) {
        m_target->DeleteBlurImage(filename);
    }
    
Q_SIGNALS:
    void BlurDone(const QString &file, const QString &blurFile, bool ok);

private:
    WallpaperCacheService *m_target;
};

#endif // WALLPAPER_CACHE_SERVICE_H
