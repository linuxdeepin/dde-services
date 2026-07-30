// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "ambientbrightnessservice.h"
#include "ambientbrightnesslogging.h"
#include "ambientbrightnesspolicyfactory.h"
#include "continuous/continuousambientlightpolicy.h"


#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
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

    m_runtimeReady = true;
    refreshSensorConnection();
    return true;
}

void AmbientBrightnessService::onSensorServiceRegistered()
{
    refreshSensorConnection();
}

void AmbientBrightnessService::onSensorServiceUnregistered()
{
    disconnectSensor();
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
    if (!enabled) {
        stopInitialSampleWait();
        if (m_evaluationTimer)
            m_evaluationTimer->stop();
        if (m_sensor && m_claimed)
            m_sensor->call(QStringLiteral("ReleaseLight"));
        m_claimed = false;
        m_haveSample = false;
        m_model.setDisabled();
    } else {
        refreshSensorConnection();
    }
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
        // 关闭：释放传感器但保留 Supported=true，状态变为 Disabled
        stopInitialSampleWait();
        if (m_evaluationTimer)
            m_evaluationTimer->stop();
        if (m_sensor && m_claimed)
            m_sensor->call(QStringLiteral("ReleaseLight"));
        m_claimed = false;
        m_haveSample = false;
        m_model.setDisabled();
    } else {
        refreshSensorConnection();
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
        m_model.makeUnavailable();
        return;
    }

    const bool hasAmbientLight = m_sensor->property("HasAmbientLight").toBool();
    const QString unit = m_sensor->property("LightLevelUnit").toString();
    if (!hasAmbientLight || (!unit.isEmpty() && unit != QLatin1String("lux"))) {
        qCWarning(logAmbientBrightness) << "sensor not suitable: hasAmbientLight=" << hasAmbientLight
                                        << "unit=" << unit;
        delete m_sensor;
        m_sensor = nullptr;
        m_model.makeUnavailable();
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
    m_model.makeUnavailable();
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

void AmbientBrightnessService::refreshSensorConnection()
{
    if (!m_runtimeReady)
        return;

    if (!m_lifecycle.shouldRun()) {
        if (m_sensor || m_claimed)
            disconnectSensor();
        else
            m_model.makeUnavailable();
        return;
    }

    if (m_claimed)
        return;

    auto *interface = QDBusConnection::systemBus().interface();
    if (!interface) {
        qCWarning(logAmbientBrightness) << "no system bus interface; sensor unavailable";
        m_model.makeUnavailable();
        return;
    }
    const auto registered = interface->isServiceRegistered(QString::fromLatin1(kSensorService));
    if (registered.isValid() && registered.value())
        connectSensor();
    else
        m_model.makeUnavailable();
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

    // 读取光感开关初始值
    const QVariant enabled = m_config->value(
        QString::fromLatin1(kAmbientLightAdjustBrightnessKey));
    if (enabled.isValid() && enabled.canConvert<bool>())
        m_lifecycle.enabled = enabled.toBool();

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
