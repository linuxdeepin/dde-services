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
#include <QStandardPaths>
#include <QLoggingCategory>

#include <unistd.h>
#include <functional>
#include <vector>

using namespace PowerDBus;
using namespace PowerDConfig;
using namespace PowerFS;
using namespace Dtk::Core;

Q_LOGGING_CATEGORY(logPowerSession, "dde.power.session")

#define DEF_SETTER_PERSIST(T, Suffix, member, signal, dkey) \
    void PowerManager::set##Suffix(T v) { \
        if (m_##member != v) { m_##member = v; Q_EMIT signal(); \
        if (m_config) m_config->setValue(dkey, QVariant::fromValue(v)); } }

DEF_SETTER_PERSIST(int, LinePowerScreensaverDelay, linePowerScreensaverDelay, linePowerScreensaverDelayChanged, kLinePowerScreensaverDelay)
DEF_SETTER_PERSIST(int, LinePowerScreenBlackDelay, linePowerScreenBlackDelay, linePowerScreenBlackDelayChanged, kLinePowerScreenBlackDelay)
DEF_SETTER_PERSIST(int, LinePowerSleepDelay, linePowerSleepDelay, linePowerSleepDelayChanged, kLinePowerSleepDelay)
DEF_SETTER_PERSIST(int, LinePowerLockDelay, linePowerLockDelay, linePowerLockDelayChanged, kLinePowerLockDelay)
DEF_SETTER_PERSIST(int, BatteryScreensaverDelay, batteryScreensaverDelay, batteryScreensaverDelayChanged, kBatteryScreensaverDelay)
DEF_SETTER_PERSIST(int, BatteryScreenBlackDelay, batteryScreenBlackDelay, batteryScreenBlackDelayChanged, kBatteryScreenBlackDelay)
DEF_SETTER_PERSIST(int, BatterySleepDelay, batterySleepDelay, batterySleepDelayChanged, kBatterySleepDelay)
DEF_SETTER_PERSIST(int, BatteryLockDelay, batteryLockDelay, batteryLockDelayChanged, kBatteryLockDelay)
DEF_SETTER_PERSIST(bool, ScreenBlackLock, screenBlackLock, screenBlackLockChanged, kScreenBlackLock)
DEF_SETTER_PERSIST(bool, SleepLock, sleepLock, sleepLockChanged, kSleepLock)
DEF_SETTER_PERSIST(int, LinePowerLidClosedAction, linePowerLidClosedAction, linePowerLidClosedActionChanged, kLinePowerLidClosedAction)
DEF_SETTER_PERSIST(int, BatteryLidClosedAction, batteryLidClosedAction, batteryLidClosedActionChanged, kBatteryLidClosedAction)
DEF_SETTER_PERSIST(int, LinePowerPressPowerBtnAction, linePowerPressPowerBtnAction, linePowerPressPowerBtnActionChanged, kLinePowerPressPowerButton)
DEF_SETTER_PERSIST(int, BatteryPressPowerBtnAction, batteryPressPowerBtnAction, batteryPressPowerBtnActionChanged, kBatteryPressPowerButton)
DEF_SETTER_PERSIST(bool, LowPowerNotifyEnable, lowPowerNotifyEnable, lowPowerNotifyEnableChanged, kLowPowerNotifyEnable)
DEF_SETTER_PERSIST(int, LowPowerNotifyThreshold, lowPowerNotifyThreshold, lowPowerNotifyThresholdChanged, kLowPowerNotifyThreshold)
DEF_SETTER_PERSIST(int, LowPowerAutoSleepThreshold, lowPowerAutoSleepThreshold, lowPowerAutoSleepThresholdChanged, kPercentageAction)
DEF_SETTER_PERSIST(int, LowPowerAction, lowPowerAction, lowPowerActionChanged, kLowPowerAction)
DEF_SETTER_PERSIST(bool, AmbientLightAdjustBrightness, ambientLightAdjustBrightness, ambientLightAdjustBrightnessChanged, kAmbientLightAdjustBrightness)
DEF_SETTER_PERSIST(bool, ScheduledShutdownState, scheduledShutdownState, scheduledShutdownStateChanged, kScheduledShutdownState)
DEF_SETTER_PERSIST(int, ShutdownRepetition, shutdownRepetition, shutdownRepetitionChanged, kShutdownRepetition)

PowerManager::PowerManager(QDBusConnection *conn, const QString &svc, QObject *parent)
    : QObject(parent), m_conn(conn)
{
    Q_UNUSED(svc);
    m_useWayland = (qEnvironmentVariable("XDG_SESSION_TYPE") == QLatin1String("wayland"));

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
    if (m_sleepInhibitor) m_sleepInhibitor->unblock();
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
    m_lowPowerMgr = new LowPowerManager(this, this);
    m_lowPowerMgr->initConfig(m_config);

    if (m_idleWatcher) {
        connect(m_idleWatcher, &IdleWatcher::idled, m_powerSavePlan, &PowerSavePlan::HandleIdleOn);
        connect(m_idleWatcher, &IdleWatcher::resumed, m_powerSavePlan, &PowerSavePlan::HandleIdleOff);
    }
    connect(this, &PowerManager::onBatteryChanged, this, [this]() { m_powerSavePlan->Reset(); });

    connect(this, &PowerManager::linePowerScreensaverDelayChanged, this, &PowerManager::onLinePowerDelayChanged);
    connect(this, &PowerManager::linePowerScreenBlackDelayChanged, this, &PowerManager::onLinePowerDelayChanged);
    connect(this, &PowerManager::linePowerLockDelayChanged, this, &PowerManager::onLinePowerDelayChanged);
    connect(this, &PowerManager::linePowerSleepDelayChanged, this, &PowerManager::onLinePowerDelayChanged);
    connect(this, &PowerManager::batteryScreensaverDelayChanged, this, &PowerManager::onBatteryDelayChanged);
    connect(this, &PowerManager::batteryScreenBlackDelayChanged, this, &PowerManager::onBatteryDelayChanged);
    connect(this, &PowerManager::batteryLockDelayChanged, this, &PowerManager::onBatteryDelayChanged);
    connect(this, &PowerManager::batterySleepDelayChanged, this, &PowerManager::onBatteryDelayChanged);

    initBatteryWatcher();
    initSleepWatcher();
    initLogindInhibit();
    initScheduledShutdown();

    m_powerSavePlan->Start();

    if (!m_conn->registerObject(kPath, this,
            QDBusConnection::ExportAllSlots |
            QDBusConnection::ExportAllSignals |
            QDBusConnection::ExportAllProperties)) {
        qWarning(logPowerSession) << "Failed to register D-Bus object:" << m_conn->lastError().message();
        return false;
    }

    // ExportAllProperties 只提供 Get/Set/GetAll 访问，不会自动把 NOTIFY 信号
    // 转成 org.freedesktop.DBus.Properties.PropertiesChanged。
    // 用 notifyPropertyChanged 槽函数统一桥接所有 Q_PROPERTY(NOTIFY) 到标准 D-Bus 属性变更通知。
    const QMetaObject *mo = metaObject();
    int slotIdx = mo->indexOfSlot("notifyPropertyChanged()");
    if (slotIdx >= 0) {
        QMetaMethod slot = mo->method(slotIdx);
        for (int i = mo->propertyOffset(); i < mo->propertyCount(); ++i) {
            QMetaProperty prop = mo->property(i);
            if (!prop.hasNotifySignal())
                continue;
            connect(this, prop.notifySignal(), this, slot);
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

    QTimer::singleShot(1000, this, [this]() {
        refreshBatteryInfo();
        // 拉取 PSM 初始值 (因为 system 端 registerObject 在 DConfig init 之后,
        // 初始 NOTIFY 信号丢失, DDBusInterface 的 GetAll 也不可靠)
        if (m_powerSavePlan) {
            m_powerSavePlan->onPowerSavingModeEnabledChanged(m_proxy->powerSavingModeEnabled());
            m_powerSavePlan->onBrightnessDropPercentChanged(m_proxy->powerSavingModeBrightnessDropPercent());
        }
    });
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
    bool hasBattery = m_proxy->hasBattery();
    if (hasBattery) {
        m_batteryIsPresent["Display"] = true;
        m_batteryPercentage["Display"] = m_proxy->batteryPercentage();
        m_batteryState["Display"] = m_proxy->batteryStatus();
        m_batteryTimeToEmpty = m_proxy->batteryTimeToEmpty();
        Q_EMIT batteryIsPresentChanged();
        Q_EMIT batteryPercentageChanged();
        Q_EMIT batteryStateChanged();
    }

    bool onBattery = m_proxy->onBattery();
    if (onBattery != m_onBattery) {
        m_onBattery = onBattery;
        Q_EMIT onBatteryChanged();
    }

    bool hasLid = m_proxy->hasLidSwitch();
    if (hasLid != m_lidIsPresent) {
        m_lidIsPresent = hasLid;
        Q_EMIT lidIsPresentChanged();
    }

    bool hps = m_proxy->isHighPerformanceSupported();
    if (m_config) {
        bool e = m_config->value(PowerDConfig::kHighPerformanceEnabled).toBool();
        hps = hps && e;
    }
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
    m_batteryIsPresent["Display"] = value;
    Q_EMIT batteryIsPresentChanged();
}

void PowerManager::handleBatteryPercentageChanged(double value)
{
    m_batteryPercentage["Display"] = value;
    Q_EMIT batteryPercentageChanged();
}

void PowerManager::handleBatteryStatusChanged(uint value)
{
    m_batteryState["Display"] = value;
    Q_EMIT batteryStateChanged();
}

void PowerManager::handleBatteryTimeToEmptyChanged(quint64 value)
{
    m_batteryTimeToEmpty = value;
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

void PowerManager::initLogindInhibit()
{
    auto fd = m_proxy->inhibit(
        "handle-power-key:handle-lid-switch:handle-suspend-key",
        PowerDBus::kService, "handling key press and lid switch close", "block");
    m_inhibitFd = fd.isValid() ? dup(fd.fileDescriptor()) : -1;

    connect(m_proxy, &SessionDBusProxy::login1OwnerChanged,
            this, &PowerManager::onLogin1OwnerChanged);
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

void PowerManager::handleBeforeSleep(bool)
{
    qDebug(logPowerSession) << "System is going to sleep, prepare suspend state:" << m_prepareSuspendState;
    m_prepareSuspendState = PS_Sleeping;
    setBlackScreenActive(true);
    if (m_useWayland && m_sleepLock) {
        m_proxy->lockSession(currentSessionId());
    }
}

void PowerManager::handleWakeup()
{
    qDebug(logPowerSession) << "System woke up";
    m_prepareSuspendState = PS_Resume;
    m_delayInActive = true;
    QTimer::singleShot(m_delayWakeupInterval * 1000, this, [this]() {
        m_delayInActive = false;
        setBlackScreenActive(false);
    });
    setDPMSModeOn();
    if (m_powerSavePlan) m_powerSavePlan->HandleIdleOff();
    if (m_scheduledShutdownState) scheduledShutdown(SchedInit);
}

void PowerManager::Reset()
{
    if (!m_config) return;
    static const char *keys[] = {
        kLinePowerScreenBlackDelay, kLinePowerSleepDelay, kLinePowerLockDelay,
        kLinePowerLidClosedAction, kLinePowerPressPowerButton,
        kBatteryScreenBlackDelay, kBatterySleepDelay, kBatteryLockDelay,
        kBatteryLidClosedAction, kBatteryPressPowerButton,
        kScreenBlackLock, kSleepLock, kPowerButtonPressedExec,
        kLowPowerNotifyEnable, kLowPowerNotifyThreshold,
        kPercentageAction, kPowerSavingModeBrightnessDropPercent,
    };
    for (auto k : keys) {
        if (m_config->value(k).isValid()) {
            m_config->reset(k);
        }
    }
    if (m_powerSavePlan) m_powerSavePlan->Reset();
}

void PowerManager::SetPrepareSuspend(int state)
{
    m_prepareSuspendState = static_cast<PrepareSuspendState>(state);
}

void PowerManager::TurnOffScreen()
{
    setDPMSModeOff();
}

void PowerManager::TurnOnScreen()
{
    setDPMSModeOn();
}

bool PowerManager::shouldIgnoreIdleOn() const
{
    return m_prepareSuspendState > PS_Finish;
}

void PowerManager::doSuspend()
{
    qInfo(logPowerSession) << "Requesting suspend, canSuspend=" << canSuspend();
    if (!canSuspend())
        return;

    if (!m_useWayland) {
        m_proxy->requestSuspendByFront();
        return;
    } else {
        m_proxy->requestSuspend();
    }
}

void PowerManager::doSuspendByFront() {
    qInfo(logPowerSession) << "Requesting suspend by front, canSuspend=" << canSuspend();
    if (canSuspend()) {
        m_proxy->requestSuspendByFront();
    }
}

void PowerManager::doShutdown() {
    qInfo(logPowerSession) << "Requesting shutdown";
    m_proxy->requestShutdown();
}

void PowerManager::doHibernate() {
    qInfo(logPowerSession) << "Requesting hibernate, canHibernate=" << canHibernate();
    if (canHibernate()) {
        m_proxy->requestHibernate();
    }
}

void PowerManager::doTurnOffScreen()
{
    qInfo(logPowerSession) << "Turning off screen";
    if (m_screenBlackLock) { 
        doLock(true); 
    }
    QTimer::singleShot(500, this, [this]() { setDPMSModeOff(); });
}

void PowerManager::doLock(bool autoStartAuth)
{
    qInfo(logPowerSession) << "Locking session";
    if (m_useWayland) { 
        m_proxy->lockSession(currentSessionId()); 
        return; 
    }
    m_proxy->showLockAuth(autoStartAuth);
}

void PowerManager::setDPMSModeOn()  { 
    qInfo(logPowerSession) << "Setting DPMS mode to on";
    if (m_screenCtrl) {
        m_screenCtrl->setAllModes(ScreenController::On);
    }
}

void PowerManager::setDPMSModeOff()
{
    qInfo(logPowerSession) << "Setting DPMS mode to off";
    if (m_screenCtrl) {
        m_screenCtrl->setAllModes(ScreenController::Off);
    }

    const auto dpmsPath = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation)
                          + QStringLiteral("/dpms-state");
    QFile f(dpmsPath);
    if (f.open(QIODevice::WriteOnly)) {
        f.write("1");
        f.close();
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

// TODO: x11
IdleWatcher *PowerManager::createIdleWatcher()
{
    return m_useWayland ? new WaylandIdleWatcher(this) : nullptr; 
}

// TODO x11
ScreenController *PowerManager::createScreenController()
{
    return m_useWayland ? new WaylandScreenController(this) : nullptr;
}

void PowerManager::recalculateScheduledShutdown()
{
    qDebug(logPowerSession) << "Recalculating scheduled shutdown, current nextShutdownTime=" << m_nextShutdownTime;
    m_nextShutdownTime = getNextShutdownTime(0);

    if (m_config)
        m_config->setValue(PowerDConfig::kNextShutdownTime, m_nextShutdownTime);
    scheduledShutdown(SchedInit);
}

void PowerManager::initDConfig()
{
    m_config = DConfig::create(PowerDConfig::kAppId, PowerDConfig::kPowerName, "", this);
    if (!m_config)
        return;

    using Setter = std::function<void(const QVariant &)>;
    using Hook   = std::function<void()>;
    struct Binding { const char *key; Setter apply; Hook onChange = {}; };

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

        // ── Battery delays ──
        { PowerDConfig::kBatteryScreensaverDelay,
          [this](const QVariant &v) { setBatteryScreensaverDelay(v.toInt()); } },
        { PowerDConfig::kBatteryScreenBlackDelay,
          [this](const QVariant &v) { setBatteryScreenBlackDelay(v.toInt()); } },
        { PowerDConfig::kBatterySleepDelay,
          [this](const QVariant &v) { setBatterySleepDelay(v.toInt()); } },
        { PowerDConfig::kBatteryLockDelay,
          [this](const QVariant &v) { setBatteryLockDelay(v.toInt()); } },

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
        { PowerDConfig::kLowPowerNotifyThreshold,
          [this](const QVariant &v) { setLowPowerNotifyThreshold(v.toInt()); } },
        { PowerDConfig::kPercentageAction,
          [this](const QVariant &v) { setLowPowerAutoSleepThreshold(v.toInt()); } },
        { PowerDConfig::kLowPowerAction,
          [this](const QVariant &v) { setLowPowerAction(v.toInt()); } },
        { PowerDConfig::kAmbientLightAdjustBrightness,
          [this](const QVariant &v) { setAmbientLightAdjustBrightness(v.toBool()); } },

        // ── Scheduled shutdown ──
        { PowerDConfig::kScheduledShutdownState,
          [this](const QVariant &v) { setScheduledShutdownState(v.toBool()); },
          [this]() {
              if (m_scheduledShutdownState) {
                  recalculateScheduledShutdown();
              } else {
                  scheduledShutdown(SchedCancel);
              }
          }
        },
        { PowerDConfig::kShutdownTime,
          [this](const QVariant &v) { setShutdownTime(v.toString()); },
          [this]() {
              if (m_scheduledShutdownState) {
                  recalculateScheduledShutdown();
              }
          }
        },
        { PowerDConfig::kShutdownRepetition,
          [this](const QVariant &v) { setShutdownRepetition(v.toInt()); },
          [this]() {
              if (m_scheduledShutdownState) {
                  recalculateScheduledShutdown();
              }
          }
        },
        { PowerDConfig::kCustomShutdownWeekDays,
          [this](const QVariant &v) { setCustomShutdownWeekDays(v.toByteArray()); },
          [this]() {
              if (m_scheduledShutdownState) {
                  recalculateScheduledShutdown();
              }
          }
        },
        { PowerDConfig::kShutdownCountdown,
          [this](const QVariant &v) { m_shutdownCountdown = v.toInt(); } },
        { PowerDConfig::kNextShutdownTime,
          [this](const QVariant &v) { m_nextShutdownTime = v.toLongLong(); } },
    };

    for (const auto &b : bindings)
        b.apply(m_config->value(QLatin1String(b.key)));

    connect(m_config, &DConfig::valueChanged, this,
            [this, bindings = std::move(bindings)](const QString &k) {
        QVariant v = m_config->value(k);
        qDebug(logPowerSession) << "DConfig value changed:" << k << "=" << v;

        for (const auto &b : bindings) {
            if (k == QLatin1String(b.key)) {
                b.apply(v);
                if (b.onChange)
                    b.onChange();
                return;
            }
        }
    });

    qInfo(logPowerSession) << "initial load: ss=" << m_linePowerScreensaverDelay
                           << " black=" << m_linePowerScreenBlackDelay
                           << " sleep=" << m_linePowerSleepDelay
                           << " lock=" << m_linePowerLockDelay;
}

void PowerManager::setShutdownTime(const QString &v)
{
    if (m_shutdownTime != v) {
        m_shutdownTime = v;
        Q_EMIT shutdownTimeChanged();
        if (m_config) m_config->setValue(kShutdownTime, v);
    }
}

void PowerManager::setCustomShutdownWeekDays(const QByteArray &v)
{
    if (m_customShutdownWeekDays != v) { 
        m_customShutdownWeekDays = v; Q_EMIT customShutdownWeekDaysChanged();
        if (m_config) m_config->setValue(kCustomShutdownWeekDays, QVariant(v));
    }
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
            if (m_config) m_config->setValue(kNextShutdownTime, m_nextShutdownTime);
        }
        scheduledShutdown(SchedInit);
    }
}

void PowerManager::onSystemTimeChanged()
{
    if (!m_scheduledShutdownState)
        return;

    m_nextShutdownTime = getNextShutdownTime(0);
    if (m_config) m_config->setValue(kNextShutdownTime, m_nextShutdownTime);
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
        qint64 msToNotify = now.msecsTo(notifyAt);
        if (msToNotify < 0) msToNotify = 0;
        m_shutdownTimer->start(msToNotify);
        disconnect(m_shutdownTimer, &QTimer::timeout, this, nullptr);
        connect(m_shutdownTimer, &QTimer::timeout, this, [this]() {
            scheduledShutdown(SchedCountdowning);
        });
        break;
    }
    case SchedCountdowning: {
        m_shutdownStatus = SchedCountdowning;
        int remaining = m_shutdownCountdown + 1;
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
        if (state == SchedTimeout) {
            closeNotify();
        }
        m_shutdownStatus = state;

        if (m_shutdownRepetition == RepOnce) {
            m_scheduledShutdownState = false;
            m_nextShutdownTime = 0;
            if (m_config) {
                m_config->setValue(kNextShutdownTime, 0);
                m_config->setValue(kScheduledShutdownState, false);
            }
            Q_EMIT scheduledShutdownStateChanged();
        } else {
            m_nextShutdownTime = getNextShutdownTime(m_nextShutdownTime);
            if (m_config) m_config->setValue(kNextShutdownTime, m_nextShutdownTime);

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
    if (reply.isEmpty()) {
        int dow = date.date().dayOfWeek();
        return dow != Qt::Saturday && dow != Qt::Sunday;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply.toUtf8());
    if (!doc.isArray() || doc.array().isEmpty()) {
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
    int dow = date.date().dayOfWeek();
    for (int i = 0; i < m_customShutdownWeekDays.size(); ++i) {
        if (m_customShutdownWeekDays[i] - '0' == dow)
            return true;
    }
    return false;
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
