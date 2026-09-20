// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "touchpadcontroller.h"

#include <QDebug>
#include <QDBusInterface>
#include <QDBusConnection>

TouchPadController::TouchPadController(QObject *parent)
    : BaseController(parent)
{
}

QStringList TouchPadController::commandActions()
{
    return QStringList{
        "toggle",
        "on",
        "off"
    };
}

QMap<QString, QString> TouchPadController::commandActionHelp()
{
    return {
        {"toggle", "Toggle touchpad on/off"},
        {"on", "Enable touchpad"},
        {"off", "Disable touchpad"}
    };
}

QStringList TouchPadController::supportedActions() const
{
    return commandActions();
}

bool TouchPadController::execute(const QString &action, const QStringList &args)
{
    Q_UNUSED(args);
    
    if (action == "toggle") {
        return toggle();
    } else if (action == "on") {
        return setEnabled(true);
    } else if (action == "off") {
        return setEnabled(false);
    }
    
    qWarning() << "Unknown touchpad action:" << action;
    return false;
}

QString TouchPadController::actionHelp(const QString &action) const
{
    return commandActionHelp().value(action);
}

bool TouchPadController::toggle()
{
    // The shortcut only shows the OSD; the actual touchpad switch is handled
    // elsewhere (e.g. the system keyevent service).
    showOSD("TouchpadToggle");
    return true;
}

bool TouchPadController::setEnabled(bool enabled)
{
    // The shortcut only shows the OSD; the actual touchpad switch is handled
    // elsewhere (e.g. the system keyevent service).
    QString osd = enabled ? "TouchpadOn" : "TouchpadOff";
    showOSD(osd);
    return true;
}

void TouchPadController::showOSD(const QString &signal)
{
    QDBusInterface osdInterface(
        "org.deepin.dde.Osd1",
        "/org/deepin/dde/shell/osd",
        "org.deepin.dde.shell.osd",
        QDBusConnection::sessionBus()
    );
    
    if (osdInterface.isValid()) {
        osdInterface.call("ShowOSD", signal);
    } else {
        qWarning() << "Failed to connect to OSD interface";
    }
}
