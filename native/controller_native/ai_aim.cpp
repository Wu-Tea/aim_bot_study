#include "ai_aim.h"

#include "../tracking_native/tracker_authority.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace controller_native {

namespace {

float clamp_unit(float value) {
    return std::max(-1.0f, std::min(1.0f, value));
}

}  // namespace

NativeAiAim::NativeAiAim(GamepadAiAimConfig config)
    : config_(std::move(config)),
      body_lock_motion_(config_) {}

void NativeAiAim::reset() {
    body_lock_frames_ = 0;
    has_body_lock_reference_ = false;
    body_lock_reference_x_ = 0.0f;
    body_lock_reference_y_ = 0.0f;
    body_lock_reference_left_ = 0.0f;
    body_lock_reference_top_ = 0.0f;
    body_lock_reference_right_ = 0.0f;
    body_lock_reference_bottom_ = 0.0f;
    ads_snap_ai_stick_x_ = 0.0f;
    ads_snap_ai_stick_y_ = 0.0f;
    body_lock_ai_stick_x_ = 0.0f;
    body_lock_ai_stick_y_ = 0.0f;
    has_last_body_lock_error_x_ = false;
    has_last_body_lock_error_y_ = false;
    last_body_lock_error_x_ = 0.0f;
    last_body_lock_error_y_ = 0.0f;
    body_lock_zero_cross_hold_x_ = 0;
    body_lock_zero_cross_hold_y_ = 0;
    last_mode_ = "manual";
    reset_motion_tracking();
}

NativeAiAimOutput NativeAiAim::compute(const NativeAiAimInput& input) {
    NativeAiAimOutput output;
    if (!input.aiming || !input.has_target || !input.aim_authority) {
        reset();
        return output;
    }
    if (config_.target_max_age_ms > 0.0f && input.observed_at_seconds > 0.0 &&
        input.now_seconds > 0.0) {
        const double age_ms = (input.now_seconds - input.observed_at_seconds) * 1000.0;
        if (age_ms > static_cast<double>(config_.target_max_age_ms)) {
            reset();
            return output;
        }
    }

    const float scale = target_authority_scale(input.target_tier);
    if (scale <= 0.0f) {
        reset();
        return output;
    }

    const bool body_lock_active = should_body_lock(input);
    if (!body_lock_active && !input.ads_snap_active) {
        reset();
        return output;
    }
    if (!body_lock_active) {
        body_lock_ai_stick_x_ = 0.0f;
        body_lock_ai_stick_y_ = 0.0f;
    }

    const bool ads_snap_mode = !body_lock_active && input.ads_snap_active;
    last_mode_ = ads_snap_mode ? "ads_snap" : "body_lock";
    if (!ads_snap_mode) {
        ads_snap_ai_stick_x_ = 0.0f;
        ads_snap_ai_stick_y_ = 0.0f;
    }
    float target_error_x = input.dx;
    float target_error_y = input.dy;
    float body_lock_guard_error_x = target_error_x;
    float body_lock_guard_error_y = target_error_y;
    float max_force_x = input.ads_snap_active
        ? config_.ads_snap_max_ai_force
        : config_.max_ai_force;
    float max_force_y = input.ads_snap_active
        ? config_.ads_snap_max_ai_force_y
        : config_.max_ai_force_y;

    if (ads_snap_mode) {
        const float ads_fov_scale = std::max(0.05f, std::min(2.0f, config_.ads_snap_fov_scale));
        const float transition_ms = std::max(0.0f, config_.ads_snap_fov_transition_ms);
        float transition_progress = 1.0f;
        if (transition_ms > 0.0f) {
            const float snap_window_ms = static_cast<float>(std::max(1, config_.ads_snap_window_ms));
            const float elapsed_ms =
                std::max(0.0f, std::min(1.0f, input.ads_snap_progress_ratio)) * snap_window_ms;
            transition_progress = std::max(0.0f, std::min(1.0f, elapsed_ms / transition_ms));
        }
        const float fov_scale = 1.0f + ((ads_fov_scale - 1.0f) * transition_progress);
        target_error_x *= fov_scale;
        target_error_y *= fov_scale;
        const float scaled_dy = target_error_y * config_.ai_delta_gain;
        const float limit = std::max(0.0f, config_.ads_snap_max_target_dy_px);
        const float clamped_dy = std::max(-limit, std::min(limit, scaled_dy));
        target_error_y = std::fabs(config_.ai_delta_gain) > 0.000001f
            ? clamped_dy / config_.ai_delta_gain
            : 0.0f;
    }

    float lock_confidence = 0.0f;
    if (body_lock_active) {
        if (tracking_native::is_strong_observation(input.target_tier)) {
            observe_body_lock_motion(input);
        } else {
            reset_motion_tracking();
        }
        const auto [lock_dx, lock_dy] = body_lock_target_delta(input);
        target_error_x = body_lock_lateral_motion_delta(lock_dx);
        target_error_y = lock_dy;
        body_lock_guard_error_x = lock_dx;
        body_lock_guard_error_y = lock_dy;
        lock_confidence = observe_body_lock_confidence(input, lock_dx, lock_dy);
        max_force_x = config_.body_lock_max_ai_force;
        if (input.manual_right_x * target_error_x < 0.0f) {
            max_force_x = std::max(max_force_x, config_.body_lock_opposing_boost_max_ai_force);
        }
        max_force_y = config_.body_lock_max_ai_force_y;
    }

    const auto [x_strength, y_strength] = axis_soft_strengths(target_error_x, target_error_y);
    if (x_strength <= 0.0f && y_strength <= 0.0f) {
        return output;
    }
    float body_lock_y_force_scale = 1.0f;
    if (body_lock_active) {
        const float near_lock_px = std::max(1.0f, config_.body_lock_near_lock_error_px);
        const float abs_guard_y = std::fabs(body_lock_guard_error_y);
        if (abs_guard_y < near_lock_px) {
            body_lock_y_force_scale = std::max(0.55f, abs_guard_y / near_lock_px);
        }
    }

    output.assist_x = compute_axis(
        target_error_x,
        (ads_snap_mode || body_lock_active) ? 0.0f : input.manual_right_x,
        max_force_x,
        scale,
        x_strength,
        false);
    output.assist_y = compute_axis(
        -target_error_y,
        (ads_snap_mode || body_lock_active) ? 0.0f : input.manual_right_y,
        max_force_y * body_lock_y_force_scale,
        scale,
        y_strength,
        true);
    if (ads_snap_mode) {
        output.assist_x = prefer_larger_magnitude(
            output.assist_x,
            ads_snap_time_to_go_stick(
                target_error_x * config_.ai_delta_gain,
                x_strength,
                input.ads_snap_remaining_seconds) *
                std::max(0.0f, max_force_x) * scale);
        output.assist_y = prefer_larger_magnitude(
            output.assist_y,
            ads_snap_time_to_go_stick(
                -target_error_y * config_.ai_delta_gain,
                y_strength,
                input.ads_snap_remaining_seconds) *
                std::max(0.0f, max_force_y) * scale);
        apply_ads_snap_smoothing(output);
    }
    if (body_lock_active) {
        output.assist_x = apply_body_lock_axis_guard(output.assist_x, body_lock_guard_error_x, false);
        output.assist_y = apply_body_lock_axis_guard(output.assist_y, body_lock_guard_error_y, true);
        remember_body_lock_errors(body_lock_guard_error_x, body_lock_guard_error_y);
    }
    if (body_lock_active) {
        const float stabilize_ratio =
            body_lock_motion_.stabilize_ratio(body_lock_guard_error_x, body_lock_guard_error_y);
        if (stabilize_ratio > 0.0f) {
            output.assist_x *= 1.0f + (0.85f * stabilize_ratio);
            output.assist_y *= 1.0f + (0.35f * stabilize_ratio);
        }
        const float vertical_scale = body_lock_vertical_ai_scale(target_error_y);
        if (vertical_scale < 0.0f) {
            output.assist_y = 0.0f;
        } else {
            output.assist_y *= vertical_scale;
        }
        apply_body_lock_smoothing(output, stabilize_ratio);
    }
    if (ads_snap_mode) {
        const float planned_x = output.assist_x;
        const float planned_y = output.assist_y;
        const float reference_x = input.has_mixing_reference ? input.mixing_reference_dx : planned_x;
        const float reference_y = input.has_mixing_reference ? -input.mixing_reference_dy : planned_y;
        const auto [manual_x, manual_y] = resolve_ads_snap_manual(
            input.manual_right_x,
            input.manual_right_y,
            reference_x,
            reference_y,
            input.ads_snap_progress_ratio);
        const auto [remaining_x, remaining_y] = resolve_ads_snap_planned_after_manual(
            planned_x,
            planned_y,
            manual_x,
            manual_y);
        output.assist_x = (manual_x - input.manual_right_x) +
            remaining_x;
        output.assist_y = (manual_y - input.manual_right_y) +
            remaining_y;
    } else if (body_lock_active) {
        const float planned_x = output.assist_x;
        const float planned_y = output.assist_y;
        const auto [manual_x, manual_y] = resolve_body_lock_manual(
            input.manual_right_x,
            input.manual_right_y,
            planned_x,
            planned_y,
            target_error_x,
            -target_error_y,
            lock_confidence);
        const float error_radius =
            std::sqrt((target_error_x * target_error_x) + (target_error_y * target_error_y));
        output.assist_x = (manual_x - input.manual_right_x) +
            resolve_body_lock_manual_overlap(planned_x, manual_x, error_radius);
        output.assist_y = (manual_y - input.manual_right_y) +
            resolve_body_lock_manual_overlap(planned_y, manual_y, error_radius);
        output.assist_x =
            apply_body_lock_manual_escape_floor(output.assist_x, input.manual_right_x, lock_confidence);
        output.assist_y =
            apply_body_lock_manual_escape_floor(output.assist_y, input.manual_right_y, lock_confidence);
        const float manual_escape_threshold = std::max(
            0.0f,
            config_.body_lock_manual_escape_input_threshold);
        if (std::fabs(input.manual_right_y) >= manual_escape_threshold &&
            input.manual_right_y * output.assist_y < 0.0f) {
            output.assist_y = 0.0f;
        }
    }
    output.assist_y = apply_fire_active_vertical_guard(output.assist_y, input);
    output.has_assist = output.assist_x != 0.0f || output.assist_y != 0.0f;
    return output;
}

float NativeAiAim::target_authority_scale(const std::string& target_tier) const {
    if (tracking_native::is_cue_hold_observation(target_tier)) {
        return std::max(0.0f, std::min(1.0f, config_.cue_hold_body_lock_force_scale));
    }
    if (tracking_native::classify_target_tier(target_tier) ==
        tracking_native::TargetTierClass::WeakContinuity) {
        return std::max(0.0f, std::min(1.0f, config_.weak_target_body_lock_force_scale));
    }
    return 1.0f;
}

bool NativeAiAim::should_body_lock(const NativeAiAimInput& input) const {
    if (!input.has_body_box || input.body_x2 <= input.body_x1 || input.body_y2 <= input.body_y1) {
        return false;
    }
    const float tolerance = std::max(0.0f, config_.body_lock_box_tolerance_px);
    if (input.screen_center_x < input.body_x1 - tolerance ||
        input.screen_center_x > input.body_x2 + tolerance ||
        input.screen_center_y < input.body_y1 - tolerance ||
        input.screen_center_y > input.body_y2 + tolerance) {
        return false;
    }
    const auto [lock_dx, lock_dy] = body_lock_target_delta(input);
    const float activation_half = std::max(0.0f, config_.body_lock_activation_box_px) * 0.5f;
    return std::fabs(lock_dx) <= activation_half && std::fabs(lock_dy) <= activation_half;
}

std::pair<float, float> NativeAiAim::body_lock_target_delta(const NativeAiAimInput& input) const {
    const auto [lead_x, lead_y] = body_lock_motion_lead_delta(input);
    const float lock_x = ((input.body_x1 + input.body_x2) * 0.5f) + lead_x;
    const float body_width = input.body_x2 - input.body_x1;
    const float body_height = input.body_y2 - input.body_y1;
    const bool wide_low_body = body_width > 0.0f && (body_height / body_width) < 0.65f;
    const bool selected_y_inside_box = wide_low_body &&
        input.target_y >= input.body_y1 && input.target_y <= input.body_y2;
    const float upper_body_ratio = std::max(
        0.0f,
        std::min(1.0f, config_.body_lock_upper_body_ratio));
    const float selected_or_fallback_y = selected_y_inside_box
        ? input.target_y
        : input.body_y1 + ((input.body_y2 - input.body_y1) * upper_body_ratio);
    const float lock_y = selected_or_fallback_y + lead_y;
    return {lock_x - input.screen_center_x, lock_y - input.screen_center_y};
}

std::pair<float, float> NativeAiAim::body_lock_motion_lead_delta(
    const NativeAiAimInput& input) const {
    if (!tracking_native::is_strong_observation(input.target_tier)) {
        return {0.0f, 0.0f};
    }
    const common_native::Vec2f lead = body_lock_motion_.lead_delta();
    return {lead.x, lead.y};
}

void NativeAiAim::observe_body_lock_motion(const NativeAiAimInput& input) {
    BodyLockMotionObservation observation;
    observation.strong_observation = tracking_native::is_strong_observation(input.target_tier);
    observation.has_body_box = input.has_body_box;
    observation.body_x1 = input.body_x1;
    observation.body_y1 = input.body_y1;
    observation.body_x2 = input.body_x2;
    observation.body_y2 = input.body_y2;
    observation.observed_at_seconds = input.observed_at_seconds;
    observation.now_seconds = input.now_seconds;
    body_lock_motion_.observe(observation);
}

void NativeAiAim::reset_motion_tracking() {
    body_lock_motion_.reset();
}

float NativeAiAim::body_lock_lateral_motion_delta(float dx) const {
    return body_lock_motion_.lateral_motion_delta(
        dx,
        body_lock_axis_release_threshold(false));
}

float NativeAiAim::body_lock_axis_release_threshold(bool y_axis) const {
    if (y_axis) {
        return std::max(
            config_.body_lock_vertical_tail_inner_px + 0.5f,
            config_.deadzone_inner + 1.0f);
    }
    return std::max(
        config_.deadzone_inner + 1.0f,
        config_.x_deadzone_outer * 0.85f);
}

float NativeAiAim::body_lock_axis_release_tail_scale(bool y_axis) const {
    const float tail_scale = std::max(0.0f, std::min(1.0f, config_.body_lock_release_tail_scale));
    return body_lock_motion_.axis_release_tail_scale(y_axis, tail_scale);
}

float NativeAiAim::body_lock_zero_cross_guard_px(bool y_axis) const {
    if (y_axis) {
        return std::max(
            config_.body_lock_vertical_deadzone_px,
            body_lock_axis_release_threshold(true));
    }
    return std::max(config_.x_deadzone_outer, body_lock_axis_release_threshold(false));
}

bool NativeAiAim::body_lock_reference_matches(const NativeAiAimInput& input) const {
    if (!has_body_lock_reference_) {
        return false;
    }
    const float iou_threshold = std::max(0.0f, std::min(1.0f, config_.body_lock_target_match_iou));
    if (body_lock_reference_iou(input) >= iou_threshold) {
        return true;
    }

    const float center_x = (input.body_x1 + input.body_x2) * 0.5f;
    const float center_y = (input.body_y1 + input.body_y2) * 0.5f;
    const float center_limit = std::max(0.0f, config_.body_lock_target_match_center_px);
    return std::fabs(center_x - body_lock_reference_x_) <= center_limit &&
        std::fabs(center_y - body_lock_reference_y_) <= center_limit;
}

float NativeAiAim::body_lock_reference_iou(const NativeAiAimInput& input) const {
    const float inter_left = std::max(body_lock_reference_left_, input.body_x1);
    const float inter_top = std::max(body_lock_reference_top_, input.body_y1);
    const float inter_right = std::min(body_lock_reference_right_, input.body_x2);
    const float inter_bottom = std::min(body_lock_reference_bottom_, input.body_y2);
    const float inter_width = std::max(0.0f, inter_right - inter_left);
    const float inter_height = std::max(0.0f, inter_bottom - inter_top);
    const float intersection = inter_width * inter_height;
    const float reference_area = std::max(0.0f, body_lock_reference_right_ - body_lock_reference_left_) *
        std::max(0.0f, body_lock_reference_bottom_ - body_lock_reference_top_);
    const float current_area = std::max(0.0f, input.body_x2 - input.body_x1) *
        std::max(0.0f, input.body_y2 - input.body_y1);
    const float union_area = reference_area + current_area - intersection;
    if (union_area <= 0.0f) {
        return 0.0f;
    }
    return intersection / union_area;
}

bool NativeAiAim::is_body_lock_zero_cross(bool y_axis, float current_error) const {
    const bool has_previous = y_axis ? has_last_body_lock_error_y_ : has_last_body_lock_error_x_;
    if (!has_previous) {
        return false;
    }
    const float previous_error = y_axis ? last_body_lock_error_y_ : last_body_lock_error_x_;
    if (previous_error == 0.0f || current_error == 0.0f) {
        return false;
    }
    if ((previous_error > 0.0f) == (current_error > 0.0f)) {
        return false;
    }
    return std::fabs(previous_error) <= body_lock_zero_cross_guard_px(y_axis);
}

int NativeAiAim::body_lock_axis_hold_remaining(bool y_axis) const {
    return y_axis ? body_lock_zero_cross_hold_y_ : body_lock_zero_cross_hold_x_;
}

void NativeAiAim::set_body_lock_axis_hold(bool y_axis, int value) {
    if (y_axis) {
        body_lock_zero_cross_hold_y_ = std::max(0, value);
        return;
    }
    body_lock_zero_cross_hold_x_ = std::max(0, value);
}

void NativeAiAim::clear_body_lock_axis_carry(bool y_axis) {
    if (y_axis) {
        body_lock_ai_stick_y_ = 0.0f;
        return;
    }
    body_lock_ai_stick_x_ = 0.0f;
}

float NativeAiAim::apply_body_lock_axis_guard(
    float desired_ai,
    float desired_error,
    bool y_axis) {
    if (std::fabs(desired_error) <= body_lock_axis_release_threshold(y_axis)) {
        set_body_lock_axis_hold(y_axis, 0);
        const float tail_scale = body_lock_axis_release_tail_scale(y_axis);
        if (is_body_lock_zero_cross(y_axis, desired_error)) {
            clear_body_lock_axis_carry(y_axis);
        }
        if (tail_scale > 0.0f) {
            return desired_ai * tail_scale;
        }
        clear_body_lock_axis_carry(y_axis);
        return 0.0f;
    }

    if (body_lock_axis_hold_remaining(y_axis) > 0) {
        set_body_lock_axis_hold(y_axis, body_lock_axis_hold_remaining(y_axis) - 1);
        clear_body_lock_axis_carry(y_axis);
        return 0.0f;
    }

    if (is_body_lock_zero_cross(y_axis, desired_error)) {
        set_body_lock_axis_hold(y_axis, 1);
        clear_body_lock_axis_carry(y_axis);
        return 0.0f;
    }
    return desired_ai;
}

void NativeAiAim::remember_body_lock_errors(float error_x, float error_y) {
    has_last_body_lock_error_x_ = true;
    has_last_body_lock_error_y_ = true;
    last_body_lock_error_x_ = error_x;
    last_body_lock_error_y_ = error_y;
}

void NativeAiAim::apply_ads_snap_smoothing(NativeAiAimOutput& output) {
    const float smoothing = std::max(0.0f, std::min(0.95f, config_.ads_snap_smoothing));
    ads_snap_ai_stick_x_ =
        (ads_snap_ai_stick_x_ * smoothing) + (output.assist_x * (1.0f - smoothing));
    ads_snap_ai_stick_y_ =
        (ads_snap_ai_stick_y_ * smoothing) + (output.assist_y * (1.0f - smoothing));
    output.assist_x = ads_snap_ai_stick_x_;
    output.assist_y = ads_snap_ai_stick_y_;
}

void NativeAiAim::apply_body_lock_smoothing(NativeAiAimOutput& output, float stabilize_ratio) {
    float smoothing = std::max(0.0f, std::min(0.95f, config_.body_lock_smoothing));
    const float clamped_stabilize = std::max(0.0f, std::min(1.0f, stabilize_ratio));
    if (clamped_stabilize > 0.0f) {
        smoothing *= std::max(0.15f, 1.0f - (0.85f * clamped_stabilize));
    }
    body_lock_ai_stick_x_ =
        (body_lock_ai_stick_x_ * smoothing) + (output.assist_x * (1.0f - smoothing));
    body_lock_ai_stick_y_ =
        (body_lock_ai_stick_y_ * smoothing) + (output.assist_y * (1.0f - smoothing));
    output.assist_x = body_lock_ai_stick_x_;
    output.assist_y = body_lock_ai_stick_y_;
}

float NativeAiAim::observe_body_lock_confidence(
    const NativeAiAimInput& input,
    float lock_dx,
    float lock_dy) {
    const float center_x = (input.body_x1 + input.body_x2) * 0.5f;
    const float center_y = (input.body_y1 + input.body_y2) * 0.5f;
    if (!body_lock_reference_matches(input)) {
        has_body_lock_reference_ = true;
        body_lock_reference_x_ = center_x;
        body_lock_reference_y_ = center_y;
        body_lock_reference_left_ = input.body_x1;
        body_lock_reference_top_ = input.body_y1;
        body_lock_reference_right_ = input.body_x2;
        body_lock_reference_bottom_ = input.body_y2;
        body_lock_frames_ = 1;
    } else {
        ++body_lock_frames_;
        body_lock_reference_x_ = center_x;
        body_lock_reference_y_ = center_y;
        body_lock_reference_left_ = input.body_x1;
        body_lock_reference_top_ = input.body_y1;
        body_lock_reference_right_ = input.body_x2;
        body_lock_reference_bottom_ = input.body_y2;
    }

    const float continuity = std::min(
        1.0f,
        static_cast<float>(body_lock_frames_) /
            static_cast<float>(std::max(1, config_.body_lock_confidence_frames)));
    const float activation_half = std::max(1.0f, config_.body_lock_activation_box_px * 0.5f);
    const float activation_distance = std::max(std::fabs(lock_dx), std::fabs(lock_dy));
    const float activation_ratio = std::max(
        0.0f,
        1.0f - std::min(1.0f, activation_distance / activation_half));
    return std::max(0.0f, std::min(1.0f, (0.60f * continuity) + (0.40f * activation_ratio)));
}

std::pair<float, float> NativeAiAim::resolve_body_lock_manual(
    float manual_x,
    float manual_y,
    float planned_x,
    float planned_y,
    float error_x,
    float error_y,
    float lock_confidence) const {
    const float error_radius = std::sqrt((error_x * error_x) + (error_y * error_y));
    if (lock_confidence <= 0.0f) {
        return {manual_x, manual_y};
    }

    float arbitration_x = planned_x;
    float arbitration_y = planned_y;
    float desired_norm = std::sqrt((arbitration_x * arbitration_x) + (arbitration_y * arbitration_y));
    if (desired_norm <= 0.000001f && error_radius > 0.0f) {
        arbitration_x = error_x;
        arbitration_y = error_y;
        desired_norm = error_radius;
    }
    if (desired_norm <= 0.0f) {
        const float terminal_manual_scale = std::max(
            0.10f,
            std::min(
                1.0f,
                error_radius / std::max(1.0f, config_.body_lock_near_lock_error_px)));
        return {manual_x * terminal_manual_scale, manual_y * terminal_manual_scale};
    }

    const float ux = arbitration_x / desired_norm;
    const float uy = arbitration_y / desired_norm;
    const float parallel = (manual_x * ux) + (manual_y * uy);
    const float parallel_x = ux * parallel;
    const float parallel_y = uy * parallel;
    const float orth_x = manual_x - parallel_x;
    const float orth_y = manual_y - parallel_y;
    const float helpful = std::max(0.0f, parallel);
    const float harmful = std::max(0.0f, -parallel);
    const float near_lock_ratio = std::max(
        0.0f,
        1.0f - std::min(
            1.0f,
            error_radius / std::max(1.0f, config_.body_lock_near_lock_error_px)));
    const float confidence_floor = std::max(
        0.0f,
        std::min(1.0f, config_.body_lock_confidence_min_strong));
    const float confidence_ratio = std::max(
        0.0f,
        std::min(
            1.0f,
            (lock_confidence - confidence_floor) / std::max(0.001f, 1.0f - confidence_floor)));
    const float helpful_floor = std::max(
        0.0f,
        std::min(1.0f, config_.body_lock_helpful_preservation_floor));
    const float helpful_scale = std::min(
        1.0f,
        helpful_floor + ((1.0f - near_lock_ratio) * (1.0f - helpful_floor)));
    const float max_harmful_suppression = std::max(
        0.0f,
        std::min(1.0f, config_.body_lock_opposing_suppression_max));
    const float harmful_suppression = std::min(
        max_harmful_suppression,
        max_harmful_suppression * confidence_ratio);
    const float max_orthogonal_suppression = std::max(
        0.0f,
        std::min(1.0f, config_.body_lock_orthogonal_suppression_max));
    const float orthogonal_suppression = std::min(
        max_orthogonal_suppression,
        max_orthogonal_suppression * std::max(lock_confidence, near_lock_ratio));
    const float vertical_orthogonal_suppression = std::min(
        1.0f,
        orthogonal_suppression * std::max(0.0f, config_.body_lock_vertical_orthogonal_bias));

    const float sanitized_parallel =
        (helpful * helpful_scale) - (harmful * (1.0f - harmful_suppression));
    const float sanitized_orth_x = orth_x * (1.0f - orthogonal_suppression);
    const float sanitized_orth_y = orth_y * (1.0f - vertical_orthogonal_suppression);
    return {
        (ux * sanitized_parallel) + sanitized_orth_x,
        (uy * sanitized_parallel) + sanitized_orth_y};
}

float NativeAiAim::body_lock_vertical_ai_scale(float desired_dy) const {
    return body_lock_motion_.vertical_ai_scale(desired_dy);
}

float NativeAiAim::resolve_body_lock_harmful_manual(
    float manual_input,
    float planned_ai,
    float lock_confidence) const {
    if (manual_input == 0.0f || planned_ai == 0.0f || manual_input * planned_ai >= 0.0f) {
        return manual_input;
    }
    const float confidence_floor = std::max(
        0.0f,
        std::min(1.0f, config_.body_lock_confidence_min_strong));
    if (lock_confidence <= confidence_floor) {
        return manual_input;
    }
    const float confidence_ratio = (lock_confidence - confidence_floor) /
        std::max(0.001f, 1.0f - confidence_floor);
    const float suppression = std::max(
        0.0f,
        std::min(1.0f, config_.body_lock_opposing_suppression_max * confidence_ratio));
    return manual_input * (1.0f - suppression);
}

float NativeAiAim::resolve_body_lock_manual_overlap(
    float planned_ai,
    float manual_input,
    float error_radius) const {
    if (planned_ai == 0.0f || manual_input == 0.0f) {
        return planned_ai;
    }
    if (planned_ai * manual_input <= 0.0f) {
        return planned_ai;
    }
    const float overlap_scale = std::max(
        0.0f,
        std::min(1.0f, config_.body_lock_manual_overlap_scale));
    if (overlap_scale <= 0.0f) {
        return planned_ai;
    }
    const float near_lock_ratio = std::max(
        0.0f,
        1.0f - std::min(
            1.0f,
            error_radius / std::max(1.0f, config_.body_lock_near_lock_error_px)));
    const float manual_credit =
        std::fabs(manual_input) * overlap_scale * (0.25f + (0.75f * near_lock_ratio));
    const float remaining = std::fabs(planned_ai) - manual_credit;
    if (remaining <= 0.0f) {
        return 0.0f;
    }
    return std::copysign(remaining, planned_ai);
}

float NativeAiAim::apply_body_lock_manual_escape_floor(
    float assist,
    float manual_input,
    float lock_confidence) const {
    const float confidence_floor = std::max(
        0.0f,
        std::min(1.0f, config_.body_lock_confidence_min_strong));
    if (lock_confidence < confidence_floor) {
        return assist;
    }
    const float manual_abs = std::fabs(manual_input);
    const float threshold = std::max(
        0.0f,
        std::min(1.0f, config_.body_lock_manual_escape_input_threshold));
    if (manual_abs < threshold) {
        return assist;
    }
    const float preservation = std::max(
        0.0f,
        std::min(1.0f, config_.body_lock_manual_escape_preservation));
    if (preservation <= 0.0f) {
        return assist;
    }

    const float final_input = manual_input + assist;
    const float minimum_final_abs = manual_abs * preservation;
    if (manual_input * final_input > 0.0f &&
        std::fabs(final_input) >= minimum_final_abs) {
        return assist;
    }
    const float preserved_final = std::copysign(minimum_final_abs, manual_input);
    return preserved_final - manual_input;
}

float NativeAiAim::apply_fire_active_vertical_guard(
    float assist_y,
    const NativeAiAimInput& input) const {
    if (!input.fire_active || assist_y >= 0.0f) {
        return assist_y;
    }
    if (tracking_native::is_projected_observation(input.target_tier)) {
        return 0.0f;
    }
    constexpr float kFireActiveDownwardAssistCap = 0.18f;
    return std::max(assist_y, -kFireActiveDownwardAssistCap);
}

std::pair<float, float> NativeAiAim::resolve_ads_snap_manual(
    float manual_x,
    float manual_y,
    float reference_x,
    float reference_y,
    float progress_ratio) const {
    const float reference_norm = std::sqrt((reference_x * reference_x) + (reference_y * reference_y));
    if (reference_norm <= 0.000001f) {
        return {manual_x, manual_y};
    }

    const float max_suppression = std::max(
        0.0f,
        std::min(1.0f, config_.ads_snap_opposing_manual_suppression_max));
    if (max_suppression <= 0.0f) {
        return {manual_x, manual_y};
    }

    const float progress = std::max(0.0f, std::min(1.0f, progress_ratio));
    const float ux = reference_x / reference_norm;
    const float uy = reference_y / reference_norm;
    const float parallel = (manual_x * ux) + (manual_y * uy);
    const float parallel_x = ux * parallel;
    const float parallel_y = uy * parallel;
    const float orthogonal_x = manual_x - parallel_x;
    const float orthogonal_y = manual_y - parallel_y;
    const float helpful = std::max(0.0f, parallel);
    const float harmful = std::max(0.0f, -parallel);
    const float harmful_suppression = max_suppression * (0.35f + (0.65f * progress));
    const float orthogonal_suppression = max_suppression * (0.20f + (0.60f * progress));
    const float sanitized_parallel = helpful - (harmful * (1.0f - harmful_suppression));
    return {
        (ux * sanitized_parallel) + (orthogonal_x * (1.0f - orthogonal_suppression)),
        (uy * sanitized_parallel) + (orthogonal_y * (1.0f - orthogonal_suppression))};
}

std::pair<float, float> NativeAiAim::resolve_ads_snap_planned_after_manual(
    float planned_x,
    float planned_y,
    float manual_x,
    float manual_y) const {
    const float planned_norm = std::sqrt((planned_x * planned_x) + (planned_y * planned_y));
    if (planned_norm <= 0.000001f) {
        return {planned_x, planned_y};
    }

    const float ux = planned_x / planned_norm;
    const float uy = planned_y / planned_norm;
    const float helpful_parallel = std::max(0.0f, (manual_x * ux) + (manual_y * uy));
    const float remaining = std::max(0.0f, planned_norm - helpful_parallel);
    return {ux * remaining, uy * remaining};
}

float NativeAiAim::compute_axis(
    float error_px,
    float manual,
    float max_force,
    float scale,
    float axis_strength,
    bool y_axis) const {
    float assist = map_pixels_to_stick(error_px * config_.ai_delta_gain, y_axis);
    assist *= std::max(0.0f, std::min(1.0f, axis_strength));
    assist *= std::max(0.0f, max_force) * scale;
    if (manual * assist < 0.0f) {
        const float opposing_suppression = clamp_unit(std::fabs(manual));
        assist *= std::max(0.0f, 1.0f - opposing_suppression);
    }
    return clamp_unit(assist);
}

std::pair<float, float> NativeAiAim::axis_soft_strengths(float dx, float dy) const {
    const float radial = std::sqrt((dx * dx) + (dy * dy));
    const float radial_strength =
        soft_ramp_strength(radial, config_.deadzone_inner, config_.deadzone_outer);
    const float x_strength = std::max(
        radial_strength,
        soft_ramp_strength(std::fabs(dx), config_.deadzone_inner, config_.x_deadzone_outer));
    return {x_strength, radial_strength};
}

float NativeAiAim::soft_ramp_strength(float magnitude, float inner, float outer) const {
    if (magnitude <= inner) {
        return 0.0f;
    }
    if (magnitude >= outer) {
        return 1.0f;
    }
    if (outer <= inner) {
        return 1.0f;
    }
    return (magnitude - inner) / (outer - inner);
}

float NativeAiAim::map_pixels_to_stick(float delta, bool y_axis) const {
    const float abs_delta = std::fabs(delta);
    const float sign = delta < 0.0f ? -1.0f : 1.0f;
    const float piecewise = piecewise_map(abs_delta, y_axis);
    if (piecewise >= 0.0f) {
        return piecewise * sign;
    }
    const float max_pixels = std::max(1.0f, config_.max_pixels);
    return clamp_unit(delta / max_pixels);
}

float NativeAiAim::piecewise_map(float abs_delta, bool y_axis) const {
    const float mid_pixels = y_axis ? config_.piecewise_mid_pixels_y : config_.piecewise_mid_pixels;
    const float max_pixels = y_axis ? config_.piecewise_max_pixels_y : config_.piecewise_max_pixels;
    const float mid_ratio = y_axis ? config_.piecewise_mid_ratio_y : config_.piecewise_mid_ratio;
    if (mid_pixels <= 0.0f || max_pixels <= mid_pixels || mid_ratio <= 0.0f || mid_ratio >= 1.0f) {
        return -1.0f;
    }
    if (abs_delta >= max_pixels) {
        return 1.0f;
    }
    if (abs_delta <= mid_pixels) {
        return mid_ratio * (abs_delta / mid_pixels);
    }
    const float progress = (abs_delta - mid_pixels) / (max_pixels - mid_pixels);
    return mid_ratio + ((1.0f - mid_ratio) * progress);
}

float NativeAiAim::ads_snap_time_to_go_stick(
    float delta,
    float axis_strength,
    float remaining_seconds) const {
    if (remaining_seconds <= 0.0f || axis_strength <= 0.0f || delta == 0.0f) {
        return 0.0f;
    }
    const float reticle_speed = std::max(1.0f, config_.ads_snap_reticle_speed_px_per_sec);
    const float min_remaining = std::max(
        0.001f,
        config_.ads_snap_time_to_go_min_remaining_ms / 1000.0f);
    const float effective_remaining = std::max(min_remaining, remaining_seconds);
    const float gain = std::max(0.0f, config_.ads_snap_time_to_go_gain);
    const float required_ratio = std::min(
        1.0f,
        ((std::fabs(delta) / effective_remaining) * gain) / reticle_speed);
    return std::copysign(required_ratio * std::max(0.0f, std::min(1.0f, axis_strength)), delta);
}

float NativeAiAim::prefer_larger_magnitude(float current, float candidate) const {
    return std::fabs(candidate) > std::fabs(current) ? candidate : current;
}

const std::string& NativeAiAim::last_mode() const {
    return last_mode_;
}

}  // namespace controller_native
