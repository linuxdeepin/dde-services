// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "serviceactionexecutor.h"
#include "actionexecutor.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDebug>

ServiceActionExecutor::ServiceActionExecutor(ActionExecutor *actionExecutor, QObject *parent)
    : QObject(parent)
    , m_actionExecutor(actionExecutor)
{
}

bool ServiceActionExecutor::execute(TriggerActionId actionId, const QString &context)
{
    bool success = false;
    switch (actionId) {
    case TriggerActionId::LockScreen:
        success = m_actionExecutor
                && m_actionExecutor->executeCommand({
                        QStringLiteral("/usr/bin/dde-shortcut-tool"),
                        QStringLiteral("power"),
                        QStringLiteral("system-away"),
                });
        break;
    case TriggerActionId::ToggleGrandSearch:
        success = m_actionExecutor
                && m_actionExecutor->executeCommand({
                        QStringLiteral(
                                "/usr/libexec/dde-daemon/keybinding/shortcut-dde-grand-search.sh"),
                });
        break;
    case TriggerActionId::ToggleLauncher:
        success = call(QStringLiteral("org.deepin.dde.Launcher1"),
                       QStringLiteral("/org/deepin/dde/Launcher1"),
                       QStringLiteral("org.deepin.dde.Launcher1"),
                       QStringLiteral("Toggle"), actionId, context);
        break;
    case TriggerActionId::ToggleClipboard:
        success = call(QStringLiteral("org.deepin.dde.Clipboard1"),
                       QStringLiteral("/org/deepin/dde/Clipboard1"),
                       QStringLiteral("org.deepin.dde.Clipboard1"),
                       QStringLiteral("Toggle"), actionId, context);
        break;
    case TriggerActionId::ToggleNotifications:
        success = call(QStringLiteral("org.deepin.dde.Widgets1"),
                       QStringLiteral("/org/deepin/dde/Widgets1"),
                       QStringLiteral("org.deepin.dde.Widgets1"),
                       QStringLiteral("Toggle"), actionId, context);
        break;
    default:
        break;
    }

    if (!success) {
        qWarning() << "ServiceActionExecutor: action failed:" << int(actionId)
                   << "context" << context;
    }
    return success;
}

bool ServiceActionExecutor::call(
        const QString &service, const QString &path, const QString &interface,
        const QString &method, TriggerActionId actionId, const QString &context)
{
    if (!QDBusConnection::sessionBus().isConnected())
        return false;

    QDBusMessage message = QDBusMessage::createMethodCall(service, path, interface, method);
    auto *watcher = new QDBusPendingCallWatcher(
            QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [actionId, context](QDBusPendingCallWatcher *finishedWatcher) {
        const QDBusPendingReply<> reply = *finishedWatcher;
        if (reply.isError()) {
            qWarning() << "ServiceActionExecutor: action failed:" << int(actionId)
                       << "context" << context << reply.error().message();
        } else {
            qDebug() << "ServiceActionExecutor: action completed:" << int(actionId)
                     << "context" << context;
        }
        finishedWatcher->deleteLater();
    });
    return true;
}
