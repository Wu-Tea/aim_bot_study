#pragma once

#include "../common_native/screen_geometry.h"

#include "runtime_config.h"

#include <cstdint>

namespace controller_native {

struct BodyLockMotionObservation {
    bool strong_observation = false;
    bool has_body_box = false;
    float body_x1 = 0.0f;
    float body_y1 = 0.0f;
    float body_x2 = 0.0f;
    float body_y2 = 0.0f;
    bool fresh_observation = true;
    std::uint64_t vision_sequence = 0;
    std::uint64_t selected_track_id = 0;
    float left_x = 0.0f;
    bool has_camera_attributed_velocity = false;
    float camera_attributed_velocity_x_px_per_sec = 0.0f;
    double observed_at_seconds = 0.0;
    double now_seconds = 0.0;
};

enum class RelativeMotionState : std::uint8_t {
    Cold = 0,
    Validating = 1,
    Warm = 2,
    Rejected = 3,
};

struct RelativeMotionEstimate {
    float measured_rate_body_per_sec = 0.0f;
    float predicted_rate_body_per_sec = 0.0f;
    float strafe_gain = 0.0f;
    float confidence = 0.0f;
    float lead_x_px = 0.0f;
    RelativeMotionState state = RelativeMotionState::Cold;
};

class BodyLockMotionPolicy {
public:
    explicit BodyLockMotionPolicy(GamepadAiAimConfig config = {});

    void reset();
    void observe(const BodyLockMotionObservation& observation);
    bool has_sustained_motion() const;
    int motion_frames() const;
    common_native::Vec2f velocity_px_per_sec() const;
    RelativeMotionEstimate relative_motion_estimate() const;
    common_native::Vec2f lead_delta() const;
    float lateral_motion_delta(float dx, float release_threshold_px) const;
    float axis_release_tail_scale(bool y_axis, float base_tail_scale) const;
    float vertical_ai_scale(float desired_dy) const;

private:
    void reset_target_motion(bool retain_mobility_prior);
    void reset_consistency();
    void update_consistency(float velocity_x, float velocity_y);
    void update_relative_motion(
        float body_height_px,
        float shaped_left_x,
        float relative_velocity_x_px_per_sec);
    float shaped_left(float left_x) const;
    float soft_ramp_strength(float magnitude, float inner, float outer) const;

    GamepadAiAimConfig config_;
    int motion_frames_ = 0;
    bool has_motion_reference_ = false;
    float motion_box_center_x_ = 0.0f;
    float motion_box_center_y_ = 0.0f;
    float motion_point_x_ = 0.0f;
    float motion_point_y_ = 0.0f;
    float motion_velocity_x_ = 0.0f;
    float motion_velocity_y_ = 0.0f;
    float body_height_px_ = 0.0f;
    double motion_timestamp_seconds_ = 0.0;
    int motion_consistent_frames_ = 0;
    bool has_motion_direction_ = false;
    float motion_direction_x_ = 0.0f;
    float motion_direction_y_ = 0.0f;
    float current_left_x_ = 0.0f;
    float left_at_last_observation_ = 0.0f;
    float measured_rate_body_per_sec_ = 0.0f;
    bool has_measured_rate_ = false;
    float strafe_gain_ = 0.0f;
    bool has_strafe_gain_ = false;
    float mobility_confidence_ = 0.0f;
    RelativeMotionState relative_motion_state_ = RelativeMotionState::Cold;
    std::uint64_t last_consumed_vision_sequence_ = 0;
    std::uint64_t selected_track_id_ = 0;
};

}  // namespace controller_native
