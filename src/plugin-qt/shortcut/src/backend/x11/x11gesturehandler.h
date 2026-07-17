// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "backend/abstractgesturehandler.h"

#include <QMap>

class SessionGestureGuard;
class SystemGestureProxy;
class QTimer;
class X11GestureActionExecutor;

class X11GestureHandler : public AbstractGestureHandler
{
    Q_OBJECT
public:
    explicit X11GestureHandler(X11GestureActionExecutor *executor, QObject *parent = nullptr);
    ~X11GestureHandler() override;

    bool registerGesture(const GestureConfig &config) override;
    bool unregisterGesture(const QString &id) override;
    bool isAvailable() const override;

private slots:
    void onEvent(const QString &name, const QString &direction, int fingers);
    void onAvailabilityChanged(bool available);
    void onDoubleClickDown(int fingers);
    void onSwipeMoving(int fingers, double accelX, double accelY);
    void onSwipeStopped(int fingers);

private:
    static int gestureType(const QString &name);
    static int direction(const QString &direction);
    void stopWindowMove();

    SystemGestureProxy *m_proxy = nullptr;
    SessionGestureGuard *m_guard = nullptr;
    X11GestureActionExecutor *m_executor = nullptr;
    QTimer *m_windowMoveTimeout = nullptr;
    bool m_windowMoveActive = false;
    QMap<QString, GestureConfig> m_bindings;
};
