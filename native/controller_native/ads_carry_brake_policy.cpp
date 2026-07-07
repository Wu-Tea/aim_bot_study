#include "ads_carry_brake_policy.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace controller_native {

namespace {

float clamp_unit(float value) {
    return std::max(-1.0f, std::min(1.0f, value));
}

int axis_sign(float value, float deadzone) {
    if (value > deadzone) {
        return 1;
    }
    if (value < -deadzone) {
        return -1;
    }
    return 0;
}

float target_correction_axis(float target_error_px, bool y_axis, float deadzone) {
    const int error_sign = axis_sign(target_error_px, deadzone);
    if (error_sign == 0) {
        return 0.0f;
    }
    return static_cast<float>(y_axis ? -error_sign : error_sign);
}

}  // namespace

AdsCarryBrakePolicy::AdsCarryBrakePolicy(GamepadAiAimConfig ai_config)
    : ai_config_(std::move(ai_config)) {}

void AdsCarryBrakePolicy::reset() {}

GamepadOutputState AdsCarryBrakePolicy::apply(
    const AdsCarryBrakeInput& input) const {
    constexpr float kStrongManualCarryThreshold = 0.55f;
    if (!input.ads_active || !input.ads_acquisition_active) {
        return input.output;
    }
    if (std::max(std::fabs(input.manual_right_x), std::fabs(input.manual_right_y)) <
        kStrongManualCarryThreshold) {
        return input.output;
    }

    GamepadOutputState output = input.output;
    if (!input.has_fresh_target && !input.candidate_output_hold_active) {
        constexpr float kUnfreshCarryVectorCap = 0.45f;
        output.right_x = apply_unfresh_axis_limit(output.right_x, input.manual_right_x);
        output.right_y = apply_unfresh_axis_limit(output.right_y, input.manual_right_y);
        const float magnitude = std::hypot(output.right_x, output.right_y);
        if (magnitude > kUnfreshCarryVectorCap) {
            const float scale = kUnfreshCarryVectorCap / magnitude;
            output.right_x *= scale;
            output.right_y *= scale;
        }
        return output;
    }
    if (!input.body_lock_active) {
        return input.output;
    }

    output.right_x = apply_axis(
        input.target_error_x,
        output.right_x,
        input.manual_right_x,
        input.reticle_speed_px_per_sec,
        false);

    const float output_move_y = -output.right_y;
    const float manual_move_y = -input.manual_right_y;
    const float shaped_move_y = apply_axis(
        input.target_error_y,
        output_move_y,
        manual_move_y,
        input.reticle_speed_px_per_sec,
        true);
    output.right_y = clamp_unit(-shaped_move_y);
    return output;
}

float AdsCarryBrakePolicy::apply_axis(
    float target_error_px,
    float output_axis,
    float manual_axis,
    float reticle_speed_px_per_sec,
    bool y_axis) const {
    constexpr float kAxisDeadzone = 0.015f;
    constexpr float kManualAxisCarryThreshold = 0.22f;
    constexpr float kErrorDeadzonePx = 1.0f;
    constexpr float kNearErrorPx = 18.0f;
    constexpr float kFarErrorPx = 54.0f;
    constexpr float kNearHorizonSeconds = 0.050f;
    constexpr float kFarHorizonSeconds = 0.038f;
    constexpr float kNearOvershootBudgetPx = 5.0f;
    constexpr float kFarOvershootBudgetPx = 11.0f;
    constexpr float kWrongWayCorrectionCap = 0.22f;
    constexpr float kWrongWayCorrectionMin = 0.06f;

    const float abs_error = std::fabs(target_error_px);
    if (abs_error <= kErrorDeadzonePx || abs_error > kFarErrorPx ||
        std::fabs(output_axis) <= kAxisDeadzone) {
        return output_axis;
    }

    const float correction_sign =
        target_correction_axis(target_error_px, y_axis, kErrorDeadzonePx);
    if (correction_sign == 0.0f) {
        return output_axis;
    }

    const int output_sign = axis_sign(output_axis, kAxisDeadzone);
    if (output_sign == 0) {
        return output_axis;
    }

    if (static_cast<float>(output_sign) != correction_sign) {
        const float correction_magnitude = std::min(
            kWrongWayCorrectionCap,
            std::max(kWrongWayCorrectionMin, std::fabs(output_axis) * 0.35f));
        return correction_sign * correction_magnitude;
    }

    const int manual_sign = axis_sign(manual_axis, kManualAxisCarryThreshold);
    const bool manual_carrying_same_axis =
        manual_sign != 0 && manual_sign == output_sign;
    if (!manual_carrying_same_axis) {
        return output_axis;
    }

    const float t =
        std::max(0.0f, std::min(1.0f, (abs_error - kNearErrorPx) / (kFarErrorPx - kNearErrorPx)));
    const float horizon_seconds =
        kNearHorizonSeconds + ((kFarHorizonSeconds - kNearHorizonSeconds) * t);
    const float overshoot_budget_px =
        kNearOvershootBudgetPx + ((kFarOvershootBudgetPx - kNearOvershootBudgetPx) * t);
    const float safe_speed =
        std::max(1.0f, std::fabs(reticle_speed_px_per_sec));
    const float allowed_abs_axis =
        std::max(kAxisDeadzone, (abs_error + overshoot_budget_px) / (safe_speed * horizon_seconds));
    if (std::fabs(output_axis) <= allowed_abs_axis) {
        return output_axis;
    }

    return clamp_unit(correction_sign * allowed_abs_axis);
}

float AdsCarryBrakePolicy::apply_unfresh_axis_limit(
    float output_axis,
    float manual_axis) const {
    constexpr float kAxisDeadzone = 0.015f;
    constexpr float kManualAxisCarryThreshold = 0.22f;
    constexpr float kUnfreshCarryCap = 0.45f;

    const int output_sign = axis_sign(output_axis, kAxisDeadzone);
    if (output_sign == 0) {
        return output_axis;
    }

    const int manual_sign = axis_sign(manual_axis, kManualAxisCarryThreshold);
    const bool manual_carrying_same_axis =
        manual_sign != 0 && manual_sign == output_sign;
    if (!manual_carrying_same_axis || std::fabs(output_axis) <= kUnfreshCarryCap) {
        return output_axis;
    }

    return static_cast<float>(output_sign) * kUnfreshCarryCap;
}

}  // namespace controller_native
