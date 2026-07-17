#include "runtime_config.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_set>
#include <vector>

namespace controller_native {

namespace {

std::string trim(std::string value) {
    auto is_not_space = [](unsigned char ch) {
        return !std::isspace(ch);
    };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), is_not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), is_not_space).base(), value.end());
    return value;
}

std::string strip_comment(const std::string& line) {
    bool in_string = false;
    for (std::size_t index = 0; index < line.size(); ++index) {
        const char ch = line[index];
        if (ch == '"' && (index == 0 || line[index - 1] != '\\')) {
            in_string = !in_string;
        }
        if (ch == '#' && !in_string) {
            return line.substr(0, index);
        }
    }
    return line;
}

std::string parse_string_value(std::string value) {
    value = trim(std::move(value));
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

bool parse_bool_value(const std::string& value, bool fallback) {
    std::string normalized = trim(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (normalized == "true" || normalized == "1" || normalized == "yes" || normalized == "on") {
        return true;
    }
    if (normalized == "false" || normalized == "0" || normalized == "no" || normalized == "off") {
        return false;
    }
    return fallback;
}

int parse_int_value(const std::string& value, int fallback) {
    try {
        return std::stoi(trim(value));
    } catch (const std::exception&) {
        return fallback;
    }
}

unsigned int parse_uint_value(const std::string& value, unsigned int fallback) {
    try {
        const int parsed = std::stoi(trim(value));
        return parsed < 0 ? fallback : static_cast<unsigned int>(parsed);
    } catch (const std::exception&) {
        return fallback;
    }
}

float parse_float_value(const std::string& value, float fallback) {
    try {
        return std::stof(trim(value));
    } catch (const std::exception&) {
        return fallback;
    }
}

void apply_runtime_vision_value(
    VisionRuntimeConfig& config,
    const std::string& key,
    const std::string& value) {
    if (key == "crop_width" || key == "capture_width") {
        config.capture_width = parse_int_value(value, config.capture_width);
    } else if (key == "crop_height" || key == "capture_height") {
        config.capture_height = parse_int_value(value, config.capture_height);
    } else if (key == "capture_fps") {
        config.capture_fps = parse_int_value(value, config.capture_fps);
    } else if (key == "idle_capture_fps") {
        config.idle_capture_fps = parse_int_value(value, config.idle_capture_fps);
        config.gpu_service_idle_fps = config.idle_capture_fps;
    } else if (key == "keepwarm_when_idle") {
        config.keepwarm_when_idle = parse_bool_value(value, config.keepwarm_when_idle);
        config.gpu_service_keepwarm_when_idle = config.keepwarm_when_idle;
    } else if (key == "color_readback_mode") {
        config.color_readback_mode = parse_string_value(value);
    } else if (key == "model_path") {
        config.model_path = parse_string_value(value);
    } else if (key == "fallback_model_path") {
        config.fallback_model_path = parse_string_value(value);
    } else if (key == "quit_key") {
        config.quit_key = parse_string_value(value);
    } else if (key == "native_cue_sidecar") {
        config.native_cue_sidecar = parse_bool_value(value, config.native_cue_sidecar);
    } else if (key == "perf_log") {
        config.perf_log = parse_bool_value(value, config.perf_log);
    } else if (key == "aim_perf_file_log") {
        config.aim_perf_file_log = parse_bool_value(value, config.aim_perf_file_log);
    } else if (key == "aim_perf_log_dir") {
        config.aim_perf_log_dir = parse_string_value(value);
    } else if (key == "aim_perf_log_interval_ticks") {
        config.aim_perf_log_interval_ticks =
            parse_uint_value(value, config.aim_perf_log_interval_ticks);
    } else if (key == "gpu_service_enabled") {
        config.gpu_service_enabled = parse_bool_value(value, config.gpu_service_enabled);
    } else if (key == "gpu_service_active_fps") {
        config.gpu_service_active_fps =
            parse_int_value(value, config.gpu_service_active_fps);
    } else if (key == "gpu_service_idle_fps") {
        config.gpu_service_idle_fps =
            parse_int_value(value, config.gpu_service_idle_fps);
    } else if (key == "gpu_service_keepwarm_when_idle") {
        config.gpu_service_keepwarm_when_idle =
            parse_bool_value(value, config.gpu_service_keepwarm_when_idle);
    } else if (key == "gpu_service_repeat_last_on_no_update") {
        config.gpu_service_repeat_last_on_no_update =
            parse_bool_value(value, config.gpu_service_repeat_last_on_no_update);
    } else if (key == "fusion_enabled") {
        config.fusion_enabled = parse_bool_value(value, config.fusion_enabled);
    } else if (key == "fusion_session") {
        config.fusion_session = parse_string_value(value);
    } else if (key == "fusion_show_all_detections") {
        config.fusion_show_all_detections =
            parse_bool_value(value, config.fusion_show_all_detections);
    }
}

bool is_known_key(const std::string& section, const std::string& key) {
    static const std::unordered_set<std::string> runtime_keys{"profile"};
    static const std::unordered_set<std::string> vision_keys{
        "crop_width", "capture_width", "crop_height", "capture_height", "capture_fps",
        "idle_capture_fps", "keepwarm_when_idle", "color_readback_mode", "model_path", "fallback_model_path",
        "quit_key", "native_cue_sidecar", "perf_log", "aim_perf_file_log",
        "aim_perf_log_dir", "aim_perf_log_interval_ticks", "gpu_service_enabled",
        "gpu_service_active_fps", "gpu_service_idle_fps",
        "gpu_service_keepwarm_when_idle", "gpu_service_repeat_last_on_no_update",
        "fusion_enabled", "fusion_session", "fusion_show_all_detections"};
    static const std::unordered_set<std::string> telemetry_keys{
        "enabled", "mode", "manual_controller_hz", "vision_on_new_frame",
        "candidate_details", "queue_capacity", "rotate_size_mb", "max_files",
        "event_pre_ms", "event_post_ms"};
    static const std::unordered_set<std::string> scheduler_keys{
        "controller_tick_hz", "mode", "spin_tail_us"};
    static const std::unordered_set<std::string> input_keys{
        "auto_detect", "controller_index", "rb_counts_as_aiming"};
    static const std::unordered_set<std::string> output_keys{"enabled", "validation_mode"};
    static const std::unordered_set<std::string> tracker_keys{
        "backend", "projection_age_ms", "responsiveness", "max_velocity_px_per_sec",
        "weak_memory_decay", "lead_seconds", "lead_max_px", "aim_height_ratio"};
    static const std::unordered_set<std::string> intent_keys{
        "wrong_way_manual_preservation_floor"};
    static const std::unordered_set<std::string> ads_keys{
        "strength_scale", "vertical_strength_scale", "range_px", "snap_duration_ms",
        "completion_radius_px", "completion_fresh_frames", "max_acquisition_ms"};
    static const std::unordered_set<std::string> bodylock_keys{
        "strength", "vertical_strength", "activation_range_px", "tolerance_px",
        "manual_escape_threshold",
        "manual_escape_preservation", "manual_takeover_enabled",
        "manual_takeover_threshold", "manual_takeover_commit_ms",
        "manual_takeover_release_ms"};
    static const std::unordered_set<std::string> gamepad_keys{
        "auto_fire_output", "rb_counts_as_aiming", "xinput_auto_detect",
        "xinput_user_index", "tracker_backend", "body_lock_upper_body_ratio"};
    static const std::unordered_set<std::string> auto_fire_keys{
        "fire_output", "aim_only", "max_source_age_ms", "require_aim_ready",
        "manual_takeover_release_seconds", "manual_takeover_resume_delay_seconds"};
    static const std::unordered_set<std::string> dynamics_keys{
        "enabled", "recoil_jitter_guard_enabled", "recoil_jitter_assist_threshold",
        "recoil_jitter_flip_scale", "recoil_jitter_memory_seconds",
        "manual_curve_straighten_enabled", "manual_curve_straighten_strength",
        "manual_curve_straighten_min_manual", "manual_curve_straighten_min_assist"};
    static const std::unordered_set<std::string> recoil_keys{
        "enabled", "selection_log_enabled", "profile_despike_enabled",
        "native_recognizer_enabled", "recognizer_log_enabled", "recognizer_game",
        "profile_directory", "calibration_directory", "weapon_directory",
        "recognizer_state_path", "recognizer_fps", "profile_amount", "profile_x_amount",
        "feedback_amount", "profile_lead_ms", "profile_velocity_reference_ms",
        "profile_despike_threshold_px", "profile_despike_ratio", "piecewise_mid_pixels_y",
        "piecewise_max_pixels_y", "piecewise_mid_ratio_y"};
    static const std::unordered_set<std::string> ai_aim_keys{
        "smoothing", "max_pixels", "max_ai_force", "max_ai_force_y", "ai_delta_gain",
        "piecewise_mid_pixels", "piecewise_max_pixels", "piecewise_mid_ratio",
        "piecewise_mid_pixels_y", "piecewise_max_pixels_y", "piecewise_mid_ratio_y",
        "deadzone_inner", "deadzone_outer", "x_deadzone_outer", "target_max_age_ms",
        "target_projection_max_age_ms", "target_projection_reticle_speed_px_per_sec",
        "target_projection_velocity_lowpass_alpha", "target_projection_max_velocity_px_per_sec",
        "target_projection_weak_velocity_decay", "ads_snap_window_ms", "ads_snap_smoothing",
        "ads_snap_max_ai_force", "ads_snap_max_ai_force_y", "ads_snap_fov_scale",
        "ads_snap_fov_transition_ms", "ads_snap_max_target_dy_px",
        "ads_snap_reticle_speed_px_per_sec", "ads_snap_time_to_go_gain",
        "ads_snap_time_to_go_min_remaining_ms", "ads_snap_opposing_manual_suppression_max",
        "auto_fire_ready_error_px", "auto_fire_ready_frames", "auto_fire_ready_min_ads_ms",
        "auto_fire_ready_max_ai_stick", "weak_target_body_lock_force_scale",
        "cue_hold_body_lock_force_scale", "body_lock_smoothing", "body_lock_max_ai_force",
        "body_lock_opposing_boost_max_ai_force", "body_lock_max_ai_force_y",
        "body_lock_box_tolerance_px", "body_lock_activation_box_px",
        "body_lock_confidence_frames", "body_lock_confidence_min_strong",
        "body_lock_opposing_suppression_max", "body_lock_orthogonal_suppression_max",
        "body_lock_helpful_preservation_floor", "body_lock_manual_overlap_scale",
        "body_lock_manual_escape_input_threshold", "body_lock_manual_escape_preservation",
        "body_lock_manual_takeover_enabled", "body_lock_manual_takeover_input_threshold",
        "body_lock_manual_takeover_commit_ms", "body_lock_manual_takeover_release_ms",
        "body_lock_near_lock_error_px", "body_lock_vertical_orthogonal_bias",
        "body_lock_vertical_deadzone_px", "body_lock_vertical_tail_inner_px",
        "body_lock_vertical_tail_speed_threshold_px_per_sec", "body_lock_release_tail_scale",
        "body_lock_lateral_motion_min_speed_px_per_sec", "body_lock_lateral_motion_lead_seconds",
        "body_lock_lateral_motion_lead_window_px", "body_lock_lateral_motion_lead_max_px",
        "body_lock_lateral_motion_tail_scale", "body_lock_lead_frames", "body_lock_lead_seconds",
        "body_lock_vertical_lead_scale", "body_lock_lead_max_px", "body_lock_target_match_iou",
        "body_lock_target_match_center_px", "body_lock_upper_body_ratio"};
    if (section == "runtime") return runtime_keys.count(key) != 0;
    if (section == "runtime.vision") return vision_keys.count(key) != 0;
    if (section == "runtime.telemetry") return telemetry_keys.count(key) != 0;
    if (section == "runtime.scheduler") return scheduler_keys.count(key) != 0;
    if (section == "runtime.input") return input_keys.count(key) != 0;
    if (section == "runtime.output") return output_keys.count(key) != 0;
    if (section == "gamepad.tracker") return tracker_keys.count(key) != 0;
    if (section == "gamepad.intent") return intent_keys.count(key) != 0;
    if (section == "gamepad.ads") return ads_keys.count(key) != 0;
    if (section == "gamepad.bodylock") return bodylock_keys.count(key) != 0;
    if (section == "runtime.gamepad") return gamepad_keys.count(key) != 0;
    if (section == "gamepad.auto_fire") return auto_fire_keys.count(key) != 0;
    if (section == "gamepad.ai_aim") return ai_aim_keys.count(key) != 0;
    if (section == "gamepad.aim_assist_dynamics") return dynamics_keys.count(key) != 0;
    if (section == "gamepad.recoil") return recoil_keys.count(key) != 0;
    return false;
}

void apply_profile(RuntimeConfig& config, const std::string& profile) {
    if (profile == "performance") {
        config.vision.capture_fps = 240;
        config.vision.idle_capture_fps = 20;
    } else if (profile == "balanced") {
        config.vision.capture_fps = 160;
        config.vision.idle_capture_fps = 20;
    } else if (profile == "low_latency") {
        config.vision.capture_fps = 200;
        config.vision.idle_capture_fps = 20;
    } else if (profile == "legacy") {
        return;
    } else {
        throw std::runtime_error(
            "invalid runtime profile '" + profile +
            "'; available profiles: performance, balanced, low_latency");
    }
    config.profile = profile;
    config.vision.keepwarm_when_idle = true;
    config.vision.gpu_service_active_fps = config.vision.capture_fps;
    config.vision.gpu_service_idle_fps = config.vision.idle_capture_fps;
    config.vision.gpu_service_keepwarm_when_idle = config.vision.keepwarm_when_idle;
    config.vision.aim_perf_file_log = false;
    config.effective_sources["runtime.profile"] = "user";
    config.effective_sources["runtime.vision.capture_fps"] = "profile";
    config.effective_sources["runtime.vision.idle_capture_fps"] = "profile";
    config.effective_sources["runtime.vision.keepwarm_when_idle"] = "profile";
}

void apply_runtime_gamepad_value(
    GamepadRuntimeConfig& config,
    const std::string& key,
    const std::string& value) {
    if (key == "auto_fire_output") {
        config.auto_fire_output = parse_string_value(value);
        config.auto_fire.fire_output = config.auto_fire_output;
    } else if (key == "rb_counts_as_aiming") {
        config.rb_counts_as_aiming = parse_bool_value(value, config.rb_counts_as_aiming);
    } else if (key == "xinput_auto_detect") {
        config.xinput_auto_detect = parse_bool_value(value, config.xinput_auto_detect);
    } else if (key == "xinput_user_index") {
        config.xinput_user_index = parse_uint_value(value, config.xinput_user_index);
    } else if (key == "tracker_backend") {
        config.tracker_backend =
            tracking_native::parse_tracker_backend_kind(parse_string_value(value));
    }
}

void apply_gamepad_auto_fire_value(
    GamepadAutoFireConfig& config,
    const std::string& key,
    const std::string& value) {
    if (key == "fire_output") {
        config.fire_output = parse_string_value(value);
    } else if (key == "aim_only") {
        config.aim_only = parse_bool_value(value, config.aim_only);
    } else if (key == "max_source_age_ms") {
        config.max_source_age_ms = parse_float_value(value, config.max_source_age_ms);
    } else if (key == "require_aim_ready") {
        config.require_aim_ready = parse_bool_value(value, config.require_aim_ready);
    } else if (key == "manual_takeover_release_seconds") {
        config.manual_takeover_release_seconds =
            parse_float_value(value, config.manual_takeover_release_seconds);
    } else if (key == "manual_takeover_resume_delay_seconds") {
        config.manual_takeover_resume_delay_seconds =
            parse_float_value(value, config.manual_takeover_resume_delay_seconds);
    }
}

void apply_gamepad_ai_aim_value(
    GamepadAiAimConfig& config,
    const std::string& key,
    const std::string& value) {
    if (key == "smoothing") {
        config.smoothing = parse_float_value(value, config.smoothing);
    } else if (key == "max_pixels") {
        config.max_pixels = parse_float_value(value, config.max_pixels);
    } else if (key == "max_ai_force") {
        config.max_ai_force = parse_float_value(value, config.max_ai_force);
    } else if (key == "max_ai_force_y") {
        config.max_ai_force_y = parse_float_value(value, config.max_ai_force_y);
    } else if (key == "ai_delta_gain") {
        config.ai_delta_gain = parse_float_value(value, config.ai_delta_gain);
    } else if (key == "piecewise_mid_pixels") {
        config.piecewise_mid_pixels = parse_float_value(value, config.piecewise_mid_pixels);
    } else if (key == "piecewise_max_pixels") {
        config.piecewise_max_pixels = parse_float_value(value, config.piecewise_max_pixels);
    } else if (key == "piecewise_mid_ratio") {
        config.piecewise_mid_ratio = parse_float_value(value, config.piecewise_mid_ratio);
    } else if (key == "piecewise_mid_pixels_y") {
        config.piecewise_mid_pixels_y = parse_float_value(value, config.piecewise_mid_pixels_y);
    } else if (key == "piecewise_max_pixels_y") {
        config.piecewise_max_pixels_y = parse_float_value(value, config.piecewise_max_pixels_y);
    } else if (key == "piecewise_mid_ratio_y") {
        config.piecewise_mid_ratio_y = parse_float_value(value, config.piecewise_mid_ratio_y);
    } else if (key == "deadzone_inner") {
        config.deadzone_inner = parse_float_value(value, config.deadzone_inner);
    } else if (key == "deadzone_outer") {
        config.deadzone_outer = parse_float_value(value, config.deadzone_outer);
    } else if (key == "x_deadzone_outer") {
        config.x_deadzone_outer = parse_float_value(value, config.x_deadzone_outer);
    } else if (key == "target_max_age_ms") {
        config.target_max_age_ms = parse_float_value(value, config.target_max_age_ms);
    } else if (key == "target_projection_max_age_ms") {
        config.target_projection_max_age_ms =
            parse_float_value(value, config.target_projection_max_age_ms);
    } else if (key == "target_projection_reticle_speed_px_per_sec") {
        config.target_projection_reticle_speed_px_per_sec =
            parse_float_value(value, config.target_projection_reticle_speed_px_per_sec);
    } else if (key == "target_projection_velocity_lowpass_alpha") {
        config.target_projection_velocity_lowpass_alpha =
            parse_float_value(value, config.target_projection_velocity_lowpass_alpha);
    } else if (key == "target_projection_max_velocity_px_per_sec") {
        config.target_projection_max_velocity_px_per_sec =
            parse_float_value(value, config.target_projection_max_velocity_px_per_sec);
    } else if (key == "target_projection_weak_velocity_decay") {
        config.target_projection_weak_velocity_decay =
            parse_float_value(value, config.target_projection_weak_velocity_decay);
    } else if (key == "ads_snap_window_ms") {
        config.ads_snap_window_ms = parse_int_value(value, config.ads_snap_window_ms);
    } else if (key == "ads_snap_smoothing") {
        config.ads_snap_smoothing = parse_float_value(value, config.ads_snap_smoothing);
    } else if (key == "ads_snap_max_ai_force") {
        config.ads_snap_max_ai_force = parse_float_value(value, config.ads_snap_max_ai_force);
    } else if (key == "ads_snap_max_ai_force_y") {
        config.ads_snap_max_ai_force_y = parse_float_value(value, config.ads_snap_max_ai_force_y);
    } else if (key == "ads_snap_fov_scale") {
        config.ads_snap_fov_scale = parse_float_value(value, config.ads_snap_fov_scale);
    } else if (key == "ads_snap_fov_transition_ms") {
        config.ads_snap_fov_transition_ms =
            parse_float_value(value, config.ads_snap_fov_transition_ms);
    } else if (key == "ads_snap_max_target_dy_px") {
        config.ads_snap_max_target_dy_px =
            parse_float_value(value, config.ads_snap_max_target_dy_px);
    } else if (key == "ads_snap_reticle_speed_px_per_sec") {
        config.ads_snap_reticle_speed_px_per_sec =
            parse_float_value(value, config.ads_snap_reticle_speed_px_per_sec);
    } else if (key == "ads_snap_time_to_go_gain") {
        config.ads_snap_time_to_go_gain =
            parse_float_value(value, config.ads_snap_time_to_go_gain);
    } else if (key == "ads_snap_time_to_go_min_remaining_ms") {
        config.ads_snap_time_to_go_min_remaining_ms =
            parse_float_value(value, config.ads_snap_time_to_go_min_remaining_ms);
    } else if (key == "ads_snap_opposing_manual_suppression_max") {
        config.ads_snap_opposing_manual_suppression_max =
            parse_float_value(value, config.ads_snap_opposing_manual_suppression_max);
    } else if (key == "auto_fire_ready_error_px") {
        config.auto_fire_ready_error_px = parse_float_value(value, config.auto_fire_ready_error_px);
    } else if (key == "auto_fire_ready_frames") {
        config.auto_fire_ready_frames = parse_int_value(value, config.auto_fire_ready_frames);
    } else if (key == "auto_fire_ready_min_ads_ms") {
        config.auto_fire_ready_min_ads_ms =
            parse_float_value(value, config.auto_fire_ready_min_ads_ms);
    } else if (key == "auto_fire_ready_max_ai_stick") {
        config.auto_fire_ready_max_ai_stick =
            parse_float_value(value, config.auto_fire_ready_max_ai_stick);
    } else if (key == "weak_target_body_lock_force_scale") {
        config.weak_target_body_lock_force_scale =
            parse_float_value(value, config.weak_target_body_lock_force_scale);
    } else if (key == "cue_hold_body_lock_force_scale") {
        config.cue_hold_body_lock_force_scale =
            parse_float_value(value, config.cue_hold_body_lock_force_scale);
    } else if (key == "body_lock_smoothing") {
        config.body_lock_smoothing = parse_float_value(value, config.body_lock_smoothing);
    } else if (key == "body_lock_max_ai_force") {
        config.body_lock_max_ai_force = parse_float_value(value, config.body_lock_max_ai_force);
    } else if (key == "body_lock_opposing_boost_max_ai_force") {
        config.body_lock_opposing_boost_max_ai_force =
            parse_float_value(value, config.body_lock_opposing_boost_max_ai_force);
    } else if (key == "body_lock_max_ai_force_y") {
        config.body_lock_max_ai_force_y =
            parse_float_value(value, config.body_lock_max_ai_force_y);
    } else if (key == "body_lock_box_tolerance_px") {
        config.body_lock_box_tolerance_px =
            parse_float_value(value, config.body_lock_box_tolerance_px);
    } else if (key == "body_lock_activation_box_px") {
        config.body_lock_activation_box_px =
            parse_float_value(value, config.body_lock_activation_box_px);
    } else if (key == "body_lock_confidence_frames") {
        config.body_lock_confidence_frames =
            parse_int_value(value, config.body_lock_confidence_frames);
    } else if (key == "body_lock_confidence_min_strong") {
        config.body_lock_confidence_min_strong =
            parse_float_value(value, config.body_lock_confidence_min_strong);
    } else if (key == "body_lock_opposing_suppression_max") {
        config.body_lock_opposing_suppression_max =
            parse_float_value(value, config.body_lock_opposing_suppression_max);
    } else if (key == "body_lock_orthogonal_suppression_max") {
        config.body_lock_orthogonal_suppression_max =
            parse_float_value(value, config.body_lock_orthogonal_suppression_max);
    } else if (key == "body_lock_helpful_preservation_floor") {
        config.body_lock_helpful_preservation_floor =
            parse_float_value(value, config.body_lock_helpful_preservation_floor);
    } else if (key == "body_lock_manual_overlap_scale") {
        config.body_lock_manual_overlap_scale =
            parse_float_value(value, config.body_lock_manual_overlap_scale);
    } else if (key == "body_lock_manual_escape_input_threshold") {
        config.body_lock_manual_escape_input_threshold =
            parse_float_value(value, config.body_lock_manual_escape_input_threshold);
    } else if (key == "body_lock_manual_escape_preservation") {
        config.body_lock_manual_escape_preservation =
            parse_float_value(value, config.body_lock_manual_escape_preservation);
    } else if (key == "body_lock_manual_takeover_enabled") {
        config.body_lock_manual_takeover_enabled =
            parse_bool_value(value, config.body_lock_manual_takeover_enabled);
    } else if (key == "body_lock_manual_takeover_input_threshold") {
        config.body_lock_manual_takeover_input_threshold =
            parse_float_value(value, config.body_lock_manual_takeover_input_threshold);
    } else if (key == "body_lock_manual_takeover_commit_ms") {
        config.body_lock_manual_takeover_commit_ms =
            parse_float_value(value, config.body_lock_manual_takeover_commit_ms);
    } else if (key == "body_lock_manual_takeover_release_ms") {
        config.body_lock_manual_takeover_release_ms =
            parse_float_value(value, config.body_lock_manual_takeover_release_ms);
    } else if (key == "body_lock_near_lock_error_px") {
        config.body_lock_near_lock_error_px =
            parse_float_value(value, config.body_lock_near_lock_error_px);
    } else if (key == "body_lock_vertical_orthogonal_bias") {
        config.body_lock_vertical_orthogonal_bias =
            parse_float_value(value, config.body_lock_vertical_orthogonal_bias);
    } else if (key == "body_lock_vertical_deadzone_px") {
        config.body_lock_vertical_deadzone_px =
            parse_float_value(value, config.body_lock_vertical_deadzone_px);
    } else if (key == "body_lock_vertical_tail_inner_px") {
        config.body_lock_vertical_tail_inner_px =
            parse_float_value(value, config.body_lock_vertical_tail_inner_px);
    } else if (key == "body_lock_vertical_tail_speed_threshold_px_per_sec") {
        config.body_lock_vertical_tail_speed_threshold_px_per_sec =
            parse_float_value(value, config.body_lock_vertical_tail_speed_threshold_px_per_sec);
    } else if (key == "body_lock_release_tail_scale") {
        config.body_lock_release_tail_scale =
            parse_float_value(value, config.body_lock_release_tail_scale);
    } else if (key == "body_lock_lateral_motion_min_speed_px_per_sec") {
        config.body_lock_lateral_motion_min_speed_px_per_sec =
            parse_float_value(value, config.body_lock_lateral_motion_min_speed_px_per_sec);
    } else if (key == "body_lock_lateral_motion_lead_seconds") {
        config.body_lock_lateral_motion_lead_seconds =
            parse_float_value(value, config.body_lock_lateral_motion_lead_seconds);
    } else if (key == "body_lock_lateral_motion_lead_window_px") {
        config.body_lock_lateral_motion_lead_window_px =
            parse_float_value(value, config.body_lock_lateral_motion_lead_window_px);
    } else if (key == "body_lock_lateral_motion_lead_max_px") {
        config.body_lock_lateral_motion_lead_max_px =
            parse_float_value(value, config.body_lock_lateral_motion_lead_max_px);
    } else if (key == "body_lock_lateral_motion_tail_scale") {
        config.body_lock_lateral_motion_tail_scale =
            parse_float_value(value, config.body_lock_lateral_motion_tail_scale);
    } else if (key == "body_lock_lead_frames") {
        config.body_lock_lead_frames = parse_int_value(value, config.body_lock_lead_frames);
    } else if (key == "body_lock_lead_seconds") {
        config.body_lock_lead_seconds = parse_float_value(value, config.body_lock_lead_seconds);
    } else if (key == "body_lock_vertical_lead_scale") {
        config.body_lock_vertical_lead_scale =
            parse_float_value(value, config.body_lock_vertical_lead_scale);
    } else if (key == "body_lock_lead_max_px") {
        config.body_lock_lead_max_px = parse_float_value(value, config.body_lock_lead_max_px);
    } else if (key == "body_lock_target_match_iou") {
        config.body_lock_target_match_iou =
            parse_float_value(value, config.body_lock_target_match_iou);
    } else if (key == "body_lock_target_match_center_px") {
        config.body_lock_target_match_center_px =
            parse_float_value(value, config.body_lock_target_match_center_px);
    } else if (key == "body_lock_upper_body_ratio") {
        config.body_lock_upper_body_ratio =
            parse_float_value(value, config.body_lock_upper_body_ratio);
    }
}

void apply_gamepad_aim_assist_dynamics_value(
    GamepadAimAssistDynamicsConfig& config,
    const std::string& key,
    const std::string& value) {
    if (key == "enabled") {
        config.enabled = parse_bool_value(value, config.enabled);
    } else if (key == "recoil_jitter_guard_enabled") {
        config.recoil_jitter_guard_enabled =
            parse_bool_value(value, config.recoil_jitter_guard_enabled);
    } else if (key == "recoil_jitter_assist_threshold") {
        config.recoil_jitter_assist_threshold =
            parse_float_value(value, config.recoil_jitter_assist_threshold);
    } else if (key == "recoil_jitter_flip_scale") {
        config.recoil_jitter_flip_scale = parse_float_value(value, config.recoil_jitter_flip_scale);
    } else if (key == "recoil_jitter_memory_seconds") {
        config.recoil_jitter_memory_seconds =
            parse_float_value(value, config.recoil_jitter_memory_seconds);
    } else if (key == "manual_curve_straighten_enabled") {
        config.manual_curve_straighten_enabled =
            parse_bool_value(value, config.manual_curve_straighten_enabled);
    } else if (key == "manual_curve_straighten_strength") {
        config.manual_curve_straighten_strength =
            parse_float_value(value, config.manual_curve_straighten_strength);
    } else if (key == "manual_curve_straighten_min_manual") {
        config.manual_curve_straighten_min_manual =
            parse_float_value(value, config.manual_curve_straighten_min_manual);
    } else if (key == "manual_curve_straighten_min_assist") {
        config.manual_curve_straighten_min_assist =
            parse_float_value(value, config.manual_curve_straighten_min_assist);
    }
}

void apply_gamepad_recoil_value(
    GamepadRecoilConfig& config,
    const std::string& key,
    const std::string& value) {
    if (key == "enabled") {
        config.enabled = parse_bool_value(value, config.enabled);
    } else if (key == "selection_log_enabled") {
        config.selection_log_enabled = parse_bool_value(value, config.selection_log_enabled);
    } else if (key == "profile_despike_enabled") {
        config.profile_despike_enabled = parse_bool_value(value, config.profile_despike_enabled);
    } else if (key == "native_recognizer_enabled") {
        config.native_recognizer_enabled = parse_bool_value(value, config.native_recognizer_enabled);
    } else if (key == "recognizer_log_enabled") {
        config.recognizer_log_enabled = parse_bool_value(value, config.recognizer_log_enabled);
    } else if (key == "recognizer_game") {
        config.recognizer_game = parse_string_value(value);
    } else if (key == "profile_directory") {
        config.profile_directory = parse_string_value(value);
    } else if (key == "calibration_directory") {
        config.calibration_directory = parse_string_value(value);
    } else if (key == "weapon_directory") {
        config.weapon_directory = parse_string_value(value);
    } else if (key == "recognizer_state_path") {
        config.recognizer_state_path = parse_string_value(value);
    } else if (key == "recognizer_fps") {
        config.recognizer_fps = parse_int_value(value, config.recognizer_fps);
    } else if (key == "profile_amount") {
        config.profile_amount = parse_float_value(value, config.profile_amount);
    } else if (key == "profile_x_amount") {
        config.profile_x_amount = parse_float_value(value, config.profile_x_amount);
    } else if (key == "feedback_amount") {
        config.feedback_amount = parse_float_value(value, config.feedback_amount);
    } else if (key == "profile_lead_ms") {
        config.profile_lead_ms = parse_float_value(value, config.profile_lead_ms);
    } else if (key == "profile_velocity_reference_ms") {
        config.profile_velocity_reference_ms =
            parse_float_value(value, config.profile_velocity_reference_ms);
    } else if (key == "profile_despike_threshold_px") {
        config.profile_despike_threshold_px =
            parse_float_value(value, config.profile_despike_threshold_px);
    } else if (key == "profile_despike_ratio") {
        config.profile_despike_ratio = parse_float_value(value, config.profile_despike_ratio);
    } else if (key == "piecewise_mid_pixels_y") {
        config.piecewise_mid_pixels_y = parse_float_value(value, config.piecewise_mid_pixels_y);
    } else if (key == "piecewise_max_pixels_y") {
        config.piecewise_max_pixels_y = parse_float_value(value, config.piecewise_max_pixels_y);
    } else if (key == "piecewise_mid_ratio_y") {
        config.piecewise_mid_ratio_y = parse_float_value(value, config.piecewise_mid_ratio_y);
    }
}

void apply_recoil_environment_overrides(GamepadRecoilConfig& config) {
    if (const char* enabled = std::getenv("ENABLE_RECOIL_RUNTIME")) {
        if (enabled[0] != '\0') {
            config.enabled = parse_bool_value(enabled, config.enabled);
        }
    }
    if (const char* enabled = std::getenv("RECOIL_ENABLED")) {
        if (enabled[0] != '\0') {
            config.enabled = parse_bool_value(enabled, config.enabled);
        }
    }
    if (const char* game = std::getenv("RECOIL_GAME")) {
        if (game[0] != '\0') {
            config.recognizer_game = game;
        }
    }
    if (const char* native_recognizer = std::getenv("RECOIL_NATIVE_RECOGNIZER")) {
        if (native_recognizer[0] != '\0') {
            config.native_recognizer_enabled =
                parse_bool_value(native_recognizer, config.native_recognizer_enabled);
        }
    }
    if (const char* recognizer_log = std::getenv("RECOIL_RECOGNIZER_LOG")) {
        if (recognizer_log[0] != '\0') {
            config.recognizer_log_enabled =
                parse_bool_value(recognizer_log, config.recognizer_log_enabled);
        }
    }
    if (const char* recognizer_fps = std::getenv("RECOIL_RECOGNIZER_FPS")) {
        if (recognizer_fps[0] != '\0') {
            config.recognizer_fps = parse_int_value(recognizer_fps, config.recognizer_fps);
        }
    }
    if (const char* profile_dir = std::getenv("RECOIL_PROFILE_DIR")) {
        if (profile_dir[0] != '\0') {
            config.profile_directory = profile_dir;
        }
    }
    if (const char* calibration_dir = std::getenv("RECOIL_CALIBRATION_DIR")) {
        if (calibration_dir[0] != '\0') {
            config.calibration_directory = calibration_dir;
        }
    }
    if (const char* weapon_dir = std::getenv("RECOIL_WEAPON_DIR")) {
        if (weapon_dir[0] != '\0') {
            config.weapon_directory = weapon_dir;
        }
    } else if (const char* signature_dir = std::getenv("RECOIL_SIGNATURE_DIR")) {
        if (signature_dir[0] != '\0') {
            config.weapon_directory = signature_dir;
        }
    }
    if (const char* recognizer_state_path = std::getenv("RECOIL_RECOGNIZER_STATE_PATH")) {
        if (recognizer_state_path[0] != '\0') {
            config.recognizer_state_path = recognizer_state_path;
        }
    }
}

void apply_recoil_runtime_defaults(GamepadRecoilConfig& config) {
    if (config.recognizer_state_path.empty()) {
        config.recognizer_state_path = "artifacts/recoil_app/current_weapon.json";
    }
}

void apply_gamepad_environment_overrides(GamepadRuntimeConfig& config) {
    if (const char* auto_detect = std::getenv("GAMEPAD_XINPUT_AUTO_DETECT")) {
        if (auto_detect[0] != '\0') {
            config.xinput_auto_detect = parse_bool_value(auto_detect, config.xinput_auto_detect);
        }
    }
    if (const char* user_index = std::getenv("GAMEPAD_XINPUT_USER_INDEX")) {
        if (user_index[0] != '\0') {
            config.xinput_user_index = parse_uint_value(user_index, config.xinput_user_index);
            config.xinput_auto_detect = false;
        }
    }
}

void apply_vision_environment_overrides(VisionRuntimeConfig& config) {
    if (const char* capture_fps = std::getenv("VISION_CAPTURE_FPS")) {
        if (capture_fps[0] != '\0') {
            config.capture_fps = parse_int_value(capture_fps, config.capture_fps);
            config.gpu_service_active_fps = config.capture_fps;
        }
    }
    if (const char* aim_perf_file_log = std::getenv("VISION_AIM_PERF_FILE_LOG")) {
        if (aim_perf_file_log[0] != '\0') {
            config.aim_perf_file_log =
                parse_bool_value(aim_perf_file_log, config.aim_perf_file_log);
        }
    }
    if (const char* aim_perf_log_dir = std::getenv("VISION_AIM_PERF_LOG_DIR")) {
        if (aim_perf_log_dir[0] != '\0') {
            config.aim_perf_log_dir = aim_perf_log_dir;
        }
    }
    if (const char* interval = std::getenv("VISION_AIM_PERF_LOG_INTERVAL_TICKS")) {
        if (interval[0] != '\0') {
            config.aim_perf_log_interval_ticks =
                parse_uint_value(interval, config.aim_perf_log_interval_ticks);
        }
    }
    if (const char* enabled = std::getenv("VISION_GPU_SERVICE_ENABLED")) {
        if (enabled[0] != '\0') {
            config.gpu_service_enabled =
                parse_bool_value(enabled, config.gpu_service_enabled);
        }
    }
    if (const char* active_fps = std::getenv("VISION_GPU_SERVICE_ACTIVE_FPS")) {
        if (active_fps[0] != '\0') {
            config.gpu_service_active_fps =
                parse_int_value(active_fps, config.gpu_service_active_fps);
        }
    }
    if (const char* idle_fps = std::getenv("VISION_GPU_SERVICE_IDLE_FPS")) {
        if (idle_fps[0] != '\0') {
            config.gpu_service_idle_fps =
                parse_int_value(idle_fps, config.gpu_service_idle_fps);
        }
    }
    if (const char* keepwarm = std::getenv("VISION_GPU_SERVICE_KEEPWARM_WHEN_IDLE")) {
        if (keepwarm[0] != '\0') {
            config.gpu_service_keepwarm_when_idle =
                parse_bool_value(keepwarm, config.gpu_service_keepwarm_when_idle);
        }
    }
    if (const char* repeat = std::getenv("VISION_GPU_SERVICE_REPEAT_LAST_ON_NO_UPDATE")) {
        if (repeat[0] != '\0') {
            config.gpu_service_repeat_last_on_no_update =
                parse_bool_value(repeat, config.gpu_service_repeat_last_on_no_update);
        }
    }
    // fusion channel env overrides
    if (const char* fusion_enabled = std::getenv("FUSION_ENABLED")) {
        if (fusion_enabled[0] != '\0') {
            config.fusion_enabled =
                parse_bool_value(fusion_enabled, config.fusion_enabled);
        }
    }
    if (const char* fusion_off = std::getenv("FUSION_FORCE_OFF")) {
        if (fusion_off[0] != '\0') {
            if (parse_bool_value(fusion_off, false)) {
                config.fusion_enabled = false;
            }
        }
    }
    if (const char* fusion_session = std::getenv("FUSION_SESSION")) {
        if (fusion_session[0] != '\0') {
            config.fusion_session = fusion_session;
        }
    }
    if (const char* fusion_show = std::getenv("FUSION_SHOW_ALL_DETECTIONS")) {
        if (fusion_show[0] != '\0') {
            config.fusion_show_all_detections =
                parse_bool_value(fusion_show, config.fusion_show_all_detections);
        }
    }
}

void apply_value(
    RuntimeConfig& config,
    const std::string& section,
    const std::string& key,
    const std::string& value) {
    if (section == "runtime.vision") {
        apply_runtime_vision_value(config.vision, key, value);
    } else if (section == "runtime.telemetry") {
        if (key == "enabled") {
            config.telemetry.enabled = parse_bool_value(value, config.telemetry.enabled);
        } else if (key == "mode") {
            config.telemetry.mode = parse_string_value(value);
        } else if (key == "manual_controller_hz") {
            config.telemetry.manual_controller_hz = parse_int_value(value, config.telemetry.manual_controller_hz);
        } else if (key == "vision_on_new_frame") {
            config.telemetry.vision_on_new_frame = parse_bool_value(value, config.telemetry.vision_on_new_frame);
        } else if (key == "candidate_details") {
            config.telemetry.candidate_details = parse_string_value(value);
        } else if (key == "queue_capacity") {
            config.telemetry.queue_capacity = parse_uint_value(value, config.telemetry.queue_capacity);
        } else if (key == "rotate_size_mb") {
            config.telemetry.rotate_size_mb = parse_uint_value(value, config.telemetry.rotate_size_mb);
        } else if (key == "max_files") {
            config.telemetry.max_files = parse_uint_value(value, config.telemetry.max_files);
        } else if (key == "event_pre_ms") {
            config.telemetry.event_pre_ms = parse_uint_value(value, config.telemetry.event_pre_ms);
        } else if (key == "event_post_ms") {
            config.telemetry.event_post_ms = parse_uint_value(value, config.telemetry.event_post_ms);
        }
    } else if (section == "runtime.scheduler") {
        if (key == "controller_tick_hz") {
            config.scheduler.controller_tick_hz =
                parse_int_value(value, config.scheduler.controller_tick_hz);
        } else if (key == "mode") {
            config.scheduler.mode = parse_string_value(value);
        } else if (key == "spin_tail_us") {
            config.scheduler.spin_tail_us = parse_uint_value(value, config.scheduler.spin_tail_us);
        }
    } else if (section == "runtime.input") {
        if (key == "auto_detect") {
            config.gamepad.xinput_auto_detect = parse_bool_value(value, config.gamepad.xinput_auto_detect);
        } else if (key == "controller_index") {
            config.gamepad.xinput_user_index = parse_uint_value(value, config.gamepad.xinput_user_index);
        } else if (key == "rb_counts_as_aiming") {
            config.gamepad.rb_counts_as_aiming = parse_bool_value(value, config.gamepad.rb_counts_as_aiming);
        }
    } else if (section == "runtime.output") {
        if (key == "enabled") config.output.enabled = parse_bool_value(value, config.output.enabled);
        else if (key == "validation_mode") config.output.validation_mode = parse_string_value(value);
    } else if (section == "gamepad.tracker") {
        auto& tracker = config.gamepad.ai_aim;
        if (key == "backend") {
            config.gamepad.tracker_backend = tracking_native::parse_tracker_backend_kind(parse_string_value(value));
        } else if (key == "projection_age_ms") {
            tracker.target_projection_max_age_ms = parse_float_value(value, tracker.target_projection_max_age_ms);
        } else if (key == "responsiveness") {
            tracker.target_projection_velocity_lowpass_alpha =
                parse_float_value(value, tracker.target_projection_velocity_lowpass_alpha);
        } else if (key == "max_velocity_px_per_sec") {
            tracker.target_projection_max_velocity_px_per_sec =
                parse_float_value(value, tracker.target_projection_max_velocity_px_per_sec);
        } else if (key == "weak_memory_decay") {
            tracker.target_projection_weak_velocity_decay =
                parse_float_value(value, tracker.target_projection_weak_velocity_decay);
        } else if (key == "lead_seconds") {
            tracker.body_lock_lead_seconds = parse_float_value(value, tracker.body_lock_lead_seconds);
        } else if (key == "lead_max_px") {
            tracker.body_lock_lead_max_px = parse_float_value(value, tracker.body_lock_lead_max_px);
        }
    } else if (section == "gamepad.intent") {
        if (key == "wrong_way_manual_preservation_floor") {
            config.gamepad.intent.wrong_way_manual_preservation_floor = std::clamp(
                parse_float_value(
                    value,
                    config.gamepad.intent.wrong_way_manual_preservation_floor),
                0.50f,
                1.00f);
        }
    } else if (section == "runtime.gamepad") {
        apply_runtime_gamepad_value(config.gamepad, key, value);
    } else if (section == "gamepad.ads") {
        auto& ads = config.gamepad.ai_aim;
        if (key == "strength_scale") {
            const float scale = parse_float_value(value, 1.0f);
            config.ads.strength_scale = scale;
            ads.max_ai_force *= scale;
            ads.ads_snap_max_ai_force *= scale;
        } else if (key == "vertical_strength_scale") {
            const float scale = parse_float_value(value, 1.0f);
            config.ads.vertical_strength_scale = scale;
            ads.max_ai_force_y *= scale;
            ads.ads_snap_max_ai_force_y *= scale;
        } else if (key == "range_px") {
            ads.max_pixels = parse_float_value(value, ads.max_pixels);
        } else if (key == "snap_duration_ms") {
            ads.ads_snap_window_ms = parse_int_value(value, ads.ads_snap_window_ms);
        } else if (key == "completion_radius_px") {
            ads.ads_completion_radius_px = parse_float_value(value, ads.ads_completion_radius_px);
            config.ads.completion_radius_px = ads.ads_completion_radius_px;
        } else if (key == "completion_fresh_frames") {
            ads.ads_completion_fresh_frames = parse_int_value(value, ads.ads_completion_fresh_frames);
            config.ads.completion_fresh_frames = ads.ads_completion_fresh_frames;
        } else if (key == "max_acquisition_ms") {
            ads.ads_max_acquisition_ms = parse_float_value(value, ads.ads_max_acquisition_ms);
            config.ads.max_acquisition_ms = ads.ads_max_acquisition_ms;
        }
    } else if (section == "gamepad.bodylock") {
        auto& body = config.gamepad.ai_aim;
        if (key == "strength") {
            body.body_lock_max_ai_force = parse_float_value(value, body.body_lock_max_ai_force);
        } else if (key == "vertical_strength") {
            body.body_lock_max_ai_force_y = parse_float_value(value, body.body_lock_max_ai_force_y);
        } else if (key == "activation_range_px") {
            body.body_lock_activation_box_px = parse_float_value(value, body.body_lock_activation_box_px);
        } else if (key == "tolerance_px") {
            body.body_lock_box_tolerance_px = parse_float_value(value, body.body_lock_box_tolerance_px);
        } else if (key == "manual_escape_threshold") {
            body.body_lock_manual_escape_input_threshold =
                parse_float_value(value, body.body_lock_manual_escape_input_threshold);
        } else if (key == "manual_escape_preservation") {
            body.body_lock_manual_escape_preservation =
                parse_float_value(value, body.body_lock_manual_escape_preservation);
        } else if (key == "manual_takeover_enabled") {
            body.body_lock_manual_takeover_enabled =
                parse_bool_value(value, body.body_lock_manual_takeover_enabled);
        } else if (key == "manual_takeover_threshold") {
            body.body_lock_manual_takeover_input_threshold =
                parse_float_value(value, body.body_lock_manual_takeover_input_threshold);
        } else if (key == "manual_takeover_commit_ms") {
            body.body_lock_manual_takeover_commit_ms =
                parse_float_value(value, body.body_lock_manual_takeover_commit_ms);
        } else if (key == "manual_takeover_release_ms") {
            body.body_lock_manual_takeover_release_ms =
                parse_float_value(value, body.body_lock_manual_takeover_release_ms);
        }
    } else if (section == "gamepad.auto_fire") {
        apply_gamepad_auto_fire_value(config.gamepad.auto_fire, key, value);
    } else if (section == "gamepad.ai_aim") {
        apply_gamepad_ai_aim_value(config.gamepad.ai_aim, key, value);
    } else if (section == "gamepad.aim_assist_dynamics") {
        apply_gamepad_aim_assist_dynamics_value(config.gamepad.aim_assist_dynamics, key, value);
    } else if (section == "gamepad.recoil") {
        apply_gamepad_recoil_value(config.gamepad.recoil, key, value);
    }
}

void mark_environment_sources(RuntimeConfig& config) {
    auto mark = [&config](const char* environment, const char* key) {
        const char* value = std::getenv(environment);
        if (value != nullptr && value[0] != '\0') config.effective_sources[key] = "environment";
    };
    mark("VISION_CAPTURE_FPS", "runtime.vision.capture_fps");
    mark("VISION_CAPTURE_FPS", "runtime.vision.gpu_service_active_fps");
    mark("VISION_AIM_PERF_FILE_LOG", "runtime.vision.aim_perf_file_log");
    mark("VISION_AIM_PERF_LOG_DIR", "runtime.vision.aim_perf_log_dir");
    mark("VISION_AIM_PERF_LOG_INTERVAL_TICKS", "runtime.vision.aim_perf_log_interval_ticks");
    mark("VISION_GPU_SERVICE_ENABLED", "runtime.vision.gpu_service_enabled");
    mark("VISION_GPU_SERVICE_ACTIVE_FPS", "runtime.vision.gpu_service_active_fps");
    mark("VISION_GPU_SERVICE_IDLE_FPS", "runtime.vision.gpu_service_idle_fps");
    mark("VISION_GPU_SERVICE_KEEPWARM_WHEN_IDLE", "runtime.vision.gpu_service_keepwarm_when_idle");
    mark("VISION_GPU_SERVICE_REPEAT_LAST_ON_NO_UPDATE", "runtime.vision.gpu_service_repeat_last_on_no_update");
    mark("GAMEPAD_XINPUT_AUTO_DETECT", "runtime.gamepad.xinput_auto_detect");
    mark("GAMEPAD_XINPUT_USER_INDEX", "runtime.gamepad.xinput_user_index");
    mark("ENABLE_RECOIL_RUNTIME", "gamepad.recoil.enabled");
    mark("RECOIL_ENABLED", "gamepad.recoil.enabled");
    mark("RECOIL_GAME", "gamepad.recoil.recognizer_game");
    mark("RECOIL_NATIVE_RECOGNIZER", "gamepad.recoil.native_recognizer_enabled");
    mark("RECOIL_RECOGNIZER_LOG", "gamepad.recoil.recognizer_log_enabled");
    mark("RECOIL_RECOGNIZER_FPS", "gamepad.recoil.recognizer_fps");
    mark("RECOIL_PROFILE_DIR", "gamepad.recoil.profile_directory");
    mark("RECOIL_CALIBRATION_DIR", "gamepad.recoil.calibration_directory");
    mark("RECOIL_WEAPON_DIR", "gamepad.recoil.weapon_directory");
    mark("RECOIL_SIGNATURE_DIR", "gamepad.recoil.weapon_directory");
    mark("RECOIL_RECOGNIZER_STATE_PATH", "gamepad.recoil.recognizer_state_path");
}

void validate_runtime_config(const RuntimeConfig& config) {
    auto invalid = [](const std::string& key, const std::string& range) {
        throw std::runtime_error(
            "invalid user override for " + key + "; accepted range: " + range);
    };
    if (config.vision.capture_fps < 1 || config.vision.capture_fps > 1000)
        invalid("runtime.vision.capture_fps", "1..1000");
    if (config.vision.idle_capture_fps < 1 || config.vision.idle_capture_fps > 240)
        invalid("runtime.vision.idle_capture_fps", "1..240");
    if (config.vision.color_readback_mode != "pageable" && config.vision.color_readback_mode != "pinned")
        invalid("runtime.vision.color_readback_mode", "pageable|pinned");
    if (config.telemetry.mode != "debug" && config.telemetry.mode != "profile")
        invalid("runtime.telemetry.mode", "debug|profile");
    if (config.telemetry.manual_controller_hz < 1 || config.telemetry.manual_controller_hz > 1000)
        invalid("runtime.telemetry.manual_controller_hz", "1..1000");
    if (config.scheduler.controller_tick_hz < 100 || config.scheduler.controller_tick_hz > 2000)
        invalid("runtime.scheduler.controller_tick_hz", "100..2000");
    if (config.scheduler.mode != "legacy" && config.scheduler.mode != "precision")
        invalid("runtime.scheduler.mode", "legacy|precision");
    if (config.scheduler.spin_tail_us > 1000)
        invalid("runtime.scheduler.spin_tail_us", "0..1000");
    if (config.output.validation_mode != "strict")
        invalid("runtime.output.validation_mode", "strict");
    if (config.ads.strength_scale < 0.0f || config.ads.strength_scale > 3.0f)
        invalid("gamepad.ads.strength_scale", "0..3");
    if (config.ads.vertical_strength_scale < 0.0f || config.ads.vertical_strength_scale > 3.0f)
        invalid("gamepad.ads.vertical_strength_scale", "0..3");
    if (config.ads.completion_radius_px < 1.0f || config.ads.completion_radius_px > 64.0f)
        invalid("gamepad.ads.completion_radius_px", "1..64");
    if (config.ads.completion_fresh_frames < 1 || config.ads.completion_fresh_frames > 20)
        invalid("gamepad.ads.completion_fresh_frames", "1..20");
    if (config.ads.max_acquisition_ms < 50.0f || config.ads.max_acquisition_ms > 1000.0f)
        invalid("gamepad.ads.max_acquisition_ms", "50..1000");
    if (config.gamepad.tracker.aim_height_ratio < 0.0f ||
        config.gamepad.tracker.aim_height_ratio > 1.0f)
        invalid("gamepad.tracker.aim_height_ratio", "0..1");
}

}  // namespace

RuntimeConfig load_runtime_config(
    const std::filesystem::path& path,
    const std::string& profile_override) {
    RuntimeConfig config;
    const std::filesystem::path config_path = path.empty()
        ? std::filesystem::path("config.toml")
        : path;
    std::ifstream input(config_path);
    if (!input) {
        if (!profile_override.empty()) {
            apply_profile(config, profile_override);
            config.effective_sources["runtime.profile"] = "cli";
        }
        apply_recoil_runtime_defaults(config.gamepad.recoil);
        apply_vision_environment_overrides(config.vision);
        apply_recoil_environment_overrides(config.gamepad.recoil);
        apply_gamepad_environment_overrides(config.gamepad);
        mark_environment_sources(config);
        validate_runtime_config(config);
        return config;
    }

    struct Entry { std::string section; std::string key; std::string value; };
    std::vector<Entry> entries;
    std::string section;
    std::string line;
    while (std::getline(input, line)) {
        line = trim(strip_comment(line));
        if (line.empty()) {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            section = trim(line.substr(1, line.size() - 2));
            continue;
        }

        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        const std::string key = trim(line.substr(0, equals));
        const std::string value = trim(line.substr(equals + 1));
        entries.push_back(Entry{section, key, value});
    }

    std::optional<float> canonical_aim_height_ratio;
    std::optional<float> legacy_aim_height_ratio;
    for (const Entry& entry : entries) {
        if (entry.section == "gamepad.tracker" && entry.key == "aim_height_ratio") {
            canonical_aim_height_ratio = parse_float_value(
                entry.value,
                config.gamepad.tracker.aim_height_ratio);
        } else if (
            (entry.section == "runtime.gamepad" || entry.section == "gamepad.ai_aim") &&
            entry.key == "body_lock_upper_body_ratio") {
            legacy_aim_height_ratio = parse_float_value(
                entry.value,
                config.gamepad.tracker.aim_height_ratio);
        }
    }

    if (!profile_override.empty()) {
        apply_profile(config, profile_override);
        config.effective_sources["runtime.profile"] = "cli";
    } else {
        for (const Entry& entry : entries) {
            if (entry.section == "runtime" && entry.key == "profile") {
                apply_profile(config, parse_string_value(entry.value));
            }
        }
    }
    for (const Entry& entry : entries) {
        if (entry.section == "runtime" && entry.key == "profile") continue;
        if (!is_known_key(entry.section, entry.key)) {
            config.diagnostics.push_back(
                "unknown config key: " + entry.section + "." + entry.key);
            continue;
        }
        const std::string full_key = entry.section + "." + entry.key;
        const bool canonical_aim_height =
            full_key == "gamepad.tracker.aim_height_ratio";
        const bool legacy_aim_height =
            entry.key == "body_lock_upper_body_ratio" &&
            (entry.section == "runtime.gamepad" || entry.section == "gamepad.ai_aim");
        if (!canonical_aim_height && !legacy_aim_height) {
            apply_value(config, entry.section, entry.key, entry.value);
        }
        if (canonical_aim_height) {
            config.effective_sources[full_key] = "user";
            continue;
        }
        if (legacy_aim_height) {
            config.diagnostics.push_back(
                std::string(canonical_aim_height_ratio ? "ignored deprecated config key: " :
                                                         "deprecated config key: ") +
                full_key + "; use gamepad.tracker.aim_height_ratio");
            if (canonical_aim_height_ratio) {
                config.effective_sources[full_key] = "ignored_alias";
            }
            continue;
        }
        const bool legacy = entry.key == "gpu_service_active_fps" ||
            entry.key == "gpu_service_idle_fps" ||
            entry.key == "gpu_service_keepwarm_when_idle" ||
            entry.key == "aim_perf_file_log" ||
            entry.key == "aim_perf_log_interval_ticks";
        config.effective_sources[full_key] = legacy ? "legacy_user" : "user";
        if (legacy) config.diagnostics.push_back("deprecated config key: " + full_key);
    }


    if (canonical_aim_height_ratio) {
        config.gamepad.tracker.aim_height_ratio = *canonical_aim_height_ratio;
        config.gamepad.ai_aim.body_lock_upper_body_ratio = *canonical_aim_height_ratio;
        config.effective_sources["gamepad.tracker.aim_height_ratio"] = "user";
    } else if (legacy_aim_height_ratio) {
        config.gamepad.tracker.aim_height_ratio = *legacy_aim_height_ratio;
        config.gamepad.ai_aim.body_lock_upper_body_ratio = *legacy_aim_height_ratio;
        config.effective_sources["gamepad.tracker.aim_height_ratio"] = "deprecated_alias";
    }

    if (config.effective_source("runtime.vision.gpu_service_active_fps") != "legacy_user") {
        config.vision.gpu_service_active_fps = config.vision.capture_fps;
        config.effective_sources["runtime.vision.gpu_service_active_fps"] =
            config.effective_source("runtime.vision.capture_fps");
    }

    apply_recoil_runtime_defaults(config.gamepad.recoil);
    apply_vision_environment_overrides(config.vision);
    apply_recoil_environment_overrides(config.gamepad.recoil);
    apply_gamepad_environment_overrides(config.gamepad);
    mark_environment_sources(config);
    validate_runtime_config(config);
    return config;
}

RuntimeConfig load_runtime_config(const std::filesystem::path& path) {
    return load_runtime_config(path, std::string{});
}

}  // namespace controller_native
