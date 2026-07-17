// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "sessiongestureguard.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QDBusReply>
#include <QTimer>
#include <QVariant>

#include <xcb/xcb.h>

#include <functional>
#include <utility>

namespace {

constexpr auto PropertiesInterface = "org.freedesktop.DBus.Properties";
constexpr auto SessionManagerService = "org.deepin.dde.SessionManager1";
constexpr auto SessionManagerPath = "/org/deepin/dde/SessionManager1";
constexpr auto SessionManagerInterface = "org.deepin.dde.SessionManager1";
constexpr auto LoginService = "org.freedesktop.login1";
constexpr auto LoginSessionInterface = "org.freedesktop.login1.Session";
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

class LoginSessionSignalRelay : public QObject
{
    Q_OBJECT
public:
    using Callback = std::function<void(const QString &, quint64, const QString &,
                                        const QVariantMap &, const QStringList &)>;

    LoginSessionSignalRelay(QString path, quint64 generation, Callback callback, QObject *parent)
        : QObject(parent)
        , m_path(std::move(path))
        , m_generation(generation)
        , m_callback(std::move(callback))
    {
    }

private slots:
    void onPropertiesChanged(const QString &interface, const QVariantMap &changed,
                             const QStringList &invalidated)
    {
        m_callback(m_path, m_generation, interface, changed, invalidated);
    }

private:
    QString m_path;
    quint64 m_generation = 0;
    Callback m_callback;
};

QString objectPathString(const QVariant &value)
{
    const QVariant unwrapped = unwrapDbusValue(value);
    if (unwrapped.canConvert<QDBusObjectPath>())
        return qvariant_cast<QDBusObjectPath>(unwrapped).path();
    return unwrapped.toString();
}

}

SessionGestureGuard::SessionGestureGuard(QObject *parent)
    : QObject(parent)
    , m_multitaskRefreshTimer(new QTimer(this))
{
    m_xConnection = xcb_connect(nullptr, nullptr);
    if (m_xConnection && !xcb_connection_has_error(m_xConnection)) {
        const xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(m_xConnection)).data;
        if (screen)
            m_rootWindow = screen->root;
    }

    QDBusConnection::sessionBus().connect(QLatin1String(SessionManagerService),
                                          QLatin1String(SessionManagerPath),
                                          QLatin1String(PropertiesInterface),
                                          QStringLiteral("PropertiesChanged"),
                                          this,
                                          SLOT(onSessionManagerPropertiesChanged(QString,QVariantMap,QStringList)));
    QDBusConnection::sessionBus().connect(QLatin1String(TouchpadService),
                                          QLatin1String(TouchpadPath),
                                          QLatin1String(PropertiesInterface),
                                          QStringLiteral("PropertiesChanged"),
                                          this,
                                          SLOT(onTouchpadPropertiesChanged(QString,QVariantMap,QStringList)));

    auto *sessionManagerWatcher = new QDBusServiceWatcher(
            QLatin1String(SessionManagerService), QDBusConnection::sessionBus(),
            QDBusServiceWatcher::WatchForRegistration
                | QDBusServiceWatcher::WatchForUnregistration, this);
    connect(sessionManagerWatcher, &QDBusServiceWatcher::serviceRegistered,
            this, [this]() { refreshSessionManagerState(); });
    connect(sessionManagerWatcher, &QDBusServiceWatcher::serviceUnregistered,
            this, [this]() {
        m_locked = true;
        updateCurrentSession(QString());
    });

    auto *touchpadWatcher = new QDBusServiceWatcher(
            QLatin1String(TouchpadService), QDBusConnection::sessionBus(),
            QDBusServiceWatcher::WatchForRegistration
                | QDBusServiceWatcher::WatchForUnregistration, this);
    connect(touchpadWatcher, &QDBusServiceWatcher::serviceRegistered,
            this, [this]() { refreshTouchpadState(); });
    connect(touchpadWatcher, &QDBusServiceWatcher::serviceUnregistered,
            this, [this]() { m_touchpadEnabled = false; });

    auto *loginWatcher = new QDBusServiceWatcher(
            QLatin1String(LoginService), QDBusConnection::systemBus(),
            QDBusServiceWatcher::WatchForRegistration
                | QDBusServiceWatcher::WatchForUnregistration, this);
    connect(loginWatcher, &QDBusServiceWatcher::serviceRegistered,
            this, [this]() { refreshLoginSessionState(); });
    connect(loginWatcher, &QDBusServiceWatcher::serviceUnregistered,
            this, [this]() { m_sessionActive = false; });

    auto *wmWatcher = new QDBusServiceWatcher(
            QLatin1String(WmService), QDBusConnection::sessionBus(),
            QDBusServiceWatcher::WatchForRegistration
                | QDBusServiceWatcher::WatchForUnregistration, this);
    connect(wmWatcher, &QDBusServiceWatcher::serviceRegistered,
            this, [this]() { refreshWmOwner(); });
    connect(wmWatcher, &QDBusServiceWatcher::serviceUnregistered,
            this, [this]() { setWmOwner(QString()); });

    refreshSessionManagerState();
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
    return !m_locked && m_sessionActive && m_touchpadEnabled;
}

bool SessionGestureGuard::canBeginWindowMove() const
{
    return canHandleTouchpadEvent() && !isKeyboardGrabbed();
}

void SessionGestureGuard::refreshSessionManagerState()
{
    QDBusInterface sessionManager(QLatin1String(SessionManagerService),
                                  QLatin1String(SessionManagerPath),
                                  QLatin1String(SessionManagerInterface),
                                  QDBusConnection::sessionBus());
    if (!sessionManager.isValid()) {
        m_locked = true;
        updateCurrentSession(QString());
        return;
    }

    m_locked = sessionManager.property("Locked").toBool();
    updateCurrentSession(objectPathString(sessionManager.property("CurrentSessionPath")));
}

void SessionGestureGuard::refreshTouchpadState()
{
    QDBusInterface touchpad(QLatin1String(TouchpadService),
                            QLatin1String(TouchpadPath),
                            QLatin1String(TouchpadInterface),
                            QDBusConnection::sessionBus());
    m_touchpadEnabled = touchpad.isValid() && touchpad.property("TPadEnable").toBool();
}

void SessionGestureGuard::updateCurrentSession(const QString &path)
{
    if (path == m_currentSessionPath && !path.isEmpty()) {
        refreshLoginSessionState();
        return;
    }

    if (!m_currentSessionPath.isEmpty() && m_loginSessionSignalRelay) {
        QDBusConnection::systemBus().disconnect(QLatin1String(LoginService),
                                                m_currentSessionPath,
                                                QLatin1String(PropertiesInterface),
                                                QStringLiteral("PropertiesChanged"),
                                                m_loginSessionSignalRelay,
                                                SLOT(onPropertiesChanged(QString,QVariantMap,QStringList)));
    }
    delete m_loginSessionSignalRelay;
    m_loginSessionSignalRelay = nullptr;

    m_currentSessionPath = path;
    ++m_sessionGeneration;
    m_sessionActive = false;
    if (path.isEmpty())
        return;

    const quint64 generation = m_sessionGeneration;
    m_loginSessionSignalRelay = new LoginSessionSignalRelay(
            path, generation,
            [this](const QString &signalPath, quint64 signalGeneration,
                   const QString &interface, const QVariantMap &changed,
                   const QStringList &invalidated) {
        handleLoginSessionPropertiesChanged(signalPath, signalGeneration,
                                            interface, changed, invalidated);
    }, this);
    QDBusConnection::systemBus().connect(QLatin1String(LoginService),
                                         path,
                                         QLatin1String(PropertiesInterface),
                                         QStringLiteral("PropertiesChanged"),
                                         m_loginSessionSignalRelay,
                                         SLOT(onPropertiesChanged(QString,QVariantMap,QStringList)));
    refreshLoginSessionState();
}

void SessionGestureGuard::refreshLoginSessionState()
{
    if (m_currentSessionPath.isEmpty()) {
        m_sessionActive = false;
        return;
    }
    QDBusInterface loginSession(QLatin1String(LoginService), m_currentSessionPath,
                                QLatin1String(LoginSessionInterface),
                                QDBusConnection::systemBus());
    m_sessionActive = loginSession.isValid() && loginSession.property("Active").toBool();
}

void SessionGestureGuard::onSessionManagerPropertiesChanged(const QString &interface,
                                                             const QVariantMap &changed,
                                                             const QStringList &invalidated)
{
    if (interface != QLatin1String(SessionManagerInterface))
        return;
    if (changed.contains(QStringLiteral("Locked")))
        m_locked = unwrapDbusValue(changed.value(QStringLiteral("Locked"))).toBool();
    if (changed.contains(QStringLiteral("CurrentSessionPath")))
        updateCurrentSession(objectPathString(changed.value(QStringLiteral("CurrentSessionPath"))));
    if (invalidated.contains(QStringLiteral("Locked"))
            || invalidated.contains(QStringLiteral("CurrentSessionPath"))) {
        refreshSessionManagerState();
    }
}

void SessionGestureGuard::handleLoginSessionPropertiesChanged(const QString &path,
                                                               quint64 generation,
                                                               const QString &interface,
                                                               const QVariantMap &changed,
                                                               const QStringList &invalidated)
{
    if (path != m_currentSessionPath || generation != m_sessionGeneration
            || interface != QLatin1String(LoginSessionInterface)) {
        return;
    }
    if (changed.contains(QStringLiteral("Active")))
        m_sessionActive = unwrapDbusValue(changed.value(QStringLiteral("Active"))).toBool();
    if (invalidated.contains(QStringLiteral("Active"))) {
        refreshLoginSessionState();
    }
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

#include "sessiongestureguard.moc"
