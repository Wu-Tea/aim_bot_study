#include "aim_response_estimator.h"

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

int main() {
    try {
        test_fallback_and_convergence_across_response_scales();
        test_persists_across_targets_and_adapts_to_slowdown();
        test_rejects_ambiguous_and_invalid_intervals();
        test_reset_clears_learned_response();
        std::cout << "cod_native_aim_response_estimator_tests PASS\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "cod_native_aim_response_estimator_tests FAIL: "
                  << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
