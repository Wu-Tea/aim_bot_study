#include "bodylock_follow_controller.h"
#include "test_support/native_test_registry.h"

#include <cmath>
#include <algorithm>
#include <fstream>
#include <iomanip>
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
    require_true(near(output.effective_motion_stick.y, 0.0f),
                 "unconfirmed screen rate cannot own a centered orthogonal axis");
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

// Log-derived local invariant, not a replay of the recorded game/plant.
// Freeze before the production repair: continuous work through e=0, nonzero
// sustaining demand, and no centered work from untrusted screen-rate noise.
void test_center_crossing_incident(const native_test::TestContext& context) {
    float maximum_crossing_step = 0.0f;
    float minimum_sustaining_ratio = 1.0f;
    float maximum_untrusted_jitter = 0.0f;
    float maximum_static_jitter = 0.0f;
    int crossings = 0;
    int far_position_controls = 0;
    int reversal_controls = 0;
    int lifecycle_controls = 0;
    int zero_motion_controls = 0;
    std::filesystem::create_directories(context.artifact_directory);
    std::ofstream report(context.artifact_path("bodylock-center-crossing.json"));
    report << std::setprecision(9) << "{\n\"samples\":[";
    bool first = true;
    for (const bool dynamic : {false, true}) {
        controller_native::BodylockFollowControllerConfig config;
        config.max_force_x = 0.60f;
        config.max_force_y = 0.66f;
        config.feedback_range_x_px = config.feedback_range_y_px = 24.0f;
        config.feedforward_gain = 0.72f;
        config.response_curve.algorithm = dynamic
            ? controller_native::AimResponseCurveAlgorithm::CodDynamicLegacyLut
            : controller_native::AimResponseCurveAlgorithm::Linear;
        controller_native::BodylockFollowController controller(config);
        for (const bool y_axis : {false, true}) {
            const auto axis = [y_axis](pipeline_contract::Vec2f value) {
                return y_axis ? -value.y : value.x; // screen coordinate
            };
            for (const float direction : {-1.0f, 1.0f}) {
                for (const float authority : {0.65f, 1.0f}) {
                    auto plan = active_plan(0.0f, 0.0f);
                    plan.selector_target_generation = 340;
                    plan.physical_ads_epoch = 148;
                    plan.direct_person_observation = true;
                    plan.aim_authority = authority;
                    plan.bodylock_target_motion_valid = true;
                    plan.bodylock_target_motion_confidence = 0.8f;
                    plan.bodylock_target_motion_px_per_sec = y_axis
                        ? pipeline_contract::Vec2f{0.0f, direction * 100.0f}
                        : pipeline_contract::Vec2f{direction * 100.0f, 0.0f};
                    const auto set_error = [&](float value) {
                        plan.error_px = y_axis
                            ? pipeline_contract::Vec2f{0.0f, value}
                            : pipeline_contract::Vec2f{value, 0.0f};
                    };
                    const float centered = axis(controller.compute(plan, {}, 0.001f));
                    require_true(centered * direction > 0.01f,
                        "trigger requires nonzero sustaining work at center");
                    float previous = 0.0f;
                    bool have_previous = false;
                    for (const float error : {-0.001f, 0.0f, 0.001f}) {
                        set_error(error);
                        const auto result = controller.compute_detailed(plan, {}, 0.001f);
                        require_true(error == 0.0f || std::fabs(axis(result.position_stick)) > 1e-5f,
                            "nonzero probes must lie outside the known-bad exact-zero exception");
                        const float value = axis(result.stick);
                        require_true(std::fabs(axis(result.motion_stick) - direction * 0.2f) < 1e-6f,
                            "trigger must hold total motion fixed on both sides of zero");
                        if (have_previous) maximum_crossing_step = std::max(
                            maximum_crossing_step, std::fabs(value - previous));
                        previous = value;
                        have_previous = true;
                        if (!first) report << ',';
                        first = false;
                        report << "{\"dynamic\":" << dynamic << ",\"y_axis\":" << y_axis
                               << ",\"direction\":" << direction << ",\"authority\":" << authority
                               << ",\"error\":" << error << ",\"request\":" << value << '}';
                    }
                    ++crossings;
                    for (const float error : {-1.0f, 0.0f, 1.0f}) {
                        set_error(error);
                        minimum_sustaining_ratio = std::min(minimum_sustaining_ratio,
                            axis(controller.compute(plan, {}, 0.001f)) / centered);
                    }
                    set_error(-direction * 20.0f);
                    if (axis(controller.compute(plan, {}, 0.001f)) * direction < 0.0f)
                        ++far_position_controls;
                    set_error(0.0f);
                    plan.bodylock_target_motion_px_per_sec = y_axis
                        ? pipeline_contract::Vec2f{0.0f, -direction * 100.0f}
                        : pipeline_contract::Vec2f{-direction * 100.0f, 0.0f};
                    if (axis(controller.compute(plan, {}, 0.001f)) * direction < -0.01f)
                        ++reversal_controls;
                    plan.lifecycle = pipeline_contract::TargetLifecycle::None;
                    if (axis(controller.compute(plan, {}, 0.001f)) == 0.0f)
                        ++lifecycle_controls;
                    plan.lifecycle = pipeline_contract::TargetLifecycle::Observed;
                    plan.bodylock_target_motion_px_per_sec = {};
                    if (axis(controller.compute(plan, {}, 0.001f)) == 0.0f)
                        ++zero_motion_controls;
                    plan.bodylock_target_motion_valid = false;
                    for (const float error : {-0.25f, 0.0f, 0.25f}) {
                        set_error(error);
                        plan.error_rate_px_per_sec = {};
                        maximum_static_jitter = std::max(maximum_static_jitter,
                            std::fabs(axis(controller.compute(plan, {}, 0.001f))));
                        // Screen-rate disturbance does not establish target velocity.
                        plan.error_rate_px_per_sec = y_axis
                            ? pipeline_contract::Vec2f{0.0f, direction * 100.0f}
                            : pipeline_contract::Vec2f{direction * 100.0f, 0.0f};
                        maximum_untrusted_jitter = std::max(maximum_untrusted_jitter,
                            std::fabs(axis(controller.compute(plan, {}, 0.001f))));
                    }
                }
            }
        }
    }
    const bool controls_valid = crossings == 16 && far_position_controls == 16 &&
        reversal_controls == 16 && lifecycle_controls == 16 && zero_motion_controls == 16;
    report << "],\n\"trigger_count\":" << crossings
           << ",\n\"counterfactuals_valid\":" << std::boolalpha << controls_valid
           << ",\n\"maximum_crossing_step\":" << maximum_crossing_step
           << ",\n\"minimum_sustaining_ratio\":" << minimum_sustaining_ratio
           << ",\n\"maximum_untrusted_jitter\":" << maximum_untrusted_jitter
           << ",\n\"maximum_static_jitter\":" << maximum_static_jitter << "\n}\n";
    report.close();
    require_true(controls_valid, "incident trigger and negative controls must execute");
    require_true(maximum_crossing_step <= 0.001f && minimum_sustaining_ratio >= 0.5f &&
                     maximum_untrusted_jitter <= 0.03f && maximum_static_jitter <= 0.03f,
                 "BodyLock crossing/noise contract failed; see measured incident artifact");
}

}  // namespace

void register_bodylock_follow_controller_tests(native_test::Registry& registry) {
    registry.add_context_case("BaseBodyLock", "center_crossing_incident", test_center_crossing_incident);
    registry.add_case("BaseBodyLock", "inactive_plan_is_neutral", test_inactive_plan_is_neutral);
    registry.add_case("BaseBodyLock", "current_error_owns_position_proposal", test_current_error_owns_position_proposal);
    registry.add_case("BaseBodyLock", "cue_uses_same_source_owned_solve", test_cue_lifecycle_uses_same_source_owned_solve);
    registry.add_case("BaseBodyLock", "manual_input_does_not_create_second_policy", test_manual_input_does_not_create_a_second_authority_policy);
    registry.add_case("BaseBodyLock", "motion_cannot_reverse_position_axis", test_motion_cannot_reverse_current_position_axis);
    registry.add_case("BaseBodyLock", "orthogonal_motion_cannot_mask_reversal", test_orthogonal_motion_cannot_mask_bodylock_axis_reversal);
    registry.add_case("BaseBodyLock", "force_envelope_remains_bounded", test_force_envelope_remains_bounded);
    registry.add_case("BaseBodyLock", "aligned_motion_replaces_screen_hint", test_aligned_target_motion_replaces_screen_relative_hint);
}
