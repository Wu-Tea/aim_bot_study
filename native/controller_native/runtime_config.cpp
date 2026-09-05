#include "runtime_config.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
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
    } else if (key == "tensor_width") {
        config.tensor_width = parse_int_value(value, config.tensor_width);
    } else if (key == "tensor_height") {
        config.tensor_height = parse_int_value(value, config.tensor_height);
    } else if (key == "require_isotropic_resize") {
        config.require_isotropic_resize =
            parse_bool_value(value, config.require_isotropic_resize);
    } else if (key == "dynamic_viewport_enabled") {
        config.dynamic_viewport_enabled =
            parse_bool_value(value, config.dynamic_viewport_enabled);
    } else if (key == "viewport_precision_width") {
        config.viewport_precision_width =
            parse_int_value(value, config.viewport_precision_width);
    } else if (key == "viewport_precision_height") {
        config.viewport_precision_height =
            parse_int_value(value, config.viewport_precision_height);
    } else if (key == "viewport_normal_width") {
        config.viewport_normal_width =
            parse_int_value(value, config.viewport_normal_width);
    } else if (key == "viewport_normal_height") {
        config.viewport_normal_height =
            parse_int_value(value, config.viewport_normal_height);
    } else if (key == "viewport_rescue_width") {
        config.viewport_rescue_width =
            parse_int_value(value, config.viewport_rescue_width);
    } else if (key == "viewport_rescue_height") {
        config.viewport_rescue_height =
            parse_int_value(value, config.viewport_rescue_height);
    } else if (key == "viewport_prediction_ms") {
        config.viewport_prediction_ms =
            parse_float_value(value, config.viewport_prediction_ms);
    } else if (key == "capture_fps") {
        config.capture_fps = parse_int_value(value, config.capture_fps);
    } else if (key == "idle_capture_fps") {
        config.idle_capture_fps = parse_int_value(value, config.idle_capture_fps);
    } else if (key == "keepwarm_when_idle") {
        config.keepwarm_when_idle = parse_bool_value(value, config.keepwarm_when_idle);
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
    } else if (key == "gpu_service_enabled") {
        config.gpu_service_enabled = parse_bool_value(value, config.gpu_service_enabled);
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
        "crop_width", "capture_width", "crop_height", "capture_height",
        "tensor_width", "tensor_height", "require_isotropic_resize", "capture_fps",
        "dynamic_viewport_enabled", "viewport_precision_width",
        "viewport_precision_height", "viewport_normal_width",
        "viewport_normal_height", "viewport_rescue_width",
        "viewport_rescue_height", "viewport_prediction_ms",
        "idle_capture_fps", "keepwarm_when_idle", "color_readback_mode", "model_path", "fallback_model_path",
        "quit_key", "native_cue_sidecar", "perf_log", "gpu_service_enabled",
        "fusion_enabled", "fusion_session", "fusion_show_all_detections"};
    static const std::unordered_set<std::string> telemetry_keys{
        "enabled", "directory", "manual_controller_hz", "queue_capacity",
        "rotate_size_mb", "max_files"};
    static const std::unordered_set<std::string> performance_keys{
        "enabled", "interval_ms", "directory", "stdout_enabled"};
    static const std::unordered_set<std::string> scheduler_keys{
        "controller_tick_hz", "mode", "spin_tail_us", "efficiency_core_affinity",
        "efficiency_core_count"};
    static const std::unordered_set<std::string> input_keys{
        "auto_detect", "controller_index", "rb_counts_as_aiming"};
    static const std::unordered_set<std::string> output_keys{"enabled", "validation_mode"};
    static const std::unordered_set<std::string> tracker_keys{
        "aim_height_ratio", "max_observation_age_ms"};
    static const std::unordered_set<std::string> aim_response_curve_keys{
        "algorithm", "calibration_reference_stick"};
    static const std::unordered_set<std::string> ads_keys{
        "strength_scale", "vertical_strength_scale", "activation_radius_px",
        "pickup_base_radius_px",
        "scope_ready_trigger", "snap_duration_ms",
        "completion_radius_px", "completion_fresh_frames",
        "target_wait_ms", "extension_budget_ms", "max_acquisition_ms"};
    static const std::unordered_set<std::string> bodylock_keys{
        "strength", "vertical_strength", "activation_range_px", "tolerance_px"};
    static const std::unordered_set<std::string> gamepad_keys{
        "auto_fire_output", "rb_counts_as_aiming", "xinput_auto_detect",
        "xinput_user_index"};
    static const std::unordered_set<std::string> auto_fire_keys{
        "fire_output", "manual_fire_activates_ai_aim", "aim_only",
        "max_source_age_ms", "require_aim_ready",
        "manual_takeover_release_seconds", "manual_takeover_resume_delay_seconds",
        "pulse_width_ms", "pulse_period_ms"};
    static const std::unordered_set<std::string> enemy_mark_keys{
        "enabled", "l3_cooldown_ms", "lt_cooldown_ms"};
    static const std::unordered_set<std::string> recoil_keys{
        "enabled", "selection_log_enabled", "profile_despike_enabled",
        "profile_playback_enabled", "native_recognizer_enabled",
        "recognizer_log_enabled", "recognizer_game",
        "profile_directory", "calibration_directory", "weapon_directory",
        "recognizer_state_path", "recognizer_fps", "profile_amount", "profile_x_amount",
        "feedback_amount", "feedback_min_amount", "feedback_max_amount",
        "profile_lead_ms", "profile_velocity_reference_ms",
        "profile_despike_threshold_px", "profile_despike_ratio", "piecewise_mid_pixels_y",
        "piecewise_max_pixels_y", "piecewise_mid_ratio_y"};
    static const std::unordered_set<std::string> ai_aim_keys{
        "ai_delta_gain", "target_max_age_ms", "ads_activation_radius_px",
        "ads_pickup_base_radius_px", "ads_scope_ready_trigger", "ads_snap_window_ms",
        "ads_snap_max_ai_force", "ads_snap_max_ai_force_y",
        "ads_completion_radius_px", "ads_completion_fresh_frames",
        "ads_target_wait_ms", "ads_extension_budget_ms",
        // Legacy key retained for old generated configs; it maps only to the
        // extension budget and is never reused as the wait deadline.
        "ads_max_acquisition_ms",
        "auto_fire_ready_error_px", "auto_fire_ready_frames",
        "auto_fire_ready_max_ai_stick", "cue_hold_body_lock_force_scale",
        "cue_hold_full_force_min_target_height_ratio",
        "body_lock_max_ai_force", "body_lock_max_ai_force_y",
        "body_lock_box_tolerance_px", "body_lock_activation_box_px",
        "aim_response_effect_delay_ms",
        "desired_point_traversal_ms", "desired_point_boundary_exit_ms",
        "visual_authority_enabled"};
    if (section == "runtime") return runtime_keys.count(key) != 0;
    if (section == "runtime.vision") return vision_keys.count(key) != 0;
    if (section == "runtime.telemetry") return telemetry_keys.count(key) != 0;
    if (section == "runtime.performance") return performance_keys.count(key) != 0;
    if (section == "runtime.scheduler") return scheduler_keys.count(key) != 0;
    if (section == "runtime.input") return input_keys.count(key) != 0;
    if (section == "runtime.output") return output_keys.count(key) != 0;
    if (section == "gamepad.tracker") return tracker_keys.count(key) != 0;
    if (section == "gamepad.aim_response_curve")
        return aim_response_curve_keys.count(key) != 0;
    if (section == "gamepad.ads") return ads_keys.count(key) != 0;
    if (section == "gamepad.bodylock") return bodylock_keys.count(key) != 0;
    if (section == "runtime.gamepad") return gamepad_keys.count(key) != 0;
    if (section == "gamepad.enemy_mark") return enemy_mark_keys.count(key) != 0;
    if (section == "gamepad.auto_fire") return auto_fire_keys.count(key) != 0;
    if (section == "gamepad.ai_aim") return ai_aim_keys.count(key) != 0;
    if (section == "gamepad.recoil") return recoil_keys.count(key) != 0;
    return false;
}

void apply_profile(RuntimeConfig& config, const std::string& profile) {
    if (profile == "performance") {
        config.vision.capture_fps = 240;
        config.vision.idle_capture_fps = 60;
    } else if (profile == "balanced") {
        config.vision.capture_fps = 160;
        config.vision.idle_capture_fps = 60;
    } else if (profile == "low_latency") {
        config.vision.capture_fps = 200;
        config.vision.idle_capture_fps = 60;
    } else if (profile == "legacy") {
        return;
    } else {
        throw std::runtime_error(
            "invalid runtime profile '" + profile +
            "'; available profiles: performance, balanced, low_latency");
    }
    config.profile = profile;
    config.vision.keepwarm_when_idle = true;
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
    }
}

void apply_gamepad_auto_fire_value(
    GamepadAutoFireConfig& config,
    const std::string& key,
    const std::string& value) {
    if (key == "fire_output") {
        config.fire_output = parse_string_value(value);
    } else if (key == "manual_fire_activates_ai_aim") {
        config.manual_fire_activates_ai_aim = parse_bool_value(
            value, config.manual_fire_activates_ai_aim);
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
    } else if (key == "pulse_width_ms") {
        config.pulse_width_ms = parse_float_value(value, config.pulse_width_ms);
    } else if (key == "pulse_period_ms") {
        config.pulse_period_ms = parse_float_value(value, config.pulse_period_ms);
    }
}

void apply_gamepad_aim_response_curve_value(
    AimResponseCurveConfig& config,
    const std::string& key,
    const std::string& value) {
    if (key == "algorithm") {
        const std::string name = parse_string_value(value);
        AimResponseCurveAlgorithm algorithm{};
        if (!try_parse_aim_response_curve_algorithm(name, algorithm)) {
            throw std::runtime_error(
                "invalid gamepad.aim_response_curve.algorithm '" + name +
                "'; expected linear|cod_dynamic_legacy_lut");
        }
        config.algorithm = algorithm;
    } else if (key == "calibration_reference_stick") {
        config.calibration_reference_stick = parse_float_value(
            value, config.calibration_reference_stick);
    }
}

void apply_gamepad_ai_aim_value(
    GamepadAiAimConfig& config,
    const std::string& key,
    const std::string& value) {
    if (key == "ai_delta_gain") {
        config.ai_delta_gain = parse_float_value(value, config.ai_delta_gain);
    } else if (key == "target_max_age_ms") {
        config.target_max_age_ms = parse_float_value(value, config.target_max_age_ms);
    } else if (key == "ads_activation_radius_px") {
        config.ads_activation_radius_px =
            parse_float_value(value, config.ads_activation_radius_px);
    } else if (key == "ads_pickup_base_radius_px") {
        config.ads_pickup_base_radius_px =
            parse_float_value(value, config.ads_pickup_base_radius_px);
    } else if (key == "ads_scope_ready_trigger") {
        config.ads_scope_ready_trigger =
            parse_float_value(value, config.ads_scope_ready_trigger);
    } else if (key == "ads_snap_window_ms") {
        config.ads_snap_window_ms = parse_int_value(value, config.ads_snap_window_ms);
    } else if (key == "ads_snap_max_ai_force") {
        config.ads_snap_max_ai_force = parse_float_value(value, config.ads_snap_max_ai_force);
    } else if (key == "ads_snap_max_ai_force_y") {
        config.ads_snap_max_ai_force_y = parse_float_value(value, config.ads_snap_max_ai_force_y);
    } else if (key == "ads_completion_radius_px") {
        config.ads_completion_radius_px =
            parse_float_value(value, config.ads_completion_radius_px);
    } else if (key == "ads_completion_fresh_frames") {
        config.ads_completion_fresh_frames =
            parse_int_value(value, config.ads_completion_fresh_frames);
    } else if (key == "ads_target_wait_ms") {
        config.ads_target_wait_ms =
            parse_float_value(value, config.ads_target_wait_ms);
    } else if (
        key == "ads_extension_budget_ms" ||
        key == "ads_max_acquisition_ms") {
        config.ads_extension_budget_ms =
            parse_float_value(value, config.ads_extension_budget_ms);
    } else if (key == "auto_fire_ready_error_px") {
        config.auto_fire_ready_error_px = parse_float_value(value, config.auto_fire_ready_error_px);
    } else if (key == "auto_fire_ready_frames") {
        config.auto_fire_ready_frames = parse_int_value(value, config.auto_fire_ready_frames);
    } else if (key == "auto_fire_ready_max_ai_stick") {
        config.auto_fire_ready_max_ai_stick =
            parse_float_value(value, config.auto_fire_ready_max_ai_stick);
    } else if (key == "cue_hold_body_lock_force_scale") {
        config.cue_hold_body_lock_force_scale =
            parse_float_value(value, config.cue_hold_body_lock_force_scale);
    } else if (key == "cue_hold_full_force_min_target_height_ratio") {
        config.cue_hold_full_force_min_target_height_ratio =
            parse_float_value(
                value,
                config.cue_hold_full_force_min_target_height_ratio);
    } else if (key == "body_lock_max_ai_force") {
        config.body_lock_max_ai_force = parse_float_value(value, config.body_lock_max_ai_force);
    } else if (key == "body_lock_max_ai_force_y") {
        config.body_lock_max_ai_force_y =
            parse_float_value(value, config.body_lock_max_ai_force_y);
    } else if (key == "body_lock_box_tolerance_px") {
        config.body_lock_box_tolerance_px =
            parse_float_value(value, config.body_lock_box_tolerance_px);
    } else if (key == "body_lock_activation_box_px") {
        config.body_lock_activation_box_px =
            parse_float_value(value, config.body_lock_activation_box_px);
    } else if (key == "aim_response_effect_delay_ms") {
        config.aim_response_effect_delay_ms = parse_float_value(
            value, config.aim_response_effect_delay_ms);
    } else if (key == "desired_point_traversal_ms") {
        config.desired_point_traversal_ms =
            parse_float_value(value, config.desired_point_traversal_ms);
    } else if (key == "desired_point_boundary_exit_ms") {
        config.desired_point_boundary_exit_ms =
            parse_float_value(value, config.desired_point_boundary_exit_ms);
    } else if (key == "visual_authority_enabled") {
        config.visual_authority_enabled =
            parse_bool_value(value, config.visual_authority_enabled);
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
    } else if (key == "profile_playback_enabled") {
        // Accepted for old config files, but retired from the live runtime.
        config.profile_playback_enabled = false;
    } else if (key == "native_recognizer_enabled") {
        config.native_recognizer_enabled = false;
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
    } else if (key == "feedback_min_amount") {
        config.feedback_min_amount =
            parse_float_value(value, config.feedback_min_amount);
    } else if (key == "feedback_max_amount") {
        config.feedback_max_amount =
            parse_float_value(value, config.feedback_max_amount);
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
    // RECOIL_NATIVE_RECOGNIZER is retired and intentionally inert.
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
    if ((config.profile_playback_enabled || config.native_recognizer_enabled) &&
        config.recognizer_state_path.empty()) {
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
        }
    }
    if (const char* enabled = std::getenv("VISION_GPU_SERVICE_ENABLED")) {
        if (enabled[0] != '\0') {
            config.gpu_service_enabled =
                parse_bool_value(enabled, config.gpu_service_enabled);
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

void apply_telemetry_environment_overrides(RuntimeTelemetryConfig& config) {
    if (const char* enabled = std::getenv("RUNTIME_TELEMETRY_ENABLED")) {
        if (enabled[0] != '\0') {
            config.enabled = parse_bool_value(enabled, config.enabled);
        }
    }
    if (const char* directory = std::getenv("RUNTIME_TELEMETRY_DIRECTORY")) {
        if (directory[0] != '\0') config.directory = directory;
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
        } else if (key == "directory") {
            config.telemetry.directory = parse_string_value(value);
        } else if (key == "manual_controller_hz") {
            config.telemetry.manual_controller_hz = parse_int_value(value, config.telemetry.manual_controller_hz);
        } else if (key == "queue_capacity") {
            config.telemetry.queue_capacity = parse_uint_value(value, config.telemetry.queue_capacity);
        } else if (key == "rotate_size_mb") {
            config.telemetry.rotate_size_mb = parse_uint_value(value, config.telemetry.rotate_size_mb);
        } else if (key == "max_files") {
            config.telemetry.max_files = parse_uint_value(value, config.telemetry.max_files);
        }
    } else if (section == "runtime.performance") {
        if (key == "enabled") {
            config.performance.enabled = parse_bool_value(value, config.performance.enabled);
        } else if (key == "interval_ms") {
            config.performance.interval_ms = parse_uint_value(
                value, config.performance.interval_ms);
        } else if (key == "directory") {
            config.performance.directory = parse_string_value(value);
        } else if (key == "stdout_enabled") {
            config.performance.stdout_enabled = parse_bool_value(
                value, config.performance.stdout_enabled);
        }
    } else if (section == "runtime.scheduler") {
        if (key == "controller_tick_hz") {
            config.scheduler.controller_tick_hz =
                parse_int_value(value, config.scheduler.controller_tick_hz);
        } else if (key == "mode") {
            config.scheduler.mode = parse_string_value(value);
        } else if (key == "spin_tail_us") {
            config.scheduler.spin_tail_us = parse_uint_value(value, config.scheduler.spin_tail_us);
        } else if (key == "efficiency_core_affinity") {
            config.scheduler.efficiency_core_affinity =
                parse_bool_value(value, config.scheduler.efficiency_core_affinity);
        } else if (key == "efficiency_core_count") {
            config.scheduler.efficiency_core_count = parse_uint_value(
                value, config.scheduler.efficiency_core_count);
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
        if (key == "aim_height_ratio") {
            config.gamepad.tracker.aim_height_ratio = std::clamp(
                parse_float_value(
                    value,
                    config.gamepad.tracker.aim_height_ratio),
                0.0f,
                1.0f);
        } else if (key == "max_observation_age_ms") {
            config.gamepad.tracker.max_observation_age_ms = std::clamp(
                parse_float_value(
                    value,
                    config.gamepad.tracker.max_observation_age_ms),
                1.0f,
                250.0f);
        }
    } else if (section == "gamepad.aim_response_curve") {
        apply_gamepad_aim_response_curve_value(
            config.gamepad.aim_response_curve, key, value);
    } else if (section == "runtime.gamepad") {
        apply_runtime_gamepad_value(config.gamepad, key, value);
    } else if (section == "gamepad.ads") {
        auto& ads = config.gamepad.ai_aim;
        if (key == "strength_scale") {
            const float scale = parse_float_value(value, 1.0f);
            config.ads.strength_scale = scale;
            ads.ads_snap_max_ai_force *= scale;
        } else if (key == "vertical_strength_scale") {
            const float scale = parse_float_value(value, 1.0f);
            config.ads.vertical_strength_scale = scale;
            ads.ads_snap_max_ai_force_y *= scale;
        } else if (key == "activation_radius_px") {
            ads.ads_activation_radius_px =
                parse_float_value(value, ads.ads_activation_radius_px);
        } else if (key == "pickup_base_radius_px") {
            ads.ads_pickup_base_radius_px =
                parse_float_value(value, ads.ads_pickup_base_radius_px);
        } else if (key == "scope_ready_trigger") {
            ads.ads_scope_ready_trigger =
                parse_float_value(value, ads.ads_scope_ready_trigger);
        } else if (key == "snap_duration_ms") {
            ads.ads_snap_window_ms = parse_int_value(value, ads.ads_snap_window_ms);
        } else if (key == "completion_radius_px") {
            ads.ads_completion_radius_px = parse_float_value(value, ads.ads_completion_radius_px);
            config.ads.completion_radius_px = ads.ads_completion_radius_px;
        } else if (key == "completion_fresh_frames") {
            ads.ads_completion_fresh_frames = parse_int_value(value, ads.ads_completion_fresh_frames);
            config.ads.completion_fresh_frames = ads.ads_completion_fresh_frames;
        } else if (key == "target_wait_ms") {
            ads.ads_target_wait_ms = parse_float_value(
                value, ads.ads_target_wait_ms);
            config.ads.target_wait_ms = ads.ads_target_wait_ms;
        } else if (
            key == "extension_budget_ms" ||
            key == "max_acquisition_ms") {
            ads.ads_extension_budget_ms = parse_float_value(value, ads.ads_extension_budget_ms);
            config.ads.extension_budget_ms = ads.ads_extension_budget_ms;
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
        }
    } else if (section == "gamepad.enemy_mark") {
        if (key == "enabled") {
            config.gamepad.enemy_mark.enabled =
                parse_bool_value(value, config.gamepad.enemy_mark.enabled);
        } else if (key == "l3_cooldown_ms") {
            config.gamepad.enemy_mark.l3_cooldown_ms = parse_uint_value(
                value, config.gamepad.enemy_mark.l3_cooldown_ms);
        } else if (key == "lt_cooldown_ms") {
            config.gamepad.enemy_mark.lt_cooldown_ms = parse_uint_value(
                value, config.gamepad.enemy_mark.lt_cooldown_ms);
        }
    } else if (section == "gamepad.auto_fire") {
        apply_gamepad_auto_fire_value(config.gamepad.auto_fire, key, value);
    } else if (section == "gamepad.ai_aim") {
        apply_gamepad_ai_aim_value(config.gamepad.ai_aim, key, value);
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
    mark("VISION_GPU_SERVICE_ENABLED", "runtime.vision.gpu_service_enabled");
    mark("RUNTIME_TELEMETRY_ENABLED", "runtime.telemetry.enabled");
    mark("RUNTIME_TELEMETRY_DIRECTORY", "runtime.telemetry.directory");
    mark("GAMEPAD_XINPUT_AUTO_DETECT", "runtime.gamepad.xinput_auto_detect");
    mark("GAMEPAD_XINPUT_USER_INDEX", "runtime.gamepad.xinput_user_index");
    mark("ENABLE_RECOIL_RUNTIME", "gamepad.recoil.enabled");
    mark("RECOIL_ENABLED", "gamepad.recoil.enabled");
    mark("RECOIL_GAME", "gamepad.recoil.recognizer_game");
    mark("RECOIL_RECOGNIZER_LOG", "gamepad.recoil.recognizer_log_enabled");
    mark("RECOIL_RECOGNIZER_FPS", "gamepad.recoil.recognizer_fps");
    mark("RECOIL_PROFILE_DIR", "gamepad.recoil.profile_directory");
    mark("RECOIL_CALIBRATION_DIR", "gamepad.recoil.calibration_directory");
    mark("RECOIL_WEAPON_DIR", "gamepad.recoil.weapon_directory");
    mark("RECOIL_SIGNATURE_DIR", "gamepad.recoil.weapon_directory");
    mark("RECOIL_RECOGNIZER_STATE_PATH", "gamepad.recoil.recognizer_state_path");
}

void validate_runtime_config(RuntimeConfig& config) {
    auto invalid = [](const std::string& key, const std::string& range) {
        throw std::runtime_error(
            "invalid user override for " + key + "; accepted range: " + range);
    };
    if (config.vision.capture_fps < 1 || config.vision.capture_fps > 1000)
        invalid("runtime.vision.capture_fps", "1..1000");
    if (config.vision.capture_width < 32 || config.vision.capture_width > 8192)
        invalid("runtime.vision.capture_width", "32..8192");
    if (config.vision.capture_height < 32 || config.vision.capture_height > 8192)
        invalid("runtime.vision.capture_height", "32..8192");
    if (config.vision.tensor_width < 32 || config.vision.tensor_width > 8192)
        invalid("runtime.vision.tensor_width", "32..8192");
    if (config.vision.tensor_height < 32 || config.vision.tensor_height > 8192)
        invalid("runtime.vision.tensor_height", "32..8192");
    if (config.vision.dynamic_viewport_enabled) {
        const auto valid_dimension = [](int value) {
            return value >= 32 && value <= 8192;
        };
        if (!valid_dimension(config.vision.viewport_precision_width) ||
            !valid_dimension(config.vision.viewport_precision_height) ||
            !valid_dimension(config.vision.viewport_normal_width) ||
            !valid_dimension(config.vision.viewport_normal_height) ||
            !valid_dimension(config.vision.viewport_rescue_width) ||
            !valid_dimension(config.vision.viewport_rescue_height)) {
            invalid("runtime.vision.viewport_*", "32..8192");
        }
        if (config.vision.viewport_precision_width >
                config.vision.viewport_normal_width ||
            config.vision.viewport_precision_height >
                config.vision.viewport_normal_height ||
            config.vision.viewport_normal_width >
                config.vision.viewport_rescue_width ||
            config.vision.viewport_normal_height >
                config.vision.viewport_rescue_height) {
            invalid(
                "runtime.vision.viewport_*",
                "precision <= normal <= rescue");
        }
        if (config.vision.viewport_rescue_width >
                config.vision.capture_width ||
            config.vision.viewport_rescue_height >
                config.vision.capture_height) {
            invalid(
                "runtime.vision.viewport_rescue_*",
                "rescue viewport must fit inside capture");
        }
        const auto matches_tensor_aspect =
            [&config](int width, int height) {
                return static_cast<long long>(width) *
                        config.vision.tensor_height ==
                    static_cast<long long>(height) *
                        config.vision.tensor_width;
            };
        if (!matches_tensor_aspect(
                 config.vision.viewport_precision_width,
                 config.vision.viewport_precision_height) ||
             !matches_tensor_aspect(
                 config.vision.viewport_normal_width,
                 config.vision.viewport_normal_height) ||
             !matches_tensor_aspect(
                 config.vision.viewport_rescue_width,
                 config.vision.viewport_rescue_height)) {
            invalid(
                "runtime.vision.viewport_*",
                "each viewport must match tensor aspect ratio");
        }
        if (config.vision.viewport_prediction_ms < 0.0f ||
            config.vision.viewport_prediction_ms > 500.0f) {
            invalid("runtime.vision.viewport_prediction_ms", "0..500");
        }
    }
    if (config.vision.idle_capture_fps < 1 || config.vision.idle_capture_fps > 240)
        invalid("runtime.vision.idle_capture_fps", "1..240");
    if (config.vision.color_readback_mode != "pageable" && config.vision.color_readback_mode != "pinned")
        invalid("runtime.vision.color_readback_mode", "pageable|pinned");
    if (config.telemetry.manual_controller_hz < 1 || config.telemetry.manual_controller_hz > 1000)
        invalid("runtime.telemetry.manual_controller_hz", "1..1000");
    if (config.telemetry.directory.empty())
        invalid("runtime.telemetry.directory", "non-empty path");
    if (config.performance.interval_ms < 1000 || config.performance.interval_ms > 60000)
        invalid("runtime.performance.interval_ms", "1000..60000");
    if (config.performance.directory.empty())
        invalid("runtime.performance.directory", "non-empty path");
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
    if (config.gamepad.ai_aim.ads_target_wait_ms < 50.0f ||
        config.gamepad.ai_aim.ads_target_wait_ms > 1000.0f)
        invalid("gamepad.ads.target_wait_ms", "50..1000");
    if (config.gamepad.ai_aim.ads_extension_budget_ms < 0.0f ||
        config.gamepad.ai_aim.ads_extension_budget_ms > 1000.0f)
        invalid("gamepad.ads.extension_budget_ms", "0..1000");
    if (config.gamepad.ai_aim.ads_activation_radius_px <= 0.0f ||
        config.gamepad.ai_aim.ads_activation_radius_px > 2000.0f)
        invalid("gamepad.ads.activation_radius_px", "0..2000");
    if (config.gamepad.ai_aim.ads_pickup_base_radius_px <= 0.0f ||
        config.gamepad.ai_aim.ads_pickup_base_radius_px > 2000.0f)
        invalid("gamepad.ads.pickup_base_radius_px", "0..2000");
    if (config.gamepad.ai_aim.ads_scope_ready_trigger <= 0.05f ||
        config.gamepad.ai_aim.ads_scope_ready_trigger > 1.0f)
        invalid("gamepad.ads.scope_ready_trigger", "(0.05)..1");
    if (config.gamepad.ai_aim.desired_point_traversal_ms < 40.0f ||
        config.gamepad.ai_aim.desired_point_traversal_ms > 2000.0f)
        invalid("gamepad.ai_aim.desired_point_traversal_ms", "40..2000");
    if (config.gamepad.ai_aim.desired_point_boundary_exit_ms < 50.0f ||
        config.gamepad.ai_aim.desired_point_boundary_exit_ms > 2000.0f)
        invalid("gamepad.ai_aim.desired_point_boundary_exit_ms", "50..2000");
    if (config.gamepad.aim_response_curve.calibration_reference_stick < 0.05f ||
        config.gamepad.aim_response_curve.calibration_reference_stick > 1.0f)
        invalid(
            "gamepad.aim_response_curve.calibration_reference_stick",
            "0.05..1");
    if (config.gamepad.tracker.aim_height_ratio < 0.0f ||
        config.gamepad.tracker.aim_height_ratio > 1.0f)
        invalid("gamepad.tracker.aim_height_ratio", "0..1");
    if (config.gamepad.auto_fire.pulse_width_ms <= 0.0f ||
        config.gamepad.auto_fire.pulse_period_ms <= 0.0f ||
        config.gamepad.auto_fire.pulse_width_ms >
            config.gamepad.auto_fire.pulse_period_ms)
        invalid(
            "gamepad.auto_fire.pulse_width_ms",
            "0 < pulse_width_ms <= pulse_period_ms");
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
        apply_value(config, entry.section, entry.key, entry.value);
        config.effective_sources[full_key] = "user";
    }
    apply_recoil_runtime_defaults(config.gamepad.recoil);
    apply_vision_environment_overrides(config.vision);
    apply_telemetry_environment_overrides(config.telemetry);
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
