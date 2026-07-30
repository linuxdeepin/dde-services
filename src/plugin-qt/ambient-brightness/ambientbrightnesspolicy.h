// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <optional>

namespace dde::ambient_brightness {

struct SensorSample {
    double lux = 0.0;
    double monotonicTimestampMs = 0.0;
    enum class Source { RealSensor, HeldEvaluation } source = Source::RealSensor;
};

struct Recommendation {
    double rawLux = 0.0;
    double fastLux = 0.0;
    double slowLux = 0.0;
    double stableLux = 0.0;
    double brightness = 0.0;
};

class AmbientBrightnessPolicy {
public:
    virtual ~AmbientBrightnessPolicy() = default;

    virtual std::optional<Recommendation> update(const SensorSample &sample) = 0;

    /// 无新传感器事件时的时间推进;只做候选确认/超时,不新增样本。
    virtual std::optional<Recommendation> tick(double monotonicTimestampMs) = 0;
    virtual void reset() = 0;
    virtual double nextEvaluationDelayMs() const = 0;
};

} // namespace dde::ambient_brightness
