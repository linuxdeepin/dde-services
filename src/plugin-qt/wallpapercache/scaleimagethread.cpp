// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "scaleimagethread.h"

#include <QImage>
#include <QDebug>
#include <QFileInfo>
#include <QDateTime>
#include <QCoreApplication>
#include <QCryptographicHash>

ScaleImageThread::ScaleImageThread(QObject *parent)
    : QThread(parent)
{
}

ScaleImageThread::~ScaleImageThread()
{
    stopThread();
    wait();
}

void ScaleImageThread::stopThread()
{
    QMutexLocker locker(&m_mutex);
    m_stop = true;
    m_waitCondition.wakeOne();
}

void ScaleImageThread::setCachePath(const QString &path)
{
    m_cachePath = path;
}

void ScaleImageThread::addTask(const QString &originalPath, const QSize &targetSize)
{
    if (!QFile::exists(originalPath)) {
        qWarning() << "file not exists:" << originalPath;
        return;
    }

    TaskData task;
    task.originalPath = originalPath;
    task.targetSize = targetSize;

    QMutexLocker locker(&m_mutex);
    if (!m_tasks.contains(task)) {
        m_tasks.enqueue(task);
    }

    if (isRunning()) {
        m_waitCondition.wakeOne();
    } else {
        start();
    }
}

void ScaleImageThread::addTasks(const QString &originalPath, const QList<QSize> &sizes, bool isMd5Path)
{
    if (!QFile::exists(originalPath)) {
        qWarning() << "file not exists:" << originalPath;
        return;
    }

    QList<TaskData> tasks;
    for (const QSize &size : sizes) {
        TaskData data;
        data.originalPath = originalPath;
        data.targetSize = size;
        data.isMd5Path = isMd5Path;

        tasks.append(data);
    }

    QMutexLocker locker(&m_mutex);
    for (const TaskData &task : tasks) {
        if (!m_tasks.contains(task)) {
            m_tasks.enqueue(task);
        }
    }

    if (isRunning()) {
        m_waitCondition.wakeOne();
    } else {
        start();
    }
}

bool ScaleImageThread::isIdle()
{
    QMutexLocker locker(&m_mutex);
    return m_tasks.isEmpty();
}

void ScaleImageThread::run()
{
    while (!m_stop) {
        QMutexLocker locker(&m_mutex);

        if (m_tasks.isEmpty()) {
            m_waitCondition.wait(&m_mutex);
        }

        if (!m_tasks.isEmpty()) {
            TaskData task = m_tasks.head();
            locker.unlock();

            executeTask(task);

            QMutexLocker locker2(&m_mutex);
            m_tasks.dequeue();
        }
    }
}

void ScaleImageThread::executeTask(const TaskData &task)
{
    qDebug() << "task info:" << task.originalPath << " sizes:" << task.targetSize;
    auto pixmap = scaleImage(task);
    if (pixmap.isNull()) {
        qWarning() << "scale image failed:" << task.originalPath;
        return;
    }

    QString originalPathMd5 = pathMd5(task.originalPath, task.isMd5Path);
    QString cachedFilePath = cacheImageToDisk(pixmap, task, originalPathMd5);

    if (!cachedFilePath.isEmpty()) {
        Q_EMIT imageScaled(originalPathMd5, sizeToString(task.targetSize), cachedFilePath);

        if (task.isMd5Path) {
            QFile file(task.originalPath);
            if (file.exists()) {
                file.remove();
            }
        }
    }
}

QImage ScaleImageThread::scaleImage(const TaskData &task)
{
    qDebug() << "scale image info:" << task.originalPath << " targetSize:" << task.targetSize;
    QImage image;
    image.load(task.originalPath);
    if (image.isNull()) {
        qWarning() << "load image failed:" << task.originalPath;
        return image;
    }

    QSize size = task.targetSize;
    image = image.scaled(size, Qt::KeepAspectRatioByExpanding, Qt::FastTransformation);
    image = image.copy(QRect((image.width() - size.width()) / 2,
                             (image.height() - size.height()) / 2,
                             size.width(),
                             size.height()));

    return image;
}

QString ScaleImageThread::cacheImageToDisk(QImage &image, const TaskData &task, const QString &md5String)
{
    QFileInfo originalFileInfo(task.originalPath);

    QString format = originalFileInfo.suffix(); // TODO format is null ?
    // md5_1920x1080.jpg
    QString fileName = md5String + "_" + sizeToString(task.targetSize) + "." + format;

    QString filePath = m_cachePath + "/" + fileName;
    if (image.save(filePath, format.toStdString().c_str(), 100)) {
        // Set the timestamp of the saved file to the original image's timestamp
        QFile file(filePath);
        //file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
        file.setFileTime(originalFileInfo.lastModified(), QFileDevice::FileModificationTime);
    } else {
        qWarning() << "save image failed:" << filePath;
        filePath.clear();
    }

    return filePath;
}

QString ScaleImageThread::sizeToString(const QSize &size)
{
    return QString("%1x%2").arg(size.width()).arg(size.height());
}

QString ScaleImageThread::pathMd5(const QString &path, bool isMd5Path)
{
    QString pathmd5;
    if (isMd5Path) {
        QString filename = path.split("/").last();
        pathmd5 = filename.split(".").first();
    } else {
        pathmd5 = QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Md5).toHex();
    }

    return pathmd5;
}
