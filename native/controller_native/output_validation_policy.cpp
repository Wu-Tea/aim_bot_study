#include "output_validation_policy.h"

#include "../tracking_native/tracker_authority.h"

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

constexpr float kWrongWayCorrectionCap = 0.24f;
constexpr float kWrongWayCorrectionMin = 0.08f;
constexpr float kCrossErrorDeadzonePx = 1.0f;
constexpr float kManualCorrectionDeadzone = 0.05f;
constexpr double kOutputValidationCorrectionSeconds = 0.130;

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

bool output_pushes_away(float output_axis, float error, bool y_axis) {
    if (output_axis == 0.0f || error == 0.0f) {
        return false;
    }
    return y_axis ? (output_axis * error > 0.0f) : (output_axis * error < 0.0f);
}

}  // namespace

OutputValidationPolicy::OutputValidationPolicy(GamepadAiAimConfig ai_config)
    : ai_config_(std::move(ai_config)) {}

void OutputValidationPolicy::reset() {
    has_last_error_ = false;
    last_error_x_ = 0.0f;
    last_error_y_ = 0.0f;
    correction_x_until_seconds_ = 0.0;
    correction_y_until_seconds_ = 0.0;
}

GamepadOutputState OutputValidationPolicy::apply(
    const OutputValidationPolicyInput& input) {
    const bool explicit_track_only_or_reject =
        input.vision_state.authority_decision_valid &&
        (input.vision_state.assist_authority_state ==
             pipeline_contract::AssistAuthorityState::Reject ||
         input.vision_state.assist_authority_state ==
             pipeline_contract::AssistAuthorityState::TrackOnly);
    if (!input.vision_state.aim_authority || explicit_track_only_or_reject) {
        reset();
        GamepadOutputState manual = input.output;
        manual.right_x = input.manual_right_x;
        manual.right_y = input.manual_right_y;
        return manual;
    }

    // Validation owns only the AI delta. Manual input is removed before any
    // correction and restored byte-for-byte after the assist is constrained.
    GamepadOutputState assist = input.output;
    assist.right_x -= input.manual_right_x;
    assist.right_y -= input.manual_right_y;
    apply_tracker_projection_guard(input, assist);
    apply_observed_target_guard(input, assist);
    if (input.candidate_output_hold_active) {
        if (output_pushes_away(assist.right_x, input.vision_state.dx, false)) {
            assist.right_x = bounded_wrong_way_correction(
                assist.right_x,
                input.vision_state.dx,
                false,
                kCrossErrorDeadzonePx);
        }
        if (output_pushes_away(assist.right_y, input.vision_state.dy, true)) {
            assist.right_y = bounded_wrong_way_correction(
                assist.right_y,
                input.vision_state.dy,
                true,
                kCrossErrorDeadzonePx);
        }
        const auto preserve_manual_during_candidate_hold = [](
            float manual_axis,
            float& assist_axis) {
            if (std::fabs(manual_axis) <= kManualCorrectionDeadzone) {
                return;
            }
            const float final_axis = manual_axis + assist_axis;
            if (final_axis * manual_axis <= 0.0f ||
                std::fabs(final_axis) < std::fabs(manual_axis)) {
                assist_axis = 0.0f;
            }
        };
        preserve_manual_during_candidate_hold(input.manual_right_x, assist.right_x);
        preserve_manual_during_candidate_hold(input.manual_right_y, assist.right_y);
    }
    GamepadOutputState output = input.output;
    output.right_x = input.manual_right_x + assist.right_x;
    output.right_y = input.manual_right_y + assist.right_y;
    return output;
}

void OutputValidationPolicy::apply_tracker_projection_guard(
    const OutputValidationPolicyInput& input,
    GamepadOutputState& output) const {
    const NativeControllerVisionState& vision_state = input.vision_state;
    if (!vision_state.has_tracker_projection) {
        return;
    }

    const auto constrain_assist_to_preserve_manual_correction = [](
        float manual_axis,
        float tracker_error,
        float& assist_axis,
        bool y_axis) {
        if (std::fabs(manual_axis) <= kManualCorrectionDeadzone ||
            std::fabs(tracker_error) <= kCrossErrorDeadzonePx) {
            return;
        }
        const bool manual_corrects = y_axis
            ? (manual_axis * tracker_error < 0.0f)
            : (manual_axis * tracker_error > 0.0f);
        if (!manual_corrects) {
            return;
        }
        const float final_axis = manual_axis + assist_axis;
        const bool final_opposes_manual = final_axis * manual_axis < 0.0f;
        const bool final_erases_manual =
            std::fabs(final_axis) < std::fabs(manual_axis) * 0.75f;
        if (final_opposes_manual || final_erases_manual) {
            assist_axis = 0.0f;
        }
    };
    constrain_assist_to_preserve_manual_correction(
        input.manual_right_x,
        vision_state.tracker_dx,
        output.right_x,
        false);
    constrain_assist_to_preserve_manual_correction(
        input.manual_right_y,
        vision_state.tracker_dy,
        output.right_y,
        true);

    if (tracking_native::is_projected_observation(vision_state.target_tier) ||
        (vision_state.authority_decision_valid &&
         vision_state.assist_authority_state ==
             pipeline_contract::AssistAuthorityState::Continuity)) {
        if (output_pushes_away(output.right_x, vision_state.tracker_dx, false)) {
            output.right_x = bounded_wrong_way_correction(
                output.right_x,
                vision_state.tracker_dx,
                false,
                kCrossErrorDeadzonePx);
        }
        if (output_pushes_away(output.right_y, vision_state.tracker_dy, true)) {
            output.right_y = bounded_wrong_way_correction(
                output.right_y,
                vision_state.tracker_dy,
                true,
                kCrossErrorDeadzonePx);
        }
    }
}

void OutputValidationPolicy::apply_observed_target_guard(
    const OutputValidationPolicyInput& input,
    GamepadOutputState& output) {
    const NativeControllerVisionState& vision_state = input.vision_state;
    if (!vision_state.has_target || !vision_state.aim_authority ||
        tracking_native::is_projected_observation(vision_state.target_tier)) {
        reset();
        return;
    }

    const float validation_arm_px =
        std::max(16.0f, ai_config_.body_lock_near_lock_error_px);
    const auto apply_bounded_wrong_way_correction = [](
        float current_error,
        float& output_axis,
        bool y_axis) {
        output_axis = bounded_wrong_way_correction(
            output_axis,
            current_error,
            y_axis,
            kCrossErrorDeadzonePx);
    };
    const auto correct_crossed_wrong_way_axis = [&](
        float previous_error,
        float current_error,
        float& output_axis,
        double& correction_until_seconds,
        bool y_axis) {
        const int previous_sign = axis_sign(previous_error, kCrossErrorDeadzonePx);
        const int current_sign = axis_sign(current_error, kCrossErrorDeadzonePx);
        const bool crossed =
            previous_sign != 0 && current_sign != 0 && previous_sign != current_sign;
        if (crossed &&
            std::fabs(current_error) <= validation_arm_px &&
            output_pushes_away(output_axis, current_error, y_axis)) {
            correction_until_seconds =
                input.now_seconds + kOutputValidationCorrectionSeconds;
            apply_bounded_wrong_way_correction(current_error, output_axis, y_axis);
        }
    };
    const auto correct_active_wrong_way_axis = [&](
        float current_error,
        float& output_axis,
        double& correction_until_seconds,
        bool y_axis) {
        if (correction_until_seconds <= 0.0) {
            return;
        }
        if (input.now_seconds > correction_until_seconds) {
            correction_until_seconds = 0.0;
            return;
        }
        if (output_pushes_away(output_axis, current_error, y_axis)) {
            apply_bounded_wrong_way_correction(current_error, output_axis, y_axis);
        }
    };
    const float aim_target_max_age_ms = std::max(0.0f, ai_config_.target_max_age_ms);
    const double vision_age_ms =
        vision_state.observed_at_seconds > 0.0 && input.now_seconds > 0.0
            ? std::max(0.0, input.now_seconds - vision_state.observed_at_seconds) * 1000.0
            : 0.0;
    const double stale_guard_age_ms =
        aim_target_max_age_ms > 0.0f
            ? std::min(
                60.0,
                std::max(45.0, static_cast<double>(aim_target_max_age_ms) * 0.60))
            : 0.0;
    const auto correct_stale_wrong_way_axis = [&](
        float current_error,
        float& output_axis,
        bool y_axis) {
        if (input.ads_active &&
            stale_guard_age_ms > 0.0 &&
            vision_age_ms >= stale_guard_age_ms &&
            output_pushes_away(output_axis, current_error, y_axis)) {
            apply_bounded_wrong_way_correction(current_error, output_axis, y_axis);
        }
    };

    if (has_last_error_) {
        correct_crossed_wrong_way_axis(
            last_error_x_,
            vision_state.dx,
            output.right_x,
            correction_x_until_seconds_,
            false);
        correct_crossed_wrong_way_axis(
            last_error_y_,
            vision_state.dy,
            output.right_y,
            correction_y_until_seconds_,
            true);
    }
    correct_active_wrong_way_axis(
        vision_state.dx,
        output.right_x,
        correction_x_until_seconds_,
        false);
    correct_active_wrong_way_axis(
        vision_state.dy,
        output.right_y,
        correction_y_until_seconds_,
        true);
    correct_stale_wrong_way_axis(vision_state.dx, output.right_x, false);
    correct_stale_wrong_way_axis(vision_state.dy, output.right_y, true);
    has_last_error_ = true;
    last_error_x_ = vision_state.dx;
    last_error_y_ = vision_state.dy;
}

}  // namespace controller_native
