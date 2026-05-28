// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef POWERCONTROLLER_H
#define POWERCONTROLLER_H

#include "basecontroller.h"

#include <QMap>

namespace Dtk {
namespace Core {
class DConfig;
}
}

/**
 * @brief Power-action controller for dde-shortcut-tool.
 *
 * Invoked as `/usr/bin/dde-shortcut-tool power <action>` from the keybinding
 * shortcut for the physical power key (keycode 116) and other power-related
 * shortcuts. Ports the dde-daemon keybinding1 power-button logic, including
 * the bug-209669 turn-off-screen sequence and the X11 anti-flicker path
 * through ShutdownFront1.
 */
class PowerController : public BaseController
{
    Q_OBJECT

public:
    explicit PowerController(QObject *parent = nullptr);
    ~PowerController() override;

    static QString commandName() { return "power"; }
    static QStringList commandActions();
    static QMap<QString, QString> commandActionHelp();

    QString name() const override { return commandName(); }
    QStringList supportedActions() const override;
    bool execute(const QString &action, const QStringList &args = QStringList()) override;
    QString actionHelp(const QString &action) const override;

    void handlePowerButton();
    void handleSystemSuspend();
    void switchPowerMode();

private:
    bool isOnBattery();
    int getPowerButtonAction(Dtk::Core::DConfig *config, bool onBattery);
    bool shouldLockOnScreenBlack(Dtk::Core::DConfig *config);
    bool shouldLockOnSleep(Dtk::Core::DConfig *config);

    bool callSessionBool(const char *method);
    bool canShutdown();
    bool canSuspend();
    bool canHibernate();
    bool isLocked();
    bool hasShutdownInhibit();
    bool hasMultipleDisplaySession();

    // Power1 (session bus) PrepareSuspend notification, keeps the power
    // daemon from racing the screen-off transition.
    void doPrepareSuspend();
    void undoPrepareSuspend();

    // KWin BlackScreen effect (Treeland/X11 visual mask, separate from DPMS).
    bool isWmBlackScreenActive();
    void setWmBlackScreenActive(bool active);

    void systemShutdown();
    void systemSuspend();
    void systemHibernate();
    void systemTurnOffScreen();
    void showShutdownUI();
    void systemAway();

    // autoStartAuth maps to LockFront1.ShowAuth's parameter on X11; ignored
    // on Wayland where logind Session.Lock has no equivalent.
    void doLock(bool autoStartAuth);
};

#endif // POWERCONTROLLER_H
