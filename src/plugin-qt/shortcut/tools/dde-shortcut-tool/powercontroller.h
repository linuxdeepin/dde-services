// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef POWERCONTROLLER_H
#define POWERCONTROLLER_H

#include "basecontroller.h"

class QDBusInterface;

namespace Dtk {
namespace Core {
class DConfig;
}
}

class PowerController : public BaseController 
{
    Q_OBJECT

public:
    explicit PowerController(QObject *parent = nullptr);
    ~PowerController() override;

    // BaseController interface
    QString name() const override { return "power"; }
    QStringList supportedActions() const override;
    bool execute(const QString &action, const QStringList &args = QStringList()) override;
    QString actionHelp(const QString &action) const override;
    
    /**
     * @brief Handle power button press event
     */
    void handlePowerButton();
    
    /**
     * @brief Handle system suspend event
     */
    void handleSystemSuspend();
    
    /**
     * @brief Switch power/performance mode
     */
    void switchPowerMode();

private:
    /**
     * @brief Check if running on battery power
     * @return true if on battery power
     */
    bool isOnBattery();
    
    /**
     * @brief Get power button action configuration
     * @return Power button action enum value
     */
    int getPowerButtonAction();
    
    /**
     * @brief Check screen-off lock configuration
     * @return true if screen-off lock is enabled
     */
    bool shouldLockOnScreenBlack();
    
    /**
     * @brief Check sleep lock configuration
     * @return true if sleep lock is enabled
     */
    bool shouldLockOnSleep();
    
    /**
     * @brief Execute system shutdown
     */
    void systemShutdown();
    
    /**
     * @brief Execute system suspend
     */
    void systemSuspend();
    
    /**
     * @brief Execute system hibernate
     */
    void systemHibernate();
    
    /**
     * @brief Turn off screen
     */
    void systemTurnOffScreen();
    
    /**
     * @brief Show shutdown UI dialog
     */
    void showShutdownUI();
    
    /**
     * @brief Execute system lock
     */
    void systemLock();

    void systemAway();

    QDBusInterface *m_powerInterface;
    QDBusInterface *m_sessionInterface;
    Dtk::Core::DConfig *m_powerConfig;
};

#endif // POWERCONTROLLER_H