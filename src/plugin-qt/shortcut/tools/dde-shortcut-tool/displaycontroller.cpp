// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "displaycontroller.h"
#include "treelandbrightnesscontroller.h"

#include <DGuiApplicationHelper>

#include <QDebug>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusConnection>
#include <QProcess>
#include <QSysInfo>

#include <DConfig>
#include <qlogging.h>
#include <QThread>

DCORE_USE_NAMESPACE
DGUI_USE_NAMESPACE

DisplayController::DisplayController(QObject *parent) 
    : BaseController(parent)
    , m_displayInterface(nullptr)
    , m_isWayland(DGuiApplicationHelper::testAttribute(DGuiApplicationHelper::IsWaylandPlatform))
{
    if (m_isWayland) {
        // Brightness uses Treeland directly on Wayland. Other display actions
        // still avoid the unavailable org.deepin.dde.Display1 service.
        qInfo() << "DisplayController: skip Display1 initialization on Wayland";
        return;
    }

    // Connect to Display1 service
    m_displayInterface = new QDBusInterface(
        "org.deepin.dde.Display1",
        "/org/deepin/dde/Display1",
        "org.deepin.dde.Display1",
        QDBusConnection::sessionBus(),
        this
    );
    
    if (!m_displayInterface->isValid()) {
        qWarning() << "Failed to connect to Display1 service:" 
                   << m_displayInterface->lastError().message();
    }
}

DisplayController::~DisplayController()
{
    if (m_displayInterface) {
        delete m_displayInterface;
    }
}

QStringList DisplayController::commandActions()
{
    return QStringList{
        "brightness-up",
        "brightness-down",
        "switch-mode",
        "turn-off-screen"
    };
}

QMap<QString, QString> DisplayController::commandActionHelp()
{
    return {
        {"brightness-up", "Increase display brightness"},
        {"brightness-down", "Decrease display brightness"},
        {"switch-mode", "Switch display mode (mirror/extend)"},
        {"turn-off-screen", "Turn off display screen"}
    };
}

QStringList DisplayController::supportedActions() const
{
    return commandActions();
}

bool DisplayController::execute(const QString &action, const QStringList &args)
{
    Q_UNUSED(args);
    
    if (action == "brightness-up") {
        return changeBrightness(true);
    } else if (action == "brightness-down") {
        return changeBrightness(false);
    } else if (action == "switch-mode") {
        return switchDisplayMode();
    } else if (action == "turn-off-screen") {
        return turnOffScreen();
    }
    
    qWarning() << "Unknown display action:" << action;
    return false;
}

QString DisplayController::actionHelp(const QString &action) const
{
    return commandActionHelp().value(action);
}

bool DisplayController::changeBrightness(bool raised)
{
    if (!m_isWayland && (!m_displayInterface || !m_displayInterface->isValid())) {
        qWarning() << "Display interface not available";
        return false;
    }

    auto *powerConfig = DConfig::create("org.deepin.dde.daemon", "org.deepin.dde.daemon.power", "", this);
    if (!powerConfig->isValid()) {
        qWarning() << "daemon power config is not valid";
        powerConfig->deleteLater();
        return false;
    }

    const bool autoAdjustEnabled =
            powerConfig->value("ambientLightAdjustBrightness").toBool();

    bool success = false;
    if (m_isWayland) {
        TreelandBrightnessController controller;
        success = controller.changeBrightness(raised);
    } else {
        // Call Display1's ChangeBrightness method directly
        QDBusReply<void> reply = m_displayInterface->call("ChangeBrightness", raised);
        if (!reply.isValid()) {
            qWarning() << "Failed to change brightness:" << reply.error().message();
        } else {
            success = true;
        }
    }

    if (!success) {
        powerConfig->deleteLater();
        return false;
    }

    if (autoAdjustEnabled) {
        powerConfig->setValue("ambientLightAdjustBrightness", false);
        qDebug() << "Disabled ambient light auto brightness adjustment";
    }
    powerConfig->deleteLater();

    qDebug() << "Changed brightness:" << (raised ? "up" : "down");
    showOSD(raised ? "BrightnessUp" : "BrightnessDown");
    return true;
}

bool DisplayController::switchDisplayMode()
{
    if (m_isWayland) {
        // TODO: Implement Wayland display mode switching without Display1.
        qWarning() << "DisplayController: display mode switching is not supported on Wayland";
        return false;
    }

    if (!m_displayInterface || !m_displayInterface->isValid()) {
        qWarning() << "Display interface not available";
        return false;
    }
    
    // Check if there are multiple displays
    QDBusReply<QStringList> displayListReply = m_displayInterface->call("ListOutputNames");
    if (!displayListReply.isValid()) {
        qWarning() << "Failed to get display list:" << displayListReply.error().message();
        return false;
    }
    
    QStringList displayList = displayListReply.value();
    if (displayList.size() > 1) {
        showOSD("SwitchMonitors");
        qDebug() << "Switched display mode for" << displayList.size() << "displays";
        return true;
    } else {
        qDebug() << "Only one display available, no switch needed";
        return false;
    }
}

bool DisplayController::turnOffScreen()
{
    qDebug() << "Turning off screen";

    if (m_isWayland) {
        qWarning() << "turn-off-screen via xset is not supported on Wayland";
        // TODO: Implement Wayland specific screen off if needed (e.g. via compositor DBus)
        return false;
    }

    // User suggested: sleep 0.5; xset dpms force off
    // sleep 0.5 is to avoid immediate wake up from the key release event
    QThread::msleep(500);
    return QProcess::startDetached("xset", {"dpms", "force", "off"});
}

void DisplayController::showOSD(const QString &signal)
{
    // Show OSD
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
