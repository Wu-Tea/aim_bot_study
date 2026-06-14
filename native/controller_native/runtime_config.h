#pragma once

#include "../tracking_native/tracker_backend.h"

#include <filesystem>
#include <string>

namespace controller_native {

struct VisionRuntimeConfig {
    int capture_width = 640;
    int capture_height = 512;
    int capture_fps = 140;
    std::string model_path = "models/best.engine";
    std::string fallback_model_path = "models/best.pt";
    std::string quit_key = "0";
    bool native_cue_sidecar = false;
    bool perf_log = true;
    bool aim_perf_file_log = true;
    std::string aim_perf_log_dir = "runs/native_perf";
    unsigned int aim_perf_log_interval_ticks = 1;
};

struct GamepadAiAimConfig {
    float smoothing = 0.62f;
    float max_pixels = 130.0f;
    float max_ai_force = 0.64f;
    float max_ai_force_y = 0.80f;
    float ai_delta_gain = 1.0f;
    float piecewise_mid_pixels = 60.0f;
    float piecewise_max_pixels = 230.0f;
    float piecewise_mid_ratio = 0.56f;
    float piecewise_mid_pixels_y = 45.0f;
    float piecewise_max_pixels_y = 180.0f;
    float piecewise_mid_ratio_y = 0.65f;
    float deadzone_inner = 1.5f;
    float deadzone_outer = 5.0f;
    float x_deadzone_outer = 3.0f;
    float target_max_age_ms = 50.0f;
    float target_projection_max_age_ms = 24.0f;
    float target_projection_reticle_speed_px_per_sec = 1500.0f;
    float target_projection_velocity_lowpass_alpha = 0.35f;
    float target_projection_max_velocity_px_per_sec = 1200.0f;
    float target_projection_weak_velocity_decay = 0.70f;
    int ads_snap_window_ms = 100;
    float ads_snap_smoothing = 0.0f;
    float ads_snap_max_ai_force = 1.0f;
    float ads_snap_max_ai_force_y = 1.0f;
    float ads_snap_max_target_dy_px = 90.0f;
    float ads_snap_reticle_speed_px_per_sec = 1500.0f;
    float ads_snap_time_to_go_gain = 1.0f;
    float ads_snap_time_to_go_min_remaining_ms = 35.0f;
    float ads_snap_opposing_manual_suppression_max = 0.35f;
    float auto_fire_ready_error_px = 16.0f;
    int auto_fire_ready_frames = 2;
    float auto_fire_ready_min_ads_ms = 70.0f;
    float auto_fire_ready_max_ai_stick = 6000.0f;
    float weak_target_body_lock_force_scale = 0.55f;
    float cue_hold_body_lock_force_scale = 0.35f;
    float body_lock_smoothing = 0.14f;
    float body_lock_max_ai_force = 0.30f;
    float body_lock_opposing_boost_max_ai_force = 0.42f;
    float body_lock_max_ai_force_y = 0.42f;
    float body_lock_box_tolerance_px = 18.0f;
    float body_lock_activation_box_px = 150.0f;
    int body_lock_confidence_frames = 5;
    float body_lock_confidence_min_strong = 0.50f;
    float body_lock_opposing_suppression_max = 1.0f;
    float body_lock_orthogonal_suppression_max = 0.60f;
    float body_lock_helpful_preservation_floor = 1.0f;
    float body_lock_manual_overlap_scale = 0.0f;
    float body_lock_near_lock_error_px = 32.0f;
    float body_lock_vertical_orthogonal_bias = 1.15f;
    float body_lock_vertical_deadzone_px = 6.0f;
    float body_lock_vertical_tail_inner_px = 2.0f;
    float body_lock_vertical_tail_speed_threshold_px_per_sec = 90.0f;
    float body_lock_release_tail_scale = 0.20f;
    float body_lock_lateral_motion_min_speed_px_per_sec = 120.0f;
    float body_lock_lateral_motion_lead_seconds = 0.04f;
    float body_lock_lateral_motion_lead_window_px = 8.0f;
    float body_lock_lateral_motion_lead_max_px = 7.0f;
    float body_lock_lateral_motion_tail_scale = 0.65f;
    int body_lock_lead_frames = 5;
    float body_lock_lead_seconds = 0.0f;
    float body_lock_vertical_lead_scale = 0.95f;
    float body_lock_lead_max_px = 18.0f;
    float body_lock_target_match_iou = 0.10f;
    float body_lock_target_match_center_px = 48.0f;
    float body_lock_upper_body_ratio = 0.40f;
};

struct GamepadAimAssistDynamicsConfig {
    bool enabled = true;
    bool recoil_jitter_guard_enabled = true;
    float recoil_jitter_assist_threshold = 1400.0f;
    float recoil_jitter_flip_scale = 0.20f;
    float recoil_jitter_memory_seconds = 0.050f;
    bool manual_curve_straighten_enabled = true;
    float manual_curve_straighten_strength = 0.30f;
    float manual_curve_straighten_min_manual = 1600.0f;
    float manual_curve_straighten_min_assist = 900.0f;
};

struct GamepadRecoilConfig {
    bool enabled = true;
    bool selection_log_enabled = true;
    bool target_direction_yield_enabled = true;
    bool profile_despike_enabled = true;
    bool native_recognizer_enabled = true;
    bool recognizer_log_enabled = false;
    std::string recognizer_game = "cod22";
    std::string profile_directory = "artifacts/recoil_profiles";
    std::string calibration_directory = "artifacts/recoil_calibration";
    std::string weapon_directory = "artifacts/recoil_app/weapons";
    std::string recognizer_state_path;
    int recognizer_fps = 5;
    float profile_amount = 1.0f;
    float profile_x_amount = 1.0f;
    float feedback_amount = 0.20f;
    float profile_lead_ms = 0.0f;
    float profile_velocity_reference_ms = 10.0f;
    float profile_despike_threshold_px = 2.0f;
    float profile_despike_ratio = 3.0f;
    float piecewise_mid_pixels_y = 45.0f;
    float piecewise_max_pixels_y = 180.0f;
    float piecewise_mid_ratio_y = 0.65f;
};

struct GamepadAutoFireConfig {
    std::string fire_output = "RB";
    bool aim_only = true;
    float max_source_age_ms = 50.0f;
    bool require_aim_ready = true;
    float manual_takeover_release_seconds = 0.035f;
    float manual_takeover_resume_delay_seconds = 0.085f;
};

struct GamepadRuntimeConfig {
    std::string auto_fire_output = "RB";
    bool rb_counts_as_aiming = false;
    bool xinput_auto_detect = true;
    unsigned int xinput_user_index = 0;
    tracking_native::TrackerBackendKind tracker_backend =
        tracking_native::TrackerBackendKind::LegacyProjection;
    GamepadAutoFireConfig auto_fire;
    GamepadAiAimConfig ai_aim;
    GamepadAimAssistDynamicsConfig aim_assist_dynamics;
    GamepadRecoilConfig recoil;
};

struct RuntimeConfig {
    VisionRuntimeConfig vision;
    GamepadRuntimeConfig gamepad;
};

RuntimeConfig load_runtime_config(const std::filesystem::path& path);

}  // namespace controller_native
