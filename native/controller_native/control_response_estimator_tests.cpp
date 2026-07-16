#include "control_response_estimator.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

void require_true(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void require_near(float actual, float expected, float tolerance, const char* message) {
    if (std::fabs(actual - expected) > tolerance) throw std::runtime_error(message);
}

void test_clean_excitation_converges() {
    controller_native::ControlResponseEstimator estimator;
    estimator.begin_ads_epoch(1);
    for (int i = 0; i < 80; ++i) {
        estimator.update({0.5f, 120.0f, true, false});
    }
    require_near(estimator.estimate().scale_px_per_stick_second, 240.0f, 5.0f,
                 "clean response estimate must converge");
    require_true(estimator.estimate().confidence > 0.8f,
                 "repeated clean samples must build confidence");
}

void test_ambiguous_samples_freeze_learning() {
    controller_native::ControlResponseEstimator estimator;
    estimator.begin_ads_epoch(1);
    for (int i = 0; i < 60; ++i) estimator.update({0.5f, 100.0f, true, false});
    const auto before = estimator.estimate();
    for (int i = 0; i < 100; ++i) estimator.update({0.8f, -500.0f, false, true});
    const auto after = estimator.estimate();
    require_near(after.scale_px_per_stick_second, before.scale_px_per_stick_second, 0.001f,
                 "ambiguous windows must not change response scale");
    require_true(after.confidence <= before.confidence,
                 "frozen windows may only preserve or decay confidence");
}

void test_ads_epoch_keeps_warm_scale_but_drops_confidence() {
    controller_native::ControlResponseEstimator estimator;
    estimator.begin_ads_epoch(1);
    for (int i = 0; i < 50; ++i) estimator.update({-0.5f, -150.0f, true, false});
    const auto learned = estimator.estimate();
    estimator.begin_ads_epoch(2);
    const auto reset = estimator.estimate();
    require_near(reset.scale_px_per_stick_second, learned.scale_px_per_stick_second, 0.001f,
                 "same-process epoch reset must retain a warm scale");
    require_true(reset.confidence < learned.confidence * 0.5f,
                 "new ADS epoch must require fresh confidence");
}

void test_zero_confidence_is_safe() {
    controller_native::ControlResponseEstimator estimator;
    const auto estimate = estimator.estimate();
    require_near(estimate.confidence, 0.0f, 0.0f, "new estimator confidence must be zero");
    require_near(estimate.scale_px_per_stick_second, 0.0f, 0.0f,
                 "new estimator must not invent a weapon response");
}

}  // namespace

int main() {
    try {
        test_clean_excitation_converges();
        test_ambiguous_samples_freeze_learning();
        test_ads_epoch_keeps_warm_scale_but_drops_confidence();
        test_zero_confidence_is_safe();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[ControlResponseEstimatorTests] FAIL " << error.what() << '\n';
        return 1;
    }
}
