// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef SCALE_IMAGE_THREAD_H
#define SCALE_IMAGE_THREAD_H

#include <QSize>
#include <QThread>
#include <QQueue>
#include <QMutex>
#include <QWaitCondition>

class ScaleImageThread : public QThread
{
    Q_OBJECT
public:
    explicit ScaleImageThread(QObject *parent = nullptr);
    ~ScaleImageThread() override;

    void stopThread();
    void setCachePath(const QString &path);
    void addTask(const QString &originalPath, const QSize &targetSize);
    void addTasks(const QString &originalPath, const QList<QSize> &sizes, bool isMd5Path);
    bool isIdle();

    static QString pathMd5(const QString &path, bool isMd5Path);
    static QString sizeToString(const QSize &size);

signals:
    void imageScaled(const QString &originalPath, const QString &size, const QString &scaledPath);

protected:
    void run() override;

private:
    struct TaskData {
        QString originalPath;
        QSize targetSize;
        bool isMd5Path = false;

        bool operator==(const TaskData &other) const
        {
            return (originalPath == other.originalPath) && (targetSize == other.targetSize) && (isMd5Path == other.isMd5Path);
        }
    };

private:
    QImage scaleImage(const TaskData &task);
    QString cacheImageToDisk(QImage &pixmap, const TaskData &task, const QString &md5);

    void executeTask(const TaskData &task);

private:
    QMutex m_mutex;
    QWaitCondition m_waitCondition;
    QQueue<TaskData> m_tasks;

    bool m_stop = false;
    QString m_cachePath;
};

#endif // SCALE_IMAGE_THREAD_H
