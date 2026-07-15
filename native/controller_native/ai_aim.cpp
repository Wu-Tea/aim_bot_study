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
    reset_body_lock_history();
    ads_snap_ai_stick_x_ = 0.0f;
    ads_snap_ai_stick_y_ = 0.0f;
    last_mode_ = "manual";
}

void NativeAiAim::reset_body_lock_history() {
    body_lock_frames_ = 0;
    has_body_lock_reference_ = false;
    body_lock_reference_x_ = 0.0f;
    body_lock_reference_y_ = 0.0f;
    body_lock_reference_left_ = 0.0f;
    body_lock_reference_top_ = 0.0f;
    body_lock_reference_right_ = 0.0f;
    body_lock_reference_bottom_ = 0.0f;
    reset_body_lock_manual_takeover();
    reset_motion_tracking();
}

NativeAiAimOutput NativeAiAim::compute(const NativeAiAimInput& input) {
    NativeAiAimOutput output;
    const bool lifecycle_owns_bodylock =
        !input.bodylock_lifecycle_valid ||
        input.bodylock_lifecycle == pipeline_contract::BodylockLifecycleState::Warm ||
        input.bodylock_lifecycle == pipeline_contract::BodylockLifecycleState::Tracking ||
        input.bodylock_lifecycle == pipeline_contract::BodylockLifecycleState::Coast;
    if (!input.aiming || !input.has_target || !input.aim_authority) {
        if (!input.bodylock_lifecycle_valid ||
            input.bodylock_lifecycle == pipeline_contract::BodylockLifecycleState::Inactive ||
            input.bodylock_lifecycle == pipeline_contract::BodylockLifecycleState::Yield) {
            reset();
        } else {
            last_mode_ = input.bodylock_lifecycle ==
                    pipeline_contract::BodylockLifecycleState::Coast
                ? "body_lock"
                : "manual";
        }
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

    const bool ads_snap_mode = input.ads_snap_active;
    const bool body_lock_active =
        !ads_snap_mode && lifecycle_owns_bodylock && should_body_lock(input);
    if (!body_lock_active && !input.ads_snap_active) {
        if (!input.bodylock_lifecycle_valid || !lifecycle_owns_bodylock) {
            reset();
        } else {
            last_mode_ = input.bodylock_lifecycle ==
                    pipeline_contract::BodylockLifecycleState::Coast
                ? "body_lock"
                : "manual";
        }
        return output;
    }
    last_mode_ = ads_snap_mode ? "ads_snap" : "body_lock";
    if (!ads_snap_mode) {
        ads_snap_ai_stick_x_ = 0.0f;
        ads_snap_ai_stick_y_ = 0.0f;
    }
    float target_error_x = input.dx;
    float target_error_y = input.dy;
    float body_lock_guard_error_x = target_error_x;
    float body_lock_guard_error_y = target_error_y;
    float body_lock_position_error_x = target_error_x;
    float body_lock_position_error_y = target_error_y;
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
        } else if (!input.bodylock_lifecycle_valid ||
                   input.bodylock_lifecycle !=
                       pipeline_contract::BodylockLifecycleState::Coast) {
            reset_motion_tracking();
        }
        const auto [position_dx, position_dy] = body_lock_position_delta(input);
        const auto [combined_dx, combined_dy] = body_lock_target_delta(input);
        target_error_x = body_lock_lateral_motion_delta(combined_dx);
        target_error_y = combined_dy;
        body_lock_position_error_x = position_dx;
        body_lock_position_error_y = position_dy;
        body_lock_guard_error_x = position_dx;
        body_lock_guard_error_y = position_dy;
        lock_confidence = observe_body_lock_confidence(input, position_dx, position_dy);
        max_force_x = config_.body_lock_max_ai_force;
        if (input.manual_right_x * target_error_x < 0.0f) {
            max_force_x = std::max(max_force_x, config_.body_lock_opposing_boost_max_ai_force);
        }
        max_force_y = config_.body_lock_max_ai_force_y;
    }

    if (body_lock_active) {
        const float position_x_strength = body_lock_position_strength(
            body_lock_position_error_x,
            false);
        const float position_y_strength = body_lock_position_strength(
            body_lock_position_error_y,
            true);
        float position_assist_x = compute_axis(
            body_lock_position_error_x,
            0.0f,
            max_force_x,
            scale,
            position_x_strength,
            false);
        float position_assist_y = compute_axis(
            -body_lock_position_error_y,
            0.0f,
            max_force_y,
            scale,
            position_y_strength,
            true);
        position_assist_x *= body_lock_motion_scale(
            body_lock_position_error_x,
            body_lock_position_error_y,
            false);
        position_assist_y *= body_lock_motion_scale(
            body_lock_position_error_x,
            body_lock_position_error_y,
            true);

        const float motion_error_x = target_error_x - body_lock_position_error_x;
        const float motion_error_y = target_error_y - body_lock_position_error_y;
        const auto [motion_x_strength, motion_y_strength] = axis_soft_strengths(
            motion_error_x,
            motion_error_y);
        float motion_feedforward_x = compute_axis(
            motion_error_x,
            0.0f,
            max_force_x,
            scale,
            motion_x_strength,
            false);
        float motion_feedforward_y = compute_axis(
            -motion_error_y,
            0.0f,
            max_force_y,
            scale,
            motion_y_strength,
            true);
        motion_feedforward_x *= body_lock_motion_scale(
            target_error_x,
            target_error_y,
            false);
        motion_feedforward_y *= body_lock_motion_scale(
            target_error_x,
            target_error_y,
            true);

        position_assist_x = cap_body_lock_terminal_position(
            position_assist_x,
            body_lock_position_error_x);
        position_assist_y = cap_body_lock_terminal_position(
            position_assist_y,
            -body_lock_position_error_y);
        output.position_assist_x = position_assist_x;
        output.position_assist_y = position_assist_y;
        output.motion_feedforward_x = motion_feedforward_x;
        output.motion_feedforward_y = motion_feedforward_y;
        output.assist_x = std::max(
            -std::max(0.0f, max_force_x),
            std::min(
                std::max(0.0f, max_force_x),
                position_assist_x + motion_feedforward_x));
        output.assist_y = std::max(
            -std::max(0.0f, max_force_y),
            std::min(
                std::max(0.0f, max_force_y),
                position_assist_y + motion_feedforward_y));
    } else {
        const auto [x_strength, y_strength] = axis_soft_strengths(
            target_error_x,
            target_error_y);
        if (x_strength <= 0.0f && y_strength <= 0.0f) {
            return output;
        }
        output.assist_x = compute_axis(
            target_error_x,
            ads_snap_mode ? 0.0f : input.manual_right_x,
            max_force_x,
            scale,
            x_strength,
            false);
        output.assist_y = compute_axis(
            -target_error_y,
            ads_snap_mode ? 0.0f : input.manual_right_y,
            max_force_y,
            scale,
            y_strength,
            true);
    }
    if (ads_snap_mode) {
        const auto [x_strength, y_strength] = axis_soft_strengths(
            target_error_x,
            target_error_y);
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
        const auto [assist_x, assist_y] = arbitrate_body_lock_assist(
            input,
            output.assist_x,
            output.assist_y,
            body_lock_guard_error_x,
            -body_lock_guard_error_y,
            lock_confidence);
        output.assist_x = assist_x;
        output.assist_y = assist_y;
    }
    output.assist_y = apply_fire_active_vertical_guard(output.assist_y, input);
    output.has_assist = output.assist_x != 0.0f || output.assist_y != 0.0f;
    return output;
}

void NativeAiAim::reset_body_lock_manual_takeover() {
    manual_takeover_active_ = false;
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

std::pair<float, float> NativeAiAim::body_lock_position_delta(
    const NativeAiAimInput& input) const {
    const float lock_x = (input.body_x1 + input.body_x2) * 0.5f;
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
    const float lock_y = selected_or_fallback_y;
    return {lock_x - input.screen_center_x, lock_y - input.screen_center_y};
}

std::pair<float, float> NativeAiAim::body_lock_target_delta(
    const NativeAiAimInput& input) const {
    const auto [position_x, position_y] = body_lock_position_delta(input);
    const auto [lead_x, lead_y] = body_lock_motion_lead_delta(input);
    return {position_x + lead_x, position_y + lead_y};
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
    observation.fresh_observation = input.fresh_observation;
    observation.vision_sequence = input.vision_sequence;
    observation.selected_track_id = input.selected_track_id;
    observation.left_x = input.left_x;
    observation.has_camera_attributed_velocity = input.has_camera_attributed_velocity;
    observation.camera_attributed_velocity_x_px_per_sec =
        input.camera_attributed_velocity_x_px_per_sec;
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

float NativeAiAim::body_lock_position_strength(float error_px, bool y_axis) const {
    const float inner = y_axis
        ? std::max(0.0f, config_.body_lock_vertical_tail_inner_px)
        : std::max(0.0f, config_.deadzone_inner);
    const float outer = y_axis
        ? std::max(inner, config_.body_lock_vertical_deadzone_px)
        : std::max(inner, config_.x_deadzone_outer);
    const float linear = soft_ramp_strength(std::fabs(error_px), inner, outer);
    const float smooth = linear * linear * (3.0f - (2.0f * linear));
    if (y_axis) {
        return smooth;
    }
    const float tail = body_lock_axis_release_tail_scale(y_axis);
    // Legacy tail keys now tune the shape of one continuous soft deadband.
    // There is no per-frame zero-cross hold and therefore no hidden brake.
    return (tail * smooth) + ((1.0f - tail) * smooth * smooth * smooth);
}

float NativeAiAim::body_lock_motion_scale(
    float error_x,
    float error_y,
    bool y_axis) const {
    if (!y_axis) {
        return 1.0f;
    }
    const float vertical = body_lock_motion_.vertical_ai_scale(error_y);
    if (vertical < 0.0f) {
        return 0.0f;
    }
    const float error_radius = std::hypot(error_x, error_y);
    const float near_lock_px = std::max(1.0f, config_.body_lock_near_lock_error_px);
    const float terminal_scale = std::fabs(error_y) < near_lock_px
        ? std::max(0.55f, std::fabs(error_y) / near_lock_px)
        : 1.0f;
    const float near_ratio = std::max(
        0.0f,
        1.0f - std::min(
            1.0f,
            error_radius / near_lock_px));
    // One bounded confidence/motion scale replaces the old stabilization
    // boost plus a separate vertical-tail switch.
    const float motion_confidence = std::max(
        0.0f,
        std::min(1.0f, vertical + ((1.0f - vertical) * near_ratio)));
    return terminal_scale * motion_confidence;
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

float NativeAiAim::cap_body_lock_terminal_position(
    float position_assist,
    float lock_error_px) const {
    if (!std::isfinite(position_assist) || !std::isfinite(lock_error_px) ||
        position_assist * lock_error_px <= 0.0f) {
        return position_assist;
    }
    constexpr float kTerminalHorizonSeconds = 0.050f;
    constexpr float kOvershootBudgetPx = 2.0f;
    const float reticle_speed = std::max(
        1.0f,
        config_.target_projection_reticle_speed_px_per_sec);
    const float allowed_assist =
        (std::fabs(lock_error_px) + kOvershootBudgetPx) /
        (reticle_speed * kTerminalHorizonSeconds);
    return std::copysign(
        std::min(std::fabs(position_assist), allowed_assist),
        position_assist);
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

std::pair<float, float> NativeAiAim::arbitrate_body_lock_assist(
    const NativeAiAimInput& input,
    float planned_x,
    float planned_y,
    float error_x,
    float error_y,
    float lock_confidence) {
    const float manual_x = input.manual_right_x;
    const float manual_y = input.manual_right_y;
    const float error_radius = std::sqrt((error_x * error_x) + (error_y * error_y));
    if (lock_confidence <= 0.0f) {
        manual_takeover_active_ = false;
        return {planned_x, planned_y};
    }

    float intent_x = planned_x;
    float intent_y = planned_y;
    float intent_norm = std::hypot(intent_x, intent_y);
    if (intent_norm <= 0.000001f && error_radius > 0.0f) {
        intent_x = error_x;
        intent_y = error_y;
        intent_norm = error_radius;
    }
    if (intent_norm <= 0.000001f) {
        manual_takeover_active_ = false;
        return {0.0f, 0.0f};
    }

    const float ux = intent_x / intent_norm;
    const float uy = intent_y / intent_norm;
    const float parallel = (manual_x * ux) + (manual_y * uy);
    const float parallel_x = ux * parallel;
    const float parallel_y = uy * parallel;
    const float orth_x = manual_x - parallel_x;
    const float orth_y = manual_y - parallel_y;
    const float helpful = std::max(0.0f, parallel);
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
    const float escape_threshold = std::max(
        0.01f,
        std::min(1.0f, config_.body_lock_manual_escape_input_threshold));
    const float escape_preservation = std::max(
        0.0f,
        std::min(1.0f, config_.body_lock_manual_escape_preservation));
    const float configured_onset = std::max(
        0.0f,
        config_.body_lock_manual_takeover_input_threshold);
    const float priority_onset = std::min(
        escape_threshold,
        std::max(
            configured_onset,
            escape_threshold * (0.50f + (0.50f * escape_preservation))));
    float manual_priority = 0.0f;
    if (parallel < 0.0f) {
        const float opposing_magnitude = -parallel;
        const float linear_priority = soft_ramp_strength(
            opposing_magnitude,
            priority_onset,
            escape_threshold);
        manual_priority = linear_priority * linear_priority *
            (3.0f - (2.0f * linear_priority));
    }
    manual_takeover_active_ = manual_priority >= 0.50f;
    const float assist_priority = 1.0f - manual_priority;

    const float max_harmful_suppression = std::max(
        0.0f,
        std::min(1.0f, config_.body_lock_opposing_suppression_max));
    const float harmful_suppression =
        max_harmful_suppression * confidence_ratio * assist_priority;
    const float max_orthogonal_suppression = std::max(
        0.0f,
        std::min(1.0f, config_.body_lock_orthogonal_suppression_max));
    const float orthogonal_suppression = max_orthogonal_suppression *
        std::max(lock_confidence, near_lock_ratio) * assist_priority;
    const float vertical_orthogonal_suppression = std::min(
        1.0f,
        orthogonal_suppression * std::max(0.0f, config_.body_lock_vertical_orthogonal_bias));

    const float sanitized_parallel = parallel >= 0.0f
        ? parallel
        : parallel * (1.0f - harmful_suppression);
    const float sanitized_orth_x = orth_x * (1.0f - orthogonal_suppression);
    const float sanitized_orth_y = orth_y * (1.0f - vertical_orthogonal_suppression);
    const float planned_norm = std::hypot(planned_x, planned_y);
    const float manual_credit = std::min(planned_norm, helpful);
    const float remaining_plan = std::max(0.0f, planned_norm - manual_credit) *
        assist_priority;
    const float final_x =
        (ux * sanitized_parallel) + sanitized_orth_x + (ux * remaining_plan);
    const float final_y =
        (uy * sanitized_parallel) + sanitized_orth_y + (uy * remaining_plan);
    return {final_x - manual_x, final_y - manual_y};
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

bool NativeAiAim::manual_takeover_active() const {
    return manual_takeover_active_;
}

RelativeMotionEstimate NativeAiAim::relative_motion_estimate() const {
    return body_lock_motion_.relative_motion_estimate();
}

}  // namespace controller_native
