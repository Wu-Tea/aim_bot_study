#include "bodylock_follow_controller.h"
#include "test_support/native_test_registry.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

void require_true(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

bool near(float left, float right, float tolerance = 0.0001f) {
    return std::fabs(left - right) <= tolerance;
}

pipeline_contract::TargetPlan active_plan(float error_x, float error_y) {
    pipeline_contract::TargetPlan plan;
    plan.target_id = 1;
    plan.mode = pipeline_contract::ControlMode::BodyLockFollow;
    plan.lifecycle = pipeline_contract::TargetLifecycle::Observed;
    plan.error_px = {error_x, error_y};
    plan.aim_authority = 1.0f;
    plan.reliability = 1.0f;
    plan.response_scale = 500.0f;
    plan.response_confidence = 1.0f;
    return plan;
}

void test_inactive_plan_is_neutral() {
    controller_native::BodylockFollowController controller;
    pipeline_contract::TargetPlan plan;
    const auto output = controller.compute(plan, {}, 0.001f);
    require_true(near(output.x, 0.0f) && near(output.y, 0.0f),
                 "inactive plan must produce no target proposal");
}

void test_current_error_owns_position_proposal() {
    controller_native::BodylockFollowController controller;
    const auto right = controller.compute(active_plan(24.0f, 0.0f), {}, 0.001f);
    const auto below = controller.compute(active_plan(0.0f, 24.0f), {}, 0.001f);
    require_true(right.x > 0.0f && near(right.y, 0.0f),
                 "right-side source point must request right stick");
    require_true(below.y < 0.0f && near(below.x, 0.0f),
                 "screen-down source point must request negative stick Y");
}

void test_cue_lifecycle_uses_same_source_owned_solve() {
    controller_native::BodylockFollowController controller;
    auto observed = active_plan(18.0f, -12.0f);
    auto cue = observed;
    cue.lifecycle = pipeline_contract::TargetLifecycle::CueContinuation;
    const auto observed_output = controller.compute(observed, {}, 0.001f);
    const auto cue_output = controller.compute(cue, {}, 0.001f);
    require_true(near(observed_output.x, cue_output.x) &&
                     near(observed_output.y, cue_output.y),
                 "BodyLock must not maintain fresh/non-fresh alternate solves");
}

void test_manual_input_does_not_create_a_second_authority_policy() {
    controller_native::BodylockFollowController controller;
    const auto plan = active_plan(30.0f, 0.0f);
    pipeline_contract::IntentState opposing;
    opposing.filtered_right.x = -1.0f;
    opposing.right_x.confidence = 1.0f;
    opposing.right_confidence = 1.0f;
    const auto neutral = controller.compute(plan, {}, 0.001f);
    const auto with_manual = controller.compute(plan, opposing, 0.001f);
    require_true(near(neutral.x, with_manual.x) && near(neutral.y, with_manual.y),
                 "manual authority must be owned after BodyLock by the state machine");
}

void test_motion_cannot_reverse_current_position_axis() {
    controller_native::BodylockFollowController controller;
    auto plan = active_plan(-10.0f, 0.0f);
    plan.error_rate_px_per_sec = {260.0f, 120.0f};
    const auto output = controller.compute_detailed(plan, {}, 0.001f);
    require_true(output.radial_motion_bound_applied,
                 "opposing motion must exercise the current-position bound");
    require_true(output.constraint_reason ==
                     controller_native::ResponseModelConstraintReason::
                         PositionRadialMotionBound,
                 "position-motion bound must expose its single-path reason");
    require_true(output.stick.x < 0.0f,
                 "motion metadata must not reverse a material current error");
    require_true(near(output.effective_motion_stick.y, output.motion_stick.y),
                 "axis-local bound must preserve orthogonal motion");
}

void test_orthogonal_motion_cannot_mask_bodylock_axis_reversal() {
    controller_native::BodylockFollowController controller;
    auto plan = active_plan(-7.5f, -18.5f);
    plan.error_rate_px_per_sec = {190.0f, -250.0f};
    const auto output = controller.compute_detailed(plan, {}, 0.001f);
    const float vector_dot =
        output.position_stick.x * output.motion_stick.x +
        output.position_stick.y * output.motion_stick.y;

    require_true(output.position_stick.x * output.motion_stick.x < 0.0f,
                 "fixture must contain an X position-motion conflict");
    require_true(vector_dot > 0.0f,
                 "orthogonal motion must mask the old vector-wide conflict test");
    require_true(output.radial_motion_bound_applied,
                 "BodyLock must constrain the conflict on the affected axis");
    require_true(output.stick.x * output.position_stick.x >= 0.0f,
                 "BodyLock output must preserve the current X error direction");
    require_true(near(output.effective_motion_stick.y, output.motion_stick.y),
                 "BodyLock must retain compatible orthogonal feed-forward");
}

void test_force_envelope_remains_bounded() {
    controller_native::BodylockFollowControllerConfig config;
    config.max_force_x = 0.30f;
    config.max_force_y = 0.20f;
    controller_native::BodylockFollowController controller(config);
    const auto output = controller.compute(active_plan(300.0f, 300.0f), {}, 0.001f);
    const float ellipse = std::hypot(output.x / 0.30f, output.y / 0.20f);
    require_true(ellipse <= 1.0001f,
                 "BodyLock target proposal must stay inside its force ellipse");
}

void test_aligned_target_motion_replaces_screen_relative_hint() {
    controller_native::BodylockFollowController controller;
    auto plan = active_plan(0.0f, 0.0f);
    plan.error_rate_px_per_sec = {0.0f, 0.0f};
    plan.bodylock_target_motion_px_per_sec = {200.0f, 0.0f};
    plan.bodylock_target_motion_confidence = 0.8f;
    plan.bodylock_target_motion_valid = true;
    const auto output = controller.compute_detailed(plan, {}, 0.001f);
    require_true(near(output.error_rate_px_per_sec.x, 200.0f),
                 "BodyLock must expose the aligned target-motion demand");
    require_true(near(output.motion_stick.x, 0.4f),
                 "target motion must be a full sustaining total, not a 0.72 hint");
}

}  // namespace

void register_bodylock_follow_controller_tests(native_test::Registry& registry) {
    registry.add_case("BaseBodyLock", "inactive_plan_is_neutral", test_inactive_plan_is_neutral);
    registry.add_case("BaseBodyLock", "current_error_owns_position_proposal", test_current_error_owns_position_proposal);
    registry.add_case("BaseBodyLock", "cue_uses_same_source_owned_solve", test_cue_lifecycle_uses_same_source_owned_solve);
    registry.add_case("BaseBodyLock", "manual_input_does_not_create_second_policy", test_manual_input_does_not_create_a_second_authority_policy);
    registry.add_case("BaseBodyLock", "motion_cannot_reverse_position_axis", test_motion_cannot_reverse_current_position_axis);
    registry.add_case("BaseBodyLock", "orthogonal_motion_cannot_mask_reversal", test_orthogonal_motion_cannot_mask_bodylock_axis_reversal);
    registry.add_case("BaseBodyLock", "force_envelope_remains_bounded", test_force_envelope_remains_bounded);
    registry.add_case("BaseBodyLock", "aligned_motion_replaces_screen_hint", test_aligned_target_motion_replaces_screen_relative_hint);
}
