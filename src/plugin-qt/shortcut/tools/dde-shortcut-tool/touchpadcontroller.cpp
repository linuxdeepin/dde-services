// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "touchpadcontroller.h"

#include <QDebug>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusConnection>

TouchPadController::TouchPadController(QObject *parent) 
    : BaseController(parent)
    , m_touchpadInterface(nullptr)
{
    // Connect to touchpad service - using same path and interface as Go version
    m_touchpadInterface = new QDBusInterface(
        "org.deepin.dde.InputDevices1",
        "/org/deepin/dde/InputDevice1/TouchPad",
        "org.deepin.dde.InputDevice1.TouchPad",
        QDBusConnection::sessionBus(),
        this
    );
    
    if (!m_touchpadInterface->isValid()) {
        qWarning() << "Failed to connect to TouchPad service:" 
                   << m_touchpadInterface->lastError().message();
    }
}

TouchPadController::~TouchPadController()
{
    if (m_touchpadInterface) {
        delete m_touchpadInterface;
    }
}

QStringList TouchPadController::supportedActions() const
{
    return QStringList{
        "toggle",
        "on",
        "off"
    };
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
    static const QMap<QString, QString> helpMap = {
        {"toggle", "Toggle touchpad on/off"},
        {"on", "Enable touchpad"},
        {"off", "Disable touchpad"}
    };
    return helpMap.value(action);
}

bool TouchPadController::toggle()
{
    if (!m_touchpadInterface || !m_touchpadInterface->isValid()) {
        qDebug() << "Touchpad interface not available";
        return false;
    }
    
    // Check if touchpad exists (following Go version logic)
    QVariant existVariant = m_touchpadInterface->property("Exist");
    if (!existVariant.isValid()) {
        qWarning() << "Failed to get touchpad exist state";
        return false;
    }
    
    bool exist = existVariant.toBool();
    if (!exist) {
        qDebug() << "Touchpad does not exist";
        return true; // Return success (nil in Go version)
    }
    
    // Check if HandleTouchPadToggle is enabled (following Go version logic)
    // Note: Currently assumes true, corresponds to globalConfig.HandleTouchPadToggle in Go version
    bool handleTouchPadToggle = true; // TODO: Read from config file
    
    if (handleTouchPadToggle) {
        // Get current state using TPadEnable property (Go version property name)
        QVariant enabledVariant = m_touchpadInterface->property("TPadEnable");
        if (!enabledVariant.isValid()) {
            qWarning() << "Failed to get touchpad TPadEnable state";
            return false;
        }
        
        bool currentEnabled = enabledVariant.toBool();
        
        // Set new state
        bool success = m_touchpadInterface->setProperty("TPadEnable", !currentEnabled);
        if (!success) {
            qWarning() << "Failed to set touchpad TPadEnable state";
            return false;
        }
        
        qDebug() << "Toggled touchpad TPadEnable from" << currentEnabled << "to" << !currentEnabled;
    }
    
    // Toggle always shows TouchpadToggle OSD (Go version logic)
    showOSD("TouchpadToggle");
    
    return true;
}

bool TouchPadController::setEnabled(bool enabled)
{
    if (!m_touchpadInterface || !m_touchpadInterface->isValid()) {
        qDebug() << "Touchpad interface not available";
        return false;
    }
    
    // Check if touchpad exists (following Go version logic)
    QVariant existVariant = m_touchpadInterface->property("Exist");
    if (!existVariant.isValid()) {
        qWarning() << "Failed to get touchpad exist state";
        return false;
    }
    
    bool exist = existVariant.toBool();
    if (!exist) {
        qDebug() << "Touchpad does not exist";
        return true; // Return success (nil in Go version)
    }
    
    // Set state using TPadEnable property (Go version logic)
    bool success = m_touchpadInterface->setProperty("TPadEnable", enabled);
    if (!success) {
        qWarning() << "Failed to set touchpad TPadEnable state";
        return false;
    }
    
    qDebug() << "Set touchpad TPadEnable:" << enabled;
    
    // Show corresponding OSD (Go version logic)
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
