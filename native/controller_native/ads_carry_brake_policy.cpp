#include "ads_carry_brake_policy.h"

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

}  // namespace

AdsCarryBrakePolicy::AdsCarryBrakePolicy(GamepadAiAimConfig ai_config)
    : ai_config_(std::move(ai_config)) {}

void AdsCarryBrakePolicy::reset() {}

GamepadOutputState AdsCarryBrakePolicy::apply(
    const AdsCarryBrakeInput& input) const {
    constexpr float kStrongManualCarryThreshold = 0.55f;
    if (input.body_lock_active) {
        return input.output;
    }
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
    return input.output;
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
