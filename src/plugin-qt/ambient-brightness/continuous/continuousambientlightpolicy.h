// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "../ambientbrightnesspolicy.h"
#include "brightnesscurve.h"

#include <cstddef>
#include <limits>
#include <optional>
#include <vector>

namespace dde::ambient_brightness {

/// 档位模式的 lux-亮度映射点。
struct LuxStep
{
    double lux = 0.0;
    double brightness = 0.0;
};

struct ContinuousPolicyConfig
{
    double maxSensorLux = 100000.0;
    double ambientLightHorizonMs = 10000.0;
    double fastLightHorizonMs = 1000.0;
    double weightingIntercept = 10000.0;

    double brightenHysteresisRatio = 0.15;
    double darkenHysteresisRatio = 0.15;
    double minimumHysteresisLux = 5.0;

    double brightenDebounceMs = 2000.0;
    double darkenDebounceMs = 2000.0;

    double minimumRecommendationDelta = 0.02;

    bool useWeightedWindows = false;

    // 映射模式：steps = 档位输出，curve = log1p 插值
    bool useStepsMode = true;

    // 档位模式参数。为空时由 curve 自动生成，保证内置回退配置始终可用。
    std::vector<LuxStep> luxSteps;
    double stepHysteresisRatio = 0.6;

    std::vector<CurvePoint> curve = { { 20.0, 0.20 },  { 120.0, 0.40 }, { 220.0, 0.60 },
                                      { 320.0, 0.80 }, { 650.0, 0.90 }, { 2000.0, 1.00 } };
};

/// AOSP AutomaticBrightnessController 风格的连续策略。
/// 使用时间加权 fast/slow ambient lux、亮暗滞回和最早连续越界时间 debounce。
class ContinuousAmbientLightPolicy final : public AmbientBrightnessPolicy
{
public:
    explicit ContinuousAmbientLightPolicy(const ContinuousPolicyConfig &config);

    std::optional<Recommendation> update(const SensorSample &sample) override;
    std::optional<Recommendation> tick(double monotonicTimestampMs) override;
    void reset() override;
    bool setConfig(const ContinuousPolicyConfig &config);
    double nextEvaluationDelayMs() const override;

    static bool isValidConfig(const ContinuousPolicyConfig &config);
    static bool isValidLuxSteps(const std::vector<LuxStep> &steps);

private:
    struct AmbientLightRingBuffer
    {
        explicit AmbientLightRingBuffer(size_t capacity);

        void clear();
        size_t size() const;
        double luxAt(size_t index) const;
        double timeAt(size_t index) const;
        void push(double timeMs, double lux);
        void prune(double minTimeMs);

        size_t m_capacity = 1;
        size_t m_count = 0;
        size_t m_start = 0;
        std::vector<double> m_lux;
        std::vector<double> m_time;
    };

    std::optional<Recommendation> evaluate(double monotonicTimestampMs);
    double calculateWeightedAmbientLux(double nowMs, double horizonMs) const;
    double weightIntegral(double x) const;
    double mapLuxToBrightness(double lux) const;
    double brighteningThresholdLux() const;
    double darkeningThresholdLux() const;
    double nextBrighteningTransitionMs(double nowMs) const;
    double nextDarkeningTransitionMs(double nowMs) const;
    bool latestSampleSupportsBrightening() const;
    bool latestSampleSupportsDarkening() const;

    // 档位模式相关
    struct StepThresholds
    {
        double increaseThreshold = std::numeric_limits<double>::infinity();
        double decreaseThreshold = 0.0;
    };

    size_t findStepForLux(double lux) const;
    size_t findTargetStepForBrightening(double lux) const;
    size_t findTargetStepForDarkening(double lux) const;
    void computeStepThresholds();

    std::vector<LuxStep> m_steps;
    std::vector<StepThresholds> m_stepThresholds;
    double m_stepHysteresisRatio = 0.6;
    size_t m_currentStepIndex = 0;
    bool m_useStepsMode = true;

    BrightnessCurve m_curve;
    AmbientLightRingBuffer m_buffer;

    double m_maxSensorLux = 0.0;
    double m_ambientLightHorizonMs = 0.0;
    double m_fastLightHorizonMs = 0.0;
    double m_weightingIntercept = 1.0;
    double m_brightenHysteresisRatio = 0.0;
    double m_darkenHysteresisRatio = 0.0;
    double m_minimumHysteresisLux = 0.0;
    double m_brightenDebounceMs = 0.0;
    double m_darkenDebounceMs = 0.0;
    double m_minimumRecommendationDelta = 0.0;
    bool m_useWeightedWindows = false;

    bool m_initialized = false;
    double m_lastTimestampMs = -1.0;
    double m_lastEvaluationTimestampMs = -1.0;
    double m_fastAmbientLux = 0.0;
    double m_slowAmbientLux = 0.0;
    double m_stableLux = 0.0;
    std::optional<double> m_lastPublishedBrightness;
};

} // namespace dde::ambient_brightness
