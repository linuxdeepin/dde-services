// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <QObject>

class QDBusServiceWatcher;

class SystemGestureProxy : public QObject
{
    Q_OBJECT
public:
    explicit SystemGestureProxy(QObject *parent = nullptr);

    bool isAvailable() const { return m_available; }

signals:
    void eventReceived(const QString &name, const QString &direction, int fingers);
    void doubleClickDown(int fingers);
    void swipeMoving(int fingers, double accelX, double accelY);
    void swipeStopped(int fingers);
    void availabilityChanged(bool available);

private slots:
    void onServiceRegistered();
    void onServiceUnregistered();
    void onEvent(const QString &name, const QString &direction, int fingers);
    void onDoubleClickDown(int fingers);
    void onSwipeMoving(int fingers, double accelX, double accelY);
    void onSwipeStopped(int fingers);

private:
    bool connectEventSignal();
    void disconnectEventSignal();
    void setAvailable(bool available);

    QDBusServiceWatcher *m_watcher = nullptr;
    bool m_connected = false;
    bool m_available = false;
};
