// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "powercontroller.h"
#include "constant.h"
#include "treelandlockscreenwrapper.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDebug>
#include <QFile>
#include <QProcess>
#include <QThread>

#include <DConfig>

DCORE_USE_NAMESPACE

// Power1 SetPrepareSuspend states (match dde-daemon keybinding1/utils.go).
static constexpr int kSuspendStateFinish      = 3;
static constexpr int kSuspendStateButtonClick = 7;

// Delay between switching on the KWin BlackScreen mask and triggering DPMS off.
// bug-209669: on some vendor machines DPMS-off blocks the lock screen Show
// until DPMS is back on, causing a wake-flash-then-lock visual glitch. The
// mask hides the gap; 100ms is enough for the compositor to draw it.
static constexpr int kBlackScreenMaskDelayMs = 100;

namespace {

bool isWaylandSession()
{
    return qEnvironmentVariable("XDG_SESSION_TYPE").toLower() == "wayland";
}

DConfig *createPowerConfig(QObject *parent)
{
    DConfig *config = DConfig::create("org.deepin.dde.daemon", "org.deepin.dde.daemon.power", "", parent);
    if (!config || !config->isValid())
        qWarning() << "PowerController: daemon power config is not valid";
    return config;
}

} // namespace

PowerController::PowerController(QObject *parent)
    : BaseController(parent)
{
}

PowerController::~PowerController() = default;

QStringList PowerController::commandActions()
{
    return QStringList{ "button", "switch-mode", "system-away", "show-ui" };
}

QMap<QString, QString> PowerController::commandActionHelp()
{
    return {
        {"button", "Handle power button press event"},
        {"switch-mode", "Switch power performance mode"},
        {"system-away", "Lock the system"},
        {"show-ui", "Show shutdown interface"}
    };
}

QStringList PowerController::supportedActions() const
{
    return commandActions();
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
    } else if (action == "show-ui") {
        showShutdownUI();
        return true;
    }

    qWarning() << "PowerController: unknown action" << action;
    return false;
}

QString PowerController::actionHelp(const QString &action) const
{
    return commandActionHelp().value(action);
}

void PowerController::handlePowerButton()
{
    DConfig *config = createPowerConfig(this);
    if (!config || !config->isValid()) {
        qWarning() << "PowerController: power config not available";
        return;
    }

    const bool onBattery = isOnBattery();
    int powerAction = getPowerButtonAction(config, onBattery);
    qInfo() << "PowerController: power button, onBattery:" << onBattery << "action:" << powerAction;

    switch (powerAction) {
    case PowerActionShutdown:      systemShutdown();      break;
    case PowerActionSuspend:       systemSuspend();       break;
    case PowerActionHibernate:     systemHibernate();     break;
    case PowerActionTurnOffScreen: systemTurnOffScreen(); break;
    case PowerActionShowUI:        showShutdownUI();      break;
    default:
        qWarning() << "PowerController: unknown power action" << powerAction;
        break;
    }
}

void PowerController::handleSystemSuspend()
{
    DConfig *config = createPowerConfig(this);
    if (shouldLockOnSleep(config)) {
        doLock(true);
        // Brief pause so the lock screen draws before suspend kicks in.
        QThread::msleep(500);
    }
    systemSuspend();
}

bool PowerController::isOnBattery()
{
    QDBusInterface power("org.deepin.dde.Power1", "/org/deepin/dde/Power1", "org.deepin.dde.Power1",
                         QDBusConnection::systemBus());
    if (!power.isValid())
        return false;
    QVariant v = power.property("OnBattery");
    return v.isValid() && v.toBool();
}

int PowerController::getPowerButtonAction(DConfig *config, bool onBattery)
{
    if (!config)
        return PowerActionShowUI;

    const QString key = onBattery
        ? Config::KEY_BATTERY_PRESS_POWER_BTN_ACTION
        : Config::KEY_LINE_POWER_PRESS_POWER_BTN_ACTION;
    return config->value(key, PowerActionShowUI).toInt();
}

bool PowerController::shouldLockOnScreenBlack(DConfig *config)
{
    if (!config)
        return true;
    return config->value(Config::KEY_SCREEN_BLACK_LOCK, true).toBool();
}

bool PowerController::shouldLockOnSleep(DConfig *config)
{
    if (!config)
        return true;
    return config->value(Config::KEY_SLEEP_LOCK, true).toBool();
}

bool PowerController::callSessionBool(const char *method)
{
    QDBusInterface session("org.deepin.dde.SessionManager1", "/org/deepin/dde/SessionManager1",
                           "org.deepin.dde.SessionManager1", QDBusConnection::sessionBus());
    if (!session.isValid())
        return false;
    QDBusReply<bool> reply = session.call(method);
    return reply.isValid() && reply.value();
}

bool PowerController::canSuspend()   { return callSessionBool("CanSuspend"); }
bool PowerController::canHibernate() { return callSessionBool("CanHibernate"); }
bool PowerController::canShutdown()  { return callSessionBool("CanShutdown"); }

bool PowerController::isLocked()
{
    QDBusInterface session("org.deepin.dde.SessionManager1", "/org/deepin/dde/SessionManager1",
                           "org.deepin.dde.SessionManager1", QDBusConnection::sessionBus());
    if (!session.isValid())
        return false;
    QVariant v = session.property("Locked");
    return v.isValid() && v.toBool();
}

bool PowerController::hasShutdownInhibit()
{
    // ListInhibitors returns a(ssssuu); QDBusReply<...> can't auto-unmarshal
    // a list-of-struct, so walk the QDBusArgument manually.
    QDBusMessage call = QDBusMessage::createMethodCall(
        "org.freedesktop.login1", "/org/freedesktop/login1",
        "org.freedesktop.login1.Manager", "ListInhibitors");
    QDBusMessage reply = QDBusConnection::systemBus().call(call);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        qWarning() << "PowerController: ListInhibitors failed";
        return false;
    }

    const QDBusArgument arg = reply.arguments().first().value<QDBusArgument>();
    arg.beginArray();
    while (!arg.atEnd()) {
        arg.beginStructure();
        QString what, who, why, mode;
        uint uid = 0, pid = 0;
        arg >> what >> who >> why >> mode >> uid >> pid;
        arg.endStructure();
        // logind's What is colon-delimited (e.g. "shutdown:sleep:idle").
        if (what.split(':').contains(QLatin1String("shutdown"))) {
            arg.endArray();
            return true;
        }
    }
    arg.endArray();
    return false;
}

bool PowerController::hasMultipleDisplaySession()
{
    QDBusInterface displayMgr("org.freedesktop.DisplayManager", "/org/freedesktop/DisplayManager",
                              "org.freedesktop.DisplayManager", QDBusConnection::systemBus());
    if (!displayMgr.isValid())
        return false;
    // Sessions is a property of type 'ao', not a method.
    QVariant v = displayMgr.property("Sessions");
    if (!v.isValid())
        return false;

    const QList<QDBusObjectPath> sessions = qdbus_cast<QList<QDBusObjectPath>>(v);
    if (!isWaylandSession())
        return sessions.size() >= 2;

    // On Treeland the default display manager (ddm) keeps a default session(dde)
    int userSessions = 0;
    for (const QDBusObjectPath &path : sessions) {
        QDBusInterface session("org.freedesktop.DisplayManager", path.path(),
                               "org.freedesktop.DisplayManager.Session",
                               QDBusConnection::systemBus());
        if (!session.isValid())
            continue;
        if (session.property("UserName").toString() == QLatin1String("dde"))
            continue;
        ++userSessions;
    }

    return userSessions >= 2;
}

void PowerController::doPrepareSuspend()
{
    QDBusInterface power("org.deepin.dde.Power1", "/org/deepin/dde/Power1", "org.deepin.dde.Power1",
                         QDBusConnection::sessionBus());
    if (!power.isValid())
        return;
    power.call("SetPrepareSuspend", kSuspendStateButtonClick);
}

void PowerController::undoPrepareSuspend()
{
    QDBusInterface power("org.deepin.dde.Power1", "/org/deepin/dde/Power1", "org.deepin.dde.Power1",
                         QDBusConnection::sessionBus());
    if (!power.isValid())
        return;
    power.call("SetPrepareSuspend", kSuspendStateFinish);
}

bool PowerController::isWmBlackScreenActive()
{
    QDBusInterface kwin("org.kde.KWin", "/BlackScreen", "org.kde.kwin.BlackScreen",
                        QDBusConnection::sessionBus());
    if (!kwin.isValid())
        return false;
    QDBusReply<bool> reply = kwin.call("getActive");
    return reply.isValid() && reply.value();
}

void PowerController::setWmBlackScreenActive(bool active)
{
    QDBusInterface kwin("org.kde.KWin", "/BlackScreen", "org.kde.kwin.BlackScreen",
                        QDBusConnection::sessionBus());
    if (!kwin.isValid()) {
        qWarning() << "PowerController: KWin BlackScreen not available";
        return;
    }
    kwin.call("setActive", active);
}

void PowerController::doLock(bool autoStartAuth)
{
    qInfo() << "PowerController: lock screen (autoStartAuth=" << autoStartAuth << ")";

    if (isWaylandSession()) {
        // Treeland: dde-shell-v1 lockscreen.lock is the supported way to bring
        // up the lock UI. autoStartAuth is X11-only — on Wayland Treeland's UI
        // chooses its own behavior.
        Q_UNUSED(autoStartAuth);
        TreelandLockScreenManager manager;
        if (!manager.lock())
            qWarning() << "PowerController: Treeland lock request failed";
        return;
    }

    QDBusInterface lockFront("org.deepin.dde.LockFront1", "/org/deepin/dde/LockFront1",
                             "org.deepin.dde.LockFront1", QDBusConnection::sessionBus());
    if (!lockFront.isValid()) {
        qWarning() << "PowerController: LockFront1 unavailable";
        return;
    }
    lockFront.call("ShowAuth", autoStartAuth);
}

void PowerController::systemShutdown()
{
    if (!canShutdown()) {
        qInfo() << "PowerController: shutdown not available";
        return;
    }

    if (isLocked()) {
        // Locked sessions get their own shutdown UI from the lock screen.
        qInfo() << "PowerController: session locked, skipping shutdown";
        return;
    }

    if (hasShutdownInhibit() || hasMultipleDisplaySession()) {
        if (isWaylandSession()) {
            // Treeland: route through dde-shell lockscreen.shutdown so the
            // compositor UI can show what is holding shutdown back, mirroring
            // the X11 ShutdownFront prompt.
            qInfo() << "PowerController: shutdown via Treeland lockscreen (inhibit/multi-session)";
            TreelandLockScreenManager manager;
            if (manager.showShutdown())
                return;
            qWarning() << "PowerController: Treeland shutdown UI request failed, falling back";
        } else {
            // Hand off to the blocking UI so the user sees what's preventing shutdown.
            qInfo() << "PowerController: shutdown via ShutdownFront (inhibit/multi-session)";
            QDBusInterface shutdownFront("org.deepin.dde.ShutdownFront1", "/org/deepin/dde/ShutdownFront1",
                                         "org.deepin.dde.ShutdownFront1", QDBusConnection::sessionBus());
            if (shutdownFront.isValid()) {
                shutdownFront.call("Shutdown");
                return;
            }
            qWarning() << "PowerController: ShutdownFront unavailable, falling back";
        }
    }

    qInfo() << "PowerController: shutdown via SessionManager";
    QDBusInterface session("org.deepin.dde.SessionManager1", "/org/deepin/dde/SessionManager1",
                           "org.deepin.dde.SessionManager1", QDBusConnection::sessionBus());
    if (session.isValid())
        session.call("RequestShutdown");
}

void PowerController::systemSuspend()
{
    if (!canSuspend()) {
        qInfo() << "PowerController: suspend not available";
        return;
    }

    if (isWaylandSession()) {
        qInfo() << "PowerController: suspend via SessionManager (Wayland)";
        QDBusInterface session("org.deepin.dde.SessionManager1", "/org/deepin/dde/SessionManager1",
                               "org.deepin.dde.SessionManager1", QDBusConnection::sessionBus());
        if (session.isValid())
            session.call("RequestSuspend");
        return;
    }

    // X11: ShutdownFront paints a black overlay first to mask the suspend flash.
    qInfo() << "PowerController: suspend via ShutdownFront (X11)";
    QDBusInterface shutdownFront("org.deepin.dde.ShutdownFront1", "/org/deepin/dde/ShutdownFront1",
                                 "org.deepin.dde.ShutdownFront1", QDBusConnection::sessionBus());
    if (shutdownFront.isValid()) {
        shutdownFront.call("Suspend");
    } else {
        QDBusInterface session("org.deepin.dde.SessionManager1", "/org/deepin/dde/SessionManager1",
                               "org.deepin.dde.SessionManager1", QDBusConnection::sessionBus());
        if (session.isValid())
            session.call("RequestSuspend");
    }
}

void PowerController::systemHibernate()
{
    if (!canHibernate()) {
        qInfo() << "PowerController: hibernate not available";
        return;
    }

    if (isWaylandSession()) {
        qInfo() << "PowerController: hibernate via SessionManager (Wayland)";
        QDBusInterface session("org.deepin.dde.SessionManager1", "/org/deepin/dde/SessionManager1",
                               "org.deepin.dde.SessionManager1", QDBusConnection::sessionBus());
        if (session.isValid())
            session.call("RequestHibernate");
        return;
    }

    qInfo() << "PowerController: hibernate via ShutdownFront (X11)";
    QDBusInterface shutdownFront("org.deepin.dde.ShutdownFront1", "/org/deepin/dde/ShutdownFront1",
                                 "org.deepin.dde.ShutdownFront1", QDBusConnection::sessionBus());
    if (shutdownFront.isValid()) {
        shutdownFront.call("Hibernate");
    } else {
        QDBusInterface session("org.deepin.dde.SessionManager1", "/org/deepin/dde/SessionManager1",
                               "org.deepin.dde.SessionManager1", QDBusConnection::sessionBus());
        if (session.isValid())
            session.call("RequestHibernate");
    }
}

void PowerController::systemTurnOffScreen()
{
    qInfo() << "PowerController: turn off screen";

    if (isWaylandSession()) {
        QDBusInterface power("org.deepin.dde.Power1", "/org/deepin/dde/Power1",
                             "org.deepin.dde.Power1", QDBusConnection::sessionBus());
        if (power.isValid()) {
            power.call("TurnOffScreen");
        } else {
            qWarning() << "PowerController: Power1 unavailable for TurnOffScreen";
        }
        return;
    }

    DConfig *config = createPowerConfig(this);
    const bool screenBlackLock = shouldLockOnScreenBlack(config);

    // Order matters (bug-209669):
    //   1) lock the screen BEFORE DPMS off, otherwise the lock UI's Show
    //      call blocks until DPMS comes back on, causing a wake-flash
    //   2) tell power daemon we're putting the screen down so it stops
    //      racing us
    //   3) cover the gap between "DPMS still on" and "lock UI drawn" with
    //      the KWin BlackScreen mask
    if (screenBlackLock)
        doLock(true);

    doPrepareSuspend();

    const bool needMask = screenBlackLock && !isWmBlackScreenActive();
    if (needMask) {
        setWmBlackScreenActive(true);
        QThread::msleep(kBlackScreenMaskDelayMs);
    }

    QProcess::execute("xset", {"dpms", "force", "off"});

    if (needMask)
        setWmBlackScreenActive(false);

    undoPrepareSuspend();

    QFile dpmsState("/tmp/dpms-state");
    if (dpmsState.open(QIODevice::WriteOnly | QIODevice::Truncate))
        dpmsState.write("1");
}

void PowerController::showShutdownUI()
{
    qInfo() << "PowerController: show shutdown UI";

    if (isWaylandSession()) {
        // Treeland removed ShutdownFront1; the dde-shell-v1 protocol's
        // lockscreen.shutdown is the supported way to bring up the UI.
        TreelandLockScreenManager manager;
        if (!manager.showShutdown())
            qWarning() << "PowerController: Treeland shutdown UI request failed";
        return;
    }

    if (isLocked()) {
        qInfo() << "PowerController: session locked, skip UI";
        return;
    }

    // X11: dde-shutdown.sh releases the keyboard grab before showing the dialog
    // so the keys inside the dialog (Tab, Enter, Esc) reach the GUI.
    QProcess::startDetached("/usr/lib/deepin-daemon/dde-shutdown.sh", {});
}

void PowerController::systemAway()
{
    qInfo() << "PowerController: RequestLock";

    if (isWaylandSession()) {
        TreelandLockScreenManager manager;
        if (!manager.lock())
            qWarning() << "PowerController: Treeland lock request failed";
        return;
    }

    QDBusInterface session("org.deepin.dde.SessionManager1", "/org/deepin/dde/SessionManager1",
                           "org.deepin.dde.SessionManager1", QDBusConnection::sessionBus());
    if (session.isValid())
        session.call("RequestLock");
}

void PowerController::switchPowerMode()
{
    qInfo() << "PowerController: switch power mode";

    QDBusInterface power("org.deepin.dde.Power1", "/org/deepin/dde/Power1", "org.deepin.dde.Power1",
                         QDBusConnection::systemBus());
    if (!power.isValid()) {
        qWarning() << "PowerController: Power1 unavailable";
        return;
    }

    QDBusReply<QString> modeReply = power.call("Mode");
    if (!modeReply.isValid()) {
        qWarning() << "PowerController: failed to get current power mode";
        return;
    }

    // Cycle: powersave → balance → performance → powersave
    const QString currentMode = modeReply.value();
    QString newMode;
    if (currentMode == "powersave")        newMode = "balance";
    else if (currentMode == "balance")     newMode = "performance";
    else                                   newMode = "powersave";

    QDBusReply<void> setReply = power.call("SetMode", newMode);
    if (!setReply.isValid()) {
        qWarning() << "PowerController: failed to set power mode to" << newMode;
        return;
    }

    qInfo() << "PowerController: power mode" << currentMode << "->" << newMode;
}
