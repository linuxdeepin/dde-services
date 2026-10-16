// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "cachedwallpaper.h"
#include "scaleimagethread.h"
#include "wallpapercacheservice.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <fcntl.h>
#include <unistd.h>

class WallpaperCacheTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(kWallpaperCacheDir.contains("/unit-cache"));
        QVERIFY(QDir().mkpath(kWallpaperCacheDir));
    }

    void repeatedImportReplacesLongerImage()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString input = dir.filePath("input.bmp");
        const QString key(32, '1');
        WallpaperCacheService service;
        QImage large(300, 200, QImage::Format_RGB32);
        large.fill(Qt::red);
        QVERIFY(large.save(input));
        QFile file(input);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto first = service.GetProcessedImagePathByFd(QDBusUnixFileDescriptor(file.handle()), key, {});
        QCOMPARE(first.size(), 1);
        const qint64 largeSize = QFileInfo(first.first()).size();
        file.close();

        QImage small(30, 20, QImage::Format_RGB32);
        small.fill(Qt::blue);
        QVERIFY(small.save(input));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto second = service.GetProcessedImagePathByFd(QDBusUnixFileDescriptor(file.handle()), key, {});
        QCOMPARE(second, first);
        QCOMPARE(QFileInfo(second.first()).size(), file.size());
        QVERIFY(QFileInfo(second.first()).size() < largeSize);
        QCOMPARE(QImage(second.first()).size(), small.size());
        QCOMPARE(QImage(second.first()).pixelColor(0, 0), QColor(Qt::blue));
    }

    void invalidImportPreservesPublishedImage()
    {
        QTemporaryDir dir;
        const QString input = dir.filePath("input.png");
        QImage image(20, 20, QImage::Format_RGB32);
        image.fill(Qt::green);
        QVERIFY(image.save(input));
        QFile file(input);
        QVERIFY(file.open(QIODevice::ReadOnly));
        WallpaperCacheService service;
        const QString key(32, '2');
        const auto first = service.GetProcessedImagePathByFd(QDBusUnixFileDescriptor(file.handle()), key, {});
        QCOMPARE(first.size(), 1);
        file.close();
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(file.write("invalid image"), qint64(13));
        file.close();
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(service.GetProcessedImagePathByFd(QDBusUnixFileDescriptor(file.handle()), key, {}).isEmpty());
        QCOMPARE(QImage(first.first()).pixelColor(0, 0), QColor(Qt::green));
        QCOMPARE(QDir(kWallpaperCacheDir).entryList({".fd-import-*"}, QDir::Files | QDir::Hidden).size(), 0);
    }

    void descriptorReadFailureLeavesNoImport()
    {
        const int directoryFd = ::open("/tmp", O_RDONLY | O_DIRECTORY);
        QVERIFY(directoryFd >= 0);
        QDBusUnixFileDescriptor fd;
        fd.giveFileDescriptor(directoryFd);
        WallpaperCacheService service;
        QVERIFY(service.GetProcessedImagePathByFd(fd, QString(32, '3'), {}).isEmpty());
        QVERIFY(!QFile::exists(kWallpaperCacheDir + "/" + QString(32, '3')));
        QCOMPARE(QDir(kWallpaperCacheDir).entryList({".fd-import-*"}, QDir::Files | QDir::Hidden).size(), 0);
    }

    void rejectInvalidCacheKey()
    {
        QFile file("/dev/null");
        QVERIFY(file.open(QIODevice::ReadOnly));
        WallpaperCacheService service;
        QVERIFY(service.GetProcessedImagePathByFd(QDBusUnixFileDescriptor(file.handle()), "../outside", {}).isEmpty());
    }

    void completeCacheHitDoesNotReadDescriptor()
    {
        QTemporaryDir dir;
        const QString cached = dir.filePath("cached.png");
        QImage image(40, 30, QImage::Format_RGB32);
        image.fill(Qt::yellow);
        QVERIFY(image.save(cached));
        const QString key(32, '4');
        CachedWallpaper::instance()->cacheImage(key, "40x30", cached);
        QFile input(cached);
        QVERIFY(input.open(QIODevice::ReadOnly));
        const QVariantList sizes{QVariant::fromValue(QSize(40, 30))};
        WallpaperCacheService service;
        QDBusUnixFileDescriptor fd(input.handle());
        QCOMPARE(service.GetProcessedImagePathByFd(fd, key, sizes), QStringList{cached});
        QCOMPARE(::lseek(fd.fileDescriptor(), 0, SEEK_CUR), off_t(0));
        QCOMPARE(service.GetProcessedImagePathByFdWithBlur(fd, key, sizes, false), QStringList{cached});
        QCOMPARE(::lseek(fd.fileDescriptor(), 0, SEEK_CUR), off_t(0));
        QVERIFY(!QFile::exists(kWallpaperCacheDir + "/" + key + ".png"));
    }

    void partialCacheHitStillQueuesMissingSizes()
    {
        QTemporaryDir dir;
        const QString original = dir.filePath("input.png");
        QImage image(80, 60, QImage::Format_RGB32);
        image.fill(Qt::cyan);
        QVERIFY(image.save(original));
        const QString key(32, '5');
        const QString cached = dir.filePath("cached.png");
        QVERIFY(image.save(cached));
        CachedWallpaper::instance()->cacheImage(key, "40x30", cached);
        QFile input(original);
        QVERIFY(input.open(QIODevice::ReadOnly));
        QSignalSpy pending(CachedWallpaper::instance(), &CachedWallpaper::needHandleImage);
        WallpaperCacheService service;
        QCOMPARE(service.GetProcessedImagePathByFd(QDBusUnixFileDescriptor(input.handle()), key,
                     {QVariant::fromValue(QSize(40, 30)), QVariant::fromValue(QSize(20, 15))}), QStringList{cached});
        QCOMPARE(pending.size(), 1);
        QCOMPARE(pending.first().at(1).value<QList<QSize>>(), QList<QSize>{QSize(20, 15)});
        QVERIFY(QFile::exists(pending.first().at(0).toString()));
    }

    void removedCacheEntryIsRegenerated()
    {
        QTemporaryDir dir;
        const QString missing = dir.filePath("removed.png");
        QImage image(20, 20, QImage::Format_RGB32);
        QVERIFY(image.save(missing));
        const QString source = dir.filePath("source.png");
        const QString key = ScaleImageThread::pathMd5(source, false);
        CachedWallpaper::instance()->cacheImage(key, "20x20", missing);
        QVERIFY(QFile::remove(missing));
        QSignalSpy pending(CachedWallpaper::instance(), &CachedWallpaper::needHandleImage);
        QVERIFY(CachedWallpaper::instance()->getCachedImagePaths(source, {QSize(20, 20)}).isEmpty());
        QCOMPARE(pending.size(), 1);
    }

    void multipleSizesKeepFallbackSourceReadable()
    {
        QTemporaryDir dir;
        const QString source = dir.filePath(QString(32, '6') + ".png");
        QImage image(200, 150, QImage::Format_RGB32);
        image.fill(Qt::magenta);
        QVERIFY(image.save(source));
        ScaleImageThread worker;
        worker.setCachePath(dir.path());
        QSignalSpy scaled(&worker, &ScaleImageThread::imageScaled);
        worker.addTasks(source, {QSize(80, 60), QSize(40, 30)}, true);
        QTRY_COMPARE_WITH_TIMEOUT(scaled.size(), 2, 5000);
        for (const auto &reply : scaled) {
            const QString path = reply.at(2).toString();
            const QImage result(path);
            QVERIFY(!result.isNull());
            QCOMPARE(ScaleImageThread::sizeToString(result.size()), reply.at(1).toString());
        }
        QVERIFY(!QImage(source).isNull());
        worker.addTasks(source, {QSize(20, 15)}, true);
        QTRY_COMPARE_WITH_TIMEOUT(scaled.size(), 3, 5000);
        QCOMPARE(QImage(scaled.last().at(2).toString()).size(), QSize(20, 15));
        worker.stopThread();
        QVERIFY(worker.wait(5000));
    }

    void idleWorkerStops()
    {
        ScaleImageThread worker;
        worker.start();
        worker.stopThread();
        QVERIFY(worker.wait(5000));
    }
};

QTEST_GUILESS_MAIN(WallpaperCacheTest)
#include "test_wallpaper_cache_unit.moc"
