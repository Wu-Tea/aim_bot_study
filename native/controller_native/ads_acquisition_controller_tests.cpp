#include "ads_acquisition_controller.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

void require_true(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

pipeline_contract::TargetPlan plan_with(float error_x, float error_rate_x, float reliability = 1.0f) {
    pipeline_contract::TargetPlan plan{};
    plan.lifecycle = pipeline_contract::TargetLifecycle::Observed;
    plan.mode = pipeline_contract::ControlMode::AdsAcquire;
    plan.error_px.x = error_x;
    plan.error_rate_px_per_sec.x = error_rate_x;
    plan.reliability = reliability;
    plan.confidence = reliability;
    plan.aim_authority = reliability;
    plan.ads_demand = std::min(1.0f, std::fabs(error_x) / 130.0f);
    plan.response_scale = 500.0f;
    plan.response_confidence = 1.0f;
    return plan;
}

void test_learned_slow_camera_response_automatically_increases_ads_request() {
    controller_native::AdsAcquisitionController controller;
    auto normal = plan_with(40.0f, 0.0f);
    auto slowed = normal;
    slowed.response_scale = 250.0f;
    const auto normal_output = controller.compute(normal, {}, 0.01f);
    const auto slowed_output = controller.compute(slowed, {}, 0.01f);
    require_true(slowed_output.x > normal_output.x * 1.8f,
                 "slower learned camera response must increase ADS correction without range tuning");
}

void test_shorter_arrival_horizon_increases_ads_positioning_speed() {
    controller_native::AdsAcquisitionControllerConfig fast_config{};
    fast_config.arrival_horizon_seconds = 0.120f;
    controller_native::AdsAcquisitionControllerConfig slow_config{};
    slow_config.arrival_horizon_seconds = 0.240f;
    controller_native::AdsAcquisitionController fast(fast_config);
    controller_native::AdsAcquisitionController slow(slow_config);
    const auto plan = plan_with(40.0f, 0.0f);
    require_true(fast.compute(plan, {}, 0.01f).x >
                     slow.compute(plan, {}, 0.01f).x * 1.8f,
                 "ADS snap duration must directly control response-model arrival time");
}

void test_large_reliable_error_gets_strong_output() {
    controller_native::AdsAcquisitionController controller;
    const auto output = controller.compute(plan_with(120.0f, 0.0f), {}, 0.01f);
    require_true(output.x > 0.75f, "large reliable ADS error must retain strength");
}

void test_drift_does_not_weaken_ads() {
    controller_native::AdsAcquisitionController controller;
    pipeline_contract::IntentState neutral{};
    pipeline_contract::IntentState drift{};
    drift.raw_right.x = -0.0118f;
    drift.filtered_right.x = 0.0f;
    drift.right_confidence = 0.0f;
    const auto a = controller.compute(plan_with(80.0f, 0.0f), neutral, 0.01f);
    const auto b = controller.compute(plan_with(80.0f, 0.0f), drift, 0.01f);
    require_true(std::fabs(a.x - b.x) < 0.0001f,
                 "raw deadzone drift must not reduce ADS output");
}

void test_real_opposing_correction_reduces_conflict() {
    controller_native::AdsAcquisitionController controller;
    pipeline_contract::IntentState correction{};
    correction.filtered_right.x = -0.5f;
    correction.right_confidence = 1.0f;
    correction.right_x.confidence = 1.0f;
    const auto neutral = controller.compute(plan_with(80.0f, 0.0f), {}, 0.01f);
    const auto opposed = controller.compute(plan_with(80.0f, 0.0f), correction, 0.01f);
    require_true(opposed.x < neutral.x * 0.5f,
                 "confident opposing correction must reduce AI conflict");
}

void test_predicted_crossing_applies_terminal_brake() {
    controller_native::AdsAcquisitionController controller;
    const auto approaching = controller.compute(plan_with(6.0f, -400.0f), {}, 0.01f);
    const auto stationary = controller.compute(plan_with(6.0f, 0.0f), {}, 0.01f);
    require_true(std::fabs(approaching.x) < std::fabs(stationary.x),
                 "stopping error must brake an imminent crossing");
}

void test_screen_y_error_is_converted_to_stick_y_direction() {
    controller_native::AdsAcquisitionController controller;
    auto plan = plan_with(0.0f, 0.0f);
    plan.error_px.y = 60.0f;
    const auto output = controller.compute(plan, {}, 0.01f);
    require_true(output.y < 0.0f,
                 "a target below center requires negative stick Y in screen coordinates");
}

void test_hard_start_delay_suppresses_only_early_ads_assist() {
    controller_native::AdsAcquisitionControllerConfig config{};
    config.start_delay_ms = 30.0f;
    controller_native::AdsAcquisitionController controller(config);
    auto plan = plan_with(80.0f, 0.0f);
    plan.target_acquisition_id = 1;
    plan.acquisition_elapsed_ms = 29.0f;
    require_true(
        std::fabs(controller.compute(plan, {}, 0.01f).x) < 0.0001f,
        "hard ADS delay must suppress assist before its boundary");
    plan.acquisition_elapsed_ms = 30.0f;
    require_true(
        controller.compute(plan, {}, 0.01f).x > 0.5f,
        "hard ADS delay must release full assist at its boundary");
}

void test_smooth_start_ramp_reaches_half_then_full_authority() {
    controller_native::AdsAcquisitionControllerConfig config{};
    config.start_ramp_ms = 30.0f;
    controller_native::AdsAcquisitionController controller(config);
    auto plan = plan_with(80.0f, 0.0f);
    plan.target_acquisition_id = 1;
    plan.acquisition_elapsed_ms = 15.0f;
    const float half = controller.compute(plan, {}, 0.01f).x;
    plan.acquisition_elapsed_ms = 30.0f;
    const float full = controller.compute(plan, {}, 0.01f).x;
    require_true(half > full * 0.45f && half < full * 0.55f,
                 "30ms ADS ramp must expose half authority at 15ms");
}

}  // namespace

int main() {
    try {
        test_large_reliable_error_gets_strong_output();
        test_drift_does_not_weaken_ads();
        test_real_opposing_correction_reduces_conflict();
        test_predicted_crossing_applies_terminal_brake();
        test_screen_y_error_is_converted_to_stick_y_direction();
        test_learned_slow_camera_response_automatically_increases_ads_request();
        test_shorter_arrival_horizon_increases_ads_positioning_speed();
        test_hard_start_delay_suppresses_only_early_ads_assist();
        test_smooth_start_ramp_reaches_half_then_full_authority();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[AdsAcquisitionControllerTests] FAIL " << error.what() << '\n';
        return 1;
    }
}
