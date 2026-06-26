#include "runtime_config.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>

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
    } else if (key == "fusion_enabled") {
        config.fusion_enabled = parse_bool_value(value, config.fusion_enabled);
    } else if (key == "fusion_session") {
        config.fusion_session = parse_string_value(value);
    } else if (key == "fusion_show_all_detections") {
        config.fusion_show_all_detections =
            parse_bool_value(value, config.fusion_show_all_detections);
    }
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
    if (key == "aim_only") {
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
    } else if (section == "runtime.gamepad") {
        apply_runtime_gamepad_value(config.gamepad, key, value);
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

}  // namespace

RuntimeConfig load_runtime_config(const std::filesystem::path& path) {
    RuntimeConfig config;
    const std::filesystem::path config_path = path.empty()
        ? std::filesystem::path("config.toml")
        : path;
    std::ifstream input(config_path);
    if (!input) {
        apply_recoil_runtime_defaults(config.gamepad.recoil);
        apply_vision_environment_overrides(config.vision);
        apply_recoil_environment_overrides(config.gamepad.recoil);
        apply_gamepad_environment_overrides(config.gamepad);
        return config;
    }

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
        apply_value(config, section, key, value);
    }

    apply_recoil_runtime_defaults(config.gamepad.recoil);
    apply_vision_environment_overrides(config.vision);
    apply_recoil_environment_overrides(config.gamepad.recoil);
    apply_gamepad_environment_overrides(config.gamepad);
    return config;
}

}  // namespace controller_native
