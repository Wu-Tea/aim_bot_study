#include "bodylock_policy.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace controller_native {

BodyLockMotionPolicy::BodyLockMotionPolicy(GamepadAiAimConfig config)
    : config_(std::move(config)) {}

void BodyLockMotionPolicy::reset() {
    reset_target_motion(false);
    current_left_x_ = 0.0f;
    strafe_gain_ = 0.0f;
    has_strafe_gain_ = false;
    mobility_confidence_ = 0.0f;
    relative_motion_state_ = RelativeMotionState::Cold;
}

void BodyLockMotionPolicy::reset_target_motion(bool retain_mobility_prior) {
    motion_frames_ = 0;
    has_motion_reference_ = false;
    motion_box_center_x_ = 0.0f;
    motion_box_center_y_ = 0.0f;
    motion_point_x_ = 0.0f;
    motion_point_y_ = 0.0f;
    motion_velocity_x_ = 0.0f;
    motion_velocity_y_ = 0.0f;
    body_height_px_ = 0.0f;
    motion_timestamp_seconds_ = 0.0;
    left_at_last_observation_ = current_left_x_;
    measured_rate_body_per_sec_ = 0.0f;
    has_measured_rate_ = false;
    last_consumed_vision_sequence_ = 0;
    selected_track_id_ = 0;
    reset_consistency();
    if (retain_mobility_prior && has_strafe_gain_) {
        mobility_confidence_ = std::min(mobility_confidence_, 0.25f);
        relative_motion_state_ = RelativeMotionState::Validating;
    }
}

void BodyLockMotionPolicy::observe(const BodyLockMotionObservation& observation) {
    current_left_x_ = shaped_left(observation.left_x);
    const bool sequenced_observation = observation.vision_sequence != 0;
    if (sequenced_observation &&
        (!observation.fresh_observation ||
         observation.vision_sequence == last_consumed_vision_sequence_)) {
        return;
    }
    if (sequenced_observation && selected_track_id_ != 0 &&
        observation.selected_track_id != 0 &&
        observation.selected_track_id != selected_track_id_) {
        reset_target_motion(true);
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
    const float body_height_px = observation.body_y2 - observation.body_y1;
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
        body_height_px_ = body_height_px;
        left_at_last_observation_ = current_left_x_;
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
        body_height_px_ = body_height_px;
        left_at_last_observation_ = current_left_x_;
        motion_timestamp_seconds_ = timestamp;
        motion_frames_ = 1;
        reset_consistency();
        return;
    }
    if (dt > 0.0) {
        motion_velocity_x_ = (point_x - motion_point_x_) / static_cast<float>(dt);
        motion_velocity_y_ = (point_y - motion_point_y_) / static_cast<float>(dt);
        const float relative_velocity_x =
            observation.has_camera_attributed_velocity &&
                std::isfinite(observation.camera_attributed_velocity_x_px_per_sec)
            ? observation.camera_attributed_velocity_x_px_per_sec
            : motion_velocity_x_;
        update_relative_motion(body_height_px, current_left_x_, relative_velocity_x);
        update_consistency(relative_velocity_x, motion_velocity_y_);
    }
    motion_box_center_x_ = center_x;
    motion_box_center_y_ = center_y;
    motion_point_x_ = point_x;
    motion_point_y_ = point_y;
    body_height_px_ = body_height_px;
    left_at_last_observation_ = current_left_x_;
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

RelativeMotionEstimate BodyLockMotionPolicy::relative_motion_estimate() const {
    RelativeMotionEstimate estimate;
    estimate.measured_rate_body_per_sec = measured_rate_body_per_sec_;
    estimate.predicted_rate_body_per_sec = measured_rate_body_per_sec_;
    estimate.strafe_gain = has_strafe_gain_ ? strafe_gain_ : 0.0f;
    estimate.confidence = mobility_confidence_;
    estimate.state = relative_motion_state_;

    if (relative_motion_state_ == RelativeMotionState::Warm && has_strafe_gain_) {
        estimate.predicted_rate_body_per_sec -=
            strafe_gain_ * (current_left_x_ - left_at_last_observation_) *
            mobility_confidence_;
    }

    const float lead_seconds = std::max(0.0f, config_.body_lock_lead_seconds);
    const float lead_max = std::max(0.0f, config_.body_lock_lead_max_px);
    const float raw_lead =
        estimate.predicted_rate_body_per_sec * body_height_px_ * lead_seconds;
    if (std::isfinite(raw_lead)) {
        estimate.lead_x_px = std::max(-lead_max, std::min(lead_max, raw_lead));
    } else {
        estimate = RelativeMotionEstimate{};
    }
    return estimate;
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
    const float lead_x = relative_motion_estimate().lead_x_px;
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

void BodyLockMotionPolicy::update_relative_motion(
    float body_height_px,
    float shaped_left_x,
    float relative_velocity_x_px_per_sec) {
    constexpr float kMinimumBodyHeightPx = 12.0f;
    if (!std::isfinite(relative_velocity_x_px_per_sec) ||
        !std::isfinite(body_height_px) ||
        body_height_px < kMinimumBodyHeightPx) {
        measured_rate_body_per_sec_ = 0.0f;
        has_measured_rate_ = false;
        return;
    }

    constexpr float kMaximumRateBodyPerSecond = 12.0f;
    const float measured_rate = std::max(
        -kMaximumRateBodyPerSecond,
        std::min(
            kMaximumRateBodyPerSecond,
            relative_velocity_x_px_per_sec / body_height_px));
    if (has_measured_rate_) {
        const float left_delta = shaped_left_x - left_at_last_observation_;
        constexpr float kInformativeLeftDelta = 0.20f;
        if (std::fabs(left_delta) >= kInformativeLeftDelta) {
            constexpr float kMaximumStrafeGain = 8.0f;
            const float raw_gain_sample =
                -(measured_rate - measured_rate_body_per_sec_) / left_delta;
            if (std::isfinite(raw_gain_sample) && raw_gain_sample > 0.05f) {
                const float gain_sample = std::min(kMaximumStrafeGain, raw_gain_sample);
                if (!has_strafe_gain_ ||
                    relative_motion_state_ == RelativeMotionState::Rejected) {
                    strafe_gain_ = gain_sample;
                    has_strafe_gain_ = true;
                    mobility_confidence_ = 0.75f;
                    relative_motion_state_ = RelativeMotionState::Warm;
                } else {
                    const float compatibility_limit =
                        std::max(0.60f, strafe_gain_ * 0.30f);
                    if (std::fabs(gain_sample - strafe_gain_) <= compatibility_limit) {
                        constexpr float kGainAlpha = 0.35f;
                        strafe_gain_ += kGainAlpha * (gain_sample - strafe_gain_);
                        strafe_gain_ = std::max(
                            0.0f,
                            std::min(kMaximumStrafeGain, strafe_gain_));
                        mobility_confidence_ = std::min(1.0f, mobility_confidence_ + 0.20f);
                        relative_motion_state_ = RelativeMotionState::Warm;
                    } else {
                        strafe_gain_ = 0.0f;
                        has_strafe_gain_ = false;
                        mobility_confidence_ = 0.0f;
                        relative_motion_state_ = RelativeMotionState::Rejected;
                    }
                }
            }
        }
    }
    measured_rate_body_per_sec_ = measured_rate;
    has_measured_rate_ = true;
}

float BodyLockMotionPolicy::shaped_left(float left_x) const {
    if (!std::isfinite(left_x)) {
        return 0.0f;
    }
    const float bounded = std::max(-1.0f, std::min(1.0f, left_x));
    constexpr float kLeftDeadzone = 0.08f;
    const float magnitude = std::fabs(bounded);
    if (magnitude <= kLeftDeadzone) {
        return 0.0f;
    }
    const float shaped = (magnitude - kLeftDeadzone) / (1.0f - kLeftDeadzone);
    return std::copysign(shaped, bounded);
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
