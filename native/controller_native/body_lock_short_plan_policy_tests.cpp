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

void test_policy_does_not_hold_or_zero_small_vector_turns() {
    controller_native::GamepadAiAimConfig config;
    controller_native::BodyLockShortPlanPolicy policy(config);

    controller_native::BodyLockShortPlanInput first;
    first.vision_state = aim_target(2.0f, 1.0f);
    first.body_lock_available = true;
    first.now_seconds = 11.000;
    first.output.right_x = 0.050f;
    policy.apply(first);

    controller_native::BodyLockShortPlanInput flipped;
    flipped.vision_state = aim_target(2.0f, 1.0f);
    flipped.body_lock_available = true;
    flipped.now_seconds = 11.010;
    flipped.output.right_x = -0.040f;
    const controller_native::GamepadOutputState output = policy.apply(flipped);

    require_near(
        output.right_x,
        -0.040f,
        0.001f,
        "the ADS crossing policy must not own BodyLock-style short-plan holds");
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

void test_manual_cross_brake_remains_active_for_ads_crossing() {
    controller_native::GamepadAiAimConfig config;
    config.body_lock_near_lock_error_px = 32.0f;
    config.body_lock_manual_escape_input_threshold = 0.45f;
    controller_native::BodyLockShortPlanPolicy policy(config);

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
}

}  // namespace

int main() {
    test_policy_does_not_hold_or_zero_small_vector_turns();
    test_manual_cross_brake_corrects_wrong_way_manual_after_crossing();
    test_manual_cross_brake_remains_active_for_ads_crossing();
    return 0;
}
