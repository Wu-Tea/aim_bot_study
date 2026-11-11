#include "aim_response_estimator.h"
#include "ads_response_estimator.h"
#include "test_support/native_test_registry.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

using namespace controller_native;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void require_near(float actual, float expected, float tolerance,
                  const std::string& message) {
    if (std::fabs(actual - expected) > tolerance) {
        throw std::runtime_error(message + ": expected=" +
            std::to_string(expected) + " actual=" + std::to_string(actual));
    }
}

AimResponseInterval interval(
    std::uint64_t target_id,
    float stick_x,
    float response,
    float target_rate = 75.0f) {
    AimResponseInterval sample;
    sample.target_id = target_id;
    sample.average_final_stick = {stick_x, 0.0f};
    sample.observed_error_rate_px_per_sec = {
        target_rate - response * stick_x, 0.0f};
    sample.dt_seconds = 0.010f;
    sample.reliability = 0.95f;
    sample.observed = true;
    return sample;
}

template <typename Estimator>
void train_samples(
    Estimator& estimator,
    float response,
    int samples = 80) {
    for (int index = 0; index < samples; ++index) {
        const float stick = index % 2 == 0 ? 0.20f : 0.55f;
        estimator.update(interval(7, stick, response, 75.0f));
    }
}

void train(AimResponseEstimator& estimator, float response, int samples = 80) {
    for (int index = 0; index < samples; ++index) {
        const float stick = index % 2 == 0 ? 0.20f : 0.55f;
        estimator.update(interval(7, stick, response));
    }
}

void test_fallback_and_convergence_across_response_scales() {
    for (float gain : {8000.f, 25000.f}) {
        AimResponseEstimatorConfig config;
        config.fallback_scale = gain * .8f;
        AimResponseEstimator estimator(config);
        train(estimator, gain, 160);
        require_near(estimator.estimate().scale_px_per_stick_second, gain, gain * .04f,
            "high response must remain identifiable above 4000");
    }

    AimResponseEstimator slow;
    require_near(slow.estimate().scale_px_per_stick_second, 500.0f, 1e-6f,
                 "untrained fallback");
    train(slow, 300.0f);
    require_near(slow.estimate().scale_px_per_stick_second, 300.0f, 18.0f,
                 "converges to slow ADS response");
    require(slow.estimate().confidence > 0.80f,
            "clean intervals must build confidence");

    AimResponseEstimator fast;
    train(fast, 750.0f);
    require_near(fast.estimate().scale_px_per_stick_second, 750.0f, 25.0f,
                 "converges to fast ADS response");

    AimResponseEstimator apex_fast;
    train(apex_fast, 2375.0f, 120);
    require_near(
        apex_fast.estimate().scale_px_per_stick_second,
        2375.0f,
        80.0f,
        "converges above the old 1200 px/(stick*s) ceiling");
}

void test_persists_across_targets_and_adapts_to_slowdown() {
    AimResponseEstimator estimator;
    train(estimator, 500.0f);
    const auto before = estimator.estimate();
    estimator.begin_target(99);
    require_near(estimator.estimate().scale_px_per_stick_second,
                 before.scale_px_per_stick_second, 1e-6f,
                 "target change preserves learned weapon response");
    for (int index = 0; index < 100; ++index) {
        const float stick = index % 2 == 0 ? 0.22f : 0.60f;
        estimator.update(interval(99, stick, 250.0f));
    }
    require_near(estimator.estimate().scale_px_per_stick_second, 250.0f, 20.0f,
                 "online estimate adapts to a new normal sensitivity");
}

void test_single_response_model_updates_without_region_selection() {
    AimResponseEstimator hip; AdsResponseEstimator ads;
    train_samples(hip,600); train_samples(ads,600);
    train_samples(hip,330,160); train_samples(ads,330,160);
    require_near(hip.estimate().scale_px_per_stick_second,330,25,"new evidence updates the one hipfire response");
    require_near(ads.estimate().scale_px_per_stick_second,330,25,"new evidence updates the one ADS response");
}

void test_smooth_arrival_accumulates_enough_excitation_to_learn() {
    AdsResponseEstimator estimator;
    for (int index = 0; index < 160; ++index) {
        const float stick = 0.55f - static_cast<float>(index) * 0.0025f;
        estimator.update(interval(7, stick, 200.0f, 0.0f));
    }

    const auto learned = estimator.estimate();
    require(learned.accepted_samples >= 8,
            "smooth arrival must accumulate response-identifying excitation");
    require(learned.scale_px_per_stick_second < 300.0f,
            "smooth clear-camera samples must identify response");
    require(learned.confidence >= 0.70f,
            "repeated smooth evidence must build usable confidence");
}

void test_default_estimator_preserves_adjacent_pair_policy() {
    AimResponseEstimator estimator;
    for (int index = 0; index < 160; ++index) {
        const float stick = 0.55f - static_cast<float>(index) * 0.0025f;
        estimator.update(interval(7, stick, 200.0f, 0.0f));
    }

    const auto estimate = estimator.estimate();
    require(estimate.accepted_samples == 0,
            "BodyLock's legacy estimator must retain adjacent-pair excitation");
    require_near(estimate.scale_px_per_stick_second, 500.0f, 1.0e-6f,
                 "BodyLock's default response behavior must remain unchanged");
}

void test_ads_sensitivity_change_updates_the_same_model() {
    AdsResponseEstimator estimator;
    train_samples(estimator,500,80);
    train_samples(estimator,200,160);
    require_near(estimator.estimate().scale_px_per_stick_second,200,25,"automatic learning changes the shared ADS response");
    require(estimator.estimate().confidence>.7f,"consistent evidence builds confidence");
}

void test_ads_epoch_boundary_clears_anchors_but_keeps_response() {
    AdsResponseEstimator estimator;
    train_samples(estimator, 200.0f, 100);
    const auto learned = estimator.estimate();
    require(learned.accepted_samples > 0,
            "ADS fixture must establish response evidence");

    estimator.begin_target(7);
    require_near(estimator.estimate().scale_px_per_stick_second,
                 learned.scale_px_per_stick_second, 1e-6f,
                 "ADS epoch boundary must retain learned weapon response");
    require(!estimator.update(interval(7, 0.42f, 200.0f, 0.0f)),
            "first sample of a new ADS epoch must only prime anchors");
}

void test_camera_models_keep_independent_learning() {
    AimResponseEstimator hip;
    AdsResponseEstimator ads;
    train_samples(hip,1700,160);
    const auto before=hip.estimate();
    train_samples(ads,2500,160);
    require_near(ads.estimate().scale_px_per_stick_second,2500,30,"ADS learns its own sensitivity");
    require(hip.estimate().learned_scale==before.learned_scale && hip.estimate().accepted_samples==before.accepted_samples,
        "ADS evidence cannot rewrite the hipfire camera model");
}

void test_rejects_ambiguous_and_invalid_intervals() {
    AimResponseEstimator estimator;
    train(estimator, 500.0f, 20);
    const auto before = estimator.estimate();

    AimResponseInterval bad = interval(7, 0.80f, 1000.0f);
    bad.manual_ambiguous = true;
    require(!estimator.update(bad), "manual interval must be rejected");
    bad = interval(8, 0.20f, 300.0f);
    require(!estimator.update(bad), "target swap interval must prime, not learn");
    bad = interval(8, 0.21f, 300.0f);
    bad.target_acceleration_px_per_sec2 = 2000.0f;
    require(!estimator.update(bad), "high acceleration must be rejected");
    bad = interval(8, 0.22f, 300.0f);
    bad.observed = false;
    require(!estimator.update(bad), "coasting interval must be rejected");
    bad = interval(8, 0.23f, 300.0f);
    bad.dt_seconds = 0.100f;
    require(!estimator.update(bad), "invalid cadence must be rejected");

    require(estimator.estimate().accepted_samples == before.accepted_samples,
            "ambiguous intervals must not add accepted samples");
}

template <typename Estimator>
void check_configured_prior_learning() {
    AimResponseEstimatorConfig config;
    config.fallback_scale = 650.0f;
    Estimator estimator(config);
    require_near(estimator.estimate().scale_px_per_stick_second, 650.0f, 1e-6f,
                 "configured cold prior");
    require(estimator.estimate().confidence == 0.0f,
            "a prior is not measured confidence");
    train_samples(estimator, 300.0f, 160);
    const auto learned = estimator.estimate();
    require(std::isfinite(learned.scale_px_per_stick_second) && learned.accepted_samples > 0,
            "configured prior must still admit real response samples");
    require_near(learned.scale_px_per_stick_second, 300.0f, 25.0f,
                 "learning must override the configured prior");
    estimator.begin_target(99);
    require_near(estimator.estimate().scale_px_per_stick_second,
                 learned.scale_px_per_stick_second, 1e-6f,
                 "target switch retains learning");
    estimator.reset();
    require_near(estimator.estimate().scale_px_per_stick_second, 650.0f, 1e-6f,
                 "reset restores configured prior");
    require(estimator.estimate().confidence == 0.0f, "reset clears confidence");
}

void test_configured_prior_retains_learning() {
    check_configured_prior_learning<AimResponseEstimator>();
    check_configured_prior_learning<AdsResponseEstimator>();
}

void test_reset_clears_learned_response() {
    AimResponseEstimator estimator;
    train(estimator, 300.0f);
    estimator.reset();
    require_near(estimator.estimate().scale_px_per_stick_second, 500.0f, 1e-6f,
                 "reset restores fallback");
    require(estimator.estimate().confidence == 0.0f,
            "reset clears confidence");
}

template <typename Estimator>
void check_single_response_learning() {
    for (std::uint32_t seed : {20260930u,7311985u,91837u,716993u}) {
        auto random=[&]() { seed^=seed<<13; seed^=seed>>17; seed^=seed<<5; return (seed%100000u)/100000.f; };
        for(int scenario=0;scenario<256;++scenario) {
            AimResponseEstimatorConfig config; config.fallback_scale=80+25000*random();
            Estimator estimator(config);
            const float gain=100+24000*random(), next_gain=100+24000*random();
            const int count=scenario%2 ? 160 : 8;
            train_samples(estimator,gain,count);
            const auto before=estimator.estimate();
            require(before.accepted_samples>0,"random scenario must exercise learning");
            train_samples(estimator,next_gain,count);
            const auto after=estimator.estimate();
            require(after.accepted_samples>before.accepted_samples && std::isfinite(after.scale_px_per_stick_second),
                "automatic learning updates one shared response without region switches");
            if(count==160) require_near(after.scale_px_per_stick_second,next_gain,35,"long sequences converge to the latest sensitivity");
            estimator.begin_target(99);
            require_near(estimator.estimate().scale_px_per_stick_second,after.scale_px_per_stick_second,0,"handover preserves learned response");
            estimator.reset();
            require_near(estimator.estimate().scale_px_per_stick_second,config.fallback_scale,.001f,"reset restores user prior");
        }
    }
}
void test_single_response_learning() {
    check_single_response_learning<AimResponseEstimator>();
    check_single_response_learning<AdsResponseEstimator>();
}

}  // namespace

void register_aim_response_estimator_tests(native_test::Registry& registry) {
    registry.add_case("BaseBodyLock", "response_evidence_remains_finite_at_numeric_boundary", [] {
        AimResponseEstimatorConfig config;
        config.minimum_command_delta=0;
        AimResponseEstimator estimator(config);
        for(int i=0;i<16;++i) estimator.update(interval(7,.2f,500));
        for(int i=0;i<16;++i){
            auto sample=interval(7,i%2?.6f:.2f,500);
            sample.observed_error_rate_px_per_sec={std::numeric_limits<float>::max(),0};
            estimator.update(sample);
        }
        train(estimator,750);
        const auto result=estimator.estimate();
        require(std::isfinite(result.scale_px_per_stick_second)&&std::isfinite(result.confidence),
            "zero excitation or overflowing evidence must not poison subsequent valid learning");
        require_near(result.scale_px_per_stick_second,750,25,"valid evidence must recover after numeric boundary inputs");
    });
    registry.add_case("BaseBodyLock", "inconsistent_camera_evidence_does_not_build_response_confidence", [] {
        AimResponseEstimator estimator;
        for (int i=0;i<256;++i) {
            auto sample=interval(7,i%2 ? .60f:.25f,500);
            constexpr float unexplained_rates[]{120,-150,-120,150};
            sample.observed_error_rate_px_per_sec.x=unexplained_rates[i%4];
            estimator.update(sample);
        }
        require(estimator.estimate().confidence<.2f,
            "mutually inconsistent positive and negative response evidence must not become confident learning");
    });
    registry.add_case("BaseBodyLock", "single_response_learning_randomized", test_single_response_learning);
    registry.add_case("BaseBodyLock", "configured_prior_retains_learning", test_configured_prior_retains_learning);
    registry.add_case("BaseBodyLock", "response_fallback_and_convergence", test_fallback_and_convergence_across_response_scales);
    registry.add_case("BaseBodyLock", "response_persists_and_adapts_to_sensitivity", test_persists_across_targets_and_adapts_to_slowdown);
    registry.add_case("BaseBodyLock", "single_response_without_region_selection", test_single_response_model_updates_without_region_selection);
    registry.add_case("BaseAds", "response_smooth_arrival_accumulates_excitation", test_smooth_arrival_accumulates_enough_excitation_to_learn);
    registry.add_case("BaseBodyLock", "default_response_preserves_adjacent_pair_policy", test_default_estimator_preserves_adjacent_pair_policy);
    registry.add_case("BaseAds", "sensitivity_change_updates_same_model", test_ads_sensitivity_change_updates_the_same_model);
    registry.add_case("BaseAds", "response_epoch_boundary_clears_anchors", test_ads_epoch_boundary_clears_anchors_but_keeps_response);
    registry.add_case("BaseBodyLock", "camera_models_keep_independent_learning", test_camera_models_keep_independent_learning);
    registry.add_case("BaseBodyLock", "ambiguous_intervals_are_rejected", test_rejects_ambiguous_and_invalid_intervals);
    registry.add_case("BaseBodyLock", "response_reset_clears_learning", test_reset_clears_learned_response);
}
