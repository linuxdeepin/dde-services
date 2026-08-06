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

Q_SIGNALS:
    void supportedChanged(bool value);
    void stateChanged(const QString &value);
    void recommendedBrightnessChanged(double value);

private:
    void setSupported(bool value);
    void setState(const QString &value);

    std::unique_ptr<AmbientBrightnessPolicy> m_policy;
    bool m_supported = false;
    QString m_state = QStringLiteral("Unavailable");
    double m_recommendedBrightness = 0.0;
    bool m_haveRecommendation = false;
};

} // namespace dde::ambient_brightness
