#include "body_lock_short_plan_policy.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace controller_native {

namespace {

int axis_sign(float value, float deadzone) {
    if (value > deadzone) {
        return 1;
    }
    if (value < -deadzone) {
        return -1;
    }
    return 0;
}

constexpr float kOutputDeadzone = 0.015f;
constexpr float kCrossErrorDeadzonePx = 1.0f;
constexpr float kManualCrossBrakeOutputCap = 0.08f;
constexpr float kWrongWayCorrectionCap = 0.24f;
constexpr float kWrongWayCorrectionMin = 0.08f;
constexpr double kManualCrossBrakeSeconds = 0.130;

bool manual_pushes_away(float manual, float error, bool y_axis) {
    if (manual == 0.0f || error == 0.0f) {
        return false;
    }
    return y_axis ? (manual * error > 0.0f) : (manual * error < 0.0f);
}

bool output_pushes_away(float output_axis, float error, bool y_axis) {
    if (output_axis == 0.0f || error == 0.0f) {
        return false;
    }
    return y_axis ? (output_axis * error > 0.0f) : (output_axis * error < 0.0f);
}

float target_correction_sign(float error, bool y_axis, float error_deadzone) {
    const int error_sign = axis_sign(error, error_deadzone);
    if (error_sign == 0) {
        return 0.0f;
    }
    return static_cast<float>(y_axis ? -error_sign : error_sign);
}

float bounded_wrong_way_correction(
    float output_axis,
    float current_error,
    bool y_axis,
    float error_deadzone) {
    const float correction_sign =
        target_correction_sign(current_error, y_axis, error_deadzone);
    if (correction_sign == 0.0f) {
        return 0.0f;
    }
    const float correction_magnitude = std::min(
        kWrongWayCorrectionCap,
        std::max(kWrongWayCorrectionMin, std::fabs(output_axis) * 0.45f));
    return correction_sign * correction_magnitude;
}

}  // namespace

BodyLockShortPlanPolicy::BodyLockShortPlanPolicy(GamepadAiAimConfig ai_config)
    : ai_config_(std::move(ai_config)) {}

void BodyLockShortPlanPolicy::reset() {
    has_last_aim_error_ = false;
    last_aim_error_x_ = 0.0f;
    last_aim_error_y_ = 0.0f;
    manual_brake_x_until_seconds_ = 0.0;
    manual_brake_y_until_seconds_ = 0.0;
    manual_brake_x_sign_ = 0;
    manual_brake_y_sign_ = 0;
}

GamepadOutputState BodyLockShortPlanPolicy::apply(
    const BodyLockShortPlanInput& input) {
    GamepadOutputState output = input.output;
    apply_aim_error_manual_cross_brake(input, output);
    if (input.vision_state.has_target && input.vision_state.aim_authority) {
        apply_active_manual_cross_brake(
            input.manual_right_x,
            output.right_x,
            manual_brake_x_until_seconds_,
            manual_brake_x_sign_,
            input.vision_state.dx,
            false,
            input.now_seconds);
        apply_active_manual_cross_brake(
            input.manual_right_y,
            output.right_y,
            manual_brake_y_until_seconds_,
            manual_brake_y_sign_,
            input.vision_state.dy,
            true,
            input.now_seconds);
    } else {
        manual_brake_x_until_seconds_ = 0.0;
        manual_brake_y_until_seconds_ = 0.0;
        manual_brake_x_sign_ = 0;
        manual_brake_y_sign_ = 0;
    }
    return output;
}

void BodyLockShortPlanPolicy::apply_aim_error_manual_cross_brake(
    const BodyLockShortPlanInput& input,
    GamepadOutputState& output) {
    if (input.vision_state.has_target && input.vision_state.aim_authority) {
        if (has_last_aim_error_) {
            update_manual_cross_brake(
                last_aim_error_x_,
                input.vision_state.dx,
                input.manual_right_x,
                output.right_x,
                manual_brake_x_until_seconds_,
                manual_brake_x_sign_,
                false,
                input.now_seconds);
            update_manual_cross_brake(
                last_aim_error_y_,
                input.vision_state.dy,
                input.manual_right_y,
                output.right_y,
                manual_brake_y_until_seconds_,
                manual_brake_y_sign_,
                true,
                input.now_seconds);
        }
        has_last_aim_error_ = true;
        last_aim_error_x_ = input.vision_state.dx;
        last_aim_error_y_ = input.vision_state.dy;
    } else {
        has_last_aim_error_ = false;
        last_aim_error_x_ = 0.0f;
        last_aim_error_y_ = 0.0f;
    }
}

void BodyLockShortPlanPolicy::apply_active_manual_cross_brake(
    float manual_axis,
    float& output_axis,
    double& brake_until_seconds,
    int& brake_manual_sign,
    float current_error,
    bool y_axis,
    double now_seconds) {
    if (brake_until_seconds <= 0.0) {
        return;
    }
    if (now_seconds > brake_until_seconds) {
        brake_until_seconds = 0.0;
        brake_manual_sign = 0;
        return;
    }
    const int manual_sign = axis_sign(manual_axis, kOutputDeadzone);
    if (manual_sign == 0) {
        brake_until_seconds = 0.0;
        brake_manual_sign = 0;
        return;
    }
    if (brake_manual_sign == 0) {
        brake_manual_sign = manual_sign;
    } else if (manual_sign != brake_manual_sign) {
        brake_until_seconds = 0.0;
        brake_manual_sign = 0;
        return;
    }
    if (axis_sign(output_axis, kOutputDeadzone) != brake_manual_sign) {
        return;
    }
    if (std::fabs(output_axis) <= kManualCrossBrakeOutputCap) {
        if (output_pushes_away(output_axis, current_error, y_axis)) {
            output_axis = bounded_wrong_way_correction(
                output_axis,
                current_error,
                y_axis,
                kCrossErrorDeadzonePx);
        }
        return;
    }
    if (output_pushes_away(output_axis, current_error, y_axis)) {
        output_axis = bounded_wrong_way_correction(
            output_axis,
            current_error,
            y_axis,
            kCrossErrorDeadzonePx);
        return;
    }
    output_axis = std::copysign(kManualCrossBrakeOutputCap, output_axis);
}

void BodyLockShortPlanPolicy::update_manual_cross_brake(
    float previous_error,
    float current_error,
    float manual_axis,
    float& output_axis,
    double& brake_until_seconds,
    int& brake_manual_sign,
    bool y_axis,
    double now_seconds) {
    const float manual_escape_threshold = std::max(
        0.0f,
        std::min(1.0f, ai_config_.body_lock_manual_escape_input_threshold));
    const float manual_cross_brake_arm_px =
        std::max(16.0f, ai_config_.body_lock_near_lock_error_px);
    const int previous_sign = axis_sign(previous_error, kCrossErrorDeadzonePx);
    const int current_sign = axis_sign(current_error, kCrossErrorDeadzonePx);
    const int manual_sign = axis_sign(manual_axis, kOutputDeadzone);
    const bool crossed =
        previous_sign != 0 && current_sign != 0 && previous_sign != current_sign;
    const bool same_side_moving_away =
        previous_sign != 0 &&
        previous_sign == current_sign &&
        std::fabs(current_error) <= manual_cross_brake_arm_px * 2.0f &&
        std::fabs(current_error) > std::fabs(previous_error) + kCrossErrorDeadzonePx;
    const bool crossed_with_wrong_way_manual =
        crossed &&
        std::fabs(current_error) <= manual_cross_brake_arm_px &&
        std::fabs(manual_axis) >= kOutputDeadzone &&
        manual_pushes_away(manual_axis, current_error, y_axis);
    const bool worsening_with_wrong_way_manual =
        same_side_moving_away &&
        std::fabs(manual_axis) >= manual_escape_threshold &&
        manual_pushes_away(manual_axis, current_error, y_axis);
    const bool large_wrong_way_manual =
        std::fabs(current_error) > manual_cross_brake_arm_px &&
        std::fabs(current_error) <= manual_cross_brake_arm_px * 2.0f &&
        std::fabs(manual_axis) >= manual_escape_threshold &&
        manual_pushes_away(manual_axis, current_error, y_axis);
    if (crossed_with_wrong_way_manual || worsening_with_wrong_way_manual ||
        large_wrong_way_manual) {
        brake_until_seconds = now_seconds + kManualCrossBrakeSeconds;
        brake_manual_sign = manual_sign;
    }
    apply_active_manual_cross_brake(
        manual_axis,
        output_axis,
        brake_until_seconds,
        brake_manual_sign,
        current_error,
        y_axis,
        now_seconds);
}

}  // namespace controller_native
