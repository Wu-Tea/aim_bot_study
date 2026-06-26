#pragma once

#include "bodylock_policy.h"
#include "runtime_config.h"

#include <string>
#include <utility>

namespace controller_native {

struct NativeAiAimInput {
    bool aiming = false;
    bool has_target = false;
    bool aim_authority = false;
    bool ads_snap_active = true;
    bool fire_active = false;
    float ads_snap_progress_ratio = 0.0f;
    float ads_snap_remaining_seconds = 0.0f;
    float dx = 0.0f;
    float dy = 0.0f;
    bool has_mixing_reference = false;
    float mixing_reference_dx = 0.0f;
    float mixing_reference_dy = 0.0f;
    std::string target_tier = "none";
    float target_x = 0.0f;
    float target_y = 0.0f;
    float screen_center_x = 0.0f;
    float screen_center_y = 0.0f;
    bool has_body_box = false;
    float body_x1 = 0.0f;
    float body_y1 = 0.0f;
    float body_x2 = 0.0f;
    float body_y2 = 0.0f;
    double observed_at_seconds = 0.0;
    double now_seconds = 0.0;
    float manual_right_x = 0.0f;
    float manual_right_y = 0.0f;
};

struct NativeAiAimOutput {
    bool has_assist = false;
    float assist_x = 0.0f;
    float assist_y = 0.0f;
};

class NativeAiAim {
public:
    explicit NativeAiAim(GamepadAiAimConfig config = {});

    void reset();
    NativeAiAimOutput compute(const NativeAiAimInput& input);
    const std::string& last_mode() const;

private:
    float target_authority_scale(const std::string& target_tier) const;
    bool should_body_lock(const NativeAiAimInput& input) const;
    std::pair<float, float> body_lock_target_delta(const NativeAiAimInput& input) const;
    std::pair<float, float> body_lock_motion_lead_delta(const NativeAiAimInput& input) const;
    void observe_body_lock_motion(const NativeAiAimInput& input);
    void reset_motion_tracking();
    float body_lock_lateral_motion_delta(float dx) const;
    float body_lock_axis_release_threshold(bool y_axis) const;
    float body_lock_axis_release_tail_scale(bool y_axis) const;
    float body_lock_zero_cross_guard_px(bool y_axis) const;
    bool is_body_lock_zero_cross(bool y_axis, float current_error) const;
    bool body_lock_reference_matches(const NativeAiAimInput& input) const;
    float body_lock_reference_iou(const NativeAiAimInput& input) const;
    int body_lock_axis_hold_remaining(bool y_axis) const;
    void set_body_lock_axis_hold(bool y_axis, int value);
    void clear_body_lock_axis_carry(bool y_axis);
    float apply_body_lock_axis_guard(float desired_ai, float desired_error, bool y_axis);
    void remember_body_lock_errors(float error_x, float error_y);
    void apply_ads_snap_smoothing(NativeAiAimOutput& output);
    void apply_body_lock_smoothing(NativeAiAimOutput& output, float stabilize_ratio);
    float observe_body_lock_confidence(const NativeAiAimInput& input, float lock_dx, float lock_dy);
    std::pair<float, float> resolve_body_lock_manual(
        float manual_x,
        float manual_y,
        float planned_x,
        float planned_y,
        float error_x,
        float error_y,
        float lock_confidence) const;
    float body_lock_vertical_ai_scale(float desired_dy) const;
    float resolve_body_lock_harmful_manual(
        float manual_input,
        float planned_ai,
        float lock_confidence) const;
    float resolve_body_lock_manual_overlap(
        float planned_ai,
        float manual_input,
        float error_radius) const;
    float apply_body_lock_manual_escape_floor(
        float assist,
        float manual_input,
        float lock_confidence) const;
    float apply_fire_active_vertical_guard(float assist_y, const NativeAiAimInput& input) const;
    std::pair<float, float> resolve_ads_snap_manual(
        float manual_x,
        float manual_y,
        float reference_x,
        float reference_y,
        float progress_ratio) const;
    std::pair<float, float> resolve_ads_snap_planned_after_manual(
        float planned_x,
        float planned_y,
        float manual_x,
        float manual_y) const;
    float compute_axis(
        float error_px,
        float manual,
        float max_force,
        float scale,
        float axis_strength,
        bool y_axis) const;
    std::pair<float, float> axis_soft_strengths(float dx, float dy) const;
    float soft_ramp_strength(float magnitude, float inner, float outer) const;
    float map_pixels_to_stick(float delta, bool y_axis) const;
    float piecewise_map(float abs_delta, bool y_axis) const;
    float ads_snap_time_to_go_stick(float delta, float axis_strength, float remaining_seconds) const;
    float prefer_larger_magnitude(float current, float candidate) const;

    GamepadAiAimConfig config_;
    int body_lock_frames_ = 0;
    bool has_body_lock_reference_ = false;
    float body_lock_reference_x_ = 0.0f;
    float body_lock_reference_y_ = 0.0f;
    float body_lock_reference_left_ = 0.0f;
    float body_lock_reference_top_ = 0.0f;
    float body_lock_reference_right_ = 0.0f;
    float body_lock_reference_bottom_ = 0.0f;
    float ads_snap_ai_stick_x_ = 0.0f;
    float ads_snap_ai_stick_y_ = 0.0f;
    float body_lock_ai_stick_x_ = 0.0f;
    float body_lock_ai_stick_y_ = 0.0f;
    bool has_last_body_lock_error_x_ = false;
    bool has_last_body_lock_error_y_ = false;
    float last_body_lock_error_x_ = 0.0f;
    float last_body_lock_error_y_ = 0.0f;
    int body_lock_zero_cross_hold_x_ = 0;
    int body_lock_zero_cross_hold_y_ = 0;
    BodyLockMotionPolicy body_lock_motion_;
    std::string last_mode_ = "manual";
};

}  // namespace controller_native
