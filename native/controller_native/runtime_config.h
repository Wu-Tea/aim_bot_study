#pragma once

#include "aim_response_curve_plugin.h"

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
    bool gpu_service_enabled = true;
    // fusion visual overlay channel (disabled by default)
    bool fusion_enabled = false;
    std::string fusion_session = "dev";
    bool fusion_show_all_detections = false;
};

struct GamepadAiAimConfig {
    float ai_delta_gain = 1.0f;
    float target_max_age_ms = 96.0f;
    // Legacy ADS response-normalization radius retained for configuration and
    // telemetry compatibility. It does not gate selector-owned target pickup.
    float ads_activation_radius_px = 135.0f;
    int ads_snap_window_ms = 135;
    float ads_snap_max_ai_force = 1.0f;
    float ads_snap_max_ai_force_y = 1.0f;
    float ads_completion_radius_px = 8.0f;
    int ads_completion_fresh_frames = 3;
    float ads_max_acquisition_ms = 220.0f;
    float auto_fire_ready_error_px = 16.0f;
    int auto_fire_ready_frames = 2;
    float auto_fire_ready_max_ai_stick = 6000.0f;
    float cue_hold_body_lock_force_scale = 0.35f;
    // A same-generation cue continuation retains full BodyLock authority once
    // the last direct body height reaches this capture-height ratio. Smaller
    // targets keep the conservative cue-hold scale; the transition is smooth.
    float cue_hold_full_force_min_target_height_ratio = 0.25f;
    float body_lock_max_ai_force = 0.30f;
    float body_lock_max_ai_force_y = 0.42f;
    float body_lock_box_tolerance_px = 18.0f;
    float body_lock_activation_box_px = 150.0f;
    // End-to-end command-to-captured-motion delay used only by online plant
    // identification. It does not delay actuation or change control cadence.
    float aim_response_effect_delay_ms = 9.0f;
    // Provisional V1 intent calibration. These values control how quickly a
    // sustained right-stick request can move D across the currently valid R,
    // and when continued pressure at R's boundary becomes an explicit exit.
    float desired_point_traversal_ms = 180.0f;
    float desired_point_boundary_exit_ms = 50.0f;
    // BodyLock-only rollout switch. ADS authority remains full after admission;
    // selector-owned enemy evidence bounds only continuing BodyLock authority.
    bool visual_authority_enabled = true;
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
    // Target-independent fallback boundary. Weapon profiles may provide their
    // own timeline; the fixed fallback is clamped here without reading aim,
    // target, manual, or pre-recoil output state.
    float feedback_min_amount = 0.14f;
    float feedback_max_amount = 0.34f;
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
    bool manual_fire_activates_ai_aim = true;
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
    // Fresh detector measurements older than this are never admitted.
    float max_observation_age_ms = 50.0f;
};

struct GamepadEnemyMarkConfig {
    bool enabled = true;
    unsigned int l3_cooldown_ms = 1000;
    unsigned int lt_cooldown_ms = 1000;
};

struct GamepadRuntimeConfig {
    std::string auto_fire_output = "RB";
    bool rb_counts_as_aiming = false;
    bool xinput_auto_detect = true;
    unsigned int xinput_user_index = 0;
    GamepadTrackerConfig tracker;
    GamepadEnemyMarkConfig enemy_mark;
    GamepadAutoFireConfig auto_fire;
    GamepadAiAimConfig ai_aim;
    AimResponseCurveConfig aim_response_curve;
    GamepadRecoilConfig recoil;
};

struct RuntimeTelemetryConfig {
    bool enabled = false;
    std::string directory = "runs/native_perf";
    int manual_controller_hz = 100;
    unsigned int queue_capacity = 8192;
    unsigned int rotate_size_mb = 256;
    unsigned int max_files = 10;
};

struct RuntimePerformanceConfig {
    bool enabled = false;
    unsigned int interval_ms = 5000;
    std::string directory = "runs/perf_summary";
    bool stdout_enabled = true;
};

struct RuntimeSchedulerConfig {
    int controller_tick_hz = 1000;
    std::string mode = "legacy";
    unsigned int spin_tail_us = 50;
    // Pin this process to efficiency cores (E-cores) when running on a hybrid
    // CPU (Intel 12th-gen+). Keeps the game's performance cores clear so the
    // vision/runtime load never competes with game threads. No-op on uniform
    // (non-hybrid) systems where no distinct E-core class is detected.
    bool efficiency_core_affinity = true;
    // Number of E-cores pinned to the runtime. The runtime is GPU-bound and its
    // CPU-side work (preprocess/schedule/poll) needs only a couple of cores;
    // every E-core beyond this stays available to the game.
    unsigned int efficiency_core_count = 3;
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
    std::string control_contract_sha256 = "unavailable";
    std::uint32_t control_architecture_version = 0;
    std::uint32_t control_event_schema_version = 0;

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
