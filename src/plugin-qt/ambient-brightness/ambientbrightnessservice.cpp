// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "ambientbrightnessservice.h"
#include "ambientbrightnesslogging.h"
#include "ambientbrightnesspolicyfactory.h"
#include "continuous/continuousambientlightpolicy.h"


#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QVariantMap>
#include <DConfig>
#include <algorithm>
#include <cmath>
#include <limits>

namespace dde::ambient_brightness {
namespace {

constexpr auto kSensorService = "net.hadess.SensorProxy";
constexpr auto kSensorPath = "/net/hadess/SensorProxy";
constexpr auto kSensorInterface = "net.hadess.SensorProxy";
constexpr auto kPropertiesInterface = "org.freedesktop.DBus.Properties";
constexpr auto kObjectPath = "/org/deepin/dde/AmbientBrightness1";
constexpr auto kConfigAppId = "org.deepin.dde.daemon";
constexpr auto kConfigName = "org.deepin.dde.daemon.ambient-brightness";
constexpr auto kAmbientLightAdjustBrightnessKey = "ambientLightAdjustBrightness";
constexpr auto kPowerService = "org.deepin.dde.Power1";
constexpr auto kPowerPath = "/org/deepin/dde/Power1";
constexpr auto kPowerInterface = "org.deepin.dde.Power1";

constexpr auto kUseWeightedWindowsKey = "useWeightedWindows";
constexpr auto kAmbientLightHorizonMsKey = "ambientLightHorizonMs";
constexpr auto kFastLightHorizonMsKey = "fastLightHorizonMs";
constexpr auto kContinuousMappingModeKey = "continuousMappingMode";
constexpr auto kContinuousLuxCurveKey = "continuousLuxCurve";
constexpr auto kStepHysteresisRatioKey = "stepHysteresisRatio";
constexpr auto kBrightenDebounceMsKey = "brightenDebounceMs";
constexpr auto kDarkenDebounceMsKey = "darkenDebounceMs";
constexpr int kInitialSampleTimeoutMs = 2000;
constexpr int kRuntimeRecoveryRefreshDelayMs = 1000;

bool variantToValidLux(const QVariant &value, double *lux)
{
    bool ok = false;
    const double converted = value.toDouble(&ok);
    if (!ok || !std::isfinite(converted) || converted < 0.0)
        return false;
    *lux = converted;
    return true;
}

/// 判定 LookupAmbientLightSensor 的异步回复是否代表"本机有可用光感"。
bool sensorLookupSucceeded(const QDBusPendingReply<QDBusObjectPath> &reply)
{
    if (!reply.isError())
        return !reply.value().path().isEmpty();

    // 服务不存在（未注册）时视为不支持；方法缺失等其它错误回退为支持，
    // 由 connectSensor() 的 HasAmbientLight / 单位校验做最终判定。
    const QString errorName = reply.error().name();
    if (errorName == QLatin1String("org.freedesktop.DBus.Error.ServiceUnknown")
        || errorName == QLatin1String("org.freedesktop.DBus.Error.NameHasNoOwner")) {
        return false;
    }
    return true;
}

}  // namespace

AmbientBrightnessService::AmbientBrightnessService(QDBusConnection connection, QObject *parent)
    : QObject(parent)
    , m_connection(std::move(connection))
    , m_model(createAmbientBrightnessPolicy(BrightnessAlgorithm::Continuous))
{
    connect(&m_model, &AmbientBrightnessModel::supportedChanged, this, [this](bool value) {
        Q_EMIT supportedChanged(value);
        publishPropertyChange(QStringLiteral("Supported"), value);
    });
    connect(&m_model, &AmbientBrightnessModel::stateChanged, this, [this](const QString &value) {
        Q_EMIT stateChanged(value);
        publishPropertyChange(QStringLiteral("State"), value);
    });
    connect(&m_model, &AmbientBrightnessModel::recommendedBrightnessChanged, this,
            [this](double value) {
                Q_EMIT recommendedBrightnessChanged(value);
                publishPropertyChange(QStringLiteral("RecommendedBrightness"), value);
            });
    m_monotonicClock.start();
}

AmbientBrightnessService::~AmbientBrightnessService()
{
    disconnectSensor();
}

bool AmbientBrightnessService::initialize()
{
    initAlgorithmConfig();
    initRuntimeControl();
    if (!m_connection.registerObject(QString::fromLatin1(kObjectPath), this,
                                     QDBusConnection::ExportAllProperties
                                         | QDBusConnection::ExportAllSignals
                                         | QDBusConnection::ExportAllSlots)) {
        qCWarning(logAmbientBrightness) << "Failed to register D-Bus object"
                                        << m_connection.lastError().message();
        return false;
    }

    m_watcher = new QDBusServiceWatcher(QString::fromLatin1(kSensorService),
                                        QDBusConnection::systemBus(),
                                        QDBusServiceWatcher::WatchForRegistration
                                            | QDBusServiceWatcher::WatchForUnregistration,
                                        this);
    connect(m_watcher, &QDBusServiceWatcher::serviceRegistered,
            this, &AmbientBrightnessService::onSensorServiceRegistered);
    connect(m_watcher, &QDBusServiceWatcher::serviceUnregistered,
            this, &AmbientBrightnessService::onSensorServiceUnregistered);

    start();
    return true;
}

void AmbientBrightnessService::start()
{
    m_runtimeReady = true;
    refreshSensorCapability();
}

void AmbientBrightnessService::onSensorServiceRegistered()
{
    refreshSensorCapability();
}

void AmbientBrightnessService::onSensorServiceUnregistered()
{
    // 传感器服务退出后需要重新探测：Supported 必须随之变为 false 并对外广播。
    refreshSensorCapability();
}

void AmbientBrightnessService::onLidClosed()
{
    qCDebug(logAmbientBrightness) << "lid closed; suspending sensor";
    m_lifecycle.lidClosed = true;
    refreshSensorConnection();
}

void AmbientBrightnessService::onLidOpened()
{
    const int delayMs = darkenDebounceDelayMs();
    qCDebug(logAmbientBrightness) << "lid opened; reconnect delay=" << delayMs << "ms";
    m_lifecycle.lidClosed = false;
    // 开盖后的 lux 可能持续抖动；复用变暗防抖时间，在此期间不重新 Claim，也不计算推荐亮度。
    scheduleDelayedRefresh(delayMs);
}

int AmbientBrightnessService::darkenDebounceDelayMs() const
{
    double delayMs = ContinuousPolicyConfig{}.darkenDebounceMs;
    if (m_config) {
        bool ok = false;
        const double configured =
            m_config->value(QString::fromLatin1(kDarkenDebounceMsKey)).toDouble(&ok);
        if (ok && std::isfinite(configured) && configured >= 0.0)
            delayMs = configured;
    }
    return static_cast<int>(
        std::min(std::ceil(delayMs), static_cast<double>(std::numeric_limits<int>::max())));
}

void AmbientBrightnessService::scheduleDelayedRefresh(int delayMs)
{
    if (!m_wakeRefreshTimer) {
        m_wakeRefreshTimer = new QTimer(this);
        m_wakeRefreshTimer->setSingleShot(true);
        connect(m_wakeRefreshTimer, &QTimer::timeout, this, [this]() {
            refreshSensorConnection();
        });
    }
    if (m_wakeRefreshTimer->isActive() && m_wakeRefreshTimer->remainingTime() >= delayMs)
        return;
    // 多个恢复事件可能连续到达，后续事件不能缩短已经安排的稳定等待时间。
    m_wakeRefreshTimer->start(delayMs);
}

void AmbientBrightnessService::onPrepareForSleep(bool beforeSleep)
{
    qCDebug(logAmbientBrightness) << "prepare for sleep=" << beforeSleep;
    m_lifecycle.sleeping = beforeSleep;
    if (!beforeSleep) {
        scheduleDelayedRefresh(kRuntimeRecoveryRefreshDelayMs);
    } else {
        refreshSensorConnection();
    }
}

void AmbientBrightnessService::onSessionActiveChanged(bool active)
{
    qCDebug(logAmbientBrightness) << "session active=" << active;
    m_lifecycle.sessionActive = active;
    if (!active)
        refreshSensorConnection();
    else
        scheduleDelayedRefresh(kRuntimeRecoveryRefreshDelayMs);
}

void AmbientBrightnessService::onAutomaticBrightnessEnabledChanged(bool enabled)
{
    if (m_lifecycle.enabled == enabled)
        return;
    qCDebug(logAmbientBrightness) << "configured enabled=" << enabled;
    m_lifecycle.enabled = enabled;
    Q_EMIT enabledChanged(enabled);
    publishPropertyChange(QStringLiteral("Enabled"), enabled);
    if (!enabled)
        stopSampling();
    else
        refreshSensorCapability();
}

void AmbientBrightnessService::Enable(bool active)
{
    qCDebug(logAmbientBrightness) << "enable requested=" << active;
    m_lifecycle.enabled = active;
    Q_EMIT enabledChanged(active);
    publishPropertyChange(QStringLiteral("Enabled"), active);
    if (m_config && m_config->isValid())
        m_config->setValue(QString::fromLatin1(kAmbientLightAdjustBrightnessKey), active);

    if (!active) {
        stopSampling();
    } else {
        refreshSensorCapability();
    }
}

void AmbientBrightnessService::onPropertiesChanged(const QString &interface,
                                                    const QVariantMap &changed,
                                                    const QStringList &)
{
    if ((!m_claimed && !m_claimInProgress) || !m_lifecycle.shouldRun())
        return;
    if (interface != QLatin1String(kSensorInterface))
        return;
    const auto it = changed.constFind(QStringLiteral("LightLevel"));
    if (it == changed.cend())
        return;
    double lux = 0.0;
    if (!variantToValidLux(*it, &lux)) {
        qCWarning(logAmbientBrightness) << "invalid sensor LightLevel=" << *it;
        return;
    }
    if (m_claimInProgress) {
        m_pendingInitialLux = lux;
        m_havePendingInitialSample = true;
        return;
    }
    if (m_waitingForInitialSample) {
        stopInitialSampleWait();
    }
    processLux(lux);
}

void AmbientBrightnessService::connectSensor()
{
    disconnectSensor();
    auto systemBus = QDBusConnection::systemBus();
    m_sensor = new QDBusInterface(QString::fromLatin1(kSensorService),
                                  QString::fromLatin1(kSensorPath),
                                  QString::fromLatin1(kSensorInterface),
                                  systemBus, this);
    if (!m_sensor->isValid()) {
        qCWarning(logAmbientBrightness) << "sensor interface invalid";
        delete m_sensor;
        m_sensor = nullptr;
        m_model.setSupported(false);
        return;
    }

    const bool hasAmbientLight = m_sensor->property("HasAmbientLight").toBool();
    const QString unit = m_sensor->property("LightLevelUnit").toString();
    if (!hasAmbientLight || (!unit.isEmpty() && unit != QLatin1String("lux"))) {
        qCWarning(logAmbientBrightness) << "sensor not suitable: hasAmbientLight=" << hasAmbientLight
                                        << "unit=" << unit;
        delete m_sensor;
        m_sensor = nullptr;
        m_model.setSupported(false);
        return;
    }

    // iio-sensor-proxy 的部分驱动会在 ClaimLight 调用期间立即上报首帧，
    // 因此必须先订阅 PropertiesChanged，再执行 ClaimLight。
    const bool signalConnected = systemBus.connect(
        QString::fromLatin1(kSensorService), QString::fromLatin1(kSensorPath),
        QString::fromLatin1(kPropertiesInterface), QStringLiteral("PropertiesChanged"),
        this, SLOT(onPropertiesChanged(QString,QVariantMap,QStringList)));
    if (!signalConnected) {
        qCWarning(logAmbientBrightness) << "failed to subscribe to sensor PropertiesChanged"
                                        << systemBus.lastError().message();
        delete m_sensor;
        m_sensor = nullptr;
        m_model.makeUnavailable();
        return;
    }

    m_waitingForInitialSample = true;
    m_claimInProgress = true;
    m_havePendingInitialSample = false;
    const QDBusReply<void> claimReply = m_sensor->call(QStringLiteral("ClaimLight"));
    m_claimInProgress = false;
    if (!claimReply.isValid()) {
        qCWarning(logAmbientBrightness) << "ClaimLight failed" << claimReply.error().message();
        stopInitialSampleWait();
        systemBus.disconnect(QString::fromLatin1(kSensorService),
                             QString::fromLatin1(kSensorPath),
                             QString::fromLatin1(kPropertiesInterface),
                             QStringLiteral("PropertiesChanged"), this,
                             SLOT(onPropertiesChanged(QString,QVariantMap,QStringList)));
        delete m_sensor;
        m_sensor = nullptr;
        m_model.makeUnavailable();
        return;
    }
    m_claimed = true;
    m_haveSample = false;
    m_model.waitForSample();
    qCDebug(logAmbientBrightness) << "sensor claimed: unit=" << unit
                                 << "pending initial sample=" << m_havePendingInitialSample;
    if (m_havePendingInitialSample) {
        const double pendingLux = m_pendingInitialLux;
        stopInitialSampleWait();
        processLux(pendingLux);
    } else {
        startInitialSampleTimeout();
    }
}

void AmbientBrightnessService::disconnectSensor()
{
    if (m_sensor || m_claimed)
        qCDebug(logAmbientBrightness) << "disconnecting sensor: claimed=" << m_claimed;
    QDBusConnection::systemBus().disconnect(QString::fromLatin1(kSensorService),
                                            QString::fromLatin1(kSensorPath),
                                            QString::fromLatin1(kPropertiesInterface),
                                            QStringLiteral("PropertiesChanged"), this,
                                            SLOT(onPropertiesChanged(QString,QVariantMap,QStringList)));
    if (m_sensor && m_claimed)
        m_sensor->call(QStringLiteral("ReleaseLight"));
    m_claimed = false;
    m_claimInProgress = false;
    stopInitialSampleWait();
    delete m_sensor;
    m_sensor = nullptr;
    if (m_evaluationTimer)
        m_evaluationTimer->stop();
    m_haveSample = false;
    // 采样状态由 Supported 与 State 分别表达，这里只负责断开信号与释放 Claim。
}

void AmbientBrightnessService::initRuntimeControl()
{
    auto systemBus = QDBusConnection::systemBus();
    systemBus.connect(QString::fromLatin1(kPowerService),
                      QString::fromLatin1(kPowerPath),
                      QString::fromLatin1(kPowerInterface),
                      QStringLiteral("LidClosed"), this, SLOT(onLidClosed()));
    systemBus.connect(QString::fromLatin1(kPowerService),
                      QString::fromLatin1(kPowerPath),
                      QString::fromLatin1(kPowerInterface),
                      QStringLiteral("LidOpened"), this, SLOT(onLidOpened()));

    // login1: PrepareForSleep (system bus)
    constexpr auto kLogin1Service = "org.freedesktop.login1";
    constexpr auto kLogin1Path = "/org/freedesktop/login1";
    constexpr auto kLogin1Manager = "org.freedesktop.login1.Manager";
    systemBus.connect(QString::fromLatin1(kLogin1Service),
                      QString::fromLatin1(kLogin1Path),
                      QString::fromLatin1(kLogin1Manager),
                      QStringLiteral("PrepareForSleep"), this,
                      SLOT(onPrepareForSleep(bool)));

    // login1: Session.Active (system bus)
    initLogin1Session();

    QDBusInterface power(QString::fromLatin1(kPowerService),
                         QString::fromLatin1(kPowerPath),
                         QString::fromLatin1(kPowerInterface), systemBus);
    if (power.isValid()) {
        const QVariant lidClosed = power.property("LidClosed");
        if (lidClosed.isValid() && lidClosed.canConvert<bool>())
            m_lifecycle.lidClosed = lidClosed.toBool();
    }

}

void AmbientBrightnessService::initLogin1Session()
{
    constexpr auto kLogin1Service = "org.freedesktop.login1";
    constexpr auto kLogin1Path = "/org/freedesktop/login1";
    constexpr auto kLogin1Manager = "org.freedesktop.login1.Manager";

    const QString sessionId = qEnvironmentVariable("XDG_SESSION_ID");
    if (sessionId.isEmpty()) {
        qCWarning(logAmbientBrightness)
            << "XDG_SESSION_ID empty; skipping login1 Session.Active monitoring";
        return;
    }

    QDBusInterface manager(QString::fromLatin1(kLogin1Service),
                           QString::fromLatin1(kLogin1Path),
                           QString::fromLatin1(kLogin1Manager),
                           QDBusConnection::systemBus());
    QDBusReply<QDBusObjectPath> reply =
        manager.call(QStringLiteral("GetSession"), sessionId);
    if (!reply.isValid()) {
        qCWarning(logAmbientBrightness)
            << "GetSession failed for" << sessionId
            << reply.error().message();
        return;
    }
    m_login1SessionPath = reply.value().path();

    // 读取当前 Session.Active
    QDBusInterface session(QString::fromLatin1(kLogin1Service),
                           m_login1SessionPath,
                           QStringLiteral("org.freedesktop.DBus.Properties"),
                           QDBusConnection::systemBus());
    QDBusReply<QVariant> activeReply = session.call(
        QStringLiteral("Get"),
        QStringLiteral("org.freedesktop.login1.Session"),
        QStringLiteral("Active"));
    if (activeReply.isValid() && activeReply.value().canConvert<bool>()) {
        m_lifecycle.sessionActive = activeReply.value().toBool();
    }

    // 监听 PropertiesChanged
    auto systemBus = QDBusConnection::systemBus();
    systemBus.connect(
        QString::fromLatin1(kLogin1Service), m_login1SessionPath,
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("PropertiesChanged"), this,
        SLOT(onSessionPropertiesChanged(QString,QVariantMap,QStringList)));
}

void AmbientBrightnessService::onSessionPropertiesChanged(
    const QString &interface, const QVariantMap &changed, const QStringList &)
{
    if (interface != QLatin1String("org.freedesktop.login1.Session"))
        return;
    const auto it = changed.constFind(QStringLiteral("Active"));
    if (it == changed.cend())
        return;
    onSessionActiveChanged(it->toBool());
}

void AmbientBrightnessService::refreshSensorCapability()
{
    // 能力探测完全异步：refreshSensorConnection() 会在合盖、休眠、会话切换、
    // 开关切换等热路径上被调用，任何同步 D-Bus 往返都可能卡住主线程。
    QDBusPendingCall call = QDBusConnection::systemBus().asyncCall(
        QDBusMessage::createMethodCall(QString::fromLatin1(kSensorService),
                                       QString::fromLatin1(kSensorPath),
                                       QString::fromLatin1(kSensorInterface),
                                       QStringLiteral("LookupAmbientLightSensor")));
    auto *watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this](QDBusPendingCallWatcher *self) {
                self->deleteLater();
                handleSensorPresentChanged(sensorLookupSucceeded(*self));
            });
}

void AmbientBrightnessService::handleSensorPresentChanged(bool present)
{
    m_present = present;
    m_model.setSupported(present);

    if (!present) {
        if (m_sensor || m_claimed)
            disconnectSensor();
        m_model.makeUnavailable();
        return;
    }
    if (!m_lifecycle.shouldRun()) {
        // 硬件存在但不该采样：Supported 保持 true，只同步 State。
        if (m_sensor || m_claimed)
            disconnectSensor();
        m_model.setDisabled();
        return;
    }
    // 直接启动，不经 refreshSensorConnection()，避免探测→刷新→探测的自我循环。
    if (!m_claimed)
        connectSensor();
}

void AmbientBrightnessService::stopSampling()
{
    // 只停采样并同步 State；Supported 仅由能力探测改写，关闭开关不得把它清成 false。
    disconnectSensor();
    if (m_model.supported())
        m_model.setDisabled();
    else
        m_model.makeUnavailable();
}

void AmbientBrightnessService::refreshSensorConnection()
{
    if (!m_runtimeReady)
        return;

    // 只读缓存的能力结论；探测由 initialize/服务上下线/开关切换触发，
    // 这里绝不同步访问 D-Bus，也不会再次触发探测（否则会自激）。

    if (!m_present) {
        if (m_sensor || m_claimed)
            disconnectSensor();
        m_model.makeUnavailable();
        return;
    }

    // 硬件存在但不该采样（用户关闭、合盖、休眠、会话非前台）：Supported 保持 true。
    if (!m_lifecycle.shouldRun()) {
        if (m_sensor || m_claimed)
            disconnectSensor();
        m_model.setDisabled();
        return;
    }

    if (m_claimed)
        return;

    connectSensor();
}

void AmbientBrightnessService::startInitialSampleTimeout()
{
    if (!m_initialSampleTimer) {
        m_initialSampleTimer = new QTimer(this);
        m_initialSampleTimer->setSingleShot(true);
        connect(m_initialSampleTimer, &QTimer::timeout, this,
                &AmbientBrightnessService::onInitialSampleTimeout);
    }
    m_initialSampleTimer->start(kInitialSampleTimeoutMs);
}

void AmbientBrightnessService::stopInitialSampleWait()
{
    m_waitingForInitialSample = false;
    m_havePendingInitialSample = false;
    if (m_initialSampleTimer)
        m_initialSampleTimer->stop();
}

void AmbientBrightnessService::onInitialSampleTimeout()
{
    if (!m_waitingForInitialSample || !m_claimed || !m_sensor
        || !m_lifecycle.shouldRun()) {
        return;
    }

    double lux = 0.0;
    const QVariant lightLevel = m_sensor->property("LightLevel");
    if (!variantToValidLux(lightLevel, &lux)) {
        qCWarning(logAmbientBrightness)
            << "no post-claim LightLevel signal and delayed property read failed"
            << m_sensor->lastError().message();
        return;
    }

    stopInitialSampleWait();
    processLux(lux);
}

void AmbientBrightnessService::processLux(double lux)
{
    if (!std::isfinite(lux) || lux < 0.0) {
        qCWarning(logAmbientBrightness) << "ignoring invalid lux=" << lux;
        return;
    }
    m_lastLux = lux;
    m_haveSample = true;
    m_model.submitSample(lux, static_cast<double>(m_monotonicClock.elapsed()));

    const double delayMs = m_model.nextEvaluationDelayMs();
    if (delayMs > 0.0)
        armEvaluationTimer(delayMs);
    else if (m_evaluationTimer)
        m_evaluationTimer->stop();
}


void AmbientBrightnessService::armEvaluationTimer(double delayMs)
{
    if (!m_evaluationTimer) {
        m_evaluationTimer = new QTimer(this);
        m_evaluationTimer->setSingleShot(true);
        connect(m_evaluationTimer, &QTimer::timeout, this,
                &AmbientBrightnessService::onEvaluationTimerElapsed);
    }
    m_evaluationTimer->start(std::max(1, static_cast<int>(std::ceil(delayMs))));
}

void AmbientBrightnessService::onEvaluationTimerElapsed()
{
    if (!m_claimed || !m_lifecycle.shouldRun())
        return;
    const double now = static_cast<double>(m_monotonicClock.elapsed());
    m_model.tick(now);

    const double delayMs = m_model.nextEvaluationDelayMs();
    if (delayMs > 0.0)
        armEvaluationTimer(delayMs);
    else if (m_evaluationTimer)
        m_evaluationTimer->stop();
}

void AmbientBrightnessService::publishPropertyChange(const QString &name, const QVariant &value)
{
    QVariantMap changed{{name, value}};
    auto signal = QDBusMessage::createSignal(QString::fromLatin1(kObjectPath),
                                             QString::fromLatin1(kPropertiesInterface),
                                             QStringLiteral("PropertiesChanged"));
    signal << QStringLiteral("org.deepin.dde.AmbientBrightness1") << changed << QStringList{};
    m_connection.send(signal);
}

void AmbientBrightnessService::initAlgorithmConfig()
{
    m_config = Dtk::Core::DConfig::create(QString::fromLatin1(kConfigAppId),
                                          QString::fromLatin1(kConfigName), {}, this);
    if (!m_config) {
        qCWarning(logAmbientBrightness) << "failed to create algorithm DConfig; using continuous";
        return;
    }

    // 冷启动时开关状态只从 DConfig 读出，必须与运行中切换一样对外广播一次。
    const QVariant enabled = m_config->value(
        QString::fromLatin1(kAmbientLightAdjustBrightnessKey));
    if (enabled.isValid() && enabled.canConvert<bool>()
        && m_lifecycle.enabled != enabled.toBool()) {
        m_lifecycle.enabled = enabled.toBool();
        Q_EMIT enabledChanged(m_lifecycle.enabled);
        publishPropertyChange(QStringLiteral("Enabled"), m_lifecycle.enabled);
    }

    rebuildCurrentPolicy();
    connect(m_config, &Dtk::Core::DConfig::valueChanged, this, [this](const QString &key) {
        if (key == QLatin1String(kAmbientLightAdjustBrightnessKey)) {
            onAutomaticBrightnessEnabledChanged(
                m_config->value(key).toBool());
        } else if (key == QLatin1String(kContinuousMappingModeKey)
                   || key == QLatin1String(kUseWeightedWindowsKey)
                   || key == QLatin1String(kAmbientLightHorizonMsKey)
                   || key == QLatin1String(kFastLightHorizonMsKey)
                   || key == QLatin1String(kContinuousLuxCurveKey)
                   || key == QLatin1String(kStepHysteresisRatioKey)
                   || key == QLatin1String(kBrightenDebounceMsKey)
                   || key == QLatin1String(kDarkenDebounceMsKey)) {
            rebuildCurrentPolicy();
        }
    });
}

void AmbientBrightnessService::rebuildCurrentPolicy()
{
    if (m_evaluationTimer)
        m_evaluationTimer->stop();

    auto policy = createAmbientBrightnessPolicy(BrightnessAlgorithm::Continuous, m_config);
    m_model.setPolicy(std::move(policy));
    if (!m_claimed)
        return;
    if (m_haveSample)
        processLux(m_lastLux);
    else
        m_model.waitForSample();
}

} // namespace dde::ambient_brightness
