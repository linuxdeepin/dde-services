// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "displaycontroller.h"

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

DisplayController::DisplayController(QObject *parent) 
    : BaseController(parent)
    , m_displayInterface(nullptr)
{
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

QStringList DisplayController::supportedActions() const
{
    return QStringList{
        "brightness-up",
        "brightness-down",
        "switch-mode",
        "turn-off-screen"
    };
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
    static const QMap<QString, QString> helpMap = {
        {"brightness-up", "Increase display brightness"},
        {"brightness-down", "Decrease display brightness"},
        {"switch-mode", "Switch display mode (mirror/extend)"},
        {"turn-off-screen", "Turn off display screen"}
    };
    return helpMap.value(action);
}

bool DisplayController::changeBrightness(bool raised)
{
    if (!m_displayInterface || !m_displayInterface->isValid()) {
        qWarning() << "Display interface not available";
        return false;
    }

    auto *powerConfig = DConfig::create("org.deepin.dde.daemon", "org.deepin.dde.daemon.power", "", this);
    if (!powerConfig->isValid()) {
        qWarning() << "daemon power config is not valid";
        return false;
    }

    // Check if ambient light auto-adjustment is enabled, disable it first if so
    QVariant autoAdjustValue = powerConfig->value("ambientLightAdjustBrightness");
    if (autoAdjustValue.toBool()) {
        powerConfig->setValue("ambientLightAdjustBrightness", false);
        qDebug() << "Disabled ambient light auto brightness adjustment";
    }

    // Call Display1's ChangeBrightness method directly
    QDBusReply<void> reply = m_displayInterface->call("ChangeBrightness", raised);
    if (!reply.isValid()) {
        qWarning() << "Failed to change brightness:" << reply.error().message();
        return false;
    }
    
    qDebug() << "Changed brightness:" << (raised ? "up" : "down");
    showOSD(raised ? "BrightnessUp" : "BrightnessDown");

    powerConfig->deleteLater();
    
    return true;
}

bool DisplayController::switchDisplayMode()
{
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

    QString sessionType = qEnvironmentVariable("XDG_SESSION_TYPE");
    if (sessionType == "wayland") {
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
