#pragma once

#include "aim_response_curve_plugin.h"
#include "../tracking_native/tracker_backend.h"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace controller_native {

struct VisionRuntimeConfig {
    int capture_width = 480;
    int capture_height = 416;
    int tensor_width = 480;
    int tensor_height = 416;
    bool require_isotropic_resize = true;
    bool dynamic_viewport_enabled = false;
    int viewport_precision_width = 360;
    int viewport_precision_height = 312;
    int viewport_normal_width = 480;
    int viewport_normal_height = 416;
    int viewport_rescue_width = 600;
    int viewport_rescue_height = 520;
    float viewport_prediction_ms = 100.0f;
    int capture_fps = 140;
    int idle_capture_fps = 20;
    bool keepwarm_when_idle = true;
    std::string color_readback_mode = "pageable";
    std::string model_path = "models/candidates/body_union_manual_core_x2_neg_e6_480x416.engine";
    std::string fallback_model_path = "models/best.pt";
    // Compatibility-only config field. Native runtime termination is window-only.
    std::string quit_key;
    bool native_cue_sidecar = false;
    bool perf_log = false;
    bool aim_perf_file_log = false;
    std::string aim_perf_log_dir = "runs/native_perf";
    unsigned int aim_perf_log_interval_ticks = 1;
    bool gpu_service_enabled = true;
    int gpu_service_active_fps = 120;
    int gpu_service_idle_fps = 20;
    bool gpu_service_keepwarm_when_idle = true;
    bool gpu_service_repeat_last_on_no_update = true;
    // Research-only background camera-motion observer. Normal runtime keeps
    // this off so no grayscale readback, CUDA synchronization, or CPU worker
    // is added to the Vision path.
    bool ego_motion_enabled = false;

    // fusion visual overlay channel (disabled by default)
    bool fusion_enabled = false;
    std::string fusion_session = "dev";
    bool fusion_show_all_detections = false;
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
    float target_max_age_ms = 96.0f;
    float target_projection_max_age_ms = 96.0f;
    float target_projection_reticle_speed_px_per_sec = 1500.0f;
    float target_projection_velocity_lowpass_alpha = 0.35f;
    float target_projection_max_velocity_px_per_sec = 1200.0f;
    float target_projection_weak_velocity_decay = 0.70f;
    // Spatial ADS admission radius. This is independent from the
    // acquisition timer and the ADS output range (max_pixels/range_px).
    float ads_activation_radius_px = 135.0f;
    int ads_snap_window_ms = 100;
    float ads_snap_smoothing = 0.0f;
    float ads_snap_max_ai_force = 1.0f;
    float ads_snap_max_ai_force_y = 1.0f;
    float ads_snap_fov_scale = 1.0f;
    float ads_snap_fov_transition_ms = 0.0f;
    float ads_snap_max_target_dy_px = 90.0f;
    float ads_snap_reticle_speed_px_per_sec = 1500.0f;
    float ads_snap_time_to_go_gain = 1.0f;
    float ads_snap_time_to_go_min_remaining_ms = 35.0f;
    float ads_snap_opposing_manual_suppression_max = 0.35f;
    float ads_completion_radius_px = 8.0f;
    int ads_completion_fresh_frames = 3;
    float ads_max_acquisition_ms = 220.0f;
    float ads_start_delay_ms = 0.0f;
    float ads_start_ramp_ms = 0.0f;
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
    float body_lock_manual_escape_input_threshold = 0.45f;
    float body_lock_manual_escape_preservation = 0.75f;
    float body_lock_manual_takeover_input_threshold = 0.22f;
    float body_lock_manual_takeover_commit_ms = 18.0f;
    float body_lock_manual_takeover_release_ms = 80.0f;
    bool body_lock_manual_takeover_enabled = true;
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
    float body_lock_lead_seconds = 0.026f;
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
    bool profile_despike_enabled = true;
    bool profile_playback_enabled = true;
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
    float pulse_width_ms = 30.0f;
    float pulse_period_ms = 100.0f;
};

struct GamepadTrackerConfig {
    float aim_height_ratio = 0.365f;
    // Fresh detector measurements older than this are ignored. Identity may
    // still coast under the separate projection/hold lease, but an old frame
    // can never be admitted again as a new observation.
    float max_observation_age_ms = 50.0f;
    bool remaining_work_enabled = true;
    float remaining_work_scale = 0.60f;
};

struct GamepadIntentConfig {
    float wrong_way_manual_preservation_floor = 0.65f;
    float fresh_vision_wrong_way_manual_floor = 0.35f;
};

struct GamepadRuntimeConfig {
    std::string auto_fire_output = "RB";
    bool rb_counts_as_aiming = false;
    bool xinput_auto_detect = true;
    unsigned int xinput_user_index = 0;
    tracking_native::TrackerBackendKind tracker_backend =
        tracking_native::TrackerBackendKind::FpsReference;
    GamepadTrackerConfig tracker;
    GamepadIntentConfig intent;
    GamepadAutoFireConfig auto_fire;
    GamepadAiAimConfig ai_aim;
    AimResponseCurveConfig aim_response_curve;
    GamepadAimAssistDynamicsConfig aim_assist_dynamics;
    GamepadRecoilConfig recoil;
};

struct RuntimeTelemetryConfig {
    bool enabled = false;
    std::string mode = "profile";
    int manual_controller_hz = 100;
    bool vision_on_new_frame = true;
    std::string candidate_details = "on_event";
    unsigned int queue_capacity = 8192;
    unsigned int rotate_size_mb = 256;
    unsigned int max_files = 10;
    unsigned int event_pre_ms = 500;
    unsigned int event_post_ms = 1000;
};

struct RuntimePerformanceConfig {
    bool enabled = false;
    unsigned int interval_ms = 5000;
    std::string directory = "runs/perf_summary";
    bool stdout_enabled = true;
};

enum class ControlLearningMode : unsigned char {
    Disabled,
    Shadow,
    RolloutShadow,
};

struct ControlLearningConfig {
    bool enabled = false;
    ControlLearningMode mode = ControlLearningMode::Disabled;
    bool telemetry_enabled = false;
};

struct RuntimeSchedulerConfig {
    int controller_tick_hz = 1000;
    std::string mode = "legacy";
    unsigned int spin_tail_us = 50;
};

struct RuntimeOutputConfig {
    bool enabled = true;
    std::string validation_mode = "strict";
};

struct CompactAdsConfig {
    float strength_scale = 1.0f;
    float vertical_strength_scale = 1.0f;
    float completion_radius_px = 8.0f;
    int completion_fresh_frames = 3;
    float max_acquisition_ms = 220.0f;
};

struct RuntimeConfig {
    std::string profile = "legacy";
    VisionRuntimeConfig vision;
    RuntimeTelemetryConfig telemetry;
    RuntimePerformanceConfig performance;
    ControlLearningConfig control_learning;
    RuntimeSchedulerConfig scheduler;
    RuntimeOutputConfig output;
    CompactAdsConfig ads;
    GamepadRuntimeConfig gamepad;
    std::map<std::string, std::string> effective_sources;
    std::vector<std::string> diagnostics;
    std::string build_commit = "unknown";
    std::string source_config_sha256;
    std::string engine_sha256;
    // Hash of the actual module selected by the process. This is kept
    // separate from source/config provenance so a session can prove which
    // executable produced its telemetry without recording a filesystem path.
    std::string executable_sha256;

    std::string effective_source(const std::string& key) const {
        const auto found = effective_sources.find(key);
        return found == effective_sources.end() ? "default" : found->second;
    }
};

RuntimeConfig load_runtime_config(const std::filesystem::path& path);
RuntimeConfig load_runtime_config(
    const std::filesystem::path& path,
    const std::string& profile_override);

}  // namespace controller_native
