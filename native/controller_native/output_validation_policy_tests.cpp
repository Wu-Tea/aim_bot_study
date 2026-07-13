#include "output_validation_policy.h"

#include <cmath>
#include <stdexcept>

namespace {

void require_near(float actual, float expected, float tolerance, const char* message) {
    if (std::fabs(actual - expected) > tolerance) {
        throw std::runtime_error(message);
    }
}

controller_native::NativeControllerVisionState observed_target(
    float dx,
    float dy,
    double observed_at_seconds) {
    controller_native::NativeControllerVisionState state;
    state.has_target = true;
    state.aim_authority = true;
    state.fire_authority = true;
    state.target_tier = "strong";
    state.dx = dx;
    state.dy = dy;
    state.observed_at_seconds = observed_at_seconds;
    return state;
}

controller_native::OutputValidationPolicyInput base_input(
    float dx,
    float dy,
    double now_seconds) {
    controller_native::OutputValidationPolicyInput input;
    input.vision_state = observed_target(dx, dy, now_seconds);
    input.now_seconds = now_seconds;
    input.output.right_x = 0.0f;
    input.output.right_y = 0.0f;
    return input;
}

void test_crossed_wrong_way_x_axis_gets_bounded_correction() {
    controller_native::GamepadAiAimConfig config;
    config.body_lock_near_lock_error_px = 32.0f;
    controller_native::OutputValidationPolicy policy(config);

    controller_native::OutputValidationPolicyInput first = base_input(20.0f, 0.0f, 10.000);
    first.output.right_x = 0.20f;
    policy.apply(first);

    controller_native::OutputValidationPolicyInput crossed = base_input(-12.0f, 0.0f, 10.010);
    crossed.output.right_x = 0.60f;
    const controller_native::GamepadOutputState output = policy.apply(crossed);

    require_near(
        output.right_x,
        -0.24f,
        0.001f,
        "crossed x axis should receive capped correction toward the target");
}

void test_active_wrong_way_window_continues_after_crossing() {
    controller_native::GamepadAiAimConfig config;
    config.body_lock_near_lock_error_px = 32.0f;
    controller_native::OutputValidationPolicy policy(config);

    controller_native::OutputValidationPolicyInput first = base_input(20.0f, 0.0f, 10.000);
    policy.apply(first);

    controller_native::OutputValidationPolicyInput crossed = base_input(-12.0f, 0.0f, 10.010);
    crossed.output.right_x = 0.60f;
    policy.apply(crossed);

    controller_native::OutputValidationPolicyInput active = base_input(-10.0f, 0.0f, 10.080);
    active.output.right_x = 0.30f;
    const controller_native::GamepadOutputState output = policy.apply(active);

    require_near(
        output.right_x,
        -0.135f,
        0.001f,
        "active correction window should keep correcting wrong-way x output");
}

void test_stale_ads_observed_target_corrects_wrong_way_without_prior_crossing() {
    controller_native::GamepadAiAimConfig config;
    config.target_max_age_ms = 96.0f;
    config.body_lock_near_lock_error_px = 32.0f;
    controller_native::OutputValidationPolicy policy(config);

    controller_native::OutputValidationPolicyInput stale = base_input(20.0f, 0.0f, 10.060);
    stale.vision_state.observed_at_seconds = 10.000;
    stale.ads_active = true;
    stale.output.right_x = -0.50f;
    const controller_native::GamepadOutputState output = policy.apply(stale);

    require_near(
        output.right_x,
        0.225f,
        0.001f,
        "stale ADS guard should bound wrong-way x output before target TTL expires");
}

void test_tracker_projection_preserves_manual_correction_and_bounds_projected_wrong_way() {
    controller_native::GamepadAiAimConfig config;
    controller_native::OutputValidationPolicy policy(config);

    controller_native::OutputValidationPolicyInput manual = base_input(0.0f, 0.0f, 11.000);
    manual.vision_state.has_tracker_projection = true;
    manual.vision_state.tracker_dx = 24.0f;
    manual.manual_right_x = 0.40f;
    manual.output.right_x = -0.20f;
    controller_native::GamepadOutputState output = policy.apply(manual);
    require_near(
        output.right_x,
        0.40f,
        0.001f,
        "tracker-backed manual correction should not be opposed by stale assist");

    controller_native::OutputValidationPolicyInput projected = base_input(0.0f, 0.0f, 11.010);
    projected.vision_state.has_tracker_projection = true;
    projected.vision_state.aim_authority = false;
    projected.vision_state.authority_decision_valid = true;
    projected.vision_state.assist_authority_state =
        pipeline_contract::AssistAuthorityState::TrackOnly;
    projected.vision_state.tracker_dx = 24.0f;
    projected.output.right_x = -0.30f;
    output = policy.apply(projected);
    require_near(
        output.right_x,
        0.0f,
        0.001f,
        "track-only projection should return exact neutral manual input");
}

void test_candidate_output_hold_bounds_wrong_way_axes_without_hard_zero() {
    controller_native::GamepadAiAimConfig config;
    controller_native::OutputValidationPolicy policy(config);

    controller_native::OutputValidationPolicyInput input = base_input(20.0f, -10.0f, 12.000);
    input.candidate_output_hold_active = true;
    input.output.right_x = -0.50f;
    input.output.right_y = -0.25f;
    const controller_native::GamepadOutputState output = policy.apply(input);

    require_near(output.right_x, 0.225f, 0.001f, "candidate hold should correct wrong-way x");
    require_near(output.right_y, 0.1125f, 0.001f, "candidate hold should correct wrong-way y");
}

void test_track_only_projection_preserves_exact_manual_axes() {
    controller_native::GamepadAiAimConfig config;
    controller_native::OutputValidationPolicy policy(config);

    controller_native::OutputValidationPolicyInput input = base_input(0.0f, 0.0f, 13.000);
    input.vision_state.aim_authority = false;
    input.vision_state.fire_authority = false;
    input.vision_state.assist_authority_state =
        pipeline_contract::AssistAuthorityState::TrackOnly;
    input.vision_state.authority_decision_valid = true;
    input.vision_state.has_tracker_projection = true;
    input.vision_state.tracker_dx = -100.0f;
    input.vision_state.tracker_dy = 100.0f;
    input.manual_right_x = 0.80f;
    input.manual_right_y = 0.66f;
    input.output.right_x = 0.80f;
    input.output.right_y = 0.66f;

    const controller_native::GamepadOutputState output = policy.apply(input);
    require_near(output.right_x, 0.80f, 0.0001f,
                 "track-only projection must not replace manual X");
    require_near(output.right_y, 0.66f, 0.0001f,
                 "track-only projection must not replace manual Y");
}

}  // namespace

int main() {
    test_crossed_wrong_way_x_axis_gets_bounded_correction();
    test_active_wrong_way_window_continues_after_crossing();
    test_stale_ads_observed_target_corrects_wrong_way_without_prior_crossing();
    test_tracker_projection_preserves_manual_correction_and_bounds_projected_wrong_way();
    test_candidate_output_hold_bounds_wrong_way_axes_without_hard_zero();
    test_track_only_projection_preserves_exact_manual_axes();
    return 0;
}
