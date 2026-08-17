// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "powermanager.h"
#include "idle/idlewatcher.h"
#include "idle/idlewatcher_wl.h"
#include "screen/screencontroller.h"
#include "screen/screencontroller_wl.h"
#include "powersaveplan.h"
#include "lidswitchhandler.h"
#include "sleepinhibitor.h"
#include "sessiondbusproxy.h"
#include "../powerconstants.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusInterface>
#include <QDBusMetaType>
#include <QDBusConnectionInterface>
#include <QMetaProperty>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QThread>
#include <QTimer>
#include <QProcess>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDBusReply>
#include <QDBusArgument>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <qlogging.h>
#include <QLoggingCategory>

#include <unistd.h>
#include <cstdio>
#include <functional>
#include <limits>
#include <vector>

using namespace PowerDBus;
using namespace PowerDConfig;
using namespace PowerFS;
using namespace Dtk::Core;
using ObjectInterfaceMap = QMap<QString, QVariantMap>;
using ObjectMap = QMap<QDBusObjectPath, ObjectInterfaceMap>;

static QStringList desktopFileNames(QStringList applications)
{
    for (QString &application : applications)
        application = QFileInfo(application).fileName();
    return applications;
}

Q_DECLARE_METATYPE(ObjectInterfaceMap)
Q_DECLARE_METATYPE(ObjectMap)

Q_LOGGING_CATEGORY(logPowerSession, "dde.power.session")

#define DEF_SETTER_PERSIST(T, Suffix, member, signal, dkey) \
    void PowerManager::set##Suffix(T v) { \
        if (m_##member != v) { m_##member = v; Q_EMIT signal(); \
        persist(dkey, QVariant::fromValue(v)); } }

DEF_SETTER_PERSIST(int, LinePowerScreensaverDelay, linePowerScreensaverDelay, linePowerScreensaverDelayChanged, kLinePowerScreensaverDelay)
DEF_SETTER_PERSIST(int, LinePowerScreenBlackDelay, linePowerScreenBlackDelay, linePowerScreenBlackDelayChanged, kLinePowerScreenBlackDelay)
DEF_SETTER_PERSIST(int, LinePowerSleepDelay, linePowerSleepDelay, linePowerSleepDelayChanged, kLinePowerSleepDelay)
DEF_SETTER_PERSIST(int, LinePowerLockDelay, linePowerLockDelay, linePowerLockDelayChanged, kLinePowerLockDelay)
DEF_SETTER_PERSIST(int, BatteryScreensaverDelay, batteryScreensaverDelay, batteryScreensaverDelayChanged, kBatteryScreensaverDelay)
DEF_SETTER_PERSIST(int, LinePowerShortIdleDelay, linePowerShortIdleDelay, linePowerShortIdleDelayChanged, kLinePowerShortIdleDelay)
DEF_SETTER_PERSIST(int, BatteryScreenBlackDelay, batteryScreenBlackDelay, batteryScreenBlackDelayChanged, kBatteryScreenBlackDelay)
DEF_SETTER_PERSIST(int, BatterySleepDelay, batterySleepDelay, batterySleepDelayChanged, kBatterySleepDelay)
DEF_SETTER_PERSIST(int, BatteryLockDelay, batteryLockDelay, batteryLockDelayChanged, kBatteryLockDelay)
DEF_SETTER_PERSIST(bool, ScreenBlackLock, screenBlackLock, screenBlackLockChanged, kScreenBlackLock)
DEF_SETTER_PERSIST(int, BatteryShortIdleDelay, batteryShortIdleDelay, batteryShortIdleDelayChanged, kBatteryShortIdleDelay)
DEF_SETTER_PERSIST(bool, SleepLock, sleepLock, sleepLockChanged, kSleepLock)
DEF_SETTER_PERSIST(int, LinePowerLidClosedAction, linePowerLidClosedAction, linePowerLidClosedActionChanged, kLinePowerLidClosedAction)
DEF_SETTER_PERSIST(int, BatteryLidClosedAction, batteryLidClosedAction, batteryLidClosedActionChanged, kBatteryLidClosedAction)
DEF_SETTER_PERSIST(int, LinePowerPressPowerBtnAction, linePowerPressPowerBtnAction, linePowerPressPowerBtnActionChanged, kLinePowerPressPowerButton)
DEF_SETTER_PERSIST(int, BatteryPressPowerBtnAction, batteryPressPowerBtnAction, batteryPressPowerBtnActionChanged, kBatteryPressPowerButton)
DEF_SETTER_PERSIST(bool, LowPowerNotifyEnable, lowPowerNotifyEnable, lowPowerNotifyEnableChanged, kLowPowerNotifyEnable)
DEF_SETTER_PERSIST(int, LowPowerNotifyThreshold, lowPowerNotifyThreshold, lowPowerNotifyThresholdChanged, kLowPowerNotifyThreshold)
DEF_SETTER_PERSIST(int, LowPowerAutoSleepThreshold, lowPowerAutoSleepThreshold, lowPowerAutoSleepThresholdChanged, kPercentageAction)
DEF_SETTER_PERSIST(int, LowPowerAction, lowPowerAction, lowPowerActionChanged, kLowPowerAction)

PowerManager::PowerManager(QDBusConnection *conn, const QString &svc, QObject *parent)
    : QObject(parent), m_conn(conn)
{
    Q_UNUSED(svc);
    m_useWayland = (qEnvironmentVariable("XDG_SESSION_TYPE") == QLatin1String("wayland"));

    qRegisterMetaType<ObjectInterfaceMap>();
    qDBusRegisterMetaType<ObjectInterfaceMap>();
    qRegisterMetaType<ObjectMap>();
    qDBusRegisterMetaType<ObjectMap>();
    qRegisterMetaType<BatteryIsPresentMap>("BatteryIsPresentMap");
    qRegisterMetaType<BatteryPercentageMap>("BatteryPercentageMap");
    qRegisterMetaType<BatteryStateMap>("BatteryStateMap");

    qDBusRegisterMetaType<BatteryIsPresentMap>();
    qDBusRegisterMetaType<BatteryPercentageMap>();
    qDBusRegisterMetaType<BatteryStateMap>();

    QDBusMetaType::registerCustomType(QMetaType::fromType<BatteryIsPresentMap>(), "a{sb}");
    QDBusMetaType::registerCustomType(QMetaType::fromType<BatteryPercentageMap>(), "a{sd}");
    QDBusMetaType::registerCustomType(QMetaType::fromType<BatteryStateMap>(), "a{su}");
}

PowerManager::~PowerManager()
{
    if (m_inhibitFd >= 0)
        ::close(m_inhibitFd);
    if (m_sleepInhibitor)
        m_sleepInhibitor->unblock();
    if (m_configReader) {
        QMetaObject::invokeMethod(m_configReader, &QObject::deleteLater,
                                  Qt::BlockingQueuedConnection);
        m_configReader = nullptr;
    }
    if (m_conn)
        m_conn->unregisterObject(kPath);
}

// Session and system PowerManager intentionally keep separate persistence paths:
// they use different DConfig lifecycles and must not share state or guards.
void PowerManager::persist(const char *key, const QVariant &value)
{
    if (!m_config || m_applyingConfig)
        return;

    const QString configKey = QLatin1String(key);
    m_writingConfigKeys.insert(configKey);
    m_config->setValue(configKey, value);
    m_writingConfigKeys.remove(configKey);
}

void PowerManager::resetConfig(const char *key)
{
    if (!m_config)
        return;

    const QString configKey = QLatin1String(key);
    m_writingConfigKeys.insert(configKey);
    m_config->reset(configKey);
    m_writingConfigKeys.remove(configKey);
}

bool PowerManager::initialize()
{
    m_proxy = new SessionDBusProxy(this);
    m_idleWatcher = createIdleWatcher();
    m_screenCtrl = createScreenController();

    initDConfig();

    m_powerSavePlan = new PowerSavePlan(this);
    m_lidSwitch = new LidSwitchHandler(this);
    m_sleepInhibitor = new SleepInhibitor(this);
    m_lowPowerMgr = new LowPowerManager(this);
    m_lowPowerMgr->initConfig(m_config);
    initAmbientBrightness();

    if (m_idleWatcher) {
        connect(m_idleWatcher, &IdleWatcher::idled, m_powerSavePlan, &PowerSavePlan::HandleIdleOn);
        connect(m_idleWatcher, &IdleWatcher::resumed, m_powerSavePlan, &PowerSavePlan::HandleIdleOff);
    }
    connect(this, &PowerManager::onBatteryChanged, this,
            [this] { m_powerSavePlan->ResetFromNow(); });

    connect(this, &PowerManager::linePowerScreensaverDelayChanged, this, &PowerManager::onLinePowerDelayChanged);
    connect(this, &PowerManager::linePowerScreenBlackDelayChanged, this, &PowerManager::onLinePowerDelayChanged);
    connect(this, &PowerManager::linePowerLockDelayChanged, this, &PowerManager::onLinePowerDelayChanged);
    connect(this, &PowerManager::linePowerSleepDelayChanged, this, &PowerManager::onLinePowerDelayChanged);
    connect(this, &PowerManager::linePowerShortIdleDelayChanged, this, &PowerManager::onLinePowerDelayChanged);
    connect(this, &PowerManager::batteryScreensaverDelayChanged, this, &PowerManager::onBatteryDelayChanged);
    connect(this, &PowerManager::batteryScreenBlackDelayChanged, this, &PowerManager::onBatteryDelayChanged);
    connect(this, &PowerManager::batteryLockDelayChanged, this, &PowerManager::onBatteryDelayChanged);
    connect(this, &PowerManager::batterySleepDelayChanged, this, &PowerManager::onBatteryDelayChanged);
    connect(this, &PowerManager::batteryShortIdleDelayChanged, this, &PowerManager::onBatteryDelayChanged);

    initBatteryWatcher();
    initSleepWatcher();
    initLogindInhibit();
    initScheduledShutdown();

    m_powerSavePlan->Start();

    if (!m_conn->registerObject(kPath, this,
            QDBusConnection::ExportAllSlots |
            QDBusConnection::ExportAllSignals |
            QDBusConnection::ExportAllProperties |
            QDBusConnection::ExportAdaptors)) {
        qWarning(logPowerSession) << "Failed to register D-Bus object:" << m_conn->lastError().message();
        return false;
    }

    // ExportAllProperties 只提供 Get/Set/GetAll 访问，不会自动把 NOTIFY 信号
    // 转成 org.freedesktop.DBus.Properties.PropertiesChanged。
    // 用 notifyPropertyChanged 槽函数统一桥接所有 Q_PROPERTY(NOTIFY) 到标准 D-Bus 属性变更通知。
    const QMetaObject *mo = metaObject();
    const int slotIndex = mo->indexOfSlot("notifyPropertyChanged()");
    if (slotIndex >= 0) {
        const QMetaMethod slot = mo->method(slotIndex);
        for (int i = mo->propertyOffset(); i < mo->propertyCount(); ++i) {
            const QMetaProperty property = mo->property(i);
            if (property.hasNotifySignal())
                connect(this, property.notifySignal(), this, slot);
        }
    }

    return true;
}

void PowerManager::onLinePowerDelayChanged()
{
    if (!m_onBattery) {
        m_powerSavePlan->OnLinePower();
    }
}

void PowerManager::onBatteryDelayChanged()
{
    if (m_onBattery) {
        m_powerSavePlan->OnBattery();
    }
}

void PowerManager::initBatteryWatcher()
{
    connect(m_proxy, &SessionDBusProxy::OnBatteryChanged, this, &PowerManager::handleOnBatteryChanged);
    connect(m_proxy, &SessionDBusProxy::HasLidSwitchChanged, this, &PowerManager::handleHasLidSwitchChanged);
    connect(m_proxy, &SessionDBusProxy::HasBatteryChanged, this, &PowerManager::handleHasBatteryChanged);
    connect(m_proxy, &SessionDBusProxy::BatteryPercentageChanged, this, &PowerManager::handleBatteryPercentageChanged);
    connect(m_proxy, &SessionDBusProxy::BatteryStatusChanged, this, &PowerManager::handleBatteryStatusChanged);
    connect(m_proxy, &SessionDBusProxy::BatteryTimeToEmptyChanged, this, &PowerManager::handleBatteryTimeToEmptyChanged);
    connect(m_proxy, &SessionDBusProxy::IsHighPerformanceSupportedChanged, this, &PowerManager::handleIsHighPerformanceSupportedChanged);
    connect(m_proxy, &SessionDBusProxy::PowerSavingModeEnabledChanged, this, &PowerManager::handlePowerSavingModeEnabledChanged);
    connect(m_proxy, &SessionDBusProxy::PowerSavingModeBrightnessDropPercentChanged, this, &PowerManager::handlePowerSavingModeBrightnessDropPercentChanged);

    const auto syncState = [this] {
        refreshBatteryInfo();
        if (m_powerSavePlan)
            m_powerSavePlan->syncPowerSavingMode(
                m_proxy->powerSavingModeEnabled(),
                m_proxy->powerSavingModeBrightnessDropPercent());
    };
    syncState();
    QTimer::singleShot(1000, this, syncState);
}

void PowerManager::initSleepWatcher()
{
    if (m_sleepInhibitor) {
        connect(m_sleepInhibitor, &SleepInhibitor::aboutToSleep,
                this, [this]() { handleBeforeSleep(true); });
        connect(m_sleepInhibitor, &SleepInhibitor::wokeUp,
                this, &PowerManager::handleWakeup);
    }
}

void PowerManager::refreshBatteryInfo()
{
    const bool hasBattery = m_proxy->hasBattery();
    if (hasBattery) {
        m_batteryIsPresent[QStringLiteral("Display")] = true;
        m_batteryPercentage[QStringLiteral("Display")] = m_proxy->batteryPercentage();
        m_batteryState[QStringLiteral("Display")] = m_proxy->batteryStatus();
    } else {
        m_batteryIsPresent.remove(QStringLiteral("Display"));
        m_batteryPercentage.remove(QStringLiteral("Display"));
        m_batteryState.remove(QStringLiteral("Display"));
    }
    Q_EMIT batteryIsPresentChanged();
    Q_EMIT batteryPercentageChanged();
    Q_EMIT batteryStateChanged();

    const quint64 timeToEmpty = hasBattery ? m_proxy->batteryTimeToEmpty() : 0;
    if (m_batteryTimeToEmpty != timeToEmpty) {
        m_batteryTimeToEmpty = timeToEmpty;
        Q_EMIT batteryTimeToEmptyChanged();
    }

    const bool onBattery = m_proxy->onBattery();
    if (onBattery != m_onBattery) {
        m_onBattery = onBattery;
        Q_EMIT onBatteryChanged();
    }

    const bool hasLid = m_proxy->hasLidSwitch();
    if (hasLid != m_lidIsPresent) {
        m_lidIsPresent = hasLid;
        Q_EMIT lidIsPresentChanged();
    }

    bool hps = m_proxy->isHighPerformanceSupported();
    if (m_config)
        hps = hps && m_config->value(PowerDConfig::kHighPerformanceEnabled).toBool();
    if (hps != m_isHighPerformanceSupported) {
        m_isHighPerformanceSupported = hps;
        Q_EMIT isHighPerformanceSupportedChanged();
    }
}

void PowerManager::handleOnBatteryChanged(bool value)
{
    if (value != m_onBattery) {
        m_onBattery = value;
        Q_EMIT onBatteryChanged();
    }
}

void PowerManager::handleHasLidSwitchChanged(bool value)
{
    if (value != m_lidIsPresent) {
        m_lidIsPresent = value;
        Q_EMIT lidIsPresentChanged();
    }
}

void PowerManager::handleHasBatteryChanged(bool value)
{
    if (value) {
        m_batteryIsPresent[QStringLiteral("Display")] = true;
    } else {
        m_batteryIsPresent.remove(QStringLiteral("Display"));
        m_batteryPercentage.remove(QStringLiteral("Display"));
        m_batteryState.remove(QStringLiteral("Display"));
        if (m_batteryTimeToEmpty != 0) {
            m_batteryTimeToEmpty = 0;
            Q_EMIT batteryTimeToEmptyChanged();
        }
        Q_EMIT batteryPercentageChanged();
        Q_EMIT batteryStateChanged();
    }
    Q_EMIT batteryIsPresentChanged();
}

void PowerManager::handleBatteryPercentageChanged(double value)
{
    if (!m_batteryIsPresent.contains(QStringLiteral("Display")))
        return;
    m_batteryPercentage[QStringLiteral("Display")] = value;
    Q_EMIT batteryPercentageChanged();
}

void PowerManager::handleBatteryStatusChanged(uint value)
{
    if (!m_batteryIsPresent.contains(QStringLiteral("Display")))
        return;
    m_batteryState[QStringLiteral("Display")] = value;
    Q_EMIT batteryStateChanged();
}

void PowerManager::handleBatteryTimeToEmptyChanged(quint64 value)
{
    if (!m_batteryIsPresent.contains(QStringLiteral("Display"))
        || m_batteryTimeToEmpty == value)
        return;
    m_batteryTimeToEmpty = value;
    Q_EMIT batteryTimeToEmptyChanged();
}

void PowerManager::handleIsHighPerformanceSupportedChanged(bool value)
{
    if (m_config) {
        bool e = m_config->value(PowerDConfig::kHighPerformanceEnabled).toBool();
        value = value && e;
    }
    if (value != m_isHighPerformanceSupported) {
        m_isHighPerformanceSupported = value;
        Q_EMIT isHighPerformanceSupportedChanged();
    }
}

void PowerManager::handlePowerSavingModeEnabledChanged(bool enabled)
{
    if (m_powerSavePlan)
        m_powerSavePlan->onPowerSavingModeEnabledChanged(enabled);
}

void PowerManager::handlePowerSavingModeBrightnessDropPercentChanged(uint value)
{
    if (m_powerSavePlan)
        m_powerSavePlan->onBrightnessDropPercentChanged(value);
}

void PowerManager::initAmbientBrightness()
{
    m_hasAmbientLightSensor = m_proxy->ambientBrightnessSupported();
    connect(m_proxy, &SessionDBusProxy::ambientBrightnessPropertiesChanged, this,
            [this](const QString &interface, const QVariantMap &changed,
                   const QStringList &invalidated) {
        if (interface != QLatin1String("org.deepin.dde.AmbientBrightness1")
            || (!changed.contains(QStringLiteral("Supported"))
                && !invalidated.contains(QStringLiteral("Supported")))) {
            return;
        }

        const bool supported = changed.contains(QStringLiteral("Supported"))
            ? changed.value(QStringLiteral("Supported")).toBool()
            : m_proxy->ambientBrightnessSupported();
        if (m_hasAmbientLightSensor == supported)
            return;
        m_hasAmbientLightSensor = supported;
        Q_EMIT hasAmbientLightSensorChanged();
    });
}

bool PowerManager::ambientLightAdjustBrightness() const
{
    return m_proxy && m_hasAmbientLightSensor && m_proxy->ambientBrightnessEnabled();
}

void PowerManager::initLogindInhibit()
{
    auto fd = m_proxy->inhibit(
        "handle-power-key:handle-lid-switch:handle-suspend-key",
        PowerDBus::kService, "handling key press and lid switch close", "block");
    m_inhibitFd = fd.isValid() ? dup(fd.fileDescriptor()) : -1;

    connect(m_proxy, &SessionDBusProxy::login1OwnerChanged,
            this, &PowerManager::onLogin1OwnerChanged, Qt::UniqueConnection);
}

void PowerManager::onLogin1OwnerChanged(const QString &name, const QString &,
                                         const QString &newOwner)
{
    if (name != QLatin1String(PowerDBus::kLogin1Service) || newOwner.isEmpty()) return;
    if (m_inhibitFd >= 0) { ::close(m_inhibitFd); m_inhibitFd = -1; }
    initLogindInhibit();
}

void PowerManager::notifyPropertyChanged()
{
    int sigIdx = senderSignalIndex();
    if (sigIdx < 0)
        return;

    const QMetaObject *mo = metaObject();
    for (int i = mo->propertyOffset(); i < mo->propertyCount(); ++i) {
        QMetaProperty prop = mo->property(i);
        if (!prop.hasNotifySignal() || !prop.isReadable())
            continue;
        if (prop.notifySignal().methodIndex() != sigIdx)
            continue;

        QDBusMessage msg = QDBusMessage::createSignal(
            kPath,
            QStringLiteral("org.freedesktop.DBus.Properties"),
            QStringLiteral("PropertiesChanged"));
        msg << QLatin1String(kInterface);
        QVariantMap changed;
        changed[QString::fromLatin1(prop.name())] = prop.read(this);
        msg << changed;
        msg << QStringList();
        m_conn->send(msg);
        return;
    }
}

static QString currentSessionId()
{
    QString sid = qEnvironmentVariable("XDG_SESSION_ID");
    if (sid.isEmpty()) sid = QString::number(getpid());
    return sid;
}

static void setAudioSuspended(bool suspended)
{
    // Suspend handling must wait for audio to quiesce before releasing the inhibitor;
    // the legacy daemon deliberately performed these commands synchronously too.
    const QString script = QStringLiteral("/usr/libexec/deepin/os-config/pipewire-suspend.sh");
    const QString state = suspended ? QStringLiteral("1") : QStringLiteral("0");
    if (QFile::exists(script) && QProcess::execute(script, {state}) == 0)
        return;

    const QStringList types = {QStringLiteral("sinks"), QStringLiteral("sources")};
    for (const QString &type : types) {
        QProcess list;
        list.start(QStringLiteral("pactl"), {QStringLiteral("list"), QStringLiteral("short"), type});
        if (!list.waitForFinished(5000) || list.exitCode() != 0)
            continue;
        const QString command = type == QLatin1String("sinks")
            ? QStringLiteral("suspend-sink") : QStringLiteral("suspend-source");
        for (const QByteArray &line : list.readAllStandardOutput().split('\n')) {
            const QString id = QString::fromUtf8(line).section(QLatin1Char('\t'), 0, 0);
            if (!id.isEmpty())
                QProcess::execute(QStringLiteral("pactl"), {command, id, state});
        }
    }
}

void PowerManager::handleBeforeSleep(bool)
{
    if (!isSessionActive()) {
        m_sleepCycleHandled = false;
        return;
    }
    m_sleepCycleHandled = true;
    m_prepareSuspendState = PS_Prepare;
    if (!m_screensaverStateCaptured) {
        m_screensaverLockAtAwake = screensaverProperty("lockScreenAtAwake");
        m_screensaverStateCaptured = true;
    }
    setBlackScreenActive(true);
    if (m_useWayland && m_sleepLock)
        doLock(false);
    setAudioSuspended(true);
}

void PowerManager::handleWakeup()
{
    if (!m_sleepCycleHandled)
        return;
    setAudioSuspended(false);
    m_sleepCycleHandled = false;
    m_prepareSuspendState = PS_Resume;
    m_screensaverStateCaptured = false;
    if (m_useWayland && m_idleWatcher)
        m_idleWatcher->simulateActivity();
    if (m_scheduledShutdownState)
        scheduledShutdown(SchedInit);

    m_delayInActive = true;
    QTimer::singleShot(m_delayWakeupInterval * 1000, this, [this] {
        m_delayInActive = false;
        m_prepareSuspendState = PS_Finish;
        if (m_screensaverWasRunning && m_screensaverLockAtAwake)
            doLock(true);
        m_screensaverWasRunning = false;
        setBlackScreenActive(false);
    });
    if (m_powerSavePlan)
        m_powerSavePlan->HandleIdleOff();
    setDPMSModeOn();
    m_proxy->refreshBrightness();
    m_proxy->refreshMains();
    m_proxy->refreshBatteries();
}
bool PowerManager::screensaverProperty(const char *name) const
{
    QDBusInterface screensaver(QStringLiteral("com.deepin.ScreenSaver"),
                               QStringLiteral("/com/deepin/ScreenSaver"),
                               QStringLiteral("com.deepin.ScreenSaver"),
                               QDBusConnection::sessionBus());
    return screensaver.property(name).toBool();
}


void PowerManager::Reset()
{
    if (!m_config)
        return;

    struct ResetItem {
        const char *key;
        const char *property;
    };
    static const ResetItem items[] = {
        { kLinePowerScreenBlackDelay, "LinePowerScreenBlackDelay" },
        { kLinePowerSleepDelay, "LinePowerSleepDelay" },
        { kLinePowerLockDelay, "LinePowerLockDelay" },
        { kLinePowerLidClosedAction, "LinePowerLidClosedAction" },
        { kLinePowerPressPowerButton, "LinePowerPressPowerBtnAction" },
        { kBatteryScreenBlackDelay, "BatteryScreenBlackDelay" },
        { kBatterySleepDelay, "BatterySleepDelay" },
        { kBatteryLockDelay, "BatteryLockDelay" },
        { kBatteryLidClosedAction, "BatteryLidClosedAction" },
        { kBatteryPressPowerButton, "BatteryPressPowerBtnAction" },
        { kScreenBlackLock, "ScreenBlackLock" },
        { kSleepLock, "SleepLock" },
        { kPowerButtonPressedExec, nullptr },
        { kLowPowerNotifyEnable, "LowPowerNotifyEnable" },
        { kLowPowerNotifyThreshold, "LowPowerNotifyThreshold" },
        { kPercentageAction, "LowPowerAutoSleepThreshold" },
        { kPowerSavingModeBrightnessDropPercent, nullptr },
        { kLinePowerShortIdleDelay, "LinePowerShortIdleDelay" },
        { kBatteryShortIdleDelay, "BatteryShortIdleDelay" },
    };

    m_applyingConfig = true;
    for (const auto &item : items) {
        resetConfig(item.key);
        if (item.property)
            setProperty(item.property, m_config->value(QLatin1String(item.key)));
    }
    m_applyingConfig = false;
    if (m_powerSavePlan)
        m_powerSavePlan->Reset();
}

void PowerManager::SetPrepareSuspend(int state)
{
    m_prepareSuspendState = static_cast<PrepareSuspendState>(state);
}

void PowerManager::TurnOffScreen()
{
    doTurnOffScreen();
}

void PowerManager::TurnOnScreen()
{
    setDPMSModeOn();
}

bool PowerManager::shouldIgnoreIdleOn() const
{
    return m_prepareSuspendState > PS_Finish;
}

bool PowerManager::shouldIgnoreIdleOff() const
{
    return m_prepareSuspendState >= PS_Prepare;
}

bool PowerManager::isSessionActive() const
{
    return m_proxy && m_proxy->sessionActive();
}

bool PowerManager::shouldPreventIdle() const
{
    // This runs only for an IdleOn event. A short-lived connection avoids retaining a
    // stale X11 handle across server restarts.
    if (m_useWayland || m_fullscreenWorkaroundApplications.isEmpty())
        return false;
    Display *display = XOpenDisplay(nullptr);
    if (!display)
        return false;
    const Window root = DefaultRootWindow(display);
    const Atom activeAtom = XInternAtom(display, "_NET_ACTIVE_WINDOW", True);
    const Atom stateAtom = XInternAtom(display, "_NET_WM_STATE", True);
    const Atom fullscreenAtom = XInternAtom(display, "_NET_WM_STATE_FULLSCREEN", True);
    const Atom pidAtom = XInternAtom(display, "_NET_WM_PID", True);
    auto property = [display](Window window, Atom atom, Atom type, unsigned long length,
                              unsigned char **data, unsigned long *count) {
        Atom actualType = None;
        int format = 0;
        unsigned long remaining = 0;
        return atom != None && XGetWindowProperty(display, window, atom, 0, length, False, type,
                                                   &actualType, &format, count, &remaining, data) == Success;
    };

    unsigned char *data = nullptr;
    unsigned long count = 0;
    Window active = None;
    if (property(root, activeAtom, XA_WINDOW, 1, &data, &count) && data && count)
        active = *reinterpret_cast<Window *>(data);
    if (data) XFree(data);
    data = nullptr;
    if (active == None || !property(active, stateAtom, XA_ATOM, 64, &data, &count)) {
        XCloseDisplay(display);
        return false;
    }
    bool fullscreen = false;
    const Atom *states = reinterpret_cast<Atom *>(data);
    for (unsigned long i = 0; i < count; ++i)
        fullscreen |= states[i] == fullscreenAtom;
    if (data) XFree(data);
    data = nullptr;
    unsigned long pid = 0;
    if (fullscreen && property(active, pidAtom, XA_CARDINAL, 1, &data, &count) && data && count)
        pid = *reinterpret_cast<unsigned long *>(data);
    if (data) XFree(data);
    XCloseDisplay(display);
    if (!pid)
        return false;
    if (QFileInfo(QStringLiteral("/proc/%1").arg(pid)).ownerId() != ::geteuid())
        return false;
    QFile commandLine(QStringLiteral("/proc/%1/cmdline").arg(pid));
    if (!commandLine.open(QIODevice::ReadOnly))
        return false;
    const QString command = QString::fromLocal8Bit(commandLine.readAll());
    return std::any_of(m_fullscreenWorkaroundApplications.cbegin(),
                       m_fullscreenWorkaroundApplications.cend(),
                       [&command](const QString &app) { return command.contains(app); });
}

bool PowerManager::canEnterShortIdle() const
{
    // This runs once when entering short idle, not as a polling loop. Fresh application
    // and service snapshots are intentional; both synchronous calls are capped at 1 s.
    if (m_useWayland)
        return true;

    QDBusInterface applications(QStringLiteral("org.desktopspec.ApplicationManager1"),
                                QStringLiteral("/org/desktopspec/ApplicationManager1"),
                                QStringLiteral("org.desktopspec.DBus.ObjectManager"),
                                QDBusConnection::sessionBus());
    applications.setTimeout(1000);

    const QDBusReply<ObjectMap> managed = applications.call(QStringLiteral("GetManagedObjects"));
    if (!managed.isValid())
        qWarning(logPowerSession) << "Failed to list launched applications:"
                                  << managed.error().message();
    const ObjectMap managedApplications = managed.isValid() ? managed.value() : ObjectMap();
    for (const ObjectInterfaceMap &interfaces : managedApplications) {
        const QVariantMap properties = interfaces.value(
            QStringLiteral("org.desktopspec.ApplicationManager1.Application"));
        if (properties.isEmpty()
            || qdbus_cast<QList<QDBusObjectPath>>(
                   properties.value(QStringLiteral("Instances"))).isEmpty())
            continue;
        const QString desktop = QFileInfo(
            properties.value(QStringLiteral("DesktopSourcePath")).toString()).fileName();
        if (m_shortIdleBlacklistApplications.contains(desktop)) {
            qInfo(logPowerSession) << "Short idle blocked by blacklisted application:" << desktop;
            return false;
        }
        const QString lower = desktop.toLower();
        if (!m_systemApplications.contains(desktop)
            && !lower.contains(QStringLiteral("deepin"))
            && !lower.contains(QStringLiteral("dde"))
            && !lower.contains(QStringLiteral("uos"))) {
            qInfo(logPowerSession) << "Short idle blocked by third-party application:" << desktop;
            return false;
        }
    }

    QDBusInterface systemd(QStringLiteral("org.freedesktop.systemd1"),
                           QStringLiteral("/org/freedesktop/systemd1"),
                           QStringLiteral("org.freedesktop.systemd1.Manager"),
                           QDBusConnection::systemBus());
    systemd.setTimeout(1000);
    const QDBusMessage reply = systemd.call(
        QStringLiteral("ListUnitsByPatterns"),
        QStringList{QStringLiteral("running")},
        QStringList{QStringLiteral("*.service")});
    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty()
        || !reply.arguments().constFirst().canConvert<QDBusArgument>()) {
        qWarning(logPowerSession) << "Failed to list running services; preventing short idle:"
                                  << reply.errorMessage();
        return false;
    }

    const QDBusArgument units = reply.arguments().constFirst().value<QDBusArgument>();
    bool allowed = true;
    units.beginArray();
    while (!units.atEnd()) {
        QString service, description, loadState, activeState, subState, following, jobType;
        QDBusObjectPath unitPath, jobPath;
        quint32 jobId = 0;
        units.beginStructure();
        units >> service >> description >> loadState >> activeState >> subState >> following
              >> unitPath >> jobId >> jobType >> jobPath;
        units.endStructure();

        const QString lower = service.toLower();
        if (allowed && !m_systemServices.contains(service)
            && !lower.startsWith(QStringLiteral("dde-"))
            && !lower.startsWith(QStringLiteral("deepin-"))
            && !lower.startsWith(QStringLiteral("uos-"))
            && !lower.startsWith(QStringLiteral("org.deepin."))
            && !lower.startsWith(QStringLiteral("com.deepin."))
            && !lower.startsWith(QStringLiteral("user@"))) {
            qInfo(logPowerSession) << "Short idle blocked by third-party service:" << service;
            allowed = false;
        }
    }
    units.endArray();
    return allowed;
}

void PowerManager::setShortIdleState(bool state)
{
    if (m_shortIdleEnabled && m_proxy)
        m_proxy->setShortIdleState(state);
}

void PowerManager::setKernelIdleState(bool state)
{
    if (m_proxy)
        m_proxy->setIdleState(state);
}

void PowerManager::setScreenIdleState(bool state)
{
    if (!m_useWayland && m_proxy)
        m_proxy->setScreenState(state);
}

bool PowerManager::isInConfigOrPowerButtonInhibitors(const QString &whatOnly,
                                                      QString &who,
                                                      bool blockOnly) const
{
    if (!m_proxy)
        return false;
    const QDBusMessage reply = m_proxy->inhibitors();
    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty())
        return false;
    const QDBusArgument array = reply.arguments().constFirst().value<QDBusArgument>();
    bool found = false;
    array.beginArray();
    while (!array.atEnd()) {
        QString what;
        QString inhibitingWho;
        QString why;
        QString mode;
        uint uid = 0;
        uint pid = 0;
        array.beginStructure();
        array >> what >> inhibitingWho >> why >> mode >> uid >> pid;
        array.endStructure();
        Q_UNUSED(why)
        Q_UNUSED(uid)
        Q_UNUSED(pid)
        if (!found
            && (!blockOnly || mode == QLatin1String("block"))
            && what.split(QLatin1Char(':')).contains(whatOnly)) {
            who = inhibitingWho;
            found = true;
        }
    }
    array.endArray();
    return found;
}

bool PowerManager::hasMultipleDisplaySessions() const
{
    if (!m_proxy)
        return false;
    const QDBusMessage reply = m_proxy->listSessions();
    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty())
        return false;
    const QDBusArgument array = reply.arguments().constFirst().value<QDBusArgument>();
    int displaySessions = 0;
    array.beginArray();
    while (!array.atEnd()) {
        QString id;
        uint uid = 0;
        QString user;
        QString seat;
        QDBusObjectPath path;
        array.beginStructure();
        array >> id >> uid >> user >> seat >> path;
        array.endStructure();
        Q_UNUSED(id)
        Q_UNUSED(uid)
        Q_UNUSED(user)
        Q_UNUSED(seat)
        QDBusInterface session(QLatin1String(PowerDBus::kLogin1Service), path.path(),
                               QLatin1String(PowerDBus::kLogin1Session),
                               QDBusConnection::systemBus());
        if (session.property("Type").toString() != QLatin1String("tty")
            && session.property("State").toString() != QLatin1String("closing"))
            ++displaySessions;
    }
    array.endArray();
    return displaySessions > 1;
}

void PowerManager::doSuspend()
{
    qInfo(logPowerSession) << "Requesting suspend, canSuspend=" << canSuspend();
    if (!canSuspend())
        return;
    QString who;
    if (isInConfigOrPowerButtonInhibitors(QStringLiteral("suspend"), who)) {
        qInfo(logPowerSession) << "Suspend blocked by" << who;
        doLock(false);
        return;
    }
    const auto suspend = [this] {
        if (m_useWayland)
            m_proxy->requestSuspend();
        else
            m_proxy->requestSuspendByFront();
    };
    if (hasMultipleDisplaySessions()) {
        doLock(false);
        QTimer::singleShot(3000, this, suspend);
    } else {
        suspend();
    }
}

void PowerManager::doSuspendByFront()
{
    if (!canSuspend())
        return;
    m_proxy->requestSuspendByFront();
}
void PowerManager::doShutdown()
{
    QString who;
    if (isInConfigOrPowerButtonInhibitors(QStringLiteral("shutdown"), who, false)
        || hasMultipleDisplaySessions())
        m_proxy->requestShutdownByFront();
    else
        m_proxy->requestShutdown();
}

void PowerManager::doHibernate()
{
    if (!canHibernate())
        return;
    QString who;
    if (isInConfigOrPowerButtonInhibitors(QStringLiteral("hibernate"), who)) {
        qInfo(logPowerSession) << "Hibernate blocked by" << who;
        doLock(false);
        return;
    }
    if (m_useWayland)
        m_proxy->requestHibernate();
    else
        m_proxy->requestHibernateByFront();
}

void PowerManager::doTurnOffScreen()
{
    qInfo(logPowerSession) << "Turning off screen";
    setDPMSModeOff();
    if (m_screenBlackLock)
        doLock(true);
}

void PowerManager::doLock(bool autoStartAuth)
{
    qInfo(logPowerSession) << "Locking session";
    if (m_useWayland) { 
        m_proxy->lockSession(currentSessionId()); 
        return; 
    }
    if (m_proxy->sessionLocked()) {
        qInfo(logPowerSession) << "Session is already locked";
        return;
    }
    m_proxy->showLockAuth(autoStartAuth);
}
void PowerManager::setDPMSModeOn()
{
    qInfo(logPowerSession) << "Setting DPMS mode to on";
    if (m_screenCtrl && m_screenCtrl->isValid()) {
        m_screenCtrl->setAllModes(ScreenController::On);
        setScreenIdleState(false);
    }
}

void PowerManager::setDPMSModeOff()
{
    qInfo(logPowerSession) << "Setting DPMS mode to off";
    if (m_screenCtrl && m_screenCtrl->isValid()) {
        m_screenCtrl->setAllModes(ScreenController::Off);
        setScreenIdleState(true);
    }
}

void PowerManager::setBlackScreenActive(bool active)
{
    if (m_delayInActive || !m_sleepLock) {
        return;
    }

    m_proxy->setBlackScreenActive(active);
}

bool PowerManager::canSuspend() const { 
    return m_proxy->canSuspend(); 
}

bool PowerManager::canHibernate() const {
    return m_proxy->canHibernate();
}

void PowerManager::sendNotify(const QString &s, const QString &b)
{
    if (!m_lowPowerNotifyEnable) return;
    m_proxy->notify(0, "dde-control-center", "notification-battery-low",
                    s, b, QStringList(), QVariantMap(), -1);
}

QMap<QString, double> PowerManager::displayBrightness() const
{
    return m_proxy ? m_proxy->brightness() : QMap<QString, double>();
}

void PowerManager::setDisplayBrightness(const QMap<QString, double> &t)
{
    for (auto it = t.begin(); it != t.end(); ++it) {
        m_proxy->setBrightness(it.key(), it.value());
    }
}

void PowerManager::setAndSaveDisplayBrightness(const QMap<QString, double> &t)
{
    for (auto it = t.begin(); it != t.end(); ++it) {
        m_proxy->setAndSaveBrightness(it.key(), it.value());
    }
}

IdleWatcher *PowerManager::createIdleWatcher()
{
    return m_useWayland ? static_cast<IdleWatcher *>(new WaylandIdleWatcher(this))
                        : createX11IdleWatcher(this);
}

ScreenController *PowerManager::createScreenController()
{
    return m_useWayland ? static_cast<ScreenController *>(new WaylandScreenController(this))
                        : createX11ScreenController(this);
}

void PowerManager::recalculateScheduledShutdown()
{
    qDebug(logPowerSession) << "Recalculating scheduled shutdown, current nextShutdownTime=" << m_nextShutdownTime;
    m_nextShutdownTime = getNextShutdownTime(0);

    persist(PowerDConfig::kNextShutdownTime, m_nextShutdownTime);
    scheduledShutdown(SchedInit);
}

void PowerManager::initDConfig()
{
    m_config = DConfig::create(PowerDConfig::kAppId, PowerDConfig::kPowerName, "", this);
    if (!m_config)
        return;

    m_configReader = DConfig::create(PowerDConfig::kAppId, PowerDConfig::kPowerName);
    if (m_configReader)
        m_configReader->moveToThread(DConfig::globalThread());

    using Setter = std::function<void(const QVariant &)>;
    struct Binding { const char *key; Setter apply; };

    std::vector<Binding> bindings = {
        // ── Line power delays ──
        { PowerDConfig::kLinePowerScreensaverDelay,
          [this](const QVariant &v) { setLinePowerScreensaverDelay(v.toInt()); } },
        { PowerDConfig::kLinePowerScreenBlackDelay,
          [this](const QVariant &v) { setLinePowerScreenBlackDelay(v.toInt()); } },
        { PowerDConfig::kLinePowerSleepDelay,
          [this](const QVariant &v) { setLinePowerSleepDelay(v.toInt()); } },
        { PowerDConfig::kLinePowerLockDelay,
          [this](const QVariant &v) { setLinePowerLockDelay(v.toInt()); } },
        { PowerDConfig::kLinePowerShortIdleDelay,
          [this](const QVariant &v) { setLinePowerShortIdleDelay(v.toInt()); } },

        // ── Battery delays ──
        { PowerDConfig::kBatteryScreensaverDelay,
          [this](const QVariant &v) { setBatteryScreensaverDelay(v.toInt()); } },
        { PowerDConfig::kBatteryScreenBlackDelay,
          [this](const QVariant &v) { setBatteryScreenBlackDelay(v.toInt()); } },
        { PowerDConfig::kBatterySleepDelay,
          [this](const QVariant &v) { setBatterySleepDelay(v.toInt()); } },
        { PowerDConfig::kBatteryLockDelay,
          [this](const QVariant &v) { setBatteryLockDelay(v.toInt()); } },
        { PowerDConfig::kShortIdleEnable,
          [this](const QVariant &v) { m_shortIdleEnabled = v.toBool(); } },
        { PowerDConfig::kShortIdleBlacklistApplications,
          [this](const QVariant &v) { m_shortIdleBlacklistApplications = desktopFileNames(v.toStringList()); } },
        { PowerDConfig::kSystemApplications,
          [this](const QVariant &v) { m_systemApplications = desktopFileNames(v.toStringList()); } },
        { PowerDConfig::kSystemServices,
          [this](const QVariant &v) { m_systemServices = v.toStringList(); } },
        { PowerDConfig::kFullscreenWorkaroundAppList,
          [this](const QVariant &v) { m_fullscreenWorkaroundApplications = v.toStringList(); } },
        { PowerDConfig::kBatteryShortIdleDelay,
          [this](const QVariant &v) { setBatteryShortIdleDelay(v.toInt()); } },

        // ── Locks ──
        { PowerDConfig::kScreenBlackLock,
          [this](const QVariant &v) { setScreenBlackLock(v.toBool()); } },
        { PowerDConfig::kSleepLock,
          [this](const QVariant &v) { setSleepLock(v.toBool()); } },

        // ── Actions ──
        { PowerDConfig::kLinePowerLidClosedAction,
          [this](const QVariant &v) { setLinePowerLidClosedAction(v.toInt()); } },
        { PowerDConfig::kBatteryLidClosedAction,
          [this](const QVariant &v) { setBatteryLidClosedAction(v.toInt()); } },
        { PowerDConfig::kLinePowerPressPowerButton,
          [this](const QVariant &v) { setLinePowerPressPowerBtnAction(v.toInt()); } },
        { PowerDConfig::kBatteryPressPowerButton,
          [this](const QVariant &v) { setBatteryPressPowerBtnAction(v.toInt()); } },

        // ── Low power ──
        { PowerDConfig::kLowPowerNotifyEnable,
          [this](const QVariant &v) { setLowPowerNotifyEnable(v.toBool()); } },
        { PowerDConfig::kAdjustBrightnessEnabled,
          [this](const QVariant &v) { m_adjustBrightnessEnabled = v.toBool(); } },
        { PowerDConfig::kLowPowerNotifyThreshold,
          [this](const QVariant &v) { setLowPowerNotifyThreshold(v.toInt()); } },
        { PowerDConfig::kAllowScreenSaver,
          [this](const QVariant &v) {
              m_allowScreenSaver = v.toBool();
              if (m_powerSavePlan)
                  m_powerSavePlan->setAllowScreenSaver(m_allowScreenSaver);
          } },
        { PowerDConfig::kDelayWakeupInterval,
          [this](const QVariant &v) { m_delayWakeupInterval = qMax(0, v.toInt()); } },
        { PowerDConfig::kDelayHandleIdleOffIntervalWhenScreenBlack,
          [this](const QVariant &v) {
              m_delayHandleIdleOffIntervalWhenScreenBlack = qMax(0, v.toInt());
          } },
        { PowerDConfig::kPercentageAction,
          [this](const QVariant &v) { setLowPowerAutoSleepThreshold(v.toInt()); } },
        { PowerDConfig::kUsePercentageForPolicy,
          [this](const QVariant &v) {
              if (m_lowPowerMgr)
                  m_lowPowerMgr->applyConfigValue(QLatin1String(PowerDConfig::kUsePercentageForPolicy), v);
          } },
        { PowerDConfig::kTimeToEmptyLow,
          [this](const QVariant &v) {
              if (m_lowPowerMgr)
                  m_lowPowerMgr->applyConfigValue(QLatin1String(PowerDConfig::kTimeToEmptyLow), v);
          } },
        { PowerDConfig::kTimeToEmptyDanger,
          [this](const QVariant &v) {
              if (m_lowPowerMgr)
                  m_lowPowerMgr->applyConfigValue(QLatin1String(PowerDConfig::kTimeToEmptyDanger), v);
          } },
        { PowerDConfig::kTimeToEmptyCritical,
          [this](const QVariant &v) {
              if (m_lowPowerMgr)
                  m_lowPowerMgr->applyConfigValue(QLatin1String(PowerDConfig::kTimeToEmptyCritical), v);
          } },
        { PowerDConfig::kTimeToEmptyAction,
          [this](const QVariant &v) {
              if (m_lowPowerMgr)
                  m_lowPowerMgr->applyConfigValue(QLatin1String(PowerDConfig::kTimeToEmptyAction), v);
          } },
        { PowerDConfig::kLowPowerAction,
          [this](const QVariant &v) { setLowPowerAction(v.toInt()); } },

        // ── Scheduled shutdown ──
        { PowerDConfig::kScheduledShutdownState,
          [this](const QVariant &v) { setScheduledShutdownState(v.toBool()); } },
        { PowerDConfig::kShutdownTime,
          [this](const QVariant &v) { setShutdownTime(v.toString()); } },
        { PowerDConfig::kShutdownRepetition,
          [this](const QVariant &v) { setShutdownRepetition(v.toInt()); } },
        { PowerDConfig::kCustomShutdownWeekDays,
          [this](const QVariant &v) {
              QByteArray days;
              if (v.metaType().id() == QMetaType::QByteArray) {
                  days = v.toByteArray();
              } else {
                  for (const QVariant &day : v.toList())
                      days.append(static_cast<char>(day.toInt()));
              }
              setCustomShutdownWeekDays(days);
          } },
        { PowerDConfig::kShutdownCountdown,
          [this](const QVariant &v) { m_shutdownCountdown = v.toInt(); } },
        { PowerDConfig::kNextShutdownTime,
          [this](const QVariant &v) { m_nextShutdownTime = v.toLongLong(); } },
    };

    m_loadingConfig = true;
    m_applyingConfig = true;
    for (const auto &b : bindings)
        b.apply(m_config->value(QLatin1String(b.key)));
    m_applyingConfig = false;
    m_loadingConfig = false;

    connect(m_config, &DConfig::valueChanged, this,
            [this, bindings = std::move(bindings)](const QString &k) {
        if (m_writingConfigKeys.contains(k))
            return;

        for (const auto &b : bindings) {
            if (k != QLatin1String(b.key))
                continue;

            auto *reader = m_configReader;
            if (!reader)
                return;

            const auto apply = b.apply;
            QMetaObject::invokeMethod(reader, [this, reader, k, apply] {
                const QVariant value = reader->value(k);
                QMetaObject::invokeMethod(this, [this, k, value, apply] {
                    qDebug(logPowerSession) << "DConfig value changed:" << k << "=" << value;
                    m_applyingConfig = true;
                    apply(value);
                    m_applyingConfig = false;
                });
            });
            return;
        }
    });

    qInfo(logPowerSession) << "initial load: ss=" << m_linePowerScreensaverDelay
                           << " black=" << m_linePowerScreenBlackDelay
                           << " sleep=" << m_linePowerSleepDelay
                           << " lock=" << m_linePowerLockDelay;
}

void PowerManager::setScheduledShutdownState(bool v)
{
    if (m_scheduledShutdownState == v)
        return;

    m_scheduledShutdownState = v;
    Q_EMIT scheduledShutdownStateChanged();
    persist(kScheduledShutdownState, v);
    if (!m_loadingConfig) {
        if (v)
            recalculateScheduledShutdown();
        else
            scheduledShutdown(SchedCancel);
    }
}

void PowerManager::setShutdownRepetition(int v)
{
    if (m_shutdownRepetition == v)
        return;

    m_shutdownRepetition = v;
    Q_EMIT shutdownRepetitionChanged();
    persist(kShutdownRepetition, v);
    if (!m_loadingConfig && m_scheduledShutdownState)
        recalculateScheduledShutdown();
}

void PowerManager::setShutdownTime(const QString &v)
{
    if (m_shutdownTime != v) {
        m_shutdownTime = v;
        Q_EMIT shutdownTimeChanged();
        persist(kShutdownTime, v);
        if (!m_loadingConfig && m_scheduledShutdownState)
            recalculateScheduledShutdown();
    }
}

void PowerManager::setCustomShutdownWeekDays(const QByteArray &v)
{
    if (m_customShutdownWeekDays == v)
        return;
    m_customShutdownWeekDays = v;
    Q_EMIT customShutdownWeekDaysChanged();
    QVariantList days;
    for (const char day : v)
        days.append(static_cast<quint8>(day));
    persist(kCustomShutdownWeekDays, days);
    if (!m_loadingConfig && m_scheduledShutdownState)
        recalculateScheduledShutdown();
}
void PowerManager::closeNotify()
{
    if (m_shutdownNotifyId == 0) return;
    m_proxy->closeNotification(m_shutdownNotifyId);
    m_shutdownNotifyId = 0;
}

void PowerManager::onNotifyActionInvoked(uint id, const QString &actionKey)
{
    qDebug(logPowerSession) << "Notification action invoked: id=" << id << " key=" << actionKey;
    if (id != m_shutdownNotifyId) return;
    int nextStatus;
    if (actionKey == "cancel") {
        nextStatus = SchedCancel;
    } else if (actionKey == "shutdown") {
        nextStatus = SchedShutdown;
    } else {
        nextStatus = SchedCancel;
    }
    scheduledShutdown(nextStatus);
}

void PowerManager::initScheduledShutdown()
{
    qInfo(logPowerSession) << "Scheduled shutdown init: state=" << m_scheduledShutdownState
                          << " time=" << m_shutdownTime
                          << " repetition=" << m_shutdownRepetition
                          << " countdown=" << m_shutdownCountdown;
    m_shutdownTimer = new QTimer(this);
    m_shutdownTimer->setSingleShot(true);
    m_countdownTimer = new QTimer(this);

    connect(m_proxy, &SessionDBusProxy::notifyActionInvoked,
            this, &PowerManager::onNotifyActionInvoked);
    connect(m_proxy, &SessionDBusProxy::timeUpdate,
            this, &PowerManager::onSystemTimeChanged);
    connect(m_proxy, &SessionDBusProxy::SessionActiveChanged,
            this, &PowerManager::onSessionActiveChanged);

    if (m_scheduledShutdownState) {
        if (m_nextShutdownTime == 0) {
            m_nextShutdownTime = getNextShutdownTime(0);
            persist(kNextShutdownTime, m_nextShutdownTime);
        }
        scheduledShutdown(SchedInit);
    }
}

void PowerManager::onSystemTimeChanged()
{
    if (!m_scheduledShutdownState)
        return;

    m_nextShutdownTime = getNextShutdownTime(0);
    persist(kNextShutdownTime, m_nextShutdownTime);
    scheduledShutdown(SchedInit);
}

void PowerManager::onSessionActiveChanged()
{
    if (!m_scheduledShutdownState)
        return;

    scheduledShutdown(SchedInit);
}

void PowerManager::doAutoShutdown()
{
    qInfo(logPowerSession) << "Performing auto shutdown";
    closeNotify();
    m_proxy->requestShutdown();
}

void PowerManager::shutdownCountdownNotify(int count, bool playSound)
{
    QString body = tr("The system will shut down automatically after %1 s").arg(count);
    QString title = tr("Scheduled Shutdown");
    QStringList actions = {"cancel", tr("Cancel"), "shutdown", tr("Shut down")};
    QVariantMap hints = {
        {"x-deepin-PlaySound", playSound},
        {"urgency", 2},
        {"x-deepin-ShowInNotifyCenter", false},
        {"x-deepin-ClickToDisappear", false},
        {"x-deepin-DisappearAfterLock", false},
    };

    m_shutdownNotifyId = m_proxy->notify(m_shutdownNotifyId, "dde-control-center",
                                         "preferences-system",
                                         title, body, actions, hints, -1);
}

void PowerManager::scheduledShutdown(int state)
{
    qInfo(logPowerSession) << "Scheduled shutdown state change: pre=" << m_shutdownStatus
          << " next=" << state
          << " nextTime=" << QDateTime::fromSecsSinceEpoch(m_nextShutdownTime).toString("yyyy-MM-dd hh:mm:ss")
          << " rep=" << m_shutdownRepetition
          << " cnt=" << m_shutdownCountdown;

    if (!m_shutdownTimer) return;
    if (m_shutdownTimer->isActive()) m_shutdownTimer->stop();
    if (m_countdownTimer && m_countdownTimer->isActive()) m_countdownTimer->stop();

    // Check session active status (match Go: !isSessionActive || m.WarnLevel == WarnLevelAction)
    bool isSessionActive = m_proxy->sessionActive();

    if (!isSessionActive || m_warnLevel == LowPowerManager::Action) {
        closeNotify();
        return;
    }

    if (!m_scheduledShutdownState && state == SchedInit) {
        return;
    }

    if (state != SchedInit && state == m_shutdownStatus) {
        return;
    }

    if (m_nextShutdownTime == 0) {
        return;
    }

    QDateTime next = QDateTime::fromSecsSinceEpoch(m_nextShutdownTime);
    QDateTime now = QDateTime::currentDateTime();

    switch (state) {
    case SchedInit: {
        if (m_shutdownStatus >= SchedCountdowning) {
            closeNotify();
        }
        if (!m_scheduledShutdownState) return;
        m_shutdownStatus = SchedInit;

        qint64 diffMins = now.secsTo(next) / 60;
        int nextStatus;
        if (diffMins < 0) {
            nextStatus = SchedTimeout;
        } else if (diffMins == 0) {
            nextStatus = SchedCountdowning;
        } else if (now.secsTo(next) <= m_shutdownCountdown) {
            nextStatus = SchedCountdowning;
        } else {
            nextStatus = SchedWaitingToNotify;
        }
        scheduledShutdown(nextStatus);
        break;
    }
    case SchedWaitingToNotify: {
        m_shutdownStatus = SchedWaitingToNotify;
        QDateTime notifyAt = next.addSecs(-m_shutdownCountdown);
        const qint64 msToNotify = qMax<qint64>(0, now.msecsTo(notifyAt));
        m_shutdownTimer->start(static_cast<int>(qMin<qint64>(
            msToNotify, std::numeric_limits<int>::max())));
        disconnect(m_shutdownTimer, &QTimer::timeout, this, nullptr);
        connect(m_shutdownTimer, &QTimer::timeout, this,
                [this] { scheduledShutdown(SchedInit); });
        break;
    }
    case SchedCountdowning: {
        m_shutdownStatus = SchedCountdowning;
        int remaining = m_shutdownCountdown;
        shutdownCountdownNotify(remaining, true);

        // Use member m_countdownTimer (matching Go: m.shutdownTimer in countdown goroutine)
        // The timer is stopped at the top of this function on any state transition
        disconnect(m_countdownTimer, &QTimer::timeout, this, nullptr);
        connect(m_countdownTimer, &QTimer::timeout, this, [this, remaining]() mutable {
            remaining--;
            if (remaining <= 0) {
                m_countdownTimer->stop();
                scheduledShutdown(SchedShutdown);
            } else {
                shutdownCountdownNotify(remaining, false);
            }
        });
        m_countdownTimer->start(1000);
        break;
    }
    case SchedShutdown:
    case SchedCancel:
    case SchedTimeout: {
        if (state == SchedCancel || state == SchedTimeout)
            closeNotify();
        m_shutdownStatus = state;

        if (m_shutdownRepetition == RepOnce) {
            m_scheduledShutdownState = false;
            m_nextShutdownTime = 0;
            persist(kNextShutdownTime, 0);
            persist(kScheduledShutdownState, false);
            Q_EMIT scheduledShutdownStateChanged();
        } else {
            m_nextShutdownTime = getNextShutdownTime(m_nextShutdownTime);
            persist(kNextShutdownTime, m_nextShutdownTime);

            qint64 t = now.secsTo(next) / 60;
            if (t > 0) {
                QTimer::singleShot(10000, this, [this]() { scheduledShutdown(SchedInit); });
            } else {
                QTimer::singleShot(m_shutdownCountdown * 1000, this, [this]() { scheduledShutdown(SchedInit); });
            }
        }

        if (state == SchedShutdown) {
            doAutoShutdown();
        }
        break;
    }
    }
}

bool PowerManager::isWorkday(const QDateTime &date) const
{
    int year = date.date().year();
    int month = date.date().month();
    QString reply = m_proxy->getFestivalMonth(year, month);
    // Match the legacy daemon: an empty reply means the calendar call failed, while a
    // valid empty array below means no holiday overrides and falls back to weekdays.
    if (reply.isEmpty())
        return false;

    QJsonDocument doc = QJsonDocument::fromJson(reply.toUtf8());
    if (!doc.isArray())
        return false;
    if (doc.array().isEmpty()) {
        int dow = date.date().dayOfWeek();
        return dow != Qt::Saturday && dow != Qt::Sunday;
    }
    QJsonObject root = doc.array().first().toObject();
    QJsonArray list = root["List"].toArray();
    if (list.isEmpty()) {
        int dow = date.date().dayOfWeek();
        return dow != Qt::Saturday && dow != Qt::Sunday;
    }

    QString dateStr1 = date.toString("yyyy-M-d");
    QString dateStr2 = date.toString("yyyy-MM-dd");
    for (const auto &item : list) {
        QJsonObject obj = item.toObject();
        if (obj["Date"].toString() == dateStr1 || obj["Date"].toString() == dateStr2) {
            return obj["Status"].toInt() == 2;
        }
    }
    int dow = date.date().dayOfWeek();
    return dow != Qt::Saturday && dow != Qt::Sunday;
}

bool PowerManager::isCustomDay(const QDateTime &date) const
{
    const int day = date.date().dayOfWeek();
    // DConfig stores these as numeric byte values (the legacy daemon used []byte), not
    // as the textual string "135"; the explicit conversion preserves that contract.
    return std::any_of(m_customShutdownWeekDays.cbegin(),
                       m_customShutdownWeekDays.cend(),
                       [day](char configured) {
                           const auto value = static_cast<quint8>(configured);
                           return value == day || (day == Qt::Sunday && value == 0);
                       });
}

qint64 PowerManager::getNextShutdownTime(qint64 baseTime) const
{
    auto getNextTime = [this](qint64 bt) -> QDateTime {
        QDateTime baseDate = QDateTime::fromSecsSinceEpoch(bt);
        QDateTime now = QDateTime::currentDateTime();
        QTime targetTime = QTime::fromString(m_shutdownTime, "hh:mm");
        QDateTime target = QDateTime(now.date(), targetTime);

        if (now.secsTo(target) / 60 < 0) { // 已经过去时间了
            target = target.addDays(1);
        }

        if (baseDate.secsTo(target) / 60 <= 0) { // 
            target = target.addDays(1);
        }
        return target;
    };

    QDateTime targetTime;
    switch (m_shutdownRepetition) {
    case RepOnce:
    case RepEveryday:
        targetTime = getNextTime(baseTime);
        break;
    case RepWorkdays: {
        targetTime = getNextTime(baseTime);
        for (int i = 0; i <= 366; ++i) {
            if (i == 366) return 0;
            if (isWorkday(targetTime)) break;
            targetTime = targetTime.addDays(1);
        }
        break;
    }
    case RepCustom: {
        targetTime = getNextTime(baseTime);
        for (int i = 0; i <= 7; ++i) {
            if (i == 7) return 0;
            if (isCustomDay(targetTime)) break;
            targetTime = targetTime.addDays(1);
        }
        break;
    }
    default:
        targetTime = getNextTime(baseTime);
        break;
    }

    return targetTime.toSecsSinceEpoch();
}
