// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "powercontroller.h"
#include "constant.h"

#include <QDebug>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusConnection>
#include <QProcess>

#include <DConfig>

DCORE_USE_NAMESPACE

PowerController::PowerController(QObject *parent)
    : BaseController(parent)
    , m_powerInterface(nullptr)
    , m_sessionInterface(nullptr)
    , m_powerConfig(nullptr)
{
    // Connect to power service
    m_powerInterface = new QDBusInterface(
        "org.deepin.dde.Power1",
        "/org/deepin/dde/Power1",
        "org.deepin.dde.Power1",
        QDBusConnection::systemBus(),
        this
    );
    
    if (!m_powerInterface->isValid()) {
        qWarning() << "Failed to connect to Power service:" 
                   << m_powerInterface->lastError().message();
    }
    
    // Connect to session manager service
    m_sessionInterface = new QDBusInterface(
        "org.deepin.dde.SessionManager1",
        "/org/deepin/dde/SessionManager1",
        "org.deepin.dde.SessionManager1",
        QDBusConnection::sessionBus(),
        this
    );
    
    if (!m_sessionInterface->isValid()) {
        qWarning() << "Failed to connect to SessionManager service:" 
                   << m_sessionInterface->lastError().message();
    }

    m_powerConfig = DConfig::create("org.deepin.dde.daemon", "org.deepin.dde.daemon.power", "", this);
    if (!m_powerConfig->isValid()) {
        qWarning() << "daemon power config is not valid";
    }
}

PowerController::~PowerController()
{
    if (m_powerInterface) {
        delete m_powerInterface;
    }
    if (m_sessionInterface) {
        delete m_sessionInterface;
    }
}

QStringList PowerController::supportedActions() const
{
    return QStringList{
        "button",
        "switch-mode",
        "system-away"
    };
}

bool PowerController::execute(const QString &action, const QStringList &args)
{
    Q_UNUSED(args);
    
    if (action == "button") {
        handlePowerButton();
        return true;
    } else if (action == "switch-mode") {
        switchPowerMode();
        return true;
    } else if (action == "system-away") {
        systemAway();
        return true;
    }
    
    qWarning() << "Unknown power action:" << action;
    return false;
}

QString PowerController::actionHelp(const QString &action) const
{
    static const QMap<QString, QString> helpMap = {
        {"button", "Handle power button press event"},
        {"switch-mode", "Switch power performance mode"},
        {"system-away", "Lock the system"}
    };
    return helpMap.value(action);
}

void PowerController::handlePowerButton()
{
    if (!m_powerConfig) {
        qWarning() << "Power config not available";
        return;
    }
    
    // Get power button action configuration
    int powerAction = getPowerButtonAction();
    bool screenBlackLock = shouldLockOnScreenBlack();
    
    qDebug() << "Power button pressed, action:" << powerAction 
             << "screenBlackLock:" << screenBlackLock;
    
    switch (powerAction) {
    case PowerAction::PowerActionShutdown:
        systemShutdown();
        break;
    case PowerAction::PowerActionSuspend:
        systemSuspend();
        break;
    case PowerAction::PowerActionHibernate:
        systemHibernate();
        break;
    case PowerAction::PowerActionTurnOffScreen:
        if (screenBlackLock) {
            systemLock();
        }
        systemTurnOffScreen();
        break;
    case PowerAction::PowerActionShowUI:
        showShutdownUI();
        break;
    default:
        qWarning() << "Unknown power action:" << powerAction;
        break;
    }
}

void PowerController::handleSystemSuspend()
{
    if (shouldLockOnSleep()) {
        // Use frontend lock method (with lock screen UI)
        systemLock();
        // Delay briefly to allow lock screen to display
        QProcess::execute("sleep", QStringList() << "0.5");
    }
    
    systemSuspend();
}

bool PowerController::isOnBattery()
{
    if (!m_powerInterface || !m_powerInterface->isValid()) {
        return false;
    }
    
    QVariant onBatteryVariant = m_powerInterface->property("OnBattery");
    return onBatteryVariant.toBool();
}

int PowerController::getPowerButtonAction()
{
    if (!m_powerConfig) {
        return PowerActionShowUI; // Default: show shutdown UI
    }
    
    QString configKey;
    if (isOnBattery()) {
        configKey = Config::KEY_BATTERY_PRESS_POWER_BTN_ACTION;
    } else {
        configKey = Config::KEY_LINE_POWER_PRESS_POWER_BTN_ACTION;
    }
    
    QVariant actionValue = m_powerConfig->value(configKey);
    return actionValue.toInt();
}

bool PowerController::shouldLockOnScreenBlack()
{
    if (!m_powerConfig) {
        return true; // Default: enable lock
    }
    
    QVariant lockValue = m_powerConfig->value(Config::KEY_SCREEN_BLACK_LOCK);
    return lockValue.toBool();
}

bool PowerController::shouldLockOnSleep()
{
    if (!m_powerConfig) {
        return true; // Default: enable lock
    }
    
    QVariant lockValue = m_powerConfig->value(Config::KEY_SLEEP_LOCK);
    return lockValue.toBool();
}

void PowerController::systemShutdown()
{
    qDebug() << "Executing system shutdown";
    if (m_sessionInterface && m_sessionInterface->isValid()) {
        m_sessionInterface->call("RequestShutdown");
    }
}

void PowerController::systemSuspend()
{
    qDebug() << "Executing system suspend";
    if (m_sessionInterface && m_sessionInterface->isValid()) {
        m_sessionInterface->call("RequestSuspend");
    }
}

void PowerController::systemHibernate()
{
    qDebug() << "Executing system hibernate";
    if (m_sessionInterface && m_sessionInterface->isValid()) {
        m_sessionInterface->call("RequestHibernate");
    }
}

void PowerController::systemTurnOffScreen()
{
    qDebug() << "Turning off screen";

    QString sessionType = qEnvironmentVariable("XDG_SESSION_TYPE");
    if (sessionType == "wayland") {
        // TODO: Implement Wayland-specific screen off via compositor DBus interface
        // xset dpms is not available on Wayland
        qWarning() << "systemTurnOffScreen() not implemented for Wayland";
        return;
    }

    // Use xset command to turn off screen (X11 only)
    QProcess::execute("xset", QStringList() << "dpms" << "force" << "off");
}

void PowerController::showShutdownUI()
{
    qDebug() << "Showing shutdown UI";
    
    // Check if already locked
    if (m_sessionInterface && m_sessionInterface->isValid()) {
        QVariant lockedVariant = m_sessionInterface->property("Locked");
        bool locked = lockedVariant.toBool();
        
        if (!locked) {
            // Execute shutdown UI script
            QProcess::startDetached("/usr/lib/deepin-daemon/dde-shutdown.sh");
        }
    }
}

void PowerController::systemLock()
{
    qDebug() << "Executing system lock";
    
    // Call lock screen service via DBus
    QDBusInterface lockInterface(
        "org.deepin.dde.LockFront1",
        "/org/deepin/dde/LockFront1",
        "org.deepin.dde.LockFront1",
        QDBusConnection::sessionBus()
    );
    
    if (lockInterface.isValid()) {
        lockInterface.call("Show");
    } else {
        qWarning() << "Failed to connect to LockFront service";
    }
}

void PowerController::switchPowerMode()
{
    qDebug() << "[PowerController] Switching power mode";
    
    // Call power management service via DBus to switch performance mode
    QDBusInterface powerInterface("org.deepin.dde.Power1",
                                 "/org/deepin/dde/Power1",
                                 "org.deepin.dde.Power1",
                                 QDBusConnection::systemBus());
    
    if (!powerInterface.isValid()) {
        qWarning() << "[PowerController] Failed to connect to Power service";
        return;
    }
    
    // Get current performance mode
    QDBusReply<QString> modeReply = powerInterface.call("Mode");
    if (!modeReply.isValid()) {
        qWarning() << "[PowerController] Failed to get current power mode";
        return;
    }
    
    QString currentMode = modeReply.value();
    QString newMode;
    
    // Cycle through performance modes: powersave -> balance -> performance -> powersave
    if (currentMode == "powersave") {
        newMode = "balance";
    } else if (currentMode == "balance") {
        newMode = "performance";
    } else {
        newMode = "powersave";
    }
    
    // Set new performance mode
    QDBusReply<void> setReply = powerInterface.call("SetMode", newMode);
    if (!setReply.isValid()) {
        qWarning() << "[PowerController] Failed to set power mode to" << newMode;
        return;
    }
    
    qInfo() << "[PowerController] Power mode switched from" << currentMode << "to" << newMode;
}

void PowerController::systemAway()
{
    qDebug() << "Executing system RequestLock";
    if (m_sessionInterface && m_sessionInterface->isValid()) {
        m_sessionInterface->call("RequestLock");
    }
}