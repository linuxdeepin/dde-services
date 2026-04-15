// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

// dde-wallpaper-cache functional test program
// Build: see tests/CMakeLists.txt
// Run:   sudo ./test_wallpaper_cache [wallpaper_path]

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusUnixFileDescriptor>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QDebug>
#include <QThread>
#include <QElapsedTimer>
#include <QVariantList>
#include <QSize>
#include <QImageReader>
#include <QImage>
#include <QDir>

// ---------- Color output ----------
#define PASS  "\033[0;32m[PASS]\033[0m "
#define FAIL  "\033[0;31m[FAIL]\033[0m "
#define INFO  "\033[1;33m[INFO]\033[0m "
#define SECT  "\033[1;36m[====]\033[0m "

static int g_pass = 0, g_fail = 0;

static void checkTrue(bool cond, const QString &msg)
{
    if (cond) { qInfo().noquote() << QString(PASS) + msg; ++g_pass; }
    else       { qWarning().noquote() << QString(FAIL) + msg; ++g_fail; }
}

// Pack QSize list into QVariantList expected by WallpaperCache D-Bus interfaces
static QVariantList makeSizeArray(const QList<QSize> &sizes)
{
    QVariantList list;
    for (const QSize &s : sizes) {
        list << QVariant::fromValue(s);
    }
    return list;
}

// Validate that a file is a loadable image and return its dimensions
static QSize validateImageFile(const QString &path, const QString &label)
{
    QImageReader reader(path);
    QSize size = reader.size();
    bool canRead = reader.canRead();

    checkTrue(canRead, QString("%1 is a valid image file").arg(label));
    if (canRead && size.isValid()) {
        qInfo().noquote() << INFO << QString("%1 dimensions: %2x%3")
                                        .arg(label).arg(size.width()).arg(size.height());
    }
    return size;
}

// ---------- Main test logic ----------
void runTests(const QString &wallpaper)
{
    const QString SVC_WC  = "org.deepin.dde.WallpaperCache";
    const QString PATH_WC = "/org/deepin/dde/WallpaperCache";
    const QString SVC_IE  = "org.deepin.dde.ImageEffect1";
    const QString PATH_IE = "/org/deepin/dde/ImageEffect1";
    const QString SVC_IB  = "org.deepin.dde.ImageBlur1";
    const QString PATH_IB = "/org/deepin/dde/ImageBlur1";
    const QString BLUR_CACHE_DIR = "/var/cache/dde-wallpaper-cache/blur";

    QDBusInterface wcIface(SVC_WC,  PATH_WC,  SVC_WC,  QDBusConnection::systemBus());
    QDBusInterface ieIface(SVC_IE,  PATH_IE,  SVC_IE,  QDBusConnection::systemBus());

    // Get original image dimensions for comparison
    QImageReader origReader(wallpaper);
    QSize origSize = origReader.size();

    // =====================================================================
    qInfo().noquote() << QString(SECT) + "Pre-checks";
    checkTrue(QFile::exists(wallpaper), QString("Wallpaper file exists: ") + wallpaper);
    checkTrue(wcIface.isValid(), "WallpaperCache service reachable");
    checkTrue(ieIface.isValid(), "ImageEffect1 compatibility service reachable");
    if (!wcIface.isValid() || !ieIface.isValid()) {
        qCritical() << FAIL "Services unreachable, please start dde-wallpaper-cache first";
        return;
    }
    if (origSize.isValid()) {
        qInfo().noquote() << INFO << "Original image dimensions:" << origSize;
    }

    // =====================================================================
    qInfo().noquote() << QString(SECT) + "Test 1: ImageEffect1.Get() blur image generation";
    QString blurPath;
    {
        QElapsedTimer timer; timer.start();
        QDBusReply<QString> reply = ieIface.call("Get", QString(""), wallpaper);
        qint64 elapsed = timer.elapsed();

        checkTrue(reply.isValid(), QString("Get() call succeeded (%1ms)").arg(elapsed));
        blurPath = reply.value();
        qInfo().noquote() << INFO << "Blur image path:" << blurPath;
        checkTrue(!blurPath.isEmpty(), "Get() returned non-empty path");
        checkTrue(QFile::exists(blurPath), "Blur image file exists on disk");
        checkTrue(QFileInfo(blurPath).size() > 0, "Blur image file is not empty");
        qInfo().noquote() << INFO << "File size:"
                          << QFileInfo(blurPath).size() / 1024 << "KB";

        // Validate cache directory
        checkTrue(blurPath.startsWith(BLUR_CACHE_DIR),
                  "Blur image stored in correct cache directory: " + BLUR_CACHE_DIR);

        // Validate it's a real image and check dimensions
        QSize blurSize = validateImageFile(blurPath, "Blur image");
        if (origSize.isValid() && blurSize.isValid()) {
            checkTrue(origSize == blurSize,
                      QString("Blur image dimensions match original: %1x%2")
                          .arg(blurSize.width()).arg(blurSize.height()));
        }
    }

    // =====================================================================
    qInfo().noquote() << QString(SECT) + "Test 2: Cache hit (second call should be faster)";
    {
        QElapsedTimer timer; timer.start();
        QDBusReply<QString> reply2 = ieIface.call("Get", QString(""), wallpaper);
        qint64 elapsed = timer.elapsed();

        checkTrue(reply2.isValid() && !reply2.value().isEmpty(), "Second Get() succeeded");
        checkTrue(reply2.value() == blurPath, "Second call returned same path");
        checkTrue(elapsed < 500, QString("Cache hit response < 500ms, actual %1ms").arg(elapsed));
        qInfo().noquote() << INFO << "Second call time:" << elapsed << "ms";
    }

    // =====================================================================
    qInfo().noquote() << QString(SECT) + "Test 3: WallpaperCache.GetBlurImagePath()";
    {
        QDBusReply<QString> reply = wcIface.call("GetBlurImagePath", wallpaper);
        checkTrue(reply.isValid(), "GetBlurImagePath() call succeeded");
        checkTrue(reply.value() == blurPath,
                  "GetBlurImagePath matches ImageEffect1.Get result");
        qInfo().noquote() << INFO << "GetBlurImagePath returned:" << reply.value();
    }

    // =====================================================================
    qInfo().noquote() << QString(SECT) + "Test 4: Get(\"pixmix\") explicit effect name";
    {
        QDBusReply<QString> reply = ieIface.call("Get", QString("pixmix"), wallpaper);
        checkTrue(reply.isValid(), "Get(\"pixmix\") call succeeded");
        checkTrue(reply.value() == blurPath,
                  "Get(\"pixmix\") returns same path as Get(\"\")");
    }

    // =====================================================================
    qInfo().noquote() << QString(SECT) + "Test 5: Get() with unsupported effect";
    {
        QDBusReply<QString> reply = ieIface.call("Get", QString("unsupported_effect"), wallpaper);
        checkTrue(reply.isValid(), "Get(\"unsupported_effect\") call did not crash");
        checkTrue(reply.value().isEmpty(),
                  "Get(\"unsupported_effect\") returned empty (expected)");
    }

    // =====================================================================
    qInfo().noquote() << QString(SECT) + "Test 6: GetProcessedImagePaths() size scaling";
    {
        QList<QSize> sizes = {QSize(1920, 1080)};
        QVariantList sizeArray = makeSizeArray(sizes);

        QDBusReply<QStringList> reply = wcIface.call("GetProcessedImagePaths",
                                                      wallpaper, sizeArray);
        checkTrue(reply.isValid(), "GetProcessedImagePaths() call succeeded");
        qInfo().noquote() << INFO << "First call returned:" << reply.value();

        // Wait for async processing
        qInfo().noquote() << INFO << "Waiting 3s for async scaling to complete...";
        QThread::sleep(3);

        QDBusReply<QStringList> reply2 = wcIface.call("GetProcessedImagePaths",
                                                       wallpaper, sizeArray);
        checkTrue(reply2.isValid(), "Second GetProcessedImagePaths() call succeeded");
        qInfo().noquote() << INFO << "Second call returned:" << reply2.value();

        bool hasScaled = false;
        for (const QString &p : reply2.value()) {
            if (p.contains("_") && QFile::exists(p)) {
                hasScaled = true;
                qInfo().noquote() << INFO << "Scaled cache file:" << p
                                  << "size:" << QFileInfo(p).size() / 1024 << "KB";

                // Validate scaled image dimensions
                QSize scaledSize = validateImageFile(p, "Scaled image");
                if (scaledSize.isValid()) {
                    checkTrue(scaledSize == QSize(1920, 1080),
                              QString("Scaled image dimensions correct: %1x%2")
                                  .arg(scaledSize.width()).arg(scaledSize.height()));
                }
            }
        }
        checkTrue(hasScaled, "Scaled cache file generated (path contains _ size marker)");
    }

    // =====================================================================
    qInfo().noquote() << QString(SECT) + "Test 7: GetProcessedImageWithBlur() blur+size in one step";
    {
        QList<QSize> sizes = {QSize(1920, 1080)};
        QVariantList sizeArray = makeSizeArray(sizes);

        QDBusReply<QStringList> reply = wcIface.call("GetProcessedImageWithBlur",
                                                  wallpaper, sizeArray, true);
        checkTrue(reply.isValid(), "GetProcessedImageWithBlur(needBlur=true) call succeeded");
        QStringList results = reply.value();
        qInfo().noquote() << INFO << "Returned paths:" << results;
        checkTrue(!results.isEmpty(), "Returned non-empty list");

        if (!results.isEmpty() && QFile::exists(results.first())) {
            validateImageFile(results.first(), "Blur+scaled image");
        }

        // needBlur=false should return non-blur path
        QDBusReply<QStringList> replyNoBlur = wcIface.call("GetProcessedImageWithBlur",
                                                        wallpaper, sizeArray, false);
        checkTrue(replyNoBlur.isValid(), "GetProcessedImageWithBlur(needBlur=false) call succeeded");
        QStringList noBlurResults = replyNoBlur.value();
        qInfo().noquote() << INFO << "needBlur=false returned:" << noBlurResults;
        checkTrue(!noBlurResults.isEmpty() && !noBlurResults.first().contains("/blur/"),
                  "needBlur=false result not from blur directory");
    }

    // =====================================================================
    qInfo().noquote() << QString(SECT) + "Test 8: GetProcessedImagePathByFd() image via FD";
    {
        QFile file(wallpaper);
        if (file.open(QIODevice::ReadOnly)) {
            int fd = file.handle();
            QDBusUnixFileDescriptor dbusFd(fd);
            QString md5 = QCryptographicHash::hash(wallpaper.toUtf8(),
                                                   QCryptographicHash::Md5).toHex();
            QList<QSize> sizes = {QSize(2560, 1440)};
            QVariantList sizeArray = makeSizeArray(sizes);

            QDBusReply<QStringList> reply = wcIface.call("GetProcessedImagePathByFd",
                                                          QVariant::fromValue(dbusFd),
                                                          md5, sizeArray);
            checkTrue(reply.isValid(), "GetProcessedImagePathByFd() call succeeded");
            qInfo().noquote() << INFO << "Returned:" << reply.value();
            file.close();
        } else {
            qWarning().noquote() << FAIL "Cannot open wallpaper file";
            ++g_fail;
        }
    }

    // =====================================================================
    qInfo().noquote() << QString(SECT) + "Test 9: GetProcessedImagePathByFdWithBlur()";
    {
        QFile file(wallpaper);
        if (file.open(QIODevice::ReadOnly)) {
            int fd = file.handle();
            QDBusUnixFileDescriptor dbusFd(fd);
            QString md5 = QCryptographicHash::hash(wallpaper.toUtf8(),
                                                   QCryptographicHash::Md5).toHex();
            QList<QSize> sizes = {QSize(1920, 1080)};
            QVariantList sizeArray = makeSizeArray(sizes);

            QDBusReply<QStringList> reply = wcIface.call("GetProcessedImagePathByFdWithBlur",
                                                          QVariant::fromValue(dbusFd),
                                                          md5, sizeArray, true);
            checkTrue(reply.isValid(), "GetProcessedImagePathByFdWithBlur(needBlur=true) call succeeded");
            qInfo().noquote() << INFO << "Returned:" << reply.value();
            file.close();
        } else {
            ++g_fail;
        }
    }

    // =====================================================================
    qInfo().noquote() << QString(SECT) + "Test 10: ImageEffect1.Delete() delete and rebuild";
    {
        // Ensure file exists first
        QDBusReply<QString> before = ieIface.call("Get", QString(""), wallpaper);
        QString delBlurPath = before.value();
        bool existsBefore = QFile::exists(delBlurPath);
        checkTrue(existsBefore, "Blur image exists before delete: " + delBlurPath);

        // Delete
        QDBusPendingReply<> delReply = ieIface.asyncCall("Delete", QString("pixmix"), wallpaper);
        delReply.waitForFinished();
        checkTrue(!delReply.isError(), "Delete() call no error");

        QThread::msleep(200);
        bool existsAfter = QFile::exists(delBlurPath);
        checkTrue(!existsAfter, "File deleted after Delete()");

        // Regenerate
        QDBusReply<QString> regen = ieIface.call("Get", QString(""), wallpaper);
        checkTrue(regen.isValid() && QFile::exists(regen.value()),
                  "Regenerated blur image after delete");
        qInfo().noquote() << INFO << "Regenerated path:" << regen.value();
    }

    // =====================================================================
    qInfo().noquote() << QString(SECT) + "Test 11: Delete(\"all\") deletes all effect caches";
    {
        // Ensure blur image exists
        QDBusReply<QString> before = ieIface.call("Get", QString(""), wallpaper);
        QString allBlurPath = before.value();
        checkTrue(QFile::exists(allBlurPath), "Blur image exists before Delete(\"all\")");

        QDBusPendingReply<> delAll = ieIface.asyncCall("Delete", QString("all"), wallpaper);
        delAll.waitForFinished();
        checkTrue(!delAll.isError(), "Delete(\"all\") call no error");

        QThread::msleep(200);
        checkTrue(!QFile::exists(allBlurPath),
                  "Delete(\"all\") actually removed the blur cache file");

        // Restore
        ieIface.call("Get", QString(""), wallpaper);
    }

    // =====================================================================
    qInfo().noquote() << QString(SECT) + "Test 12: Non-existent file error handling";
    {
        QString fakePath = "/tmp/nonexistent_wallpaper_12345.jpg";

        QDBusReply<QString> reply = ieIface.call("Get", QString(""), fakePath);
        qInfo().noquote() << INFO << "Non-existent file Get() result:" << reply.value()
                          << " (error:" << reply.error().message() << ")";
        checkTrue(true, "Service did not crash on non-existent file");

        // Verify service is still alive
        QDBusReply<QString> alive = wcIface.call("GetBlurImagePath", wallpaper);
        checkTrue(alive.isValid(), "Service still responsive after error handling");
    }

    // =====================================================================
    qInfo().noquote() << QString(SECT) + "Test 13: ImageBlur1.Get() compatibility interface";
    {
        QDBusInterface ibIface(SVC_IB, PATH_IB, SVC_IB, QDBusConnection::systemBus());
        checkTrue(ibIface.isValid(), "ImageBlur1 compatibility service reachable");

        if (ibIface.isValid()) {
            QDBusReply<QString> reply = ibIface.call("Get", wallpaper);
            checkTrue(reply.isValid(), "ImageBlur1.Get() call succeeded");
            QString ibBlurPath = reply.value();
            qInfo().noquote() << INFO << "ImageBlur1.Get returned:" << ibBlurPath;
            checkTrue(!ibBlurPath.isEmpty(), "ImageBlur1.Get() returned non-empty path");
            checkTrue(QFile::exists(ibBlurPath), "ImageBlur1.Get() file exists");

            // Consistency check with WallpaperCache.GetBlurImagePath
            QDBusReply<QString> wcReply = wcIface.call("GetBlurImagePath", wallpaper);
            checkTrue(ibBlurPath == wcReply.value(),
                      "ImageBlur1.Get matches GetBlurImagePath");
        }
    }

    // =====================================================================
    qInfo().noquote() << QString(SECT) + "Test 14: ImageBlur1.Delete() compatibility interface";
    {
        QDBusInterface ibIface(SVC_IB, PATH_IB, SVC_IB, QDBusConnection::systemBus());

        if (ibIface.isValid()) {
            // Ensure blur image exists
            QDBusReply<QString> before = ibIface.call("Get", wallpaper);
            QString ibBlurPath = before.value();
            checkTrue(QFile::exists(ibBlurPath), "Blur image exists before ImageBlur1.Delete");

            // Delete
            QDBusPendingReply<> delReply = ibIface.asyncCall("Delete", wallpaper);
            delReply.waitForFinished();
            checkTrue(!delReply.isError(), "ImageBlur1.Delete() call no error");

            QThread::msleep(200);
            checkTrue(!QFile::exists(ibBlurPath), "ImageBlur1.Delete() file removed");

            // Restore
            ibIface.call("Get", wallpaper);
        }
    }

    // =====================================================================
    qInfo().noquote() << QString(SECT) + "Test 15: GetWallpaperListForScreen() multi-size";
    {
        QList<QSize> sizes = {QSize(1920, 1080), QSize(2560, 1440)};
        QVariantList sizeArray = makeSizeArray(sizes);

        QDBusReply<QStringList> reply = wcIface.call("GetWallpaperListForScreen",
                                                      wallpaper, sizeArray, true);
        checkTrue(reply.isValid(), "GetWallpaperListForScreen(needBlur=true) call succeeded");
        QStringList results = reply.value();
        qInfo().noquote() << INFO << "Returned list:" << results;
        checkTrue(!results.isEmpty(), "GetWallpaperListForScreen returned non-empty list");

        // Validate each returned file
        for (const QString &p : results) {
            if (QFile::exists(p)) {
                validateImageFile(p, "WallpaperListForScreen item");
            }
        }

        // needBlur=false
        QDBusReply<QStringList> reply2 = wcIface.call("GetWallpaperListForScreen",
                                                       wallpaper, sizeArray, false);
        checkTrue(reply2.isValid(), "GetWallpaperListForScreen(needBlur=false) call succeeded");
        qInfo().noquote() << INFO << "needBlur=false returned:" << reply2.value();
    }

    // =====================================================================
    qInfo().noquote() << QString(SECT) + "Summary";
    qInfo().noquote() << QString("  Passed: %1  Failed: %2  Total: %3").arg(g_pass).arg(g_fail).arg(g_pass+g_fail);
    if (g_fail == 0)
        qInfo().noquote() << QString(PASS) + "All tests passed!";
    else
        qWarning().noquote() << QString(FAIL) + QString("%1 test(s) failed").arg(g_fail);
}

// ---------- main ----------
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QString wallpaper = argc > 1 ? argv[1] : "/usr/share/backgrounds/default_background.jpg";
    qInfo().noquote() << "\n" << INFO << "dde-wallpaper-cache functional test";
    qInfo().noquote() << INFO << "Test wallpaper:" << wallpaper;
    qInfo().noquote() << INFO << "Tip: observe log with: sudo journalctl -u dde-wallpaper-cache -f\n";

    QTimer::singleShot(0, &app, [&]() {
        runTests(wallpaper);
        QCoreApplication::exit(g_fail > 0 ? 1 : 0);
    });

    return app.exec();
}
