// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "ambientbrightnesslogging.h"
#include "ambientbrightnesspolicy.h"

#include <QObject>
#include <QString>

#include <memory>

namespace dde::ambient_brightness {

/// Qt 适配层:把可替换的纯算法策略包装成 QObject,
/// 对外暴露 D-Bus 可订阅的属性和信号。
class AmbientBrightnessModel : public QObject
{
    Q_OBJECT

public:
    explicit AmbientBrightnessModel(std::unique_ptr<AmbientBrightnessPolicy> policy,
                                    QObject *parent = nullptr);
    /// 本机是否具备环境光传感器（硬件能力）。与开关是否开启、当前是否在采样无关。
    bool supported() const { return m_supported; }

    QString state() const { return m_state; }

    double recommendedBrightness() const { return m_recommendedBrightness; }

    double nextEvaluationDelayMs() const { return m_policy->nextEvaluationDelayMs(); }

    void waitForSample();
    void makeUnavailable();
    void setDisabled();
    void submitSample(double lux,
                      double monotonicTimestampMs,
                      SensorSample::Source source = SensorSample::Source::RealSensor);
    void tick(double monotonicTimestampMs);
    void setPolicy(std::unique_ptr<AmbientBrightnessPolicy> policy);

    /// 由 Service 依据能力探测结果写入。关闭开关或暂停采样不得清掉它；
    /// 只有确认本机没有可用传感器时才置为 false。
    void setSupported(bool value);

Q_SIGNALS:
    void supportedChanged(bool value);
    void stateChanged(const QString &value);
    void recommendedBrightnessChanged(double value);

private:
    void setState(const QString &value);

    std::unique_ptr<AmbientBrightnessPolicy> m_policy;
    bool m_supported = false;
    QString m_state = QStringLiteral("Unavailable");
    double m_recommendedBrightness = 0.0;
    bool m_haveRecommendation = false;
};

} // namespace dde::ambient_brightness
