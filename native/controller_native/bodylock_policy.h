#pragma once

#include "../common_native/screen_geometry.h"

#include "runtime_config.h"

namespace controller_native {

struct BodyLockMotionObservation {
    bool strong_observation = false;
    bool has_body_box = false;
    float body_x1 = 0.0f;
    float body_y1 = 0.0f;
    float body_x2 = 0.0f;
    float body_y2 = 0.0f;
    double observed_at_seconds = 0.0;
    double now_seconds = 0.0;
};

class BodyLockMotionPolicy {
public:
    explicit BodyLockMotionPolicy(GamepadAiAimConfig config = {});

    void reset();
    void observe(const BodyLockMotionObservation& observation);
    bool has_sustained_motion() const;
    int motion_frames() const;
    common_native::Vec2f velocity_px_per_sec() const;
    common_native::Vec2f lead_delta() const;
    float lateral_motion_delta(float dx, float release_threshold_px) const;
    float axis_release_tail_scale(bool y_axis, float base_tail_scale) const;
    float vertical_ai_scale(float desired_dy) const;
    float stabilize_ratio(float desired_dx, float desired_dy) const;

private:
    void reset_consistency();
    void update_consistency(float velocity_x, float velocity_y);
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
    double motion_timestamp_seconds_ = 0.0;
    int motion_consistent_frames_ = 0;
    bool has_motion_direction_ = false;
    float motion_direction_x_ = 0.0f;
    float motion_direction_y_ = 0.0f;
};

}  // namespace controller_native
