// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "x11gesturehandler.h"
#include "systemgestureproxy.h"
#include "x11gestureactionexecutor.h"
#include "core/sessiongestureguard.h"

#include <QTimer>

#include <utility>

namespace {

constexpr int WindowMoveTimeoutMs = 5000;

}

X11GestureHandler::X11GestureHandler(X11GestureActionExecutor *executor, QObject *parent)
    : AbstractGestureHandler(parent)
    , m_proxy(new SystemGestureProxy(this))
    , m_guard(new SessionGestureGuard(this))
    , m_executor(executor)
    , m_windowMoveTimeout(new QTimer(this))
{
    m_windowMoveTimeout->setSingleShot(true);
    m_windowMoveTimeout->setInterval(WindowMoveTimeoutMs);
    connect(m_windowMoveTimeout, &QTimer::timeout,
            this, &X11GestureHandler::stopWindowMove);
    connect(m_proxy, &SystemGestureProxy::eventReceived,
            this, &X11GestureHandler::onEvent);
    connect(m_proxy, &SystemGestureProxy::availabilityChanged,
            this, &X11GestureHandler::onAvailabilityChanged);
    connect(m_proxy, &SystemGestureProxy::doubleClickDown,
            this, &X11GestureHandler::onDoubleClickDown);
    connect(m_proxy, &SystemGestureProxy::swipeMoving,
            this, &X11GestureHandler::onSwipeMoving);
    connect(m_proxy, &SystemGestureProxy::swipeStopped,
            this, &X11GestureHandler::onSwipeStopped);
}

X11GestureHandler::~X11GestureHandler()
{
    stopWindowMove();
}

bool X11GestureHandler::registerGesture(const GestureConfig &config)
{
    if (!isAvailable())
        return false;
    m_bindings.insert(config.getId(), config);
    return true;
}

bool X11GestureHandler::unregisterGesture(const QString &id)
{
    return m_bindings.remove(id) > 0;
}

bool X11GestureHandler::isAvailable() const
{
    return m_proxy->isAvailable();
}

void X11GestureHandler::onEvent(const QString &name, const QString &directionName, int fingers)
{
    if (!m_guard->canHandleTouchpadGesture(name))
        return;

    const int type = gestureType(name);
    const int directionValue = direction(directionName);
    if (type == 0 || directionValue < 0)
        return;

    for (const GestureConfig &config : std::as_const(m_bindings)) {
        if (config.gestureType == type
                && config.fingerCount == fingers
                && config.direction == directionValue) {
            emit activated(config.getId());
            return;
        }
    }
}

void X11GestureHandler::onDoubleClickDown(int fingers)
{
    if (fingers != 3 || !m_executor || !m_guard->canBeginWindowMove())
        return;
    if (m_windowMoveActive)
        return;

    if (m_executor->beginWindowMove()) {
        m_windowMoveActive = true;
        m_windowMoveTimeout->start();
    }
}

void X11GestureHandler::onSwipeMoving(int fingers, double accelX, double accelY)
{
    if (fingers != 3 || !m_windowMoveActive || !m_executor)
        return;

    if (!m_guard->canHandleTouchpadEvent() || !m_executor->updateWindowMove(accelX, accelY)) {
        stopWindowMove();
        return;
    }
    m_windowMoveTimeout->start();
}

void X11GestureHandler::onSwipeStopped(int fingers)
{
    if (fingers == 3)
        stopWindowMove();
}

void X11GestureHandler::onAvailabilityChanged(bool available)
{
    if (!available) {
        stopWindowMove();
        m_bindings.clear();
    }
    emit availabilityChanged(available);
}

void X11GestureHandler::stopWindowMove()
{
    if (!m_windowMoveActive)
        return;

    m_windowMoveActive = false;
    m_windowMoveTimeout->stop();
    if (m_executor)
        m_executor->endWindowMove();
}

int X11GestureHandler::gestureType(const QString &name)
{
    if (name == QLatin1String("swipe"))
        return int(GestureType::Swipe);
    if (name == QLatin1String("tap"))
        return int(GestureType::Hold);
    return 0;
}

int X11GestureHandler::direction(const QString &directionName)
{
    if (directionName == QLatin1String("none"))
        return 0;
    if (directionName == QLatin1String("down"))
        return 1;
    if (directionName == QLatin1String("left"))
        return 2;
    if (directionName == QLatin1String("up"))
        return 3;
    if (directionName == QLatin1String("right"))
        return 4;
    return -1;
}
