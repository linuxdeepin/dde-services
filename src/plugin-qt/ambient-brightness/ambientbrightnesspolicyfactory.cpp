// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "ambientbrightnesslogging.h"
#include "ambientbrightnesspolicyfactory.h"

#include "continuous/continuousambientlightpolicy.h"

#include <DConfig>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariant>

#include <cmath>

namespace dde::ambient_brightness {
namespace {

constexpr auto kContinuousMappingModeKey = "continuousMappingMode";
constexpr auto kUseWeightedWindowsKey = "useWeightedWindows";
constexpr auto kAmbientLightHorizonMsKey = "ambientLightHorizonMs";
constexpr auto kFastLightHorizonMsKey = "fastLightHorizonMs";
constexpr auto kContinuousLuxCurveKey = "continuousLuxCurve";
constexpr auto kStepHysteresisRatioKey = "stepHysteresisRatio";
constexpr auto kBrightenDebounceMsKey = "brightenDebounceMs";
constexpr auto kDarkenDebounceMsKey = "darkenDebounceMs";

std::optional<double> readFiniteDouble(Dtk::Core::DConfig *config, const char *key)
{
    bool ok = false;
    const double value = config->value(QString::fromLatin1(key)).toDouble(&ok);
    if (!ok || !std::isfinite(value)) {
        qCWarning(logAmbientBrightness) << "invalid config value for" << key << ":" << config->value(QString::fromLatin1(key));
        return std::nullopt;
    }
    return value;
}

std::vector<CurvePoint> parseCurvePoints(const QVariant &raw)
{
    std::vector<CurvePoint> points;
    QJsonArray array;
    if (raw.typeId() == QMetaType::QString) {
        const QJsonDocument doc = QJsonDocument::fromJson(raw.toString().toUtf8());
        if (!doc.isArray()) {
            qCWarning(logAmbientBrightness) << "invalid continuousLuxCurve: not a JSON array";
            return {};
        }
        array = doc.array();
    } else if (raw.typeId() == QMetaType::QVariantList) {
        array = QJsonArray::fromVariantList(raw.toList());
    } else {
        qCWarning(logAmbientBrightness) << "invalid continuousLuxCurve: unsupported type" << raw.typeId();
        return {};
    }
    points.reserve(array.size());
    for (const auto &value : array) {
        if (!value.isObject()) {
            qCWarning(logAmbientBrightness) << "invalid continuousLuxCurve: non-object element";
            return {};
        }
        const QJsonObject object = value.toObject();
        const QJsonValue lux = object.value(QStringLiteral("lux"));
        const QJsonValue brightness = object.value(QStringLiteral("brightness"));
        if (!lux.isDouble() || !brightness.isDouble()) {
            qCWarning(logAmbientBrightness) << "invalid continuousLuxCurve: missing lux/brightness";
            return {};
        }
        points.push_back({ lux.toDouble(), brightness.toDouble() });
    }
    return points;
}

ContinuousPolicyConfig buildContinuousConfig(Dtk::Core::DConfig *config)
{
    ContinuousPolicyConfig cfg;
    if (!config)
        return cfg;

    const QVariant windowFlag = config->value(QString::fromLatin1(kUseWeightedWindowsKey));
    if (windowFlag.isValid() && windowFlag.canConvert<bool>())
        cfg.useWeightedWindows = windowFlag.toBool();

    const auto ambientHorizon = readFiniteDouble(config, kAmbientLightHorizonMsKey);
    const auto fastHorizon = readFiniteDouble(config, kFastLightHorizonMsKey);
    const double candidateAmbient =
        ambientHorizon && *ambientHorizon > 0.0 && *ambientHorizon <= cfg.weightingIntercept
        ? *ambientHorizon
        : cfg.ambientLightHorizonMs;
    const double candidateFast =
        fastHorizon && *fastHorizon > 0.0 ? *fastHorizon : cfg.fastLightHorizonMs;
    if (candidateFast <= candidateAmbient) {
        cfg.ambientLightHorizonMs = candidateAmbient;
        cfg.fastLightHorizonMs = candidateFast;
    }

    if (const auto value = readFiniteDouble(config, kBrightenDebounceMsKey);
        value && *value >= 0.0) {
        cfg.brightenDebounceMs = *value;
    }
    if (const auto value = readFiniteDouble(config, kDarkenDebounceMsKey); value && *value >= 0.0) {
        cfg.darkenDebounceMs = *value;
    }
    if (const auto value = readFiniteDouble(config, kStepHysteresisRatioKey);
        value && *value >= 0.5 && *value <= 1.0) {
        cfg.stepHysteresisRatio = *value;
    }

    const QString mode = config->value(QString::fromLatin1(kContinuousMappingModeKey)).toString();
    cfg.useStepsMode = (mode != QLatin1String("curve"));

    auto curve = parseCurvePoints(config->value(QString::fromLatin1(kContinuousLuxCurveKey)));
    if (!BrightnessCurve::isValid(curve)) {
        qCWarning(logAmbientBrightness) << "invalid continuousLuxCurve curve; using defaults";
        return cfg;
    }

    cfg.curve = curve;
    if (cfg.useStepsMode) {
        // 将曲线控制点转换为档位数据
        std::vector<LuxStep> steps;
        for (const auto &p : curve)
            steps.push_back({ p.lux, p.brightness });
        if (ContinuousAmbientLightPolicy::isValidLuxSteps(steps))
            cfg.luxSteps = std::move(steps);
    }

    return cfg;
}

} // namespace

std::optional<BrightnessAlgorithm> parseBrightnessAlgorithm(const QString &value)
{
    if (value.compare(QStringLiteral("continuous"), Qt::CaseInsensitive) == 0)
        return BrightnessAlgorithm::Continuous;
    return std::nullopt;
}

QString brightnessAlgorithmName(BrightnessAlgorithm algorithm)
{
    switch (algorithm) {
    case BrightnessAlgorithm::Continuous:
        return QStringLiteral("continuous");
    }
    return QStringLiteral("continuous");
}

std::unique_ptr<AmbientBrightnessPolicy>
createAmbientBrightnessPolicy(BrightnessAlgorithm algorithm)
{
    switch (algorithm) {
    case BrightnessAlgorithm::Continuous:
        return std::make_unique<ContinuousAmbientLightPolicy>(ContinuousPolicyConfig{});
    }
    return std::make_unique<ContinuousAmbientLightPolicy>(ContinuousPolicyConfig{});
}

std::unique_ptr<AmbientBrightnessPolicy>
createAmbientBrightnessPolicy(BrightnessAlgorithm algorithm, Dtk::Core::DConfig *config)
{
    switch (algorithm) {
    case BrightnessAlgorithm::Continuous: {
        auto cfg = buildContinuousConfig(config);
        if (!ContinuousAmbientLightPolicy::isValidConfig(cfg))
            cfg = ContinuousPolicyConfig{};
        return std::make_unique<ContinuousAmbientLightPolicy>(cfg);
    }
    }
    return std::make_unique<ContinuousAmbientLightPolicy>(ContinuousPolicyConfig{});
}

} // namespace dde::ambient_brightness
