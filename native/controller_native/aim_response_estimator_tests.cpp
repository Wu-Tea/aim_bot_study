#include "aim_response_estimator.h"
#include "ads_response_estimator.h"
#include "test_support/native_test_registry.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
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
    float target_rate = 75.0f,
    float slow_zone_weight = 0.0f) {
    AimResponseInterval sample;
    sample.target_id = target_id;
    sample.average_final_stick = {stick_x, 0.0f};
    sample.observed_error_rate_px_per_sec = {
        target_rate - response * stick_x, 0.0f};
    sample.dt_seconds = 0.010f;
    sample.reliability = 0.95f;
    sample.slow_zone_weight = slow_zone_weight;
    sample.observed = true;
    return sample;
}

template <typename Estimator>
void train_zone(
    Estimator& estimator,
    float response,
    float slow_zone_weight,
    int samples = 80) {
    for (int index = 0; index < samples; ++index) {
        const float stick = index % 2 == 0 ? 0.20f : 0.55f;
        estimator.update(interval(7, stick, response, 75.0f, slow_zone_weight));
    }
}

void train(AimResponseEstimator& estimator, float response, int samples = 80) {
    for (int index = 0; index < samples; ++index) {
        const float stick = index % 2 == 0 ? 0.20f : 0.55f;
        estimator.update(interval(7, stick, response));
    }
}

void test_fallback_and_convergence_across_response_scales() {
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
                 "online estimate adapts inside slowdown");
}

void test_keeps_free_and_slow_zone_response_separate() {
    AimResponseEstimator estimator;
    train_zone(estimator, 600.0f, 0.0f);
    train_zone(estimator, 330.0f, 1.0f);

    const auto free = estimator.estimate(0.0f);
    const auto slowed = estimator.estimate(1.0f);
    const auto boundary = estimator.estimate(0.5f);
    require_near(free.scale_px_per_stick_second, 600.0f, 25.0f,
                 "free-space response must survive slowdown-zone learning");
    require_near(slowed.scale_px_per_stick_second, 330.0f, 25.0f,
                 "slowdown zone must retain its measured angular response");
    require(boundary.scale_px_per_stick_second < free.scale_px_per_stick_second &&
                boundary.scale_px_per_stick_second > slowed.scale_px_per_stick_second,
            "zone boundary must interpolate measured response continuously");
}

void test_smooth_arrival_accumulates_enough_excitation_to_learn() {
    AdsResponseEstimator estimator;
    for (int index = 0; index < 160; ++index) {
        const float stick = 0.55f - static_cast<float>(index) * 0.0025f;
        estimator.update(interval(7, stick, 200.0f, 0.0f, 1.0f));
    }

    const auto learned = estimator.estimate(1.0f);
    require(learned.accepted_samples >= 8,
            "smooth arrival must accumulate response-identifying excitation");
    require(learned.scale_px_per_stick_second < 300.0f,
            "smooth arrival must learn the material slowdown response");
    require(learned.confidence >= 0.70f,
            "repeated smooth evidence must build usable confidence");
}

void test_default_estimator_preserves_adjacent_pair_policy() {
    AimResponseEstimator estimator;
    for (int index = 0; index < 160; ++index) {
        const float stick = 0.55f - static_cast<float>(index) * 0.0025f;
        estimator.update(interval(7, stick, 200.0f, 0.0f, 1.0f));
    }

    const auto estimate = estimator.estimate(1.0f);
    require(estimate.accepted_samples == 0,
            "BodyLock's legacy estimator must retain adjacent-pair excitation");
    require_near(estimate.scale_px_per_stick_second, 500.0f, 1.0e-6f,
                 "BodyLock's default response behavior must remain unchanged");
}

void test_weighted_slowdown_transition_identifies_the_endpoint() {
    AdsResponseEstimator estimator;
    train_zone(estimator, 500.0f, 0.0f, 40);
    for (int index = 0; index < 100; ++index) {
        const float weight =
            static_cast<float>(index + 1) / 100.0f;
        const float response = 500.0f + weight * (200.0f - 500.0f);
        const float stick = 0.55f - static_cast<float>(index) * 0.0035f;
        estimator.update(interval(7, stick, response, 0.0f, weight));
    }

    const auto learned = estimator.estimate(1.0f);
    require_near(learned.scale_px_per_stick_second, 200.0f, 35.0f,
                 "spatial response blend must learn its slow endpoint");
    require(learned.confidence >= 0.70f,
            "weighted transition must provide usable endpoint confidence");
}

void test_ads_epoch_boundary_clears_anchors_but_keeps_response() {
    AdsResponseEstimator estimator;
    train_zone(estimator, 200.0f, 1.0f, 100);
    const auto learned = estimator.estimate(1.0f);
    require(learned.accepted_samples > 0,
            "ADS fixture must establish response evidence");

    estimator.begin_target(7);
    require_near(estimator.estimate(1.0f).scale_px_per_stick_second,
                 learned.scale_px_per_stick_second, 1e-6f,
                 "ADS epoch boundary must retain learned weapon response");
    require(!estimator.update(interval(7, 0.42f, 200.0f, 0.0f, 1.0f)),
            "first sample of a new ADS epoch must only prime anchors");
}

void test_slow_zone_weight_is_geometric_and_continuous() {
    const pipeline_contract::Vec2f target_size{60.0f, 140.0f};
    const float center = aim_response_slow_zone_weight({0.0f, 0.0f}, target_size);
    const float boundary = aim_response_slow_zone_weight({30.0f, 0.0f}, target_size);
    const float outside = aim_response_slow_zone_weight({80.0f, 0.0f}, target_size);
    require_near(center, 1.0f, 1.0e-6f, "target center is fully inside slowdown region");
    require(boundary > 0.0f && boundary < center,
            "target edge must blend between slowdown and free response");
    require_near(outside, 0.0f, 1.0e-6f, "far reticle uses free response");
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

void test_reset_clears_learned_response() {
    AimResponseEstimator estimator;
    train(estimator, 300.0f);
    estimator.reset();
    require_near(estimator.estimate().scale_px_per_stick_second, 500.0f, 1e-6f,
                 "reset restores fallback");
    require(estimator.estimate().confidence == 0.0f,
            "reset clears confidence");
}

}  // namespace

void register_aim_response_estimator_tests(native_test::Registry& registry) {
    registry.add_case("BaseBodyLock", "response_fallback_and_convergence", test_fallback_and_convergence_across_response_scales);
    registry.add_case("BaseBodyLock", "response_persists_and_adapts_to_slowdown", test_persists_across_targets_and_adapts_to_slowdown);
    registry.add_case("BaseBodyLock", "free_and_slow_zone_response_are_separate", test_keeps_free_and_slow_zone_response_separate);
    registry.add_case("BaseAds", "response_smooth_arrival_accumulates_excitation", test_smooth_arrival_accumulates_enough_excitation_to_learn);
    registry.add_case("BaseBodyLock", "default_response_preserves_adjacent_pair_policy", test_default_estimator_preserves_adjacent_pair_policy);
    registry.add_case("BaseAds", "response_weighted_slowdown_learns_endpoint", test_weighted_slowdown_transition_identifies_the_endpoint);
    registry.add_case("BaseAds", "response_epoch_boundary_clears_anchors", test_ads_epoch_boundary_clears_anchors_but_keeps_response);
    registry.add_case("BaseBodyLock", "slow_zone_weight_is_continuous", test_slow_zone_weight_is_geometric_and_continuous);
    registry.add_case("BaseBodyLock", "ambiguous_intervals_are_rejected", test_rejects_ambiguous_and_invalid_intervals);
    registry.add_case("BaseBodyLock", "response_reset_clears_learning", test_reset_clears_learned_response);
}
