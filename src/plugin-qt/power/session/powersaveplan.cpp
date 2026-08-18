// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "powersaveplan.h"
#include "powermanager.h"
#include "idle/idlewatcher.h"
#include "screen/screencontroller.h"
#include "sessiondbusproxy.h"
#include "../powerconstants.h"

#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <algorithm>
#include <cmath>
#include <limits>

Q_DECLARE_LOGGING_CATEGORY(logPowerSession)

using namespace PowerDBus;
using namespace PowerDConfig;
using namespace PowerFS;

static bool canAdd(const QString &type, int delay,
                   const QVector<PowerSavePlan::MetaTask> &tasks)
{
    if (tasks.isEmpty())
        return true;
    if (type == QLatin1String("sleep"))
        return true;
    if (type == QLatin1String("screenSaverStart")) {
        int min = tasks.first().delay;
        for (const auto &t : tasks)
            if (t.delay < min) min = t.delay;
        return delay <= min;
    }
    if (type == QLatin1String("screenBlack")) {
        if (delay < tasks.first().delay)
            return true;
        if (delay == tasks.first().delay && tasks.last().name == QLatin1String("lock"))
            return true;
        return false;
    }
    return false;
}
static double dropBrightness(double value, uint percent)
{
    const double ratio = std::clamp(1.0 - static_cast<double>(percent) / 100.0,
                                    0.1, 1.0);
    return std::clamp(std::round(value * ratio * 100.0) / 100.0, 0.1, 1.0);
}

static double restoreBrightness(double value, uint percent)
{
    const double ratio = std::clamp(1.0 - static_cast<double>(percent) / 100.0,
                                    0.1, 1.0);
    return std::clamp(std::round(value / ratio * 100.0) / 100.0, 0.1, 1.0);
}


PowerSavePlan::PowerSavePlan(PowerManager *powerManager, QObject *parent)
    : QObject(parent)
    , m_allowScreenSaver(powerManager ? powerManager->allowScreenSaver() : true)
    , m_powerManager(powerManager)
{
}

void PowerSavePlan::Start()
{
    Reset();
    initializePowerSavingBrightness();
}

void PowerSavePlan::Reset()
{
    if (!m_powerManager)
        return;

    if (m_powerManager->onBattery())
        OnBattery();
    else
        OnLinePower();
}

void PowerSavePlan::ResetFromNow()
{
    if (!m_powerManager)
        return;
    if (m_powerManager->onBattery()) {
        Update(m_powerManager->batteryScreensaverDelay(), m_powerManager->batteryLockDelay(),
               m_powerManager->batteryScreenBlackDelay(), m_powerManager->batterySleepDelay(),
               m_powerManager->batteryShortIdleDelay(), true);
    } else {
        Update(m_powerManager->linePowerScreensaverDelay(), m_powerManager->linePowerLockDelay(),
               m_powerManager->linePowerScreenBlackDelay(), m_powerManager->linePowerSleepDelay(),
               m_powerManager->linePowerShortIdleDelay(), true);
    }
}

void PowerSavePlan::OnBattery()
{
    if (!m_powerManager)
        return;

    Update(m_powerManager->batteryScreensaverDelay(), m_powerManager->batteryLockDelay(),
           m_powerManager->batteryScreenBlackDelay(), m_powerManager->batterySleepDelay(),
           m_powerManager->batteryShortIdleDelay());
}

void PowerSavePlan::OnLinePower()
{
    if (!m_powerManager)
        return;

    Update(m_powerManager->linePowerScreensaverDelay(), m_powerManager->linePowerLockDelay(),
           m_powerManager->linePowerScreenBlackDelay(), m_powerManager->linePowerSleepDelay(),
           m_powerManager->linePowerShortIdleDelay());
}

void PowerSavePlan::Update(int screenSaverStartDelay, int lockDelay,
                           int screenBlackDelay, int sleepDelay, int shortIdleDelay,
                           bool resetFromNow)
{
    interruptTasks();
    m_metaTasks.clear();

    qInfo(logPowerSession) << "Updating PowerSavePlan: screenSaverStartDelay=" << screenSaverStartDelay
                       << " lockDelay=" << lockDelay
                       << " screenBlackDelay=" << screenBlackDelay
                       << " sleepDelay=" << sleepDelay;

    if (sleepDelay > 0 && canAdd("sleep", sleepDelay, m_metaTasks)) {
        m_metaTasks.append({sleepDelay, 0, "sleep", [this]{
            sleep();
        }});
    }

    if (screenSaverStartDelay > 0 && canAdd("screenSaverStart", screenSaverStartDelay, m_metaTasks)) {
        m_metaTasks.append({screenSaverStartDelay, 0, "screenSaverStart", [this]{
            startScreensaver();
        }});
    }

    if (lockDelay > 0) {
        m_metaTasks.append({lockDelay, 0, "lock", [this]{
            if (m_powerManager) {
                m_powerManager->doLock();
            }
        }});
    }
    if (screenBlackDelay > 0) {
        m_metaTasks.append({screenBlackDelay, 0, "screenBlack", [this] {
            screenBlack();
        }});
    }

    if (shortIdleDelay > 0) {
        m_metaTasks.append({shortIdleDelay, 0, "shortIdle", [this]{
            setShortIdle(true);
        }});
    }

    int min = 0;
    for (const auto &t : m_metaTasks) {
        if (t.delay < min || min == 0)  {
            min = t.delay;
        }
    }

    const qint64 elapsed = m_isIdle && !resetFromNow && m_powerManager->idleWatcher()
        ? m_powerManager->idleWatcher()->idleTimeMs() : 0;

    setScreenSaverTimeout(min);

    for (auto &t : m_metaTasks) {
        int nSecs = t.delay - min;
        t.realDelay = nSecs > 0 ? nSecs * 1000 : 1;
    }
    if (m_isIdle) {
        for (auto task : std::as_const(m_metaTasks)) {
            const qint64 remaining = resetFromNow
                ? task.delay * 1000LL
                : qMax<qint64>(1, task.delay * 1000LL - elapsed);
            task.realDelay = static_cast<int>(qMin<qint64>(
                remaining, std::numeric_limits<int>::max()));
            scheduleTask(task);
        }
    }
}

void PowerSavePlan::HandleIdleOn()
{
    qDebug(logPowerSession) << "HandleIdleOn";

    if (!m_powerManager) {
        return; 
    }

    if (m_powerManager->shouldIgnoreIdleOn()) {
        qDebug(logPowerSession) << "shouldIgnoreIdleOn is true, ignoring idle on event";
        return;
    }
    if (!m_powerManager->useWayland() && !m_powerManager->isSessionActive()) {
        qDebug(logPowerSession) << "Ignoring idle event for inactive X11 session";
        return;
    }
    if (!m_powerManager->useWayland() && m_powerManager->shouldPreventIdle()) {
        qDebug(logPowerSession) << "Preventing idle for fullscreen workaround application";
        if (auto *idleWatcher = m_powerManager->idleWatcher())
            idleWatcher->simulateActivity();
        return;
    }

    m_isIdle = true;

    auto *idleWatcher = m_powerManager->idleWatcher();
    const qint64 elapsed = idleWatcher ? idleWatcher->idleTimeMs() : 0;

    for (auto t : m_metaTasks) {
        if (idleWatcher) {
            const qint64 remaining = t.delay * 1000LL - elapsed;
            t.realDelay = static_cast<int>(qMin<qint64>(
                qMax<qint64>(1, remaining), std::numeric_limits<int>::max()));
        }
        scheduleTask(t);
    }

    if (QFile::exists(kNoSuspendFile) && m_powerManager->screenBlackLock())
        m_powerManager->doLock(true);
}

void PowerSavePlan::HandleIdleOff()
{
    if (!m_powerManager)
        return;
    const int action = m_powerManager->onBattery()
        ? m_powerManager->batteryPressPowerBtnAction()
        : m_powerManager->linePowerPressPowerBtnAction();
    if (action == PA_TurnOffScreen) {
        QTimer::singleShot(qMin(m_powerManager->idleOffDelayWhenScreenBlack(), 2500),
                           this, &PowerSavePlan::handleIdleOff);
        return;
    }
    handleIdleOff();
}

void PowerSavePlan::handleIdleOff()
{
    m_isIdle = false;
    setShortIdle(false);
    m_powerManager->setScreenIdleState(false);
    if (m_powerManager->shouldIgnoreIdleOff()) {
        m_powerManager->SetPrepareSuspend(PS_Finish);
        return;
    }
    m_powerManager->SetPrepareSuspend(PS_Finish);
    interruptTasks();
    m_powerManager->setDPMSModeOn();
    m_powerManager->setBlackScreenActive(false);
    resetBrightness();
}

void PowerSavePlan::setShortIdle(bool state)
{
    if (!m_powerManager)
        return;
    qInfo(logPowerSession) << "Changing short idle state:" << state;
    if (!m_powerManager->shortIdleEnabled()) {
        qInfo(logPowerSession) << "Short idle is disabled by DConfig";
        return;
    }
    if (state && !m_powerManager->canEnterShortIdle()) {
        return;
    }
    m_powerManager->setShortIdleState(state);
    QTimer::singleShot(300, m_powerManager, [manager = m_powerManager, state]() {
        manager->setKernelIdleState(state);
    });
}

void PowerSavePlan::scheduleTask(const MetaTask &t)
{
    qDebug(logPowerSession) << "Scheduling task" << t.name << "to run in" << t.realDelay << "ms";
    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, t.fn);
    timer->start(t.realDelay > 0 ? t.realDelay : 100);
    m_timers.append(timer);
}

void PowerSavePlan::interruptTasks()
{
    for (auto *t : m_timers) {
        t->stop();
        t->deleteLater();
    }
    m_timers.clear();
}

void PowerSavePlan::setScreenSaverTimeout(int seconds)
{
    if (!m_powerManager)
        return;
    if (auto *iw = m_powerManager->idleWatcher())
        iw->setTimeout(static_cast<uint32_t>(qMax(0, seconds)));
}

void PowerSavePlan::startScreensaver()
{
    if (m_powerManager->useWayland())
        return;
    if (qEnvironmentVariable("DESKTOP_CAN_SCREENSAVER") == "N" || !m_allowScreenSaver)
        return;

    QDBusInterface iface(kScreensaver, kScreensaverPath, kScreensaver,
                         QDBusConnection::sessionBus());
    iface.call("Start");
    m_screensaverRunning = true;
}

void PowerSavePlan::sleep()
{
    if (!m_powerManager)
        return;
    if (m_powerManager->useWayland()) {
        stopScreensaver();
        m_powerManager->doSuspend();
        return;
    }

    if (!m_powerManager->m_screensaverStateCaptured) {
        m_powerManager->m_screensaverLockAtAwake =
            m_powerManager->screensaverProperty("lockScreenAtAwake");
        m_powerManager->m_screensaverStateCaptured = true;
    }
    m_powerManager->m_screensaverWasRunning =
        m_powerManager->screensaverProperty("isRunning");
    if (m_powerManager->m_screensaverWasRunning) {
        QDBusInterface screensaver(kScreensaver, kScreensaverPath, kScreensaver,
                                   QDBusConnection::sessionBus());
        screensaver.asyncCall(QStringLiteral("Stop"));
        m_screensaverRunning = false;
    }
    m_powerManager->doSuspendByFront();
}

void PowerSavePlan::stopScreensaver()
{
    if (!m_screensaverRunning)
        return;
    QDBusInterface iface(kScreensaver, kScreensaverPath, kScreensaver,
                          QDBusConnection::sessionBus());
    iface.call("Stop");
    m_screensaverRunning = false;
}

void PowerSavePlan::screenBlack()
{
    qDebug(logPowerSession) << "Blackening screen";
    if (!m_powerManager)
        return;

    bool adjustBrightness = !m_powerManager->useWayland()
        && m_powerManager->adjustBrightnessEnabled();
    if (adjustBrightness) {
        saveCurrentBrightness();
        adjustBrightness = !m_oldBrightness.isEmpty();
        QMap<QString, double> brightness;
        for (auto it = m_oldBrightness.cbegin(); it != m_oldBrightness.cend(); ++it)
            brightness.insert(it.key(), it.value() * 0.5);
        m_powerManager->setDisplayBrightness(brightness);
    }

    auto finish = [this, adjustBrightness]() {
        if (!m_powerManager)
            return;
        m_powerManager->m_screensaverWasRunning = m_screensaverRunning;
        QDBusMessage query = QDBusMessage::createMethodCall(
            kScreensaver, kScreensaverPath,
            QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
        query.setArguments({QLatin1String(kScreensaver), QStringLiteral("isRunning")});
        query.setAutoStartService(false);
        auto *watcher = new QDBusPendingCallWatcher(
            QDBusConnection::sessionBus().asyncCall(query), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this](QDBusPendingCallWatcher *finishedWatcher) {
            const QDBusPendingReply<QDBusVariant> reply = *finishedWatcher;
            finishedWatcher->deleteLater();
            if (!m_powerManager || reply.isError())
                return;
            m_powerManager->m_screensaverWasRunning = reply.value().variant().toBool();
            if (!m_powerManager->m_screensaverWasRunning)
                return;
            QDBusInterface screensaver(kScreensaver, kScreensaverPath, kScreensaver,
                                       QDBusConnection::sessionBus());
            screensaver.asyncCall(QStringLiteral("Stop"));
            m_screensaverRunning = false;
        });
        if (adjustBrightness) {
            QMap<QString, double> brightness;
            for (auto it = m_oldBrightness.cbegin(); it != m_oldBrightness.cend(); ++it)
                brightness.insert(it.key(), 0.02);
            m_powerManager->setDisplayBrightness(brightness);
        }
        if (m_powerManager->useWayland()) {
            if (auto *screen = m_powerManager->screenController())
                screen->setAllModes(ScreenController::Off);
        } else {
            m_powerManager->setDPMSModeOff();
        }
        if (m_powerManager->screenBlackLock())
            QTimer::singleShot(200, m_powerManager, [manager = m_powerManager]() { manager->doLock(); });
    };
    // scheduleTask tracks this timer so HandleIdleOff can cancel the staged blackout.
    scheduleTask({0, m_powerManager->useWayland() ? 0 : 5000,
                  QStringLiteral("screenBlackFinish"), finish});
}

void PowerSavePlan::syncPowerSavingMode(bool enabled, uint brightnessDropPercent)
{
    m_psmEnabled = enabled;
    m_psmDrop = brightnessDropPercent;
}

void PowerSavePlan::initializePowerSavingBrightness()
{
    if (!m_powerManager || m_powerManager->useWayland() || !m_powerManager->m_proxy)
        return;

    auto *proxy = m_powerManager->m_proxy;
    connect(proxy, &SessionDBusProxy::BrightnessChanged,
            this, &PowerSavePlan::handleBrightnessChanged);
    connect(proxy, &SessionDBusProxy::PowerSavingModeBrightnessDataChanged,
            this, [this](const QString &data) {
        if (!m_powerManager->isSessionActive())
            loadPowerSavingBrightness(data);
    });
    connect(proxy, &SessionDBusProxy::SessionActiveChanged, this, [this](bool active) {
        if (active)
            m_sessionActiveGrace.restart();
    });
    m_sessionActiveGrace.start();

    const QString shared = proxy->powerSavingModeBrightnessData();
    const QString saved = m_powerManager->m_config
        ? m_powerManager->m_config->value(QLatin1String(kSaveBrightnessWhilePsm)).toString()
        : QString();
    loadPowerSavingBrightness(shared.isEmpty() ? saved : shared);
    m_lastBrightness = m_powerManager->displayBrightness();

    if (shared.isEmpty()) {
        const double ratio = qMax(0.1, 1.0 - static_cast<double>(m_psmDrop) / 100.0);
        for (auto it = m_brightnessState.begin(); it != m_brightnessState.end(); ++it) {
            if (!it->manuallyModified)
                continue;
            it->manuallyModified = false;
            it->saved = qMin(1.0, it->latest / ratio);
        }
        savePowerSavingBrightness();

        const bool savedEnabled = m_powerManager->m_config
            && m_powerManager->m_config->value(QLatin1String(kPowerSavingModeEnabled)).toBool();
        if (savedEnabled != m_psmEnabled)
            onPowerSavingModeEnabledChanged(m_psmEnabled);
    } else {
        m_powerManager->persist(kPowerSavingModeEnabled, m_psmEnabled);
    }
}
void PowerSavePlan::loadPowerSavingBrightness(const QString &data)
{
    m_brightnessState.clear();
    const QJsonDocument document = QJsonDocument::fromJson(data.toUtf8());
    if (!document.isArray())
        return;
    for (const QJsonValue &value : document.array()) {
        const QJsonObject object = value.toObject();
        const QString monitor = object.value(QStringLiteral("MonitorName")).toString();
        if (monitor.isEmpty())
            continue;
        m_brightnessState.insert(monitor, {
            object.value(QStringLiteral("BrightnessSaved")).toDouble(),
            object.value(QStringLiteral("BrightnessLatest")).toDouble(),
            object.value(QStringLiteral("ManuallyModified")).toBool()
        });
    }
}

void PowerSavePlan::savePowerSavingBrightness()
{
    QJsonArray array;
    for (auto it = m_brightnessState.cbegin(); it != m_brightnessState.cend(); ++it) {
        array.append(QJsonObject {
            { QStringLiteral("MonitorName"), it.key() },
            { QStringLiteral("BrightnessSaved"), it->saved },
            { QStringLiteral("BrightnessLatest"), it->latest },
            { QStringLiteral("ManuallyModified"), it->manuallyModified }
        });
    }
    const QString data = QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
    m_powerManager->persist(kSaveBrightnessWhilePsm, data);
    m_powerManager->m_proxy->setPowerSavingModeBrightnessData(data);
}

void PowerSavePlan::handleBrightnessChanged(const QMap<QString, double> &brightness)
{
    if (!m_psmEnabled) {
        m_lastBrightness = brightness;
        return;
    }
    const bool ownChange = (m_psmEnabledGrace.isValid() && m_psmEnabledGrace.elapsed() < 2000)
        || (m_psmPercentGrace.isValid() && m_psmPercentGrace.elapsed() < 2000)
        || (m_sessionActiveGrace.isValid() && m_sessionActiveGrace.elapsed() < 3000);
    if (!ownChange) {
        bool changed = false;
        for (auto it = brightness.cbegin(); it != brightness.cend(); ++it) {
            if (!m_lastBrightness.contains(it.key())
                || std::abs(m_lastBrightness.value(it.key()) - it.value()) < 1e-6)
                continue;
            auto state = m_brightnessState.find(it.key());
            if (state == m_brightnessState.end())
                continue;
            state->latest = it.value();
            state->manuallyModified = true;
            changed = true;
        }
        if (changed)
            savePowerSavingBrightness();
    }
    m_lastBrightness = brightness;
}

void PowerSavePlan::onPowerSavingModeEnabledChanged(bool enabled)
{
    m_psmEnabled = enabled;
    qDebug(logPowerSession) << "PowerSavingModeEnabled changed to" << enabled
                            << ", drop=" << m_psmDrop;
    if (m_powerManager)
        m_powerManager->persist(kPowerSavingModeEnabled, enabled);

    if (enabled && m_powerManager && !m_powerManager->useWayland()
        && m_powerManager->isSessionActive()
        && !(m_powerManager->hasAmbientLightSensor()
             && m_powerManager->ambientLightAdjustBrightness())) {
        m_lastBrightness = m_powerManager->displayBrightness();
        m_brightnessState.clear();
        for (auto it = m_lastBrightness.cbegin(); it != m_lastBrightness.cend(); ++it)
            m_brightnessState.insert(it.key(), {it.value(), 0, false});
        savePowerSavingBrightness();
        m_psmEnabledGrace.restart();
    }

    if (enabled)
        applyBrightnessDrop();
    else
        restorePowerSavingBrightness();
}

void PowerSavePlan::onBrightnessDropPercentChanged(uint value)
{
    qDebug(logPowerSession) << "BrightnessDropPercent changed to" << value
                            << ", enabled=" << m_psmEnabled;

    if (m_psmEnabled && m_powerManager && !m_powerManager->useWayland()) {
        if (!m_powerManager->isSessionActive()
            || (m_powerManager->hasAmbientLightSensor()
                && m_powerManager->ambientLightAdjustBrightness())) {
            m_psmDrop = value;
            return;
        }
        m_psmPercentGrace.restart();
        auto brightness = m_powerManager->displayBrightness();
        for (auto it = brightness.begin(); it != brightness.end(); ++it)
            it.value() = dropBrightness(restoreBrightness(it.value(), m_psmDrop), value);
        m_powerManager->setAndSaveDisplayBrightness(brightness);
        m_psmDrop = value;
        return;
    }

    m_psmDrop = value;
    if (m_psmEnabled)
        applyBrightnessDrop();
}

void PowerSavePlan::saveCurrentBrightness()
{
    if (!m_oldBrightness.isEmpty())
        return;
    if (!m_powerManager)
        return;
    if (!m_powerManager->useWayland()) {
        m_oldBrightness = m_powerManager->displayBrightness();
        return;
    }

    auto *screen = m_powerManager->screenController();
    if (!screen || !screen->supportsBrightness())
        return;
    for (int i = 0; i < screen->outputCount(); ++i) {
        const double brightness = screen->brightness(i);
        if (brightness >= 0.0)
            m_oldBrightness.insert(QString::number(i), brightness);
    }
}

void PowerSavePlan::resetBrightness()
{
    if (!m_powerManager) {
        m_oldBrightness.clear();
        return;
    }
    if (!m_powerManager->useWayland()) {
        m_powerManager->setDisplayBrightness(m_oldBrightness);
        m_oldBrightness.clear();
        return;
    }

    auto *screen = m_powerManager->screenController();
    if (screen && screen->supportsBrightness()) {
        for (auto it = m_oldBrightness.cbegin(); it != m_oldBrightness.cend(); ++it) {
            bool ok = false;
            const int index = it.key().toInt(&ok);
            if (ok)
                screen->setBrightness(index, it.value());
        }
    }
    m_oldBrightness.clear();
}

void PowerSavePlan::applyBrightnessDrop()
{
    if (!m_powerManager)
        return;

    if (!m_powerManager->useWayland()) {
        if (!m_powerManager->isSessionActive()
            || (m_powerManager->hasAmbientLightSensor()
                && m_powerManager->ambientLightAdjustBrightness())) {
            return;
        }
        auto brightness = m_powerManager->displayBrightness();
        for (auto it = brightness.begin(); it != brightness.end(); ++it)
            it.value() = dropBrightness(it.value(), m_psmDrop);
        m_powerManager->setAndSaveDisplayBrightness(brightness);
        return;
    }

    auto *screen = m_powerManager->screenController();
    if (!screen || !screen->supportsBrightness()) {
        qWarning(logPowerSession) << "applyBrightnessDrop: brightness not supported";
        return;
    }
    if (m_powerSavingBrightness.isEmpty()) {
        for (int i = 0; i < screen->outputCount(); ++i) {
            const double brightness = screen->brightness(i);
            if (brightness >= 0.0)
                m_powerSavingBrightness.insert(QString::number(i), brightness);
        }
    }
    const double ratio = std::clamp(1.0 - static_cast<double>(m_psmDrop) / 100.0,
                                    0.1, 1.0);
    for (auto it = m_powerSavingBrightness.cbegin();
         it != m_powerSavingBrightness.cend(); ++it) {
        bool ok = false;
        const int index = it.key().toInt(&ok);
        if (ok)
            screen->setBrightness(index, std::clamp(it.value() * ratio, 10.0, 100.0));
    }
}
void PowerSavePlan::restorePowerSavingBrightness()
{
    if (!m_powerManager)
        return;

    if (!m_powerManager->useWayland()) {
        if (!m_powerManager->isSessionActive()
            || (m_powerManager->hasAmbientLightSensor()
                && m_powerManager->ambientLightAdjustBrightness())) {
            return;
        }
        auto brightness = m_powerManager->displayBrightness();
        for (auto it = brightness.begin(); it != brightness.end(); ++it)
            it.value() = restoreBrightness(it.value(), m_psmDrop);
        m_powerManager->setAndSaveDisplayBrightness(brightness);
        return;
    }

    auto *screen = m_powerManager->screenController();
    if (screen && screen->supportsBrightness()) {
        for (auto it = m_powerSavingBrightness.cbegin();
             it != m_powerSavingBrightness.cend(); ++it) {
            bool ok = false;
            const int index = it.key().toInt(&ok);
            if (ok)
                screen->setBrightness(index, it.value());
        }
    }
    m_powerSavingBrightness.clear();
}
