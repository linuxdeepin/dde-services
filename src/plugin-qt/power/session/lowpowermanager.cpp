// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "lowpowermanager.h"
#include "powermanager.h"
#include "../powerconstants.h"

#include <QDBusInterface>
#include <QDBusConnection>
#include <QProcess>
#include <QLoggingCategory>
#include <QDBusMessage>

Q_DECLARE_LOGGING_CATEGORY(logPowerSession)

using namespace PowerFS;
using namespace PowerDConfig;

LowPowerManager::LowPowerManager(PowerManager *powerManager)
    : QDBusAbstractAdaptor(powerManager), m_powerManager(powerManager)
{
    m_countTicker = new QTimer(this);
    m_countTicker->setInterval(1000);
    m_validationTimer = new QTimer(this);
    m_validationTimer->setSingleShot(true);
    m_validationTimer->setInterval(20000);

    connect(m_validationTimer, &QTimer::timeout, this, [this] {
        if (!configValid()) {
            qWarning(logPowerSession) << "Warn-level configuration is invalid; resetting";
            Reset();
        }
    });
    connect(m_countTicker, &QTimer::timeout, this, [this]() {
        m_count++;
        if (!m_powerManager) {
            disableTicker();
            return;
        }

        if (m_count == 3) {
            if (m_powerManager->sleepLock())
                lockWaitShow(5000, false);
        } else if (m_count == 4) {
            showLowPower();
        } else if (m_count >= 5) {
            disableTicker();
            if (m_powerManager->lowPowerAction() == 1)
                m_powerManager->doHibernate();
            else
                m_powerManager->doSuspend();
        }
    });

    connect(m_powerManager, &PowerManager::batteryPercentageChanged,
            this, &LowPowerManager::updateWarnLevel);
    connect(m_powerManager, &PowerManager::batteryTimeToEmptyChanged,
            this, &LowPowerManager::updateWarnLevel);
    connect(m_powerManager, &PowerManager::onBatteryChanged,
            this, &LowPowerManager::updateWarnLevel);
    connect(m_powerManager, &PowerManager::lowPowerNotifyThresholdChanged, this, [this] {
        setLowPowerNotifyThreshold(m_powerManager->lowPowerNotifyThreshold());
        scheduleValidation();
    });
    connect(m_powerManager, &PowerManager::lowPowerAutoSleepThresholdChanged, this, [this] {
        setActionPercentage(m_powerManager->lowPowerAutoSleepThreshold());
        scheduleValidation();
    });
}

#define SET_CONFIG_VALUE(Type, Name, member, signal, dbusName) \
    void LowPowerManager::set##Name(Type value) \
    { \
        if (member == static_cast<decltype(member)>(value)) \
            return; \
        member = static_cast<decltype(member)>(value); \
        Q_EMIT signal(); \
        notifyPropertyChanged(dbusName, QVariant::fromValue(value)); \
        updateWarnLevel(); \
    }

SET_CONFIG_VALUE(bool, UsePercentageForPolicy, m_usePercentageForPolicy,
                 usePercentageForPolicyChanged, "UsePercentageForPolicy")
SET_CONFIG_VALUE(qint64, LowTime, m_timeToEmptyLow, lowTimeChanged, "LowTime")
SET_CONFIG_VALUE(qint64, DangerTime, m_timeToEmptyDanger, dangerTimeChanged, "DangerTime")
SET_CONFIG_VALUE(qint64, CriticalTime, m_timeToEmptyCritical, criticalTimeChanged, "CriticalTime")
SET_CONFIG_VALUE(qint64, ActionTime, m_timeToEmptyAction, actionTimeChanged, "ActionTime")
SET_CONFIG_VALUE(qint64, LowPowerNotifyThreshold, m_lowPowerNotifyThreshold,
                 lowPowerNotifyThresholdChanged, "LowPowerNotifyThreshold")
SET_CONFIG_VALUE(qint64, ActionPercentage, m_percentageAction,
                 actionPercentageChanged, "ActionPercentage")

#undef SET_CONFIG_VALUE

void LowPowerManager::initConfig(Dtk::Core::DConfig *config)
{
    m_config = config;
    if (!m_config)
        return;

    m_usePercentageForPolicy = m_config->value(kUsePercentageForPolicy, true).toBool();
    m_lowPowerNotifyThreshold = m_config->value(kLowPowerNotifyThreshold, 0).toInt();
    m_percentageAction = m_config->value(kPercentageAction, 0).toInt();
    m_timeToEmptyLow = static_cast<quint64>(m_config->value(kTimeToEmptyLow, 0).toLongLong());
    m_timeToEmptyDanger = static_cast<quint64>(m_config->value(kTimeToEmptyDanger, 0).toLongLong());
    m_timeToEmptyCritical = static_cast<quint64>(m_config->value(kTimeToEmptyCritical, 0).toLongLong());
    m_timeToEmptyAction = static_cast<quint64>(m_config->value(kTimeToEmptyAction, 0).toLongLong());
}

void LowPowerManager::applyConfigValue(const QString &key, const QVariant &value)
{
    if (key == QLatin1String(kUsePercentageForPolicy))
        setUsePercentageForPolicy(value.toBool());
    else if (key == QLatin1String(kTimeToEmptyLow))
        setLowTime(value.toLongLong());
    else if (key == QLatin1String(kTimeToEmptyDanger))
        setDangerTime(value.toLongLong());
    else if (key == QLatin1String(kTimeToEmptyCritical))
        setCriticalTime(value.toLongLong());
    else if (key == QLatin1String(kTimeToEmptyAction))
        setActionTime(value.toLongLong());
    else
        return;

    scheduleValidation();
}

uint LowPowerManager::getWarnLevel(double percentage, quint64 timeToEmpty)
{
    if (!m_powerManager || !m_powerManager->onBattery())
        return None;

    if (m_usePercentageForPolicy) {
        if (percentage == 0.0)
            return None;

        if (percentage <= m_lowPowerNotifyThreshold) {
            if (m_percentageAction > 0 && percentage <= m_percentageAction)
                return Action;
            if (percentage <= 10.0)
                return Critical;
            if (percentage <= 15.0)
                return Danger;
            if (percentage <= 20.0)
                return Low;
            if (percentage <= 25.0)
                return Remind;
            return None;
        }

        return None;
    } else {
        if (timeToEmpty > m_timeToEmptyLow || timeToEmpty == 0)
            return None;
        if (timeToEmpty > m_timeToEmptyDanger)
            return Low;
        if (timeToEmpty > m_timeToEmptyCritical)
            return Danger;
        if (timeToEmpty > m_timeToEmptyAction)
            return Critical;
        return Action;
    }
}

void LowPowerManager::updateWarnLevel()
{
    if (!m_powerManager || !m_powerManager->onBattery()) {
        disableTicker();
        if (m_currentLevel != None) {
            m_currentLevel = None;
            handleLevelChanged(None);
        }
        return;
    }

    double pct = 100.0;
    const auto batteries = m_powerManager->batteryPercentage();
    if (!batteries.isEmpty())
        pct = batteries.first();

    // Some firmware transiently reports 0%. Keep an already-active warning
    // until a credible percentage or AC state arrives.
    if (m_usePercentageForPolicy && pct == 0.0 && m_currentLevel != None)
        return;

    const uint newLevel = getWarnLevel(pct, m_powerManager->batteryTimeToEmpty());
    if (newLevel == m_currentLevel)
        return;

    m_currentLevel = newLevel;
    handleLevelChanged(newLevel);
}

void LowPowerManager::handleLevelChanged(uint level)
{
    qDebug(logPowerSession) << "Battery level changed: " << level;
    disableTicker();
    if (m_powerManager && m_powerManager->m_warnLevel != level) {
        m_powerManager->m_warnLevel = level;
        Q_EMIT m_powerManager->warnLevelChanged();
    }

    switch (level) {
    case Action: {
        if (m_powerManager && m_powerManager->scheduledShutdownState())
            m_powerManager->scheduledShutdown(SchedInit);  // matches Go: m.scheduledShutdown(Init)
        playBatterySound();
        sendNotify(tr("Battery critically low"));
        startCountTicker();
        break;
    }
    case Critical:
    case Danger:
    case Low:
    case Remind:
        playBatterySound();
        sendNotify(tr("Battery low, please plug in"));
        break;
    case None:
        closeLowPower();
        if (m_powerManager && m_powerManager->scheduledShutdownState())
            m_powerManager->scheduledShutdown(SchedInit);
        break;
    }
}

void LowPowerManager::startCountTicker()
{
    m_count = 0;
    m_countTicker->start();
}

void LowPowerManager::lockWaitShow(int timeoutMs, bool autoStartAuth)
{
    if (!m_powerManager) return;

    m_powerManager->doLock(autoStartAuth);

    auto *pollTimer = new QTimer(this);
    auto *endTimer = new QTimer(this);
    endTimer->setSingleShot(true);

    connect(pollTimer, &QTimer::timeout, this, [pollTimer, endTimer]() {
        QDBusInterface sm(PowerDBus::kSessionManager, PowerDBus::kSessionPath,
                          PowerDBus::kSessionManager, QDBusConnection::sessionBus());
        if (sm.isValid() && sm.property("Locked").toBool()) {
            pollTimer->stop();
            endTimer->stop();
            pollTimer->deleteLater();
            endTimer->deleteLater();
        }
    });

    connect(endTimer, &QTimer::timeout, this, [pollTimer, endTimer]() {
        pollTimer->stop();
        pollTimer->deleteLater();
        endTimer->deleteLater();
    });

    pollTimer->start(300);
    endTimer->start(timeoutMs);
}

void LowPowerManager::playBatterySound()
{
    if (!QProcess::startDetached("paplay", {"/usr/share/sounds/deepin/stereo/battery-low.ogg"}))
        qWarning(logPowerSession) << "Failed to play battery sound";
}

void LowPowerManager::disableTicker()
{
    m_countTicker->stop();
}

void LowPowerManager::sendNotify(const QString &body)
{
    m_powerManager->sendNotify("", body);
}

void LowPowerManager::showLowPower()
{
    if (!QProcess::startDetached(kLowPowerCmd, {"--raise"}))
        qWarning(logPowerSession) << "Failed to start dde-lowpower --raise";
}

void LowPowerManager::closeLowPower()
{
    if (!QProcess::startDetached(kLowPowerCmd, {"--quit"}))
        qWarning(logPowerSession) << "Failed to start dde-lowpower --quit";
}

void LowPowerManager::scheduleValidation()
{
    m_validationTimer->start();
}

bool LowPowerManager::configValid() const
{
    // Legacy dde-daemon only accepted 1%-9% action thresholds; 10% is the
    // separate critical threshold, so equality would collapse two warning levels.
    return m_timeToEmptyLow > m_timeToEmptyDanger
        && m_timeToEmptyDanger > m_timeToEmptyCritical
        && m_timeToEmptyCritical > m_timeToEmptyAction
        && m_percentageAction < 10;
}

void LowPowerManager::Reset()
{
    if (!m_config)
        return;
    static const char *keys[] = {
        kUsePercentageForPolicy,
        kLowPowerNotifyThreshold,
        kPercentageAction,
        kTimeToEmptyLow,
        kTimeToEmptyDanger,
        kTimeToEmptyCritical,
        kTimeToEmptyAction,
    };
    for (const char *key : keys)
        m_config->reset(QLatin1String(key));
}

void LowPowerManager::notifyPropertyChanged(const char *name, const QVariant &value)
{
    if (!m_powerManager || !m_powerManager->m_conn)
        return;
    QDBusMessage message = QDBusMessage::createSignal(
        PowerDBus::kPath, QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("PropertiesChanged"));
    message << QStringLiteral("org.deepin.dde.Power1.WarnLevelConfig")
            << QVariantMap{{QString::fromLatin1(name), value}}
            << QStringList();
    m_powerManager->m_conn->send(message);
}
