// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "sessiongestureguard.h"
#include "sessionactivemonitor.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QDBusReply>
#include <QTimer>
#include <QVariant>

#include <xcb/xcb.h>

namespace {

constexpr auto PropertiesInterface = "org.freedesktop.DBus.Properties";
constexpr auto TouchpadService = "org.deepin.dde.InputDevices1";
constexpr auto TouchpadPath = "/org/deepin/dde/InputDevice1/TouchPad";
constexpr auto TouchpadInterface = "org.deepin.dde.InputDevice1.TouchPad";
constexpr auto WmService = "com.deepin.wm";
constexpr auto WmPath = "/com/deepin/wm";
constexpr auto WmInterface = "com.deepin.wm";

QVariant unwrapDbusValue(const QVariant &value)
{
    if (value.metaType() == QMetaType::fromType<QDBusVariant>())
        return qvariant_cast<QDBusVariant>(value).variant();
    return value;
}

}

SessionGestureGuard::SessionGestureGuard(SessionActiveMonitor *sessionMonitor,
                                         QObject *parent)
    : QObject(parent)
    , m_sessionMonitor(sessionMonitor)
    , m_multitaskRefreshTimer(new QTimer(this))
{
    m_xConnection = xcb_connect(nullptr, nullptr);
    if (m_xConnection && !xcb_connection_has_error(m_xConnection)) {
        const xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(m_xConnection)).data;
        if (screen)
            m_rootWindow = screen->root;
    }

    QDBusConnection::sessionBus().connect(QLatin1String(TouchpadService),
                                          QLatin1String(TouchpadPath),
                                          QLatin1String(PropertiesInterface),
                                          QStringLiteral("PropertiesChanged"),
                                          this,
                                          SLOT(onTouchpadPropertiesChanged(QString,QVariantMap,QStringList)));

    auto *touchpadWatcher = new QDBusServiceWatcher(
            QLatin1String(TouchpadService), QDBusConnection::sessionBus(),
            QDBusServiceWatcher::WatchForRegistration
                | QDBusServiceWatcher::WatchForUnregistration, this);
    connect(touchpadWatcher, &QDBusServiceWatcher::serviceRegistered,
            this, [this]() { refreshTouchpadState(); });
    connect(touchpadWatcher, &QDBusServiceWatcher::serviceUnregistered,
            this, [this]() { m_touchpadEnabled = false; });

    auto *wmWatcher = new QDBusServiceWatcher(
            QLatin1String(WmService), QDBusConnection::sessionBus(),
            QDBusServiceWatcher::WatchForRegistration
                | QDBusServiceWatcher::WatchForUnregistration, this);
    connect(wmWatcher, &QDBusServiceWatcher::serviceRegistered,
            this, [this]() { refreshWmOwner(); });
    connect(wmWatcher, &QDBusServiceWatcher::serviceUnregistered,
            this, [this]() { setWmOwner(QString()); });

    refreshTouchpadState();
    refreshWmOwner();

    m_multitaskRefreshTimer->setInterval(1000);
    connect(m_multitaskRefreshTimer, &QTimer::timeout, this, [this]() {
        refreshWmOwner();
        refreshMultitaskState();
    });
    m_multitaskRefreshTimer->start();
    refreshMultitaskState();
}

SessionGestureGuard::~SessionGestureGuard()
{
    if (m_xConnection)
        xcb_disconnect(m_xConnection);
}

bool SessionGestureGuard::canHandleTouchpadGesture(const QString &gestureName) const
{
    if (!canHandleTouchpadEvent())
        return false;

    if (!isKeyboardGrabbed())
        return true;

    return gestureName == QLatin1String("swipe") && m_multitaskVisible;
}

bool SessionGestureGuard::canHandleTouchpadEvent() const
{
    return m_sessionMonitor && !m_sessionMonitor->isLocked()
            && m_sessionMonitor->isActive() && m_touchpadEnabled;
}

bool SessionGestureGuard::canBeginWindowMove() const
{
    return canHandleTouchpadEvent() && !isKeyboardGrabbed();
}

void SessionGestureGuard::refreshTouchpadState()
{
    QDBusInterface touchpad(QLatin1String(TouchpadService),
                            QLatin1String(TouchpadPath),
                            QLatin1String(TouchpadInterface),
                            QDBusConnection::sessionBus());
    m_touchpadEnabled = touchpad.isValid() && touchpad.property("TPadEnable").toBool();
}

void SessionGestureGuard::onTouchpadPropertiesChanged(const QString &interface,
                                                       const QVariantMap &changed,
                                                       const QStringList &invalidated)
{
    if (interface != QLatin1String(TouchpadInterface))
        return;
    if (changed.contains(QStringLiteral("TPadEnable")))
        m_touchpadEnabled = unwrapDbusValue(changed.value(QStringLiteral("TPadEnable"))).toBool();
    if (invalidated.contains(QStringLiteral("TPadEnable")))
        refreshTouchpadState();
}

void SessionGestureGuard::refreshMultitaskState()
{
    if (m_wmOwner.isEmpty() || m_multitaskPendingGeneration == m_wmGeneration
            || !QDBusConnection::sessionBus().isConnected()) {
        return;
    }

    QDBusMessage message = QDBusMessage::createMethodCall(m_wmOwner,
                                                          QLatin1String(WmPath),
                                                          QLatin1String(WmInterface),
                                                          QStringLiteral("GetMultiTaskingStatus"));
    m_multitaskPendingGeneration = m_wmGeneration;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    watcher->setProperty("generation", QVariant::fromValue(m_wmGeneration));
    watcher->setProperty("owner", m_wmOwner);
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &SessionGestureGuard::onMultitaskStateFinished);
}

void SessionGestureGuard::onMultitaskStateFinished(QDBusPendingCallWatcher *watcher)
{
    const QDBusPendingReply<bool> reply = *watcher;
    const quint64 generation = watcher->property("generation").toULongLong();
    const QString owner = watcher->property("owner").toString();
    watcher->deleteLater();
    refreshWmOwner();
    if (generation != m_wmGeneration || owner != m_wmOwner)
        return;

    m_multitaskPendingGeneration = 0;
    if (reply.isError())
        m_multitaskVisible = false;
    else
        m_multitaskVisible = reply.value();
}

void SessionGestureGuard::refreshWmOwner()
{
    QDBusConnectionInterface *busInterface = QDBusConnection::sessionBus().interface();
    if (!busInterface) {
        setWmOwner(QString());
        return;
    }

    const QDBusReply<QString> reply = busInterface->serviceOwner(QLatin1String(WmService));
    setWmOwner(reply.isValid() ? reply.value() : QString());
}

void SessionGestureGuard::setWmOwner(const QString &owner)
{
    if (owner == m_wmOwner)
        return;

    m_wmOwner = owner;
    ++m_wmGeneration;
    m_multitaskPendingGeneration = 0;
    m_multitaskVisible = false;
    if (!m_wmOwner.isEmpty())
        refreshMultitaskState();
}

bool SessionGestureGuard::isKeyboardGrabbed() const
{
    if (!m_xConnection || xcb_connection_has_error(m_xConnection) || !m_rootWindow)
        return true;

    const xcb_grab_keyboard_cookie_t cookie = xcb_grab_keyboard(m_xConnection, false,
                                                                m_rootWindow, XCB_CURRENT_TIME,
                                                                XCB_GRAB_MODE_ASYNC,
                                                                XCB_GRAB_MODE_ASYNC);
    xcb_grab_keyboard_reply_t *reply = xcb_grab_keyboard_reply(m_xConnection, cookie, nullptr);
    if (!reply)
        return true;

    const uint8_t status = reply->status;
    free(reply);
    if (status == XCB_GRAB_STATUS_SUCCESS) {
        xcb_ungrab_keyboard(m_xConnection, XCB_CURRENT_TIME);
        xcb_flush(m_xConnection);
        return false;
    }
    return true;
}
