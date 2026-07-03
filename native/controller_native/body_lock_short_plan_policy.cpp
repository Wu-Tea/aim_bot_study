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
constexpr double kShortPlanSeconds = 0.018;
constexpr float kSmallPlanMagnitude = 0.08f;
constexpr float kSmallVectorTurnScale = 0.55f;

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
    reset_short_plan_state();
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
    const float manual_escape_threshold = std::max(
        0.0f,
        std::min(1.0f, ai_config_.body_lock_manual_escape_input_threshold));

    apply_aim_error_manual_cross_brake(input, output);

    if (!input.body_lock_available) {
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
        reset_short_plan_state();
        return output;
    }

    const float near_lock_px = std::max(1.0f, ai_config_.body_lock_near_lock_error_px);
    const float error_radius = std::hypot(input.lock_dx, input.lock_dy);

    if (has_last_body_lock_error_) {
        update_manual_cross_brake(
            last_body_lock_error_x_,
            input.lock_dx,
            input.manual_right_x,
            output.right_x,
            manual_brake_x_until_seconds_,
            manual_brake_x_sign_,
            false,
            input.now_seconds);
        update_manual_cross_brake(
            last_body_lock_error_y_,
            input.lock_dy,
            input.manual_right_y,
            output.right_y,
            manual_brake_y_until_seconds_,
            manual_brake_y_sign_,
            true,
            input.now_seconds);
    }
    if (error_radius > near_lock_px) {
        short_plan_x_until_seconds_ = 0.0;
        short_plan_y_until_seconds_ = 0.0;
        has_last_short_plan_x_ = true;
        has_last_short_plan_y_ = true;
        last_short_plan_x_ = output.right_x;
        last_short_plan_y_ = output.right_y;
        has_last_body_lock_error_ = true;
        last_body_lock_error_x_ = input.lock_dx;
        last_body_lock_error_y_ = input.lock_dy;
        return output;
    }

    const float previous_plan_x = last_short_plan_x_;
    const float previous_plan_y = last_short_plan_y_;
    const bool had_previous_plan = has_last_short_plan_x_ && has_last_short_plan_y_;
    if (std::fabs(input.manual_right_x) < manual_escape_threshold) {
        const int previous_sign =
            has_last_short_plan_x_ ? axis_sign(last_short_plan_x_, kOutputDeadzone) : 0;
        const int current_sign = axis_sign(output.right_x, kOutputDeadzone);
        if (previous_sign != 0 && current_sign != 0 && previous_sign != current_sign) {
            short_plan_x_until_seconds_ = input.now_seconds + kShortPlanSeconds;
        }
    } else {
        short_plan_x_until_seconds_ = 0.0;
    }

    if (short_plan_x_until_seconds_ > 0.0 &&
        input.now_seconds <= short_plan_x_until_seconds_) {
        output.right_x = 0.0f;
    }

    if (input.vertical_plan_allowed &&
        std::fabs(input.manual_right_y) < manual_escape_threshold) {
        const int previous_sign =
            has_last_short_plan_y_ ? axis_sign(last_short_plan_y_, kOutputDeadzone) : 0;
        const int current_sign = axis_sign(output.right_y, kOutputDeadzone);
        if (previous_sign != 0 && current_sign != 0 && previous_sign != current_sign) {
            short_plan_y_until_seconds_ = input.now_seconds + kShortPlanSeconds;
        }
    } else {
        short_plan_y_until_seconds_ = 0.0;
    }

    if (short_plan_y_until_seconds_ > 0.0 &&
        input.now_seconds <= short_plan_y_until_seconds_) {
        output.right_y = 0.0f;
    }

    if (input.vertical_plan_allowed &&
        std::fabs(input.manual_right_x) < manual_escape_threshold &&
        std::fabs(input.manual_right_y) < manual_escape_threshold &&
        had_previous_plan) {
        const float previous_mag = std::hypot(previous_plan_x, previous_plan_y);
        const float current_mag = std::hypot(output.right_x, output.right_y);
        if (previous_mag >= kOutputDeadzone &&
            current_mag >= kOutputDeadzone &&
            previous_mag <= kSmallPlanMagnitude &&
            current_mag <= kSmallPlanMagnitude) {
            const float alignment =
                ((previous_plan_x * output.right_x) + (previous_plan_y * output.right_y)) /
                (previous_mag * current_mag);
            if (alignment < 0.25f) {
                output.right_x *= kSmallVectorTurnScale;
                output.right_y *= kSmallVectorTurnScale;
            }
        }
    }

    has_last_short_plan_x_ = true;
    has_last_short_plan_y_ = true;
    last_short_plan_x_ = output.right_x;
    last_short_plan_y_ = output.right_y;
    has_last_body_lock_error_ = true;
    last_body_lock_error_x_ = input.lock_dx;
    last_body_lock_error_y_ = input.lock_dy;
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

void BodyLockShortPlanPolicy::reset_short_plan_state() {
    has_last_short_plan_x_ = false;
    has_last_short_plan_y_ = false;
    has_last_body_lock_error_ = false;
    last_short_plan_x_ = 0.0f;
    last_short_plan_y_ = 0.0f;
    last_body_lock_error_x_ = 0.0f;
    last_body_lock_error_y_ = 0.0f;
    short_plan_x_until_seconds_ = 0.0;
    short_plan_y_until_seconds_ = 0.0;
}

}  // namespace controller_native
