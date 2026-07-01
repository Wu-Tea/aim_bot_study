#include "body_lock_short_plan_policy.h"

#include <cmath>
#include <stdexcept>

namespace {

void require_near(float actual, float expected, float tolerance, const char* message) {
    if (std::fabs(actual - expected) > tolerance) {
        throw std::runtime_error(message);
    }
}

void require_true(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

controller_native::NativeControllerVisionState aim_target(float dx, float dy) {
    controller_native::NativeControllerVisionState state;
    state.has_target = true;
    state.aim_authority = true;
    state.fire_authority = true;
    state.target_tier = "strong";
    state.dx = dx;
    state.dy = dy;
    return state;
}

controller_native::BodyLockShortPlanInput body_lock_input(double now_seconds) {
    controller_native::BodyLockShortPlanInput input;
    input.vision_state = aim_target(2.0f, 1.0f);
    input.body_lock_available = true;
    input.lock_dx = 2.0f;
    input.lock_dy = 1.0f;
    input.vertical_plan_allowed = true;
    input.now_seconds = now_seconds;
    return input;
}

void test_short_plan_zeros_x_axis_on_near_lock_sign_flip() {
    controller_native::GamepadAiAimConfig config;
    config.body_lock_near_lock_error_px = 32.0f;
    config.body_lock_manual_escape_input_threshold = 0.45f;
    controller_native::BodyLockShortPlanPolicy policy(config);

    controller_native::BodyLockShortPlanInput first = body_lock_input(10.000);
    first.output.right_x = 0.050f;
    policy.apply(first);

    controller_native::BodyLockShortPlanInput flipped = body_lock_input(10.010);
    flipped.output.right_x = -0.040f;
    const controller_native::GamepadOutputState output = policy.apply(flipped);

    require_near(
        output.right_x,
        0.0f,
        0.001f,
        "near-lock x sign flip should briefly zero the x axis");
}

void test_short_plan_respects_manual_escape_threshold() {
    controller_native::GamepadAiAimConfig config;
    config.body_lock_near_lock_error_px = 32.0f;
    config.body_lock_manual_escape_input_threshold = 0.45f;
    controller_native::BodyLockShortPlanPolicy policy(config);

    controller_native::BodyLockShortPlanInput first = body_lock_input(11.000);
    first.output.right_x = 0.050f;
    policy.apply(first);

    controller_native::BodyLockShortPlanInput flipped = body_lock_input(11.010);
    flipped.manual_right_x = 0.60f;
    flipped.output.right_x = -0.040f;
    const controller_native::GamepadOutputState output = policy.apply(flipped);

    require_near(
        output.right_x,
        -0.040f,
        0.001f,
        "manual escape should prevent x short-plan zeroing");
}

void test_manual_cross_brake_corrects_wrong_way_manual_after_crossing() {
    controller_native::GamepadAiAimConfig config;
    config.body_lock_near_lock_error_px = 32.0f;
    config.body_lock_manual_escape_input_threshold = 0.45f;
    controller_native::BodyLockShortPlanPolicy policy(config);

    controller_native::BodyLockShortPlanInput first;
    first.vision_state = aim_target(20.0f, 0.0f);
    first.output.right_x = 0.10f;
    first.now_seconds = 12.000;
    policy.apply(first);

    controller_native::BodyLockShortPlanInput crossed;
    crossed.vision_state = aim_target(-10.0f, 0.0f);
    crossed.manual_right_x = 0.50f;
    crossed.output.right_x = 0.50f;
    crossed.now_seconds = 12.010;
    const controller_native::GamepadOutputState output = policy.apply(crossed);

    require_near(
        output.right_x,
        -0.225f,
        0.001f,
        "wrong-way manual crossing should apply bounded reverse correction");
}

void test_non_body_lock_resets_short_plan_but_keeps_active_manual_brake() {
    controller_native::GamepadAiAimConfig config;
    config.body_lock_near_lock_error_px = 32.0f;
    config.body_lock_manual_escape_input_threshold = 0.45f;
    controller_native::BodyLockShortPlanPolicy policy(config);

    controller_native::BodyLockShortPlanInput first = body_lock_input(13.000);
    first.output.right_x = 0.050f;
    policy.apply(first);

    controller_native::BodyLockShortPlanInput crossed;
    crossed.vision_state = aim_target(20.0f, 0.0f);
    crossed.output.right_x = 0.10f;
    crossed.now_seconds = 13.010;
    policy.apply(crossed);

    crossed.vision_state = aim_target(-10.0f, 0.0f);
    crossed.manual_right_x = 0.50f;
    crossed.output.right_x = 0.50f;
    crossed.now_seconds = 13.020;
    const controller_native::GamepadOutputState braked = policy.apply(crossed);
    require_true(braked.right_x < 0.0f, "manual brake should still run without body lock");

    controller_native::BodyLockShortPlanInput body_lock = body_lock_input(13.030);
    body_lock.output.right_x = -0.040f;
    const controller_native::GamepadOutputState output = policy.apply(body_lock);
    require_near(
        output.right_x,
        -0.040f,
        0.001f,
        "leaving body lock should reset previous short-plan sign memory");
}

}  // namespace

int main() {
    test_short_plan_zeros_x_axis_on_near_lock_sign_flip();
    test_short_plan_respects_manual_escape_threshold();
    test_manual_cross_brake_corrects_wrong_way_manual_after_crossing();
    test_non_body_lock_resets_short_plan_but_keeps_active_manual_brake();
    return 0;
}
