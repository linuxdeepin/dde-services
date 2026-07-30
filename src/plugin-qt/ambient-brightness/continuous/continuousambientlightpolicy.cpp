// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "continuousambientlightpolicy.h"

#include "ambientbrightnesslogging.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace dde::ambient_brightness {
namespace {
constexpr double kPredictionTimeMs = 100.0;
constexpr double kEvaluationIntervalMs = 100.0;

std::vector<LuxStep> luxStepsFromCurve(const std::vector<CurvePoint> &curve)
{
    std::vector<LuxStep> steps;
    steps.reserve(curve.size());
    for (const auto &point : curve)
        steps.push_back({ point.lux, point.brightness });
    return steps;
}
} // namespace

ContinuousAmbientLightPolicy::AmbientLightRingBuffer::AmbientLightRingBuffer(size_t capacity)
    : m_capacity(capacity > 0 ? capacity : 1)
    , m_lux(m_capacity, 0.0)
    , m_time(m_capacity, 0.0)
{
}

void ContinuousAmbientLightPolicy::AmbientLightRingBuffer::clear()
{
    m_count = 0;
    m_start = 0;
}

size_t ContinuousAmbientLightPolicy::AmbientLightRingBuffer::size() const
{
    return m_count;
}

double ContinuousAmbientLightPolicy::AmbientLightRingBuffer::luxAt(size_t index) const
{
    return m_lux[(m_start + index) % m_capacity];
}

double ContinuousAmbientLightPolicy::AmbientLightRingBuffer::timeAt(size_t index) const
{
    return m_time[(m_start + index) % m_capacity];
}

void ContinuousAmbientLightPolicy::AmbientLightRingBuffer::push(double timeMs, double lux)
{
    if (m_count == m_capacity) {
        const size_t newCapacity = m_capacity * 2;
        std::vector<double> grownLux(newCapacity, 0.0);
        std::vector<double> grownTime(newCapacity, 0.0);
        for (size_t i = 0; i < m_count; ++i) {
            grownLux[i] = luxAt(i);
            grownTime[i] = timeAt(i);
        }
        m_lux = std::move(grownLux);
        m_time = std::move(grownTime);
        m_capacity = newCapacity;
        m_start = 0;
    }

    const size_t pos = (m_start + m_count) % m_capacity;
    m_time[pos] = timeMs;
    m_lux[pos] = lux;
    ++m_count;
}

void ContinuousAmbientLightPolicy::AmbientLightRingBuffer::prune(double minTimeMs)
{
    while (m_count > 1 && timeAt(1) <= minTimeMs) {
        m_start = (m_start + 1) % m_capacity;
        --m_count;
    }
}

ContinuousAmbientLightPolicy::ContinuousAmbientLightPolicy(const ContinuousPolicyConfig &config)
    : m_buffer(1)
{
    setConfig(config);
}

bool ContinuousAmbientLightPolicy::isValidConfig(const ContinuousPolicyConfig &c)
{
    if (!std::isfinite(c.maxSensorLux) || c.maxSensorLux <= 0.0
        || !std::isfinite(c.ambientLightHorizonMs) || c.ambientLightHorizonMs <= 0.0
        || !std::isfinite(c.fastLightHorizonMs) || c.fastLightHorizonMs <= 0.0
        || c.fastLightHorizonMs > c.ambientLightHorizonMs || !std::isfinite(c.weightingIntercept)
        || c.weightingIntercept < c.ambientLightHorizonMs
        || !std::isfinite(c.brightenHysteresisRatio) || c.brightenHysteresisRatio < 0.0
        || !std::isfinite(c.darkenHysteresisRatio) || c.darkenHysteresisRatio < 0.0
        || !std::isfinite(c.minimumHysteresisLux) || c.minimumHysteresisLux < 0.0
        || !std::isfinite(c.brightenDebounceMs) || c.brightenDebounceMs < 0.0
        || !std::isfinite(c.darkenDebounceMs) || c.darkenDebounceMs < 0.0
        || !std::isfinite(c.minimumRecommendationDelta) || c.minimumRecommendationDelta < 0.0
        || c.minimumRecommendationDelta > 1.0) {
        return false;
    }
    if (c.useStepsMode) {
        const bool validMapping =
            c.luxSteps.empty() ? BrightnessCurve::isValid(c.curve) : isValidLuxSteps(c.luxSteps);
        return validMapping && std::isfinite(c.stepHysteresisRatio) && c.stepHysteresisRatio >= 0.5
            && c.stepHysteresisRatio <= 1.0;
    }
    return BrightnessCurve::isValid(c.curve);
}

bool ContinuousAmbientLightPolicy::setConfig(const ContinuousPolicyConfig &config)
{
    if (!isValidConfig(config))
        return false;

    m_curve = BrightnessCurve(config.curve);
    m_maxSensorLux = config.maxSensorLux;
    m_ambientLightHorizonMs = config.ambientLightHorizonMs;
    m_fastLightHorizonMs = config.fastLightHorizonMs;
    m_weightingIntercept = config.weightingIntercept;
    m_brightenHysteresisRatio = config.brightenHysteresisRatio;
    m_darkenHysteresisRatio = config.darkenHysteresisRatio;
    m_minimumHysteresisLux = config.minimumHysteresisLux;
    m_brightenDebounceMs = config.brightenDebounceMs;
    m_darkenDebounceMs = config.darkenDebounceMs;
    m_minimumRecommendationDelta = config.minimumRecommendationDelta;
    m_useWeightedWindows = config.useWeightedWindows;
    m_useStepsMode = config.useStepsMode;
    m_steps = config.luxSteps;
    if (m_useStepsMode && m_steps.empty())
        m_steps = luxStepsFromCurve(config.curve);
    m_stepHysteresisRatio = config.stepHysteresisRatio;
    if (m_useStepsMode)
        computeStepThresholds();
    m_buffer = AmbientLightRingBuffer(
        std::max<size_t>(8, static_cast<size_t>(m_ambientLightHorizonMs / 100.0)));
    reset();
    return true;
}

void ContinuousAmbientLightPolicy::reset()
{
    m_buffer.clear();
    m_initialized = false;
    m_lastTimestampMs = -1.0;
    m_lastEvaluationTimestampMs = -1.0;
    m_fastAmbientLux = 0.0;
    m_slowAmbientLux = 0.0;
    m_stableLux = 0.0;
    m_currentStepIndex = 0;
    m_lastPublishedBrightness.reset();
}

std::optional<Recommendation> ContinuousAmbientLightPolicy::update(const SensorSample &sample)
{
    if (!std::isfinite(sample.lux) || !std::isfinite(sample.monotonicTimestampMs)
        || sample.lux < 0.0
        || (m_initialized && sample.monotonicTimestampMs <= m_lastTimestampMs)) {
        return std::nullopt;
    }

    const double lux = std::min(sample.lux, m_maxSensorLux);
    m_buffer.prune(sample.monotonicTimestampMs - m_ambientLightHorizonMs);
    m_buffer.push(sample.monotonicTimestampMs, lux);
    m_lastTimestampMs = sample.monotonicTimestampMs;
    m_lastEvaluationTimestampMs = sample.monotonicTimestampMs;
    return evaluate(sample.monotonicTimestampMs);
}

std::optional<Recommendation> ContinuousAmbientLightPolicy::tick(double monotonicTimestampMs)
{
    if (!m_initialized || m_buffer.size() == 0 || !std::isfinite(monotonicTimestampMs)
        || monotonicTimestampMs < m_lastEvaluationTimestampMs) {
        return std::nullopt;
    }
    m_lastEvaluationTimestampMs = monotonicTimestampMs;
    m_buffer.prune(monotonicTimestampMs - m_ambientLightHorizonMs);
    return evaluate(monotonicTimestampMs);
}

std::optional<Recommendation> ContinuousAmbientLightPolicy::evaluate(double nowMs)
{
    const double rawLux = m_buffer.size() > 0 ? m_buffer.luxAt(m_buffer.size() - 1) : 0.0;

    if (m_useWeightedWindows) {
        m_fastAmbientLux = calculateWeightedAmbientLux(nowMs, m_fastLightHorizonMs);
        m_slowAmbientLux = calculateWeightedAmbientLux(nowMs, m_ambientLightHorizonMs);
    }

    if (!m_initialized) {
        m_initialized = true;
        m_stableLux = m_useWeightedWindows ? m_fastAmbientLux : rawLux;
        if (m_useStepsMode && !m_steps.empty())
            m_currentStepIndex = findStepForLux(m_stableLux);
        const double brightness = mapLuxToBrightness(m_stableLux);
        m_lastPublishedBrightness = brightness;
        return Recommendation{ rawLux,
                               m_fastAmbientLux,
                               m_slowAmbientLux,
                               m_stableLux,
                               brightness };
    }

    const double brightenTransition = nextBrighteningTransitionMs(nowMs);
    const double darkenTransition = nextDarkeningTransitionMs(nowMs);
    const double triggerLux = m_useWeightedWindows ? m_fastAmbientLux : rawLux;
    const double darkTriggerLux = m_useWeightedWindows ? m_slowAmbientLux : rawLux;
    const bool brightReady = triggerLux >= brighteningThresholdLux() && brightenTransition <= nowMs;
    const bool darkReady = darkTriggerLux <= darkeningThresholdLux()
        && triggerLux <= darkeningThresholdLux() && darkenTransition <= nowMs;

    if (!brightReady && !darkReady)
        return std::nullopt;

    const double previousLux = m_stableLux;
    m_stableLux = triggerLux;
    if (m_useStepsMode && !m_steps.empty()) {
        const double brightenTargetLux =
            m_useWeightedWindows ? (triggerLux > previousLux * 2.0 ? rawLux : triggerLux) : rawLux;
        const double darkenTargetLux =
            m_useWeightedWindows ? (triggerLux < previousLux * 0.5 ? rawLux : triggerLux) : rawLux;
        m_currentStepIndex = brightReady ? findTargetStepForBrightening(brightenTargetLux)
                                         : findTargetStepForDarkening(darkenTargetLux);
    }
    const double brightness = mapLuxToBrightness(m_stableLux);
    if (m_lastPublishedBrightness
        && std::abs(brightness - *m_lastPublishedBrightness) < m_minimumRecommendationDelta) {
        return std::nullopt;
    }

    m_lastPublishedBrightness = brightness;
    return Recommendation{ rawLux, m_fastAmbientLux, m_slowAmbientLux, m_stableLux, brightness };
}

double ContinuousAmbientLightPolicy::nextEvaluationDelayMs() const
{
    if (!m_initialized || m_buffer.size() == 0)
        return 0.0;

    const bool supportsBrightening = latestSampleSupportsBrightening();
    const bool supportsDarkening = latestSampleSupportsDarkening();
    if (!supportsBrightening && !supportsDarkening)
        return 0.0;

    const double nowMs = m_lastEvaluationTimestampMs;
    double next = std::numeric_limits<double>::infinity();
    if (supportsBrightening)
        next = std::min(next, nextBrighteningTransitionMs(nowMs));
    if (supportsDarkening)
        next = std::min(next, nextDarkeningTransitionMs(nowMs));
    if (next <= nowMs)
        return kEvaluationIntervalMs;
    return next - nowMs;
}

double ContinuousAmbientLightPolicy::brighteningThresholdLux() const
{
    if (m_useStepsMode && !m_steps.empty()) {
        if (m_currentStepIndex < m_stepThresholds.size())
            return m_stepThresholds[m_currentStepIndex].increaseThreshold;
        return std::numeric_limits<double>::infinity();
    }
    return m_stableLux + std::max(m_minimumHysteresisLux, m_stableLux * m_brightenHysteresisRatio);
}

double ContinuousAmbientLightPolicy::darkeningThresholdLux() const
{
    if (m_useStepsMode && !m_steps.empty()) {
        if (m_currentStepIndex < m_stepThresholds.size())
            return m_stepThresholds[m_currentStepIndex].decreaseThreshold;
        return 0.0;
    }
    return m_stableLux - std::max(m_minimumHysteresisLux, m_stableLux * m_darkenHysteresisRatio);
}

bool ContinuousAmbientLightPolicy::latestSampleSupportsBrightening() const
{
    if (m_buffer.size() == 0 || (m_useStepsMode && m_currentStepIndex + 1 >= m_steps.size())) {
        return false;
    }
    return m_buffer.luxAt(m_buffer.size() - 1) >= brighteningThresholdLux();
}

bool ContinuousAmbientLightPolicy::latestSampleSupportsDarkening() const
{
    if (m_buffer.size() == 0 || (m_useStepsMode && m_currentStepIndex == 0)) {
        return false;
    }
    return m_buffer.luxAt(m_buffer.size() - 1) <= darkeningThresholdLux();
}

double ContinuousAmbientLightPolicy::nextBrighteningTransitionMs(double nowMs) const
{
    if (!latestSampleSupportsBrightening())
        return nowMs + m_brightenDebounceMs;

    double earliest = nowMs;
    for (size_t i = m_buffer.size(); i-- > 0;) {
        if (m_buffer.luxAt(i) < brighteningThresholdLux())
            break;
        earliest = m_buffer.timeAt(i);
    }
    return earliest + m_brightenDebounceMs;
}

double ContinuousAmbientLightPolicy::nextDarkeningTransitionMs(double nowMs) const
{
    if (!latestSampleSupportsDarkening())
        return nowMs + m_darkenDebounceMs;

    double earliest = nowMs;
    for (size_t i = m_buffer.size(); i-- > 0;) {
        if (m_buffer.luxAt(i) > darkeningThresholdLux())
            break;
        earliest = m_buffer.timeAt(i);
    }
    return earliest + m_darkenDebounceMs;
}

double ContinuousAmbientLightPolicy::calculateWeightedAmbientLux(double nowMs,
                                                                 double horizonMs) const
{
    const size_t n = m_buffer.size();
    if (n == 0)
        return 0.0;

    const double horizonStart = nowMs - horizonMs;
    size_t start = 0;
    for (size_t i = 0; i + 1 < n; ++i) {
        if (m_buffer.timeAt(i + 1) <= horizonStart)
            start = i + 1;
        else
            break;
    }

    double sum = 0.0;
    double totalWeight = 0.0;
    double endDelta = kPredictionTimeMs;
    for (size_t i = n; i > start; --i) {
        const size_t index = i - 1;
        double eventTime = m_buffer.timeAt(index);
        if (index == start && eventTime < horizonStart)
            eventTime = horizonStart;
        const double startDelta = eventTime - nowMs;
        const double weight = weightIntegral(endDelta) - weightIntegral(startDelta);
        sum += m_buffer.luxAt(index) * weight;
        totalWeight += weight;
        endDelta = startDelta;
    }
    return totalWeight > 0.0 ? sum / totalWeight : m_buffer.luxAt(n - 1);
}

double ContinuousAmbientLightPolicy::weightIntegral(double x) const
{
    return x * (x * 0.5 + m_weightingIntercept);
}

double ContinuousAmbientLightPolicy::mapLuxToBrightness(double lux) const
{
    if (m_useStepsMode && !m_steps.empty())
        return m_steps[m_currentStepIndex].brightness;
    return std::round(std::clamp(m_curve.map(lux), 0.0, 1.0) * 100.0) / 100.0;
}

bool ContinuousAmbientLightPolicy::isValidLuxSteps(const std::vector<LuxStep> &steps)
{
    if (steps.size() < 1)
        return false;
    for (size_t i = 0; i < steps.size(); ++i) {
        const auto &s = steps[i];
        if (!std::isfinite(s.lux) || !std::isfinite(s.brightness) || s.lux < 0.0
            || s.brightness < 0.0 || s.brightness > 1.0)
            return false;
        if (i > 0 && (s.lux <= steps[i - 1].lux || s.brightness < steps[i - 1].brightness))
            return false;
    }
    return true;
}

void ContinuousAmbientLightPolicy::computeStepThresholds()
{
    const size_t n = m_steps.size();
    m_stepThresholds.resize(n);
    const double ratio = m_stepHysteresisRatio;

    for (size_t i = 0; i < n; ++i) {
        if (i < n - 1) {
            const double gap = m_steps[i + 1].lux - m_steps[i].lux;
            m_stepThresholds[i].increaseThreshold = m_steps[i].lux + ratio * gap;
        } else {
            m_stepThresholds[i].increaseThreshold = std::numeric_limits<double>::infinity();
        }
        if (i > 0) {
            const double gap = m_steps[i].lux - m_steps[i - 1].lux;
            m_stepThresholds[i].decreaseThreshold = m_steps[i].lux - ratio * gap;
        } else {
            m_stepThresholds[i].decreaseThreshold = 0.0;
        }
    }
}

size_t ContinuousAmbientLightPolicy::findStepForLux(double lux) const
{
    size_t index = 0;
    for (size_t i = 1; i < m_steps.size(); ++i) {
        const double neutralBoundary =
            m_steps[i - 1].lux + 0.5 * (m_steps[i].lux - m_steps[i - 1].lux);
        if (lux >= neutralBoundary)
            index = i;
        else
            break;
    }
    return index;
}

size_t ContinuousAmbientLightPolicy::findTargetStepForBrightening(double lux) const
{
    size_t index = m_currentStepIndex;
    while (index + 1 < m_steps.size() && lux >= m_stepThresholds[index].increaseThreshold)
        ++index;
    return index;
}

size_t ContinuousAmbientLightPolicy::findTargetStepForDarkening(double lux) const
{
    size_t index = m_currentStepIndex;
    while (index > 0 && lux <= m_stepThresholds[index].decreaseThreshold)
        --index;
    return index;
}

} // namespace dde::ambient_brightness
