#pragma once

#include "bodylock_policy.h"
#include "runtime_config.h"
#include "../pipeline_contract/assist_authority.h"

#include <string>
#include <utility>
#include <cstdint>

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
    bool fresh_observation = true;
    std::uint64_t vision_sequence = 0;
    std::uint64_t selected_track_id = 0;
    float left_x = 0.0f;
    bool has_camera_attributed_velocity = false;
    float camera_attributed_velocity_x_px_per_sec = 0.0f;
    float manual_right_x = 0.0f;
    float manual_right_y = 0.0f;
    bool bodylock_lifecycle_valid = false;
    pipeline_contract::BodylockLifecycleState bodylock_lifecycle =
        pipeline_contract::BodylockLifecycleState::Inactive;
};

struct NativeAiAimOutput {
    bool has_assist = false;
    float assist_x = 0.0f;
    float assist_y = 0.0f;
    float position_assist_x = 0.0f;
    float position_assist_y = 0.0f;
    float motion_feedforward_x = 0.0f;
    float motion_feedforward_y = 0.0f;
};

class NativeAiAim {
public:
    explicit NativeAiAim(GamepadAiAimConfig config = {});

    void reset();
    NativeAiAimOutput compute(const NativeAiAimInput& input);
    const std::string& last_mode() const;
    bool manual_takeover_active() const;
    RelativeMotionEstimate relative_motion_estimate() const;

private:
    float target_authority_scale(const std::string& target_tier) const;
    bool should_body_lock(const NativeAiAimInput& input) const;
    std::pair<float, float> body_lock_position_delta(const NativeAiAimInput& input) const;
    std::pair<float, float> body_lock_target_delta(const NativeAiAimInput& input) const;
    std::pair<float, float> body_lock_motion_lead_delta(const NativeAiAimInput& input) const;
    void observe_body_lock_motion(const NativeAiAimInput& input);
    void reset_motion_tracking();
    void reset_body_lock_history();
    float body_lock_lateral_motion_delta(float dx) const;
    float body_lock_axis_release_threshold(bool y_axis) const;
    float body_lock_axis_release_tail_scale(bool y_axis) const;
    bool body_lock_reference_matches(const NativeAiAimInput& input) const;
    float body_lock_reference_iou(const NativeAiAimInput& input) const;
    float body_lock_position_strength(float error_px, bool y_axis) const;
    float body_lock_motion_scale(float error_x, float error_y, bool y_axis) const;
    void apply_ads_snap_smoothing(NativeAiAimOutput& output);
    float cap_body_lock_terminal_position(float position_assist, float lock_error_px) const;
    float observe_body_lock_confidence(const NativeAiAimInput& input, float lock_dx, float lock_dy);
    std::pair<float, float> arbitrate_body_lock_assist(
        const NativeAiAimInput& input,
        float planned_x,
        float planned_y,
        float error_x,
        float error_y,
        float lock_confidence);
    float apply_fire_active_vertical_guard(float assist_y, const NativeAiAimInput& input) const;
    void reset_body_lock_manual_takeover();
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
    bool manual_takeover_active_ = false;
    double manual_takeover_candidate_since_ = 0.0;
    BodyLockMotionPolicy body_lock_motion_;
    std::string last_mode_ = "manual";
};

}  // namespace controller_native
