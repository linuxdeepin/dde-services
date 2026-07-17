// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "wmcontroller.h"

#include <DGuiApplicationHelper>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDebug>

DGUI_USE_NAMESPACE

namespace {
constexpr auto WmSwitcherService = "org.deepin.dde.WMSwitcher1";
constexpr auto WmSwitcherPath = "/org/deepin/dde/WMSwitcher1";
constexpr auto WmSwitcherInterface = "org.deepin.dde.WMSwitcher1";
}

WmController::WmController(QObject *parent)
    : BaseController(parent)
{
}

QStringList WmController::commandActions()
{
    return {QStringLiteral("switch-effects")};
}

QMap<QString, QString> WmController::commandActionHelp()
{
    return {
        {QStringLiteral("switch-effects"), QStringLiteral("Show the window effects switcher")},
    };
}

QStringList WmController::supportedActions() const
{
    return commandActions();
}

bool WmController::execute(const QString &action, const QStringList &args)
{
    Q_UNUSED(args)
    if (action == QLatin1String("switch-effects"))
        return switchEffects();

    qWarning() << "Unknown window manager action:" << action;
    return false;
}

QString WmController::actionHelp(const QString &action) const
{
    return commandActionHelp().value(action);
}

bool WmController::switchEffects()
{
    if (DGuiApplicationHelper::testAttribute(DGuiApplicationHelper::IsWaylandPlatform)) {
        // TODO: Use the dedicated Treeland window-effects protocol once it is available.
        // This operation intentionally does not use the shortcut action protocol.
        qWarning() << "Window-effects switching is not supported on Wayland yet";
        return false;
    }

    QDBusMessage message = QDBusMessage::createMethodCall(
            QLatin1String(WmSwitcherService), QLatin1String(WmSwitcherPath),
            QLatin1String(WmSwitcherInterface), QStringLiteral("RequestSwitchWM"));
    const QDBusMessage reply = QDBusConnection::sessionBus().call(message);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning() << "Failed to show the window effects switcher:"
                   << reply.errorName() << reply.errorMessage();
        return false;
    }
    return true;
}
