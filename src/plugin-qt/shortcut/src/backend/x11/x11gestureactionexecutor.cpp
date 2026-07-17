// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "x11gestureactionexecutor.h"
#include "core/gestureactioncatalog.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDebug>

#include <xcb/xcb.h>

#include <cstring>

namespace {

constexpr auto WmService = "com.deepin.wm";
constexpr auto WmPath = "/com/deepin/wm";
constexpr auto WmInterface = "com.deepin.wm";
constexpr auto KGlobalAccelService = "org.kde.kglobalaccel";
constexpr auto KWinComponentPath = "/component/kwin";
constexpr auto KGlobalAccelComponentInterface = "org.kde.kglobalaccel.Component";

}

X11GestureActionExecutor::X11GestureActionExecutor(QObject *parent)
    : QObject(parent)
{
    m_connection = xcb_connect(nullptr, nullptr);
    if (!m_connection || xcb_connection_has_error(m_connection))
        return;

    const xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(m_connection)).data;
    if (!screen)
        return;
    m_rootWindow = screen->root;

    const char atomName[] = "_NET_SHOWING_DESKTOP";
    const xcb_intern_atom_cookie_t atomCookie = xcb_intern_atom(m_connection, false,
                                                                std::strlen(atomName), atomName);
    xcb_intern_atom_reply_t *atomReply = xcb_intern_atom_reply(m_connection, atomCookie, nullptr);
    if (atomReply) {
        m_showingDesktopAtom = atomReply->atom;
        free(atomReply);
    }
}

X11GestureActionExecutor::~X11GestureActionExecutor()
{
    endWindowMove();
    if (m_connection)
        xcb_disconnect(m_connection);
}

bool X11GestureActionExecutor::execute(const GestureConfig &config)
{
    if (config.triggerValue.isEmpty())
        return false;

    const GestureActionId actionId = GestureActionCatalog::resolveActionId(
            config, config.triggerValue.first());
    const QString actionValue = GestureActionCatalog::idString(actionId);
    const bool success = executeAction(actionId, actionValue);
    if (!success) {
        qWarning() << "X11GestureActionExecutor: action failed: gesture" << config.getId()
                   << "configured action" << config.triggerValue.value(0)
                   << "resolved action" << int(actionId);
    }
    return success;
}

bool X11GestureActionExecutor::execute(const KeyConfig &config)
{
    if (config.triggerValue.isEmpty())
        return false;
    bool ok = false;
    const int value = config.triggerValue.first().toInt(&ok);
    const auto actionId = ok ? static_cast<GestureActionId>(value) : GestureActionId::Invalid;
    const bool success = executeAction(actionId, config.getId());
    if (!success)
        qWarning() << "X11GestureActionExecutor: shortcut action failed:" << config.getId()
                   << value;
    return success;
}

bool X11GestureActionExecutor::executeAction(GestureActionId actionId, const QString &context)
{
    bool success = false;
    switch (actionId) {
    case GestureActionId::MaximizeWindow:
        success = callWindowManager(QStringLiteral("MaximizeActiveWindow"), {}, context);
        break;
    case GestureActionId::RestoreWindow:
        success = callWindowManager(QStringLiteral("UnMaximizeActiveWindow"), {}, context);
        break;
    case GestureActionId::MoveWindow:
        success = callWindowManager(QStringLiteral("BeginToMoveActiveWindow"), {}, context);
        break;
    case GestureActionId::MinimizeWindow:
        success = callWindowManager(QStringLiteral("MinimizeActiveWindow"), {}, context);
        break;
    case GestureActionId::ShowWindowMenu:
        success = call(QLatin1String(KGlobalAccelService), QLatin1String(KWinComponentPath),
                       QLatin1String(KGlobalAccelComponentInterface),
                       QStringLiteral("invokeShortcut"),
                       {QStringLiteral("Window Operations Menu")}, context);
        break;
    case GestureActionId::SplitWindowLeft:
        success = callWindowManager(QStringLiteral("TileActiveWindow"),
                                    {QVariant::fromValue(uint(1))}, context);
        break;
    case GestureActionId::SplitWindowRight:
        success = callWindowManager(QStringLiteral("TileActiveWindow"),
                                    {QVariant::fromValue(uint(2))}, context);
        break;
    case GestureActionId::ShowMultitask:
        success = setMultitaskVisible(true, context);
        break;
    case GestureActionId::HideMultitask:
        success = setMultitaskVisible(false, context);
        break;
    case GestureActionId::ToggleMultitask:
        success = callWindowManager(QStringLiteral("PerformAction"), {1}, context);
        break;
    case GestureActionId::TaskSwitchNext:
        success = callWindowManager(QStringLiteral("SwitchApplication"), {false}, context);
        break;
    case GestureActionId::TaskSwitchPrevious:
        success = callWindowManager(QStringLiteral("SwitchApplication"), {true}, context);
        break;
    case GestureActionId::PreviousWorkspace:
        success = callWindowManager(QStringLiteral("PreviousWorkspace"), {}, context);
        break;
    case GestureActionId::NextWorkspace:
        success = callWindowManager(QStringLiteral("NextWorkspace"), {}, context);
        break;
    case GestureActionId::ShowDesktop:
        success = setDesktopVisible(true);
        break;
    case GestureActionId::HideDesktop:
        success = setDesktopVisible(false);
        break;
    case GestureActionId::Disable:
        success = true;
        break;
    default:
        break;
    }

    return success;
}

bool X11GestureActionExecutor::beginWindowMove()
{
    if (m_windowMoveState != WindowMoveState::Idle
            || !QDBusConnection::sessionBus().isConnected()) {
        return false;
    }

    m_windowMoveX = 0;
    m_windowMoveY = 0;
    m_windowMoveUpdatePending = false;
    m_windowMoveUpdateQueued = false;
    m_windowMoveState = WindowMoveState::Starting;
    const quint64 generation = ++m_windowMoveGeneration;

    QDBusMessage message = QDBusMessage::createMethodCall(QLatin1String(WmService),
                                                          QLatin1String(WmPath),
                                                          QLatin1String(WmInterface),
                                                          QStringLiteral("TouchToMove"));
    message.setArguments({0, 0});
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, generation](QDBusPendingCallWatcher *finishedWatcher) {
        const QDBusPendingReply<> reply = *finishedWatcher;
        finishedWatcher->deleteLater();
        if (generation != m_windowMoveGeneration)
            return;

        if (reply.isError()) {
            qWarning() << "X11GestureActionExecutor: window move begin failed:"
                       << reply.error().message();
            clearWindowMove();
            return;
        }
        if (m_windowMoveState == WindowMoveState::Starting)
            m_windowMoveState = WindowMoveState::Active;
    });
    return true;
}

bool X11GestureActionExecutor::updateWindowMove(double accelX, double accelY)
{
    if (m_windowMoveState == WindowMoveState::Starting)
        return true;
    if (m_windowMoveState != WindowMoveState::Active)
        return false;

    m_windowMoveX += int(accelX);
    m_windowMoveY += int(accelY);
    if (m_windowMoveUpdatePending) {
        m_windowMoveUpdateQueued = true;
        return true;
    }
    return sendWindowMoveUpdate();
}

bool X11GestureActionExecutor::endWindowMove()
{
    if (m_windowMoveState == WindowMoveState::Idle
            || m_windowMoveState == WindowMoveState::Ending) {
        return true;
    }

    return clearWindowMove();
}

bool X11GestureActionExecutor::sendWindowMoveUpdate()
{
    if (!QDBusConnection::sessionBus().isConnected())
        return false;

    QDBusMessage message = QDBusMessage::createMethodCall(QLatin1String(WmService),
                                                          QLatin1String(WmPath),
                                                          QLatin1String(WmInterface),
                                                          QStringLiteral("TouchToMove"));
    const int moveX = m_windowMoveX;
    const int moveY = m_windowMoveY;
    m_windowMoveX = 0;
    m_windowMoveY = 0;
    message.setArguments({moveX, moveY});
    m_windowMoveUpdatePending = true;
    const quint64 generation = m_windowMoveGeneration;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, generation](QDBusPendingCallWatcher *finishedWatcher) {
        const QDBusPendingReply<> reply = *finishedWatcher;
        finishedWatcher->deleteLater();
        if (generation != m_windowMoveGeneration)
            return;

        m_windowMoveUpdatePending = false;
        if (reply.isError()) {
            qWarning() << "X11GestureActionExecutor: window move update failed:"
                       << reply.error().message();
            clearWindowMove();
            return;
        }
        if (m_windowMoveUpdateQueued) {
            m_windowMoveUpdateQueued = false;
            sendWindowMoveUpdate();
        }
    });
    return true;
}

bool X11GestureActionExecutor::clearWindowMove()
{
    m_windowMoveState = WindowMoveState::Ending;
    ++m_windowMoveGeneration;
    m_windowMoveX = 0;
    m_windowMoveY = 0;
    m_windowMoveUpdatePending = false;
    m_windowMoveUpdateQueued = false;

    if (!QDBusConnection::sessionBus().isConnected()) {
        m_windowMoveState = WindowMoveState::Idle;
        return false;
    }

    QDBusMessage message = QDBusMessage::createMethodCall(QLatin1String(WmService),
                                                          QLatin1String(WmPath),
                                                          QLatin1String(WmInterface),
                                                          QStringLiteral("ClearMoveStatus"));
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this](QDBusPendingCallWatcher *finishedWatcher) {
        const QDBusPendingReply<> reply = *finishedWatcher;
        if (reply.isError()) {
            qWarning() << "X11GestureActionExecutor: window move end failed:"
                       << reply.error().message();
        }
        finishedWatcher->deleteLater();
        m_windowMoveState = WindowMoveState::Idle;
    });
    return true;
}

bool X11GestureActionExecutor::callWindowManager(const QString &method,
                                                 const QVariantList &arguments,
                                                 const QString &context)
{
    return call(QLatin1String(WmService), QLatin1String(WmPath),
                QLatin1String(WmInterface), method, arguments, context);
}

bool X11GestureActionExecutor::setMultitaskVisible(bool visible, const QString &actionId)
{
    if (!QDBusConnection::sessionBus().isConnected())
        return false;

    m_multitaskTargetVisible = visible;
    m_multitaskActionId = actionId;
    ++m_multitaskTargetGeneration;
    updateMultitaskVisible();
    return true;
}

void X11GestureActionExecutor::updateMultitaskVisible()
{
    if (m_multitaskUpdatePending || !QDBusConnection::sessionBus().isConnected())
        return;

    m_multitaskUpdatePending = true;
    const quint64 queryGeneration = m_multitaskTargetGeneration;
    QDBusMessage message = QDBusMessage::createMethodCall(QLatin1String(WmService),
                                                          QLatin1String(WmPath),
                                                          QLatin1String(WmInterface),
                                                          QStringLiteral("GetMultiTaskingStatus"));
    auto *watcher = new QDBusPendingCallWatcher(
            QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, queryGeneration](QDBusPendingCallWatcher *finishedWatcher) {
        const QDBusPendingReply<bool> reply = *finishedWatcher;
        finishedWatcher->deleteLater();
        if (reply.isError()) {
            qWarning() << "X11GestureActionExecutor:" << m_multitaskActionId
                       << "failed to query multitask state:" << reply.error().message();
            m_multitaskUpdatePending = false;
            if (queryGeneration != m_multitaskTargetGeneration)
                updateMultitaskVisible();
            return;
        }

        if (reply.value() == m_multitaskTargetVisible) {
            qDebug() << "X11GestureActionExecutor: action completed:" << m_multitaskActionId;
            m_multitaskUpdatePending = false;
            return;
        }

        const bool targetVisible = m_multitaskTargetVisible;
        const quint64 targetGeneration = m_multitaskTargetGeneration;
        const QString actionId = m_multitaskActionId;
        QDBusMessage toggle = QDBusMessage::createMethodCall(QLatin1String(WmService),
                                                             QLatin1String(WmPath),
                                                             QLatin1String(WmInterface),
                                                             QStringLiteral("PerformAction"));
        toggle.setArguments({1});
        auto *toggleWatcher = new QDBusPendingCallWatcher(
                QDBusConnection::sessionBus().asyncCall(toggle), this);
        connect(toggleWatcher, &QDBusPendingCallWatcher::finished, this,
                [this, targetVisible, targetGeneration, actionId](
                        QDBusPendingCallWatcher *finishedToggle) {
            const QDBusPendingReply<> toggleReply = *finishedToggle;
            if (toggleReply.isError()) {
                qWarning() << "X11GestureActionExecutor:" << actionId
                           << "failed to update multitask state:" << toggleReply.error().message();
            } else {
                qDebug() << "X11GestureActionExecutor: action completed:" << actionId;
            }
            finishedToggle->deleteLater();
            m_multitaskUpdatePending = false;
            if (targetGeneration != m_multitaskTargetGeneration
                    || targetVisible != m_multitaskTargetVisible) {
                updateMultitaskVisible();
            }
        });
    });
}

bool X11GestureActionExecutor::setDesktopVisible(bool visible)
{
    if (!m_connection || xcb_connection_has_error(m_connection)
            || !m_rootWindow || !m_showingDesktopAtom) {
        return false;
    }

    xcb_client_message_event_t event{};
    event.response_type = XCB_CLIENT_MESSAGE;
    event.window = m_rootWindow;
    event.type = m_showingDesktopAtom;
    event.format = 32;
    event.data.data32[0] = visible ? 1 : 0;
    xcb_send_event(m_connection, false, m_rootWindow,
                   XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
                   reinterpret_cast<const char *>(&event));
    xcb_flush(m_connection);
    return true;
}

bool X11GestureActionExecutor::call(const QString &service, const QString &path,
                                    const QString &interface, const QString &method,
                                    const QVariantList &arguments, const QString &context)
{
    if (!QDBusConnection::sessionBus().isConnected())
        return false;

    QDBusMessage message = QDBusMessage::createMethodCall(service, path, interface, method);
    message.setArguments(arguments);
    auto *watcher = new QDBusPendingCallWatcher(
            QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [context](QDBusPendingCallWatcher *finishedWatcher) {
        const QDBusPendingReply<> reply = *finishedWatcher;
        if (reply.isError()) {
            qWarning() << "X11GestureActionExecutor: action failed:" << context
                       << reply.error().message();
        } else {
            qDebug() << "X11GestureActionExecutor: action completed:" << context;
        }
        finishedWatcher->deleteLater();
    });
    return true;
}
