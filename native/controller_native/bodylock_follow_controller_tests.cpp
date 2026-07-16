#include "bodylock_follow_controller.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

void require_true(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

pipeline_contract::TargetPlan moving_plan(float authority = 1.0f) {
    pipeline_contract::TargetPlan plan{};
    plan.lifecycle = pipeline_contract::TargetLifecycle::Observed;
    plan.mode = pipeline_contract::ControlMode::BodyLockFollow;
    plan.error_px = {2.0f, 0.0f};
    plan.error_rate_px_per_sec = {180.0f, 0.0f};
    plan.reliability = authority;
    plan.aim_authority = authority;
    plan.bodylock_demand = 1.0f;
    plan.response_scale = 300.0f;
    plan.response_confidence = 1.0f;
    return plan;
}

void test_motion_feedforward_stays_active_near_center() {
    controller_native::BodylockFollowController controller;
    const auto output = controller.compute(moving_plan(), {}, 0.01f);
    require_true(output.x > 0.15f,
                 "BodyLock must follow motion even when positional error is small");
}

void test_coasting_authority_decays_continuously() {
    controller_native::BodylockFollowController controller;
    auto observed = moving_plan(1.0f);
    auto coasting = moving_plan(0.3f);
    coasting.lifecycle = pipeline_contract::TargetLifecycle::Coasting;
    const auto strong = controller.compute(observed, {}, 0.01f);
    const auto weak = controller.compute(coasting, {}, 0.01f);
    require_true(weak.x > 0.0f && weak.x < strong.x * 0.5f,
                 "coasting must decay rather than drop or remain full strength");
}

void test_manual_correction_remains_available() {
    controller_native::BodylockFollowController controller;
    pipeline_contract::IntentState correction{};
    correction.filtered_right.x = -0.5f;
    correction.right_confidence = 1.0f;
    const auto neutral = controller.compute(moving_plan(), {}, 0.01f);
    const auto opposed = controller.compute(moving_plan(), correction, 0.01f);
    require_true(opposed.x > 0.0f && opposed.x < neutral.x,
                 "BodyLock must yield smoothly to manual correction");
}

void test_closing_target_brakes_without_reversing_before_crossing() {
    controller_native::BodylockFollowController controller;
    auto stationary = moving_plan();
    stationary.error_px.x = 18.0f;
    stationary.error_rate_px_per_sec.x = 0.0f;
    stationary.response_scale = 500.0f;
    auto closing = stationary;
    closing.error_rate_px_per_sec.x = -240.0f;

    const auto normal = controller.compute(stationary, {}, 0.01f);
    const auto braking = controller.compute(closing, {}, 0.01f);
    require_true(braking.x >= 0.0f && braking.x < normal.x,
                 "closing BodyLock must brake without reversing before the target crossing");
}

void test_near_target_error_has_legacy_grip() {
    controller_native::BodylockFollowController controller;
    auto plan = moving_plan();
    plan.error_px.x = 12.0f;
    plan.error_rate_px_per_sec.x = 0.0f;
    const auto output = controller.compute(plan, {}, 0.01f);
    require_true(output.x >= 0.10f,
                 "near-target BodyLock feedback must retain a perceptible grip");
}

void test_left_strafe_yields_positional_grip_without_dropping_follow() {
    controller_native::BodylockFollowController controller;
    auto plan = moving_plan();
    plan.error_px.x = 12.0f;
    plan.error_rate_px_per_sec.x = 0.0f;
    const auto neutral = controller.compute(plan, {}, 0.01f);
    pipeline_contract::IntentState strafe{};
    strafe.filtered_left.x = 0.8f;
    strafe.left_confidence = 1.0f;
    const auto moving = controller.compute(plan, strafe, 0.01f);
    require_true(moving.x > 0.0f && moving.x < neutral.x * 0.6f,
                 "left strafe must soften positional grip while retaining follow authority");
}

}  // namespace

int main() {
    try {
        test_motion_feedforward_stays_active_near_center();
        test_coasting_authority_decays_continuously();
        test_manual_correction_remains_available();
        test_closing_target_brakes_without_reversing_before_crossing();
        test_near_target_error_has_legacy_grip();
        test_left_strafe_yields_positional_grip_without_dropping_follow();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[BodylockFollowControllerTests] FAIL " << error.what() << '\n';
        return 1;
    }
}
