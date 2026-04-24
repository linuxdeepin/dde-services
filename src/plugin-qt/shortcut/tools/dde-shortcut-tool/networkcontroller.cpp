// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "networkcontroller.h"

#include <QDBusInterface>
#include <QDBusReply>
#include <QDebug>

NetworkController::NetworkController(QObject *parent)
    : BaseController(parent)
{
    m_airplaneInterface = new QDBusInterface(
        "org.deepin.dde.AirplaneMode1",
        "/org/deepin/dde/AirplaneMode1",
        "org.deepin.dde.AirplaneMode1",
        QDBusConnection::systemBus(),
        this
    );
}

NetworkController::~NetworkController()
{
}

QStringList NetworkController::supportedActions() const
{
    return {"toggle-wifi", "toggle-airplane"};
}

bool NetworkController::execute(const QString &action, const QStringList &args)
{
    Q_UNUSED(args);
    
    if (action == "toggle-wifi") {
        toggleWifi();
        return true;
    } else if (action == "toggle-airplane") {
        toggleAirplaneMode();
        return true;
    }
    return false;
}

QString NetworkController::actionHelp(const QString &action) const
{
    if (action == "toggle-wifi") {
        return "Toggle WiFi status";
    } else if (action == "toggle-airplane") {
        return "Toggle Airplane Mode status";
    }
    return QString();
}

void NetworkController::toggleWifi()
{
    if (!m_airplaneInterface->isValid()) {
        qWarning() << "AirplaneMode1 interface is not valid";
        return;
    }

    // 1. Check if airplane mode is enabled
    bool airplaneEnabled = m_airplaneInterface->property("Enabled").toBool();
    if (airplaneEnabled) {
        qDebug() << "Airplane mode is enabled, cannot toggle WiFi";
        // In dde-daemon, it just returns. 
        // We could potentially show an OSD here if we had an OSD controller or similar.
        return;
    }

    // 2. Toggle WiFi
    bool wifiEnabled = m_airplaneInterface->property("WifiEnabled").toBool();
    qDebug() << "Toggling WiFi from" << wifiEnabled << "to" << !wifiEnabled;
    
    m_airplaneInterface->call("EnableWifi", !wifiEnabled);
}

void NetworkController::toggleAirplaneMode()
{
    if (!m_airplaneInterface->isValid()) {
        qWarning() << "AirplaneMode1 interface is not valid";
        return;
    }

    bool enabled = m_airplaneInterface->property("Enabled").toBool();
    qDebug() << "Toggling Airplane Mode from" << enabled << "to" << !enabled;
    
    m_airplaneInterface->call("Enable", !enabled);
}
