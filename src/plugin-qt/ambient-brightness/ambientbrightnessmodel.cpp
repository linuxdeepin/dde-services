// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "ambientbrightnessmodel.h"

#include <utility>

namespace dde::ambient_brightness {

AmbientBrightnessModel::AmbientBrightnessModel(std::unique_ptr<AmbientBrightnessPolicy> policy,
                                               QObject *parent)
    : QObject(parent)
    , m_policy(std::move(policy))
{
    Q_ASSERT(m_policy);
}

void AmbientBrightnessModel::waitForSample()
{
    m_policy->reset();
    m_haveRecommendation = false;
    setSupported(true);
    setState(QStringLiteral("WaitingForSample"));
}

void AmbientBrightnessModel::makeUnavailable()
{
    m_policy->reset();
    m_haveRecommendation = false;
    setSupported(false);
    setState(QStringLiteral("Unavailable"));
}

void AmbientBrightnessModel::setDisabled()
{
    m_policy->reset();
    m_haveRecommendation = false;
    setSupported(true);
    setState(QStringLiteral("Disabled"));
}

void AmbientBrightnessModel::submitSample(double lux,
                                          double monotonicTimestampMs,
                                          SensorSample::Source source)
{
    const auto recommendation = m_policy->update({ lux, monotonicTimestampMs, source });
    if (!recommendation) {
        return;
    }


    // +1.0 偏移:qFuzzyCompare 对 0 附近的值不可靠(brightness 合法值为 0),
    // 偏移到 [1,2] 区间使其进入 qFuzzyCompare 的有效比较范围。
    if (!m_haveRecommendation
        || !qFuzzyCompare(m_recommendedBrightness + 1.0, recommendation->brightness + 1.0)) {
        m_recommendedBrightness = recommendation->brightness;
        m_haveRecommendation = true;
        qCDebug(logAmbientBrightness)
            << "recommendation changed: stableLux=" << recommendation->stableLux
            << "brightness=" << m_recommendedBrightness;
        Q_EMIT recommendedBrightnessChanged(m_recommendedBrightness);
    }
    setState(QStringLiteral("Active"));
}

void AmbientBrightnessModel::tick(double monotonicTimestampMs)
{
    const auto recommendation = m_policy->tick(monotonicTimestampMs);
    if (!recommendation)
        return;


    // 同上:+1.0 偏移以正确处理 brightness=0 的边界。
    if (!m_haveRecommendation
        || !qFuzzyCompare(m_recommendedBrightness + 1.0, recommendation->brightness + 1.0)) {
        m_recommendedBrightness = recommendation->brightness;
        m_haveRecommendation = true;
        qCDebug(logAmbientBrightness)
            << "timer recommendation changed: stableLux=" << recommendation->stableLux
            << "brightness=" << m_recommendedBrightness;
        Q_EMIT recommendedBrightnessChanged(m_recommendedBrightness);
    }
    setState(QStringLiteral("Active"));
}

void AmbientBrightnessModel::setPolicy(std::unique_ptr<AmbientBrightnessPolicy> policy)
{
    Q_ASSERT(policy);
    m_policy = std::move(policy);
    m_policy->reset();
    m_haveRecommendation = false;
    setState(m_supported ? QStringLiteral("WaitingForSample") : QStringLiteral("Unavailable"));
}

void AmbientBrightnessModel::setSupported(bool value)
{
    if (m_supported == value)
        return;
    qCDebug(logAmbientBrightness) << "supported changed:" << m_supported << "->" << value;
    m_supported = value;
    Q_EMIT supportedChanged(value);
}

void AmbientBrightnessModel::setState(const QString &value)
{
    if (m_state == value)
        return;
    qCDebug(logAmbientBrightness) << "state changed:" << m_state << "->" << value;
    m_state = value;
    Q_EMIT stateChanged(value);
}

} // namespace dde::ambient_brightness
