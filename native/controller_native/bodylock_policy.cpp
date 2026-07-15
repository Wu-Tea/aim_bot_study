#include "bodylock_policy.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace controller_native {

BodyLockMotionPolicy::BodyLockMotionPolicy(GamepadAiAimConfig config)
    : config_(std::move(config)) {}

void BodyLockMotionPolicy::reset() {
    motion_frames_ = 0;
    has_motion_reference_ = false;
    motion_box_center_x_ = 0.0f;
    motion_box_center_y_ = 0.0f;
    motion_point_x_ = 0.0f;
    motion_point_y_ = 0.0f;
    motion_velocity_x_ = 0.0f;
    motion_velocity_y_ = 0.0f;
    motion_timestamp_seconds_ = 0.0;
    last_consumed_vision_sequence_ = 0;
    selected_track_id_ = 0;
    reset_consistency();
}

void BodyLockMotionPolicy::observe(const BodyLockMotionObservation& observation) {
    const bool sequenced_observation = observation.vision_sequence != 0;
    if (sequenced_observation &&
        (!observation.fresh_observation ||
         observation.vision_sequence == last_consumed_vision_sequence_)) {
        return;
    }
    if (sequenced_observation && selected_track_id_ != 0 &&
        observation.selected_track_id != 0 &&
        observation.selected_track_id != selected_track_id_) {
        reset();
    }
    if (sequenced_observation) {
        last_consumed_vision_sequence_ = observation.vision_sequence;
        selected_track_id_ = observation.selected_track_id;
    }

    if (!observation.strong_observation ||
        !observation.has_body_box ||
        observation.body_x2 <= observation.body_x1 ||
        observation.body_y2 <= observation.body_y1) {
        reset();
        return;
    }

    const float upper_body_ratio = std::max(
        0.0f,
        std::min(1.0f, config_.body_lock_upper_body_ratio));
    const float center_x = (observation.body_x1 + observation.body_x2) * 0.5f;
    const float center_y = (observation.body_y1 + observation.body_y2) * 0.5f;
    const float point_x = center_x;
    const float point_y =
        observation.body_y1 + ((observation.body_y2 - observation.body_y1) * upper_body_ratio);
    const double timestamp = sequenced_observation
        ? observation.observed_at_seconds
        : (observation.now_seconds > 0.0
            ? observation.now_seconds
            : observation.observed_at_seconds);
    const float match_limit = std::max(1.0f, config_.body_lock_activation_box_px * 0.5f);
    if (!has_motion_reference_ ||
        std::fabs(center_x - motion_box_center_x_) > match_limit ||
        std::fabs(center_y - motion_box_center_y_) > match_limit ||
        timestamp <= 0.0) {
        has_motion_reference_ = true;
        motion_box_center_x_ = center_x;
        motion_box_center_y_ = center_y;
        motion_point_x_ = point_x;
        motion_point_y_ = point_y;
        motion_velocity_x_ = 0.0f;
        motion_velocity_y_ = 0.0f;
        motion_timestamp_seconds_ = timestamp;
        motion_frames_ = 1;
        reset_consistency();
        return;
    }

    const double dt = timestamp - motion_timestamp_seconds_;
    if (sequenced_observation && (dt < 0.004 || dt > 0.040)) {
        motion_box_center_x_ = center_x;
        motion_box_center_y_ = center_y;
        motion_point_x_ = point_x;
        motion_point_y_ = point_y;
        motion_velocity_x_ = 0.0f;
        motion_velocity_y_ = 0.0f;
        motion_timestamp_seconds_ = timestamp;
        motion_frames_ = 1;
        reset_consistency();
        return;
    }
    if (dt > 0.0) {
        motion_velocity_x_ = (point_x - motion_point_x_) / static_cast<float>(dt);
        motion_velocity_y_ = (point_y - motion_point_y_) / static_cast<float>(dt);
        update_consistency(motion_velocity_x_, motion_velocity_y_);
    }
    motion_box_center_x_ = center_x;
    motion_box_center_y_ = center_y;
    motion_point_x_ = point_x;
    motion_point_y_ = point_y;
    motion_timestamp_seconds_ = timestamp;
    ++motion_frames_;
}

bool BodyLockMotionPolicy::has_sustained_motion() const {
    return (motion_consistent_frames_ + 1) >= std::max(1, config_.body_lock_lead_frames);
}

int BodyLockMotionPolicy::motion_frames() const {
    return motion_frames_;
}

common_native::Vec2f BodyLockMotionPolicy::velocity_px_per_sec() const {
    return {motion_velocity_x_, motion_velocity_y_};
}

common_native::Vec2f BodyLockMotionPolicy::lead_delta() const {
    if (!has_sustained_motion()) {
        return {};
    }
    if (motion_frames_ < std::max(1, config_.body_lock_lead_frames)) {
        return {};
    }
    const float lead_seconds = std::max(0.0f, config_.body_lock_lead_seconds);
    const float lead_max = std::max(0.0f, config_.body_lock_lead_max_px);
    if (lead_seconds <= 0.0f || lead_max <= 0.0f) {
        return {};
    }
    const float lead_x =
        std::max(-lead_max, std::min(lead_max, motion_velocity_x_ * lead_seconds));
    const float vertical_scale = std::max(0.0f, config_.body_lock_vertical_lead_scale);
    const float lead_y = std::max(
        -lead_max,
        std::min(lead_max, motion_velocity_y_ * lead_seconds * vertical_scale));
    return {lead_x, lead_y};
}

float BodyLockMotionPolicy::lateral_motion_delta(float dx, float release_threshold_px) const {
    if (motion_frames_ < 2) {
        return dx;
    }
    if (std::fabs(motion_velocity_x_) <
        std::max(0.0f, config_.body_lock_lateral_motion_min_speed_px_per_sec)) {
        return dx;
    }

    const float window = std::max(
        config_.body_lock_lateral_motion_lead_window_px,
        release_threshold_px);
    const float abs_dx = std::fabs(dx);
    if (abs_dx >= window) {
        return dx;
    }

    const float near_ratio = 1.0f - std::min(1.0f, abs_dx / std::max(1.0f, window));
    const float lead_limit = std::max(0.0f, config_.body_lock_lateral_motion_lead_max_px);
    const float raw_lead =
        motion_velocity_x_ * std::max(0.0f, config_.body_lock_lateral_motion_lead_seconds);
    const float lead_px = std::max(-lead_limit, std::min(lead_limit, raw_lead));
    return dx + (lead_px * near_ratio);
}

float BodyLockMotionPolicy::axis_release_tail_scale(
    bool y_axis,
    float base_tail_scale) const {
    const float tail_scale = std::max(0.0f, std::min(1.0f, base_tail_scale));
    if (y_axis || motion_frames_ < 2) {
        return tail_scale;
    }
    if (std::fabs(motion_velocity_x_) <
        std::max(0.0f, config_.body_lock_lateral_motion_min_speed_px_per_sec)) {
        return tail_scale;
    }
    const float moving_tail = std::max(
        0.0f,
        std::min(1.0f, config_.body_lock_lateral_motion_tail_scale));
    return std::max(tail_scale, moving_tail);
}

float BodyLockMotionPolicy::vertical_ai_scale(float desired_dy) const {
    const float abs_dy = std::fabs(desired_dy);
    const float deadzone = std::max(0.0f, config_.body_lock_vertical_deadzone_px);
    if (abs_dy > deadzone) {
        return 1.0f;
    }
    if (motion_frames_ < 2) {
        return -1.0f;
    }
    const float motion_speed =
        std::sqrt((motion_velocity_x_ * motion_velocity_x_) +
            (motion_velocity_y_ * motion_velocity_y_));
    if (motion_speed >
        std::max(1.0f, config_.body_lock_vertical_tail_speed_threshold_px_per_sec)) {
        return -1.0f;
    }

    float scale = soft_ramp_strength(
        abs_dy,
        std::max(0.0f, config_.body_lock_vertical_tail_inner_px),
        deadzone);
    const float settle_speed_threshold = std::max(
        1.0f,
        config_.body_lock_vertical_tail_speed_threshold_px_per_sec * 0.4f);
    if (abs_dy > (config_.body_lock_vertical_tail_inner_px * 0.6f) &&
        motion_speed <= settle_speed_threshold) {
        const float settle_ratio = std::max(
            0.0f,
            1.0f - std::min(1.0f, motion_speed / settle_speed_threshold));
        scale = std::max(scale, 0.15f + (0.30f * settle_ratio));
    }
    return scale;
}

float BodyLockMotionPolicy::stabilize_ratio(float desired_dx, float desired_dy) const {
    if (motion_frames_ < 2) {
        return 0.0f;
    }

    const float error_radius =
        std::sqrt((desired_dx * desired_dx) + (desired_dy * desired_dy));
    const float near_lock_ratio = std::max(
        0.0f,
        1.0f - std::min(
            1.0f,
            error_radius / std::max(1.0f, config_.body_lock_near_lock_error_px)));
    if (near_lock_ratio <= 0.0f) {
        return 0.0f;
    }

    const float motion_speed =
        std::sqrt((motion_velocity_x_ * motion_velocity_x_) +
            (motion_velocity_y_ * motion_velocity_y_));
    const float speed_threshold =
        std::max(1.0f, config_.body_lock_vertical_tail_speed_threshold_px_per_sec);
    const float low_speed_ratio =
        std::max(0.0f, 1.0f - std::min(1.0f, motion_speed / speed_threshold));
    return (near_lock_ratio * near_lock_ratio) * (low_speed_ratio * low_speed_ratio);
}

void BodyLockMotionPolicy::reset_consistency() {
    motion_consistent_frames_ = 0;
    has_motion_direction_ = false;
    motion_direction_x_ = 0.0f;
    motion_direction_y_ = 0.0f;
}

void BodyLockMotionPolicy::update_consistency(float velocity_x, float velocity_y) {
    const float speed = std::sqrt((velocity_x * velocity_x) + (velocity_y * velocity_y));
    constexpr float kMinSustainedLeadSpeedPxPerSec = 1.0f;
    if (speed < kMinSustainedLeadSpeedPxPerSec) {
        reset_consistency();
        return;
    }

    const float dir_x = velocity_x / speed;
    const float dir_y = velocity_y / speed;
    if (!has_motion_direction_) {
        has_motion_direction_ = true;
        motion_direction_x_ = dir_x;
        motion_direction_y_ = dir_y;
        motion_consistent_frames_ = 1;
        return;
    }

    const float dot = (motion_direction_x_ * dir_x) + (motion_direction_y_ * dir_y);
    constexpr float kMinConsistentDirectionDot = 0.72f;
    if (dot < kMinConsistentDirectionDot) {
        motion_direction_x_ = dir_x;
        motion_direction_y_ = dir_y;
        motion_consistent_frames_ = 1;
        return;
    }

    motion_direction_x_ = (motion_direction_x_ * 0.65f) + (dir_x * 0.35f);
    motion_direction_y_ = (motion_direction_y_ * 0.65f) + (dir_y * 0.35f);
    const float direction_norm = std::sqrt(
        (motion_direction_x_ * motion_direction_x_) +
        (motion_direction_y_ * motion_direction_y_));
    if (direction_norm > 0.000001f) {
        motion_direction_x_ /= direction_norm;
        motion_direction_y_ /= direction_norm;
    }
    ++motion_consistent_frames_;
}

float BodyLockMotionPolicy::soft_ramp_strength(
    float magnitude,
    float inner,
    float outer) const {
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

}  // namespace controller_native
