// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "ambientbrightnessmodel.h"
#include "ambientbrightnesspolicyfactory.h"
#include "ambientlightlifecyclestate.h"

#include <QDBusConnection>
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>

class QDBusInterface;
class QDBusServiceWatcher;

namespace Dtk::Core {
class DConfig;
}

namespace dde::ambient_brightness {

/// D-Bus 服务:连接 iio-sensor-proxy,驱动 AmbientBrightnessModel,
/// 对外暴露 org.deepin.dde.AmbientBrightness1 接口。
class AmbientBrightnessService : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.deepin.dde.AmbientBrightness1")
    Q_PROPERTY(bool Supported READ supported NOTIFY supportedChanged)
    Q_PROPERTY(QString State READ state NOTIFY stateChanged)
    Q_PROPERTY(bool Enabled READ enabled NOTIFY enabledChanged)
    Q_PROPERTY(double RecommendedBrightness READ recommendedBrightness NOTIFY recommendedBrightnessChanged)

public:
    explicit AmbientBrightnessService(QDBusConnection connection, QObject *parent = nullptr);
    ~AmbientBrightnessService() override;

    bool initialize();

    bool supported() const { return m_model.supported(); }
    QString state() const { return m_model.state(); }
    bool enabled() const { return m_lifecycle.enabled; }
    double recommendedBrightness() const { return m_model.recommendedBrightness(); }

public Q_SLOTS:
    void Enable(bool active);

Q_SIGNALS:
    void supportedChanged(bool value);
    void stateChanged(const QString &value);
    void enabledChanged(bool value);
    void recommendedBrightnessChanged(double value);

public Q_SLOTS:
    /// 启动运行时逻辑。与 initialize() 分开，便于测试在没有系统总线时构造服务。
    void start();
    /// 异步能力探测的回调入口。
    void handleSensorPresentChanged(bool present);

private Q_SLOTS:
    void onSensorServiceRegistered();
    void onSensorServiceUnregistered();
    void onLidClosed();
    void onLidOpened();
    void onPrepareForSleep(bool beforeSleep);
    void onSessionActiveChanged(bool active);
    void onSessionPropertiesChanged(const QString &interface,
                                    const QVariantMap &changed,
                                    const QStringList &);
    void onAutomaticBrightnessEnabledChanged(bool enabled);
    void onPropertiesChanged(const QString &interface,
                             const QVariantMap &changed,
                             const QStringList &invalidated);

private:
    void stopSampling();
    void connectSensor();
    void refreshSensorCapability();
    void disconnectSensor();
    void initRuntimeControl();
    void initLogin1Session();
    int darkenDebounceDelayMs() const;
    void scheduleDelayedRefresh(int delayMs);
    void refreshSensorConnection();
    void startInitialSampleTimeout();
    void stopInitialSampleWait();
    void onInitialSampleTimeout();
    void processLux(double lux);
    void publishPropertyChange(const QString &name, const QVariant &value);
    void armEvaluationTimer(double delayMs);
    void onEvaluationTimerElapsed();
    void initAlgorithmConfig();
    void rebuildCurrentPolicy();

    /// 最近一次异步能力探测的结果。只在 handleSensorPresentChanged() 里写入。
    bool m_present = false;

    QDBusConnection m_connection;
    QDBusInterface *m_sensor = nullptr;
    QDBusServiceWatcher *m_watcher = nullptr;
    Dtk::Core::DConfig *m_config = nullptr;
    AmbientBrightnessModel m_model;
    QElapsedTimer m_monotonicClock;
    bool m_claimed = false;
    QTimer *m_evaluationTimer = nullptr;
    double m_lastLux = 0.0;
    bool m_haveSample = false;
    AmbientLightLifecycleState m_lifecycle;
    bool m_runtimeReady = false;
    QString m_login1SessionPath;
    QTimer *m_wakeRefreshTimer = nullptr;
    QTimer *m_initialSampleTimer = nullptr;
    bool m_waitingForInitialSample = false;
    bool m_claimInProgress = false;
    bool m_havePendingInitialSample = false;
    double m_pendingInitialLux = 0.0;
};

} // namespace dde::ambient_brightness
