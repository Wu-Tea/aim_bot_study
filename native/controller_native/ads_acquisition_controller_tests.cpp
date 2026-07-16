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
    return plan;
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

void test_mode_controller_leaves_manual_arbitration_to_single_stage() {
    controller_native::AdsAcquisitionController controller;
    pipeline_contract::IntentState correction{};
    correction.filtered_right.x = -0.5f;
    correction.right_x.confidence = 1.0f;
    correction.right_confidence = 1.0f;
    const auto neutral = controller.compute(plan_with(80.0f, 0.0f), {}, 0.01f);
    const auto opposed = controller.compute(plan_with(80.0f, 0.0f), correction, 0.01f);
    require_true(std::fabs(opposed.x - neutral.x) < 0.0001f,
                 "ADS mode controller must not duplicate manual arbitration");
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

void test_strong_x_confidence_does_not_promote_weak_y_input() {
    controller_native::AdsAcquisitionController controller;
    auto plan = plan_with(60.0f, 0.0f);
    plan.error_px.y = 60.0f;
    const auto neutral = controller.compute(plan, {}, 0.01f);
    pipeline_contract::IntentState x_owned{};
    x_owned.filtered_right = {0.8f, 0.05f};
    x_owned.right_x.confidence = 1.0f;
    x_owned.right_y.confidence = 0.0f;
    x_owned.right_confidence = 1.0f;
    const auto candidate = controller.compute(plan, x_owned, 0.01f);
    require_true(std::fabs(candidate.y - neutral.y) < 0.0001f,
                 "strong X confidence must not attenuate Y ADS output");
}

}  // namespace

int main() {
    try {
        test_large_reliable_error_gets_strong_output();
        test_drift_does_not_weaken_ads();
        test_mode_controller_leaves_manual_arbitration_to_single_stage();
        test_predicted_crossing_applies_terminal_brake();
        test_screen_y_error_is_converted_to_stick_y_direction();
        test_strong_x_confidence_does_not_promote_weak_y_input();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[AdsAcquisitionControllerTests] FAIL " << error.what() << '\n';
        return 1;
    }
}
