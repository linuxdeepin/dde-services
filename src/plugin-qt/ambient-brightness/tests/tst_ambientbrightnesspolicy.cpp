// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "ambientbrightnessmodel.h"
#include "ambientbrightnessservice.h"
#include "continuous/continuousambientlightpolicy.h"

#include <QtTest>

using namespace dde::ambient_brightness;

namespace {
ContinuousPolicyConfig stepConfig()
{
    ContinuousPolicyConfig config;
    config.useStepsMode = true;
    config.luxSteps = { { 0.0, 0.1 }, { 10.0, 0.2 }, { 30.0, 0.3 } };
    return config;
}
} // namespace

class AmbientBrightnessPolicyTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void defaultConfigProducesRecommendation();
    void invalidConfigKeepsActiveConfiguration();
    void coldAndWarmStepClassificationAgree();
    void exactThresholdCompletesDebounce();
    void terminalStepsDoNotScheduleEvaluation();
    void overRangeLuxSaturates();
    void weightedWindowRetainsHighFrequencySamples();
    void modelRepublishesAfterReset();
    void lidOpenUsesDarkenDebounceDelay();
    void recoveryEventDoesNotShortenLidDelay();
};

void AmbientBrightnessPolicyTest::initTestCase()
{
    QLoggingCategory::setFilterRules(QStringLiteral("dde.ambientbrightness=false"));
}

void AmbientBrightnessPolicyTest::defaultConfigProducesRecommendation()
{
    const ContinuousPolicyConfig config;
    QVERIFY(ContinuousAmbientLightPolicy::isValidConfig(config));

    ContinuousAmbientLightPolicy policy(config);
    const auto recommendation = policy.update({ 10.0, 0.0 });
    QVERIFY(recommendation.has_value());
    QCOMPARE(recommendation->brightness, 0.2);
}

void AmbientBrightnessPolicyTest::invalidConfigKeepsActiveConfiguration()
{
    ContinuousAmbientLightPolicy policy(stepConfig());
    auto invalid = stepConfig();
    invalid.maxSensorLux = 0.0;
    QVERIFY(!policy.setConfig(invalid));

    const auto recommendation = policy.update({ 9.0, 0.0 });
    QVERIFY(recommendation.has_value());
    QCOMPARE(recommendation->brightness, 0.2);
}

void AmbientBrightnessPolicyTest::coldAndWarmStepClassificationAgree()
{
    const auto config = stepConfig();
    ContinuousAmbientLightPolicy cold(config);
    const auto coldRecommendation = cold.update({ 9.0, 0.0 });
    QVERIFY(coldRecommendation.has_value());

    ContinuousAmbientLightPolicy warm(config);
    QVERIFY(warm.update({ 0.0, 0.0 }).has_value());
    QVERIFY(!warm.update({ 9.0, 1000.0 }).has_value());
    const auto warmRecommendation = warm.tick(3000.0);
    QVERIFY(warmRecommendation.has_value());

    QCOMPARE(coldRecommendation->brightness, 0.2);
    QCOMPARE(warmRecommendation->brightness, coldRecommendation->brightness);
}

void AmbientBrightnessPolicyTest::exactThresholdCompletesDebounce()
{
    ContinuousAmbientLightPolicy policy(stepConfig());
    QVERIFY(policy.update({ 0.0, 0.0 }).has_value());
    QVERIFY(!policy.update({ 6.0, 1000.0 }).has_value());
    QCOMPARE(policy.nextEvaluationDelayMs(), 2000.0);
    QVERIFY(!policy.tick(2999.0).has_value());

    const auto recommendation = policy.tick(3000.0);
    QVERIFY(recommendation.has_value());
    QCOMPARE(recommendation->brightness, 0.2);
}

void AmbientBrightnessPolicyTest::terminalStepsDoNotScheduleEvaluation()
{
    ContinuousAmbientLightPolicy darkest(stepConfig());
    QVERIFY(darkest.update({ 0.0, 0.0 }).has_value());
    QCOMPARE(darkest.nextEvaluationDelayMs(), 0.0);

    ContinuousAmbientLightPolicy brightest(stepConfig());
    QVERIFY(brightest.update({ 100.0, 0.0 }).has_value());
    QCOMPARE(brightest.nextEvaluationDelayMs(), 0.0);
}

void AmbientBrightnessPolicyTest::overRangeLuxSaturates()
{
    ContinuousAmbientLightPolicy policy(ContinuousPolicyConfig{});
    const auto recommendation = policy.update({ 200000.0, 0.0 });
    QVERIFY(recommendation.has_value());
    QCOMPARE(recommendation->rawLux, 100000.0);
    QCOMPARE(recommendation->brightness, 1.0);
}

void AmbientBrightnessPolicyTest::weightedWindowRetainsHighFrequencySamples()
{
    ContinuousPolicyConfig config;
    config.useStepsMode = false;
    config.useWeightedWindows = true;
    config.ambientLightHorizonMs = 1000.0;
    config.fastLightHorizonMs = 1000.0;
    config.brightenHysteresisRatio = 0.0;
    config.darkenHysteresisRatio = 0.0;
    config.minimumHysteresisLux = 0.0;
    config.brightenDebounceMs = 0.0;
    config.darkenDebounceMs = 0.0;
    config.minimumRecommendationDelta = 0.0;

    ContinuousAmbientLightPolicy policy(config);
    std::optional<Recommendation> recommendation;
    for (int i = 0; i <= 100; ++i) {
        const double lux = i < 50 ? 0.0 : 100.0;
        recommendation = policy.update({ lux, static_cast<double>(i * 10) });
    }

    QVERIFY(recommendation.has_value());
    QVERIFY(recommendation->slowLux > 50.0);
    QVERIFY(recommendation->slowLux < 90.0);
}

void AmbientBrightnessPolicyTest::modelRepublishesAfterReset()
{
    auto policy = std::make_unique<ContinuousAmbientLightPolicy>(stepConfig());
    AmbientBrightnessModel model(std::move(policy));
    QSignalSpy spy(&model, &AmbientBrightnessModel::recommendedBrightnessChanged);

    model.waitForSample();
    model.submitSample(9.0, 0.0);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(model.state(), QStringLiteral("Active"));

    model.setDisabled();
    model.waitForSample();
    model.submitSample(9.0, 1.0);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(model.state(), QStringLiteral("Active"));
}

void AmbientBrightnessPolicyTest::lidOpenUsesDarkenDebounceDelay()
{
    AmbientBrightnessService service(QDBusConnection::sessionBus());
    QVERIFY(QMetaObject::invokeMethod(&service, "onLidOpened", Qt::DirectConnection));

    const auto timers = service.findChildren<QTimer *>();
    QCOMPARE(timers.size(), 1);
    QVERIFY(timers.constFirst()->isSingleShot());
    QVERIFY(timers.constFirst()->isActive());
    QCOMPARE(timers.constFirst()->interval(), 2000);
}

void AmbientBrightnessPolicyTest::recoveryEventDoesNotShortenLidDelay()
{
    AmbientBrightnessService service(QDBusConnection::sessionBus());
    QVERIFY(QMetaObject::invokeMethod(&service, "onLidOpened", Qt::DirectConnection));
    QVERIFY(QMetaObject::invokeMethod(&service, "onSessionActiveChanged", Qt::DirectConnection,
                                      Q_ARG(bool, true)));

    const auto timers = service.findChildren<QTimer *>();
    QCOMPARE(timers.size(), 1);
    QCOMPARE(timers.constFirst()->interval(), 2000);
}

QTEST_GUILESS_MAIN(AmbientBrightnessPolicyTest)

#include "tst_ambientbrightnesspolicy.moc"
