#include "native_gamepad_controller.h"
#include "runtime_config.h"
#include "../runtime_app/vision_controller_adapter.h"

#include "pipeline_contract/target_snapshot.h"
#include "vision_native/target_selector.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <initializer_list>
#include <iostream>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

struct CliOptions {
    std::string suite = "all";
    std::filesystem::path config_path = "config.toml";
    std::string run_key;
    std::filesystem::path output_path;
    std::filesystem::path recoil_state_path;
    int frames = 120;
    double dt_ms = 8.333;
    int random_fov_ticks = 72000;
    unsigned int random_fov_seed = 1337;
    unsigned int selector_intent_seed = 1337;
    unsigned int roi_fallback_seed = 1337;
    double random_fov_min_scale = 0.68;
    double random_fov_max_scale = 1.0;
    double random_fov_ai_force_scale = 1.0;
    bool self_test = false;
};

struct BenchmarkShortTermOutputPlan {
    bool has_output = false;
    double output_x = 0.0;
    double output_y = 0.0;
    int shaped_ticks = 0;
    bool has_expected = false;
    double expected_dx = 0.0;
    double expected_dy = 0.0;
};

struct AxisStats {
    int manual_frames = 0;
    int final_opposes_manual = 0;
    int final_blocks_manual = 0;
    double manual_preservation_sum = 0.0;
    double abs_recoil_sum = 0.0;
    double abs_final_sum = 0.0;
    int recoil_opposes_manual = 0;
};

struct RandomFovSample {
    int segment = 0;
    int tick = 0;
    int global_tick = 0;
    double error_x = 0.0;
    double error_y = 0.0;
    std::string mode = "manual";
    double final_x = 0.0;
    double final_y = 0.0;
    double manual_x = 0.0;
    double manual_y = 0.0;
    double ai_aim_x = 0.0;
    double ai_aim_y = 0.0;
    double dynamics_x = 0.0;
    double dynamics_y = 0.0;
    double fov_scale = 1.0;
    double expected_dx = 0.0;
    double expected_dy = 0.0;
    bool vision_has_target = false;
    bool vision_aim_authority = false;
    bool vision_fire_authority = false;
    std::string vision_target_tier = "none";
    double vision_dx = 0.0;
    double vision_dy = 0.0;
    bool vision_has_tracker_projection = false;
    double vision_tracker_dx = 0.0;
    double vision_tracker_dy = 0.0;
    double vision_age_ms = 0.0;
    double target_speed_px_per_sec = 0.0;
    double heading_deg = 0.0;
};

struct RandomFovOvershootEvent {
    std::string axis = "x";
    int segment = 0;
    int crossing_tick = 0;
    int peak_tick = 0;
    int crossing_global_tick = 0;
    int peak_global_tick = 0;
    std::string peak_mode = "manual";
    double previous_error = 0.0;
    double crossing_error = 0.0;
    double peak_error = 0.0;
    double peak_abs_px = 0.0;
    double final_x = 0.0;
    double final_y = 0.0;
    double manual_x = 0.0;
    double manual_y = 0.0;
    double ai_aim_x = 0.0;
    double ai_aim_y = 0.0;
    double dynamics_x = 0.0;
    double dynamics_y = 0.0;
    double fov_scale = 1.0;
    double expected_dx = 0.0;
    double expected_dy = 0.0;
    bool vision_has_target = false;
    bool vision_aim_authority = false;
    bool vision_fire_authority = false;
    std::string vision_target_tier = "none";
    double vision_dx = 0.0;
    double vision_dy = 0.0;
    bool vision_has_tracker_projection = false;
    double vision_tracker_dx = 0.0;
    double vision_tracker_dy = 0.0;
    double vision_age_ms = 0.0;
    double target_speed_px_per_sec = 0.0;
    double heading_deg = 0.0;
};

struct RandomFovTurnEvent {
    int segment = 0;
    int tick = 0;
    int global_tick = 0;
    std::string mode = "manual";
    double turn_degrees = 0.0;
    double output_delta = 0.0;
    double previous_output_x = 0.0;
    double previous_output_y = 0.0;
    double output_x = 0.0;
    double output_y = 0.0;
    double manual_x = 0.0;
    double manual_y = 0.0;
    double ai_aim_x = 0.0;
    double ai_aim_y = 0.0;
    double dynamics_x = 0.0;
    double dynamics_y = 0.0;
    double fov_scale = 1.0;
    double expected_dx = 0.0;
    double expected_dy = 0.0;
    double target_speed_px_per_sec = 0.0;
    double heading_deg = 0.0;
};

struct AdsErrTargetWindow {
    int start_tick = 0;
    int end_tick = 0;
    double offset_dx = 0.0;
    double offset_dy = 0.0;
    bool was_active = false;
    bool recovery_pending = false;
    bool recovery_recorded = false;
    int recovery_start_tick = -1;
    double peak_recovery_error_px = 0.0;
};

struct ScenarioMetrics {
    std::string name;
    int frames = 0;
    int blocked_frames = 0;
    int recovery_frames = -1;
    AxisStats x;
    AxisStats y;
    bool has_random_fov = false;
    int random_fov_vision_samples = 0;
    int random_fov_tracker_only_ticks = 0;
    int random_fov_measured_ticks = 0;
    int random_fov_change_events = 0;
    int random_fov_large_vision_jumps = 0;
    int random_fov_fresh_wrong_direction = 0;
    int random_fov_tracker_wrong_direction = 0;
    int random_fov_fresh_insensitive = 0;
    int random_fov_tracker_insensitive = 0;
    double random_fov_min_scale = 0.0;
    double random_fov_max_scale = 0.0;
    double random_fov_mean_scale = 0.0;
    double random_fov_mean_abs_expected_dx = 0.0;
    double random_fov_mean_abs_final_x = 0.0;
    double random_fov_mean_abs_ai_aim_x = 0.0;
    double random_fov_output_per_100px_error = 0.0;
    double random_fov_mean_error_px = 0.0;
    double random_fov_p95_error_px = 0.0;
    double random_fov_p99_error_px = 0.0;
    int random_fov_direction_samples = 0;
    int random_fov_manual_direction_samples = 0;
    int random_fov_turn_samples = 0;
    double random_fov_mean_target_alignment = 0.0;
    double random_fov_mean_manual_alignment = 0.0;
    double random_fov_direction_score = 0.0;
    double random_fov_manual_direction_score = 0.0;
    double random_fov_mean_turn_degrees = 0.0;
    double random_fov_p95_turn_degrees = 0.0;
    double random_fov_turn_smoothness_score = 0.0;
    double random_fov_mean_output_delta = 0.0;
    double random_fov_p95_output_delta = 0.0;
    std::vector<RandomFovTurnEvent> random_fov_turn_details;
    int random_fov_overshoot_events = 0;
    int random_fov_overshoot_events_x = 0;
    int random_fov_overshoot_events_y = 0;
    int random_fov_overshoot_ads_snap = 0;
    int random_fov_overshoot_body_lock = 0;
    int random_fov_overshoot_manual = 0;
    double random_fov_max_overshoot_px = 0.0;
    std::vector<RandomFovOvershootEvent> random_fov_overshoot_details;
    double random_fov_ai_force_scale = 1.0;
    bool has_ads_settle = false;
    int ads_settle_ticks = 0;
    int ads_settle_measured_ticks = 0;
    int ads_settle_vision_samples = 0;
    int ads_settle_settled_tick = -1;
    int ads_settle_overshoot_events = 0;
    int ads_settle_overshoot_events_x = 0;
    int ads_settle_overshoot_events_y = 0;
    double ads_settle_initial_dx = 0.0;
    double ads_settle_initial_dy = 0.0;
    double ads_settle_fov_start_scale = 1.0;
    double ads_settle_fov_end_scale = 1.0;
    double ads_settle_fov_transition_ms = 0.0;
    double ads_settle_snap_window_ms = 0.0;
    double ads_settle_ai_force_scale = 1.0;
    double ads_settle_mean_error_px = 0.0;
    double ads_settle_p95_error_px = 0.0;
    double ads_settle_p99_error_px = 0.0;
    double ads_settle_max_overshoot_px = 0.0;
    double ads_settle_max_overshoot_x_px = 0.0;
    double ads_settle_max_overshoot_y_px = 0.0;
    double ads_settle_final_error_px = 0.0;
    double ads_settle_min_abs_x_px = 0.0;
    double ads_settle_min_abs_y_px = 0.0;
    bool has_ads_parity = false;
    int ads_parity_cases = 0;
    int ads_parity_frames_per_case = 0;
    int ads_parity_overshoot_cases = 0;
    int ads_parity_overshoot_events = 0;
    int ads_parity_crossing_events = 0;
    int ads_parity_ads_snap_crossing_events = 0;
    int ads_parity_body_lock_crossing_events = 0;
    int ads_parity_manual_crossing_events = 0;
    int ads_parity_ads_snap_overshoot_events = 0;
    int ads_parity_body_lock_overshoot_events = 0;
    int ads_parity_manual_overshoot_events = 0;
    int ads_parity_overshoot_events_x = 0;
    int ads_parity_overshoot_events_y = 0;
    int ads_parity_overshoot_events_none = 0;
    int ads_parity_overshoot_events_aligned_follow = 0;
    int ads_parity_overshoot_events_opposing_burst = 0;
    int ads_parity_overshoot_events_overshoot_recover = 0;
    int ads_parity_overshoot_recover_cases = 0;
    int ads_parity_overshoot_recover_events = 0;
    int ads_parity_under_20_cases = 0;
    double ads_parity_frame_dt_ms = 0.0;
    double ads_parity_target_sample_hz = 0.0;
    double ads_parity_mean_error_px = 0.0;
    double ads_parity_p95_error_px = 0.0;
    double ads_parity_p99_error_px = 0.0;
    double ads_parity_final_error_px = 0.0;
    double ads_parity_mean_time_to_under_20_ms = 0.0;
    double ads_parity_max_single_frame_camera_delta_px = 0.0;
    double ads_parity_max_overshoot_px = 0.0;
    bool has_ads_manual_stress = false;
    int ads_manual_stress_cases = 0;
    int ads_manual_stress_ticks_per_case = 0;
    int ads_manual_stress_vision_samples = 0;
    int ads_manual_stress_vision_dropped_ticks = 0;
    int ads_manual_stress_delayed_submissions = 0;
    int ads_manual_stress_fov_change_events = 0;
    int ads_manual_stress_large_vision_jumps = 0;
    int ads_manual_stress_err_target_windows = 0;
    int ads_manual_stress_err_target_samples = 0;
    int ads_manual_stress_err_target_recovered_windows = 0;
    int ads_manual_stress_measured_ticks = 0;
    int ads_manual_stress_overshoot_events = 0;
    int ads_manual_stress_overshoot_events_x = 0;
    int ads_manual_stress_overshoot_events_y = 0;
    int ads_manual_stress_overshoot_ads_snap = 0;
    int ads_manual_stress_overshoot_body_lock = 0;
    int ads_manual_stress_overshoot_manual = 0;
    int ads_manual_stress_large_overshoot_events = 0;
    double ads_manual_stress_mean_error_px = 0.0;
    double ads_manual_stress_p95_error_px = 0.0;
    double ads_manual_stress_p99_error_px = 0.0;
    double ads_manual_stress_final_error_px = 0.0;
    double ads_manual_stress_max_overshoot_px = 0.0;
    double ads_manual_stress_max_single_frame_camera_delta_px = 0.0;
    double ads_manual_stress_vision_hz = 100.0;
    double ads_manual_stress_mean_fov_scale = 1.0;
    double ads_manual_stress_max_report_age_ms = 0.0;
    double ads_manual_stress_max_err_target_offset_px = 0.0;
    double ads_manual_stress_mean_err_target_recovery_ms = 0.0;
    double ads_manual_stress_p95_err_target_recovery_ms = 0.0;
    double ads_manual_stress_max_err_target_recovery_error_px = 0.0;
    double ads_manual_stress_mean_target_alignment = 0.0;
    double ads_manual_stress_mean_manual_alignment = 0.0;
    double ads_manual_stress_direction_score = 0.0;
    double ads_manual_stress_manual_direction_score = 0.0;
    double ads_manual_stress_p95_turn_degrees = 0.0;
    double ads_manual_stress_turn_smoothness_score = 0.0;
    int ads_manual_stress_body_lock_frames = 0;
    double ads_manual_stress_body_lock_ratio = 0.0;
    int ads_manual_stress_body_lock_tracking_samples = 0;
    double ads_manual_stress_body_lock_mean_error_px = 0.0;
    double ads_manual_stress_body_lock_p95_error_px = 0.0;
    double ads_manual_stress_body_lock_direction_score = 0.0;
    int ads_manual_stress_body_lock_chatter_events = 0;
    int ads_manual_stress_body_lock_output_spikes = 0;
    int ads_manual_stress_body_lock_turn_samples = 0;
    int ads_manual_stress_body_lock_close_assist_samples = 0;
    int ads_manual_stress_body_lock_low_output_close_frames = 0;
    int ads_manual_stress_body_lock_dropout_frames = 0;
    int ads_manual_stress_body_lock_centered_samples = 0;
    int ads_manual_stress_body_lock_centered_jitter_frames = 0;
    double ads_manual_stress_body_lock_p95_output_delta = 0.0;
    double ads_manual_stress_body_lock_p95_turn_degrees = 0.0;
    double ads_manual_stress_body_lock_turn_smoothness_score = 0.0;
    double ads_manual_stress_body_lock_close_assist_mean_ai_output = 0.0;
    double ads_manual_stress_body_lock_centered_p95_output_delta = 0.0;
    int ads_manual_stress_body_lock_sustain_cases = 0;
    int ads_manual_stress_body_lock_sustain_passes = 0;
    int ads_manual_stress_body_lock_sustain_good_frames = 0;
    double ads_manual_stress_body_lock_sustain_required_ms = 0.0;
    double ads_manual_stress_body_lock_sustain_error_px = 0.0;
    double ads_manual_stress_body_lock_sustain_longest_ms = 0.0;
    double ads_manual_stress_body_lock_sustain_pass_rate = 0.0;
    double ads_manual_stress_body_lock_sustain_score = 0.0;
    double ads_manual_stress_occlusion_peak_error_px = 0.0;
    double ads_manual_stress_slide_down_lag_p95_px = 0.0;
    double ads_manual_stress_slide_recover_ms = 0.0;
    int ads_manual_stress_unreliable_high_output_frames = 0;
    int ads_manual_stress_unreliable_same_direction_frames = 0;
    int ads_manual_stress_unreliable_fight_frames = 0;
    int ads_manual_stress_unreliable_no_fresh_target_high_output_frames = 0;
    int ads_manual_stress_unreliable_err_target_high_output_frames = 0;
    double ads_manual_stress_unreliable_max_final_output = 0.0;
    double ads_manual_stress_unreliable_mean_final_output = 0.0;
    std::vector<RandomFovOvershootEvent> ads_manual_stress_overshoot_details;
    bool has_ads_bodylock_near_high = false;
    int ads_bodylock_near_high_ticks = 0;
    int ads_bodylock_near_high_vision_samples = 0;
    int ads_bodylock_near_high_ads_snap_frames = 0;
    int ads_bodylock_near_high_body_lock_frames = 0;
    int ads_bodylock_near_high_manual_frames = 0;
    int ads_bodylock_near_high_near_target_frames = 0;
    int ads_bodylock_near_high_output_frames = 0;
    int ads_bodylock_near_high_brake_active_frames = 0;
    int ads_bodylock_near_high_brake_inactive_frames = 0;
    int ads_bodylock_near_high_acquisition_expired_high_frames = 0;
    int ads_bodylock_near_high_chatter_events = 0;
    int ads_bodylock_near_high_output_spikes = 0;
    int ads_bodylock_near_high_close_assist_frames = 0;
    int ads_bodylock_near_high_low_output_close_frames = 0;
    int ads_bodylock_near_high_centered_frames = 0;
    int ads_bodylock_near_high_centered_jitter_frames = 0;
    double ads_bodylock_near_high_initial_dx = 0.0;
    double ads_bodylock_near_high_snap_window_ms = 0.0;
    double ads_bodylock_near_high_mean_error_px = 0.0;
    double ads_bodylock_near_high_p95_error_px = 0.0;
    double ads_bodylock_near_high_final_error_px = 0.0;
    double ads_bodylock_near_high_max_final_output = 0.0;
    double ads_bodylock_near_high_p95_output_delta = 0.0;
    double ads_bodylock_near_high_p95_turn_degrees = 0.0;
    double ads_bodylock_near_high_turn_smoothness_score = 0.0;
    double ads_bodylock_near_high_close_assist_mean_output = 0.0;
    double ads_bodylock_near_high_centered_p95_output_delta = 0.0;
    bool has_ads_carry_through = false;
    int ads_carry_through_ticks = 0;
    int ads_carry_through_vision_samples = 0;
    int ads_carry_through_ads_snap_frames = 0;
    int ads_carry_through_body_lock_frames = 0;
    int ads_carry_through_manual_frames = 0;
    int ads_carry_through_same_direction_accel_frames = 0;
    int ads_carry_through_near_target_frames = 0;
    int ads_carry_through_near_high_output_frames = 0;
    int ads_carry_through_brake_active_frames = 0;
    int ads_carry_through_brake_inactive_near_high_frames = 0;
    int ads_carry_through_sign_flip_events = 0;
    double ads_carry_through_initial_dx = 0.0;
    double ads_carry_through_snap_window_ms = 0.0;
    double ads_carry_through_mean_error_px = 0.0;
    double ads_carry_through_p95_error_px = 0.0;
    double ads_carry_through_final_error_px = 0.0;
    double ads_carry_through_max_overshoot_px = 0.0;
    double ads_carry_through_max_near_final_x = 0.0;
    double ads_carry_through_max_brake_error_px = 0.0;
    bool has_adversarial_controller = false;
    int adversarial_controller_ticks = 0;
    int adversarial_controller_vision_samples = 0;
    int adversarial_controller_wrong_target_frames = 0;
    int adversarial_controller_user_fight_frames = 0;
    int adversarial_controller_invalid_strong_frames = 0;
    int adversarial_controller_stale_high_output_frames = 0;
    int adversarial_controller_err_target_frames = 0;
    int adversarial_controller_recovery_frames = 0;
    int adversarial_controller_near_high_output_frames = 0;
    int adversarial_controller_projected_frames = 0;
    double adversarial_controller_max_final_output = 0.0;
    double adversarial_controller_p95_error_px = 0.0;
    double adversarial_controller_max_stale_age_ms = 0.0;
    double adversarial_controller_mean_manual_ai_alignment = 0.0;
    bool has_selector_intent = false;
    int selector_intent_seed = 0;
    int selector_intent_ticks = 0;
    int selector_intent_target_frames = 0;
    int selector_intent_wrong_target_frames = 0;
    int selector_intent_switches = 0;
    int selector_intent_ping_pong_switches = 0;
    int selector_intent_fire_authority_leaks = 0;
    int selector_intent_intent_applied_frames = 0;
    int selector_intent_ignored_active_lock_frames = 0;
    int selector_intent_delayed_switch_confirm_frames = 0;
    int selector_intent_weak_no_fire_frames = 0;
    double selector_intent_wrong_target_rate = 0.0;
    bool has_roi_fallback = false;
    int roi_fallback_seed = 0;
    int roi_fallback_ticks = 0;
    int roi_fallback_requested_regions = 0;
    int roi_fallback_partial_regions = 0;
    int roi_fallback_full_frame_requests = 0;
    int roi_fallback_external_cue_no_full_frame_frames = 0;
    int roi_fallback_roi_miss_hold_frames = 0;
    int roi_fallback_edge_clamped_regions = 0;
    int roi_fallback_target_loss_frames = 0;
    double roi_fallback_processed_area_ratio = 0.0;
};

enum class BodylockChaseMotionProfile {
    Smooth,
    Slide,
    CrouchCycle,
    Jump,
    ArcJump,
};

double current_seconds() {
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string default_run_key() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm local_time{};
#ifdef _WIN32
    localtime_s(&local_time, &time);
#else
    localtime_r(&time, &local_time);
#endif
    std::ostringstream out;
    out << "native-gamepad-" << std::put_time(&local_time, "%Y%m%d-%H%M%S");
    return out.str();
}

void print_usage() {
    std::cout
        << "Usage: cod_native_gamepad_benchmark [--suite all|selector_intent|roi_fallback] "
        << "[--config config.toml] "
        << "[--run-key key] [--output path] [--frames n] [--dt-ms ms] "
        << "[--recoil-state path] [--random-fov-ticks n] "
        << "[--random-fov-seed n] [--random-fov-min-scale v] "
        << "[--random-fov-max-scale v] [--random-fov-ai-force-scale v] "
        << "[--selector-intent-seed n] [--roi-fallback-seed n] [--self-test]\n";
}

CliOptions parse_args(int argc, char** argv) {
    CliOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--help" || arg == "-h") {
            print_usage();
            std::exit(0);
        }
        if (arg == "--self-test") {
            options.self_test = true;
        } else if (arg == "--suite" && index + 1 < argc) {
            options.suite = argv[++index];
        } else if (arg == "--config" && index + 1 < argc) {
            options.config_path = argv[++index];
        } else if (arg == "--run-key" && index + 1 < argc) {
            options.run_key = argv[++index];
        } else if (arg == "--output" && index + 1 < argc) {
            options.output_path = argv[++index];
        } else if (arg == "--recoil-state" && index + 1 < argc) {
            options.recoil_state_path = argv[++index];
        } else if (arg == "--frames" && index + 1 < argc) {
            options.frames = std::max(1, std::stoi(argv[++index]));
        } else if (arg == "--dt-ms" && index + 1 < argc) {
            options.dt_ms = std::max(0.0, std::stod(argv[++index]));
        } else if (arg == "--random-fov-ticks" && index + 1 < argc) {
            options.random_fov_ticks = std::max(0, std::stoi(argv[++index]));
        } else if (arg == "--random-fov-seed" && index + 1 < argc) {
            options.random_fov_seed = static_cast<unsigned int>(
                std::stoul(argv[++index]));
        } else if (arg == "--selector-intent-seed" && index + 1 < argc) {
            options.selector_intent_seed = static_cast<unsigned int>(
                std::stoul(argv[++index]));
        } else if (arg == "--roi-fallback-seed" && index + 1 < argc) {
            options.roi_fallback_seed = static_cast<unsigned int>(
                std::stoul(argv[++index]));
        } else if (arg == "--random-fov-min-scale" && index + 1 < argc) {
            options.random_fov_min_scale = std::stod(argv[++index]);
        } else if (arg == "--random-fov-max-scale" && index + 1 < argc) {
            options.random_fov_max_scale = std::stod(argv[++index]);
        } else if (arg == "--random-fov-ai-force-scale" && index + 1 < argc) {
            options.random_fov_ai_force_scale = std::stod(argv[++index]);
        } else {
            throw std::runtime_error("unknown or incomplete argument: " + arg);
        }
    }
    if (options.random_fov_min_scale > options.random_fov_max_scale) {
        std::swap(options.random_fov_min_scale, options.random_fov_max_scale);
    }
    if (options.suite != "all"
        && options.suite != "selector_intent"
        && options.suite != "roi_fallback") {
        throw std::runtime_error("unknown suite: " + options.suite);
    }
    options.random_fov_min_scale =
        std::max(0.05, std::min(2.0, options.random_fov_min_scale));
    options.random_fov_max_scale =
        std::max(0.05, std::min(2.0, options.random_fov_max_scale));
    options.random_fov_ai_force_scale =
        std::max(0.1, std::min(4.0, options.random_fov_ai_force_scale));
    if (options.run_key.empty()) {
        options.run_key = default_run_key();
    }
    if (options.output_path.empty()) {
        options.output_path =
            std::filesystem::path("artifacts") / "benchmarks" / "native_gamepad" /
            (options.run_key + ".json");
    }
    return options;
}

controller_native::PhysicalGamepadState aiming_state(
    float manual_x,
    float manual_y,
    bool fire_active = false) {
    controller_native::PhysicalGamepadState state;
    state.connected = true;
    state.left_trigger = 1.0f;
    state.right_trigger = fire_active ? 1.0f : 0.0f;
    state.right_x = manual_x;
    state.right_y = manual_y;
    return state;
}

controller_native::NativeControllerVisionState target_state(float dx, float dy, double now) {
    controller_native::NativeControllerVisionState state;
    state.has_target = true;
    state.auto_fire_requested = false;
    state.dx = dx;
    state.dy = dy;
    state.screen_center_x = 320.0f;
    state.screen_center_y = 256.0f;
    state.target_x = state.screen_center_x + dx;
    state.target_y = state.screen_center_y + dy;
    state.has_body_box = true;
    state.body_x1 = state.target_x - 28.0f;
    state.body_x2 = state.target_x + 28.0f;
    state.body_y1 = state.target_y - 72.0f;
    state.body_y2 = state.target_y + 96.0f;
    state.aim_authority = true;
    state.fire_authority = true;
    state.target_tier = "observed_strong";
    state.observed_at_seconds = now;
    return state;
}

controller_native::NativeControllerVisionState benchmark_target_state(
    float dx,
    float dy,
    double now) {
    constexpr float kBodyBoxWidth = 84.0f;
    constexpr float kBodyBoxHeight = 180.0f;
    constexpr float kUpperBodyRatio = 0.40f;
    controller_native::NativeControllerVisionState state =
        target_state(dx, dy, now);
    state.body_x1 = state.target_x - (kBodyBoxWidth * 0.5f);
    state.body_x2 = state.target_x + (kBodyBoxWidth * 0.5f);
    state.body_y1 = state.target_y - (kBodyBoxHeight * kUpperBodyRatio);
    state.body_y2 = state.body_y1 + kBodyBoxHeight;
    return state;
}

double rate(int count, int total) {
    return total <= 0 ? 0.0 : static_cast<double>(count) / static_cast<double>(total);
}

double mean_ratio(const AxisStats& stats) {
    return stats.manual_frames <= 0
        ? 0.0
        : stats.manual_preservation_sum / static_cast<double>(stats.manual_frames);
}

double mean_abs_recoil(const AxisStats& stats, int frames) {
    return frames <= 0 ? 0.0 : stats.abs_recoil_sum / static_cast<double>(frames);
}

double mean_abs_final(const AxisStats& stats, int frames) {
    return frames <= 0 ? 0.0 : stats.abs_final_sum / static_cast<double>(frames);
}

int direction(double value, double deadzone) {
    if (value > deadzone) {
        return 1;
    }
    if (value < -deadzone) {
        return -1;
    }
    return 0;
}

double lerp(double from, double to, double ratio) {
    const double t = std::max(0.0, std::min(1.0, ratio));
    return from + ((to - from) * t);
}

double ads_lag_stress_fov_scale_for_tick(int tick) {
    if (tick < 45) {
        return 1.0;
    }
    if (tick < 135) {
        return lerp(1.0, 0.72, static_cast<double>(tick - 45) / 90.0);
    }
    if (tick < 240) {
        return 0.72;
    }
    if (tick < 320) {
        return lerp(0.72, 1.0, static_cast<double>(tick - 240) / 80.0);
    }
    return 1.0;
}

bool ads_lag_stress_drops_vision_tick(int tick) {
    return (tick >= 140 && tick < 186) || (tick >= 275 && tick < 338);
}

bool ads_err_target_window_active(const AdsErrTargetWindow& window, int tick) {
    return tick >= window.start_tick && tick < window.end_tick;
}

double ads_err_target_window_offset_radius(const AdsErrTargetWindow& window) {
    return std::hypot(window.offset_dx, window.offset_dy);
}

void copy_frame_vision_to_sample(
    RandomFovSample& sample,
    const controller_native::NativeControllerVisionState& vision_state,
    double now_seconds) {
    sample.vision_has_target = vision_state.has_target;
    sample.vision_aim_authority = vision_state.aim_authority;
    sample.vision_fire_authority = vision_state.fire_authority;
    sample.vision_target_tier = vision_state.target_tier;
    sample.vision_dx = vision_state.dx;
    sample.vision_dy = vision_state.dy;
    sample.vision_has_tracker_projection = vision_state.has_tracker_projection;
    sample.vision_tracker_dx = vision_state.tracker_dx;
    sample.vision_tracker_dy = vision_state.tracker_dy;
    sample.vision_age_ms =
        vision_state.observed_at_seconds > 0.0 && now_seconds > 0.0
            ? std::max(0.0, now_seconds - vision_state.observed_at_seconds) * 1000.0
            : 0.0;
}

float clamp_float(float value, float minimum, float maximum) {
    return std::max(minimum, std::min(maximum, value));
}

double clamp_double(double value, double minimum, double maximum) {
    return std::max(minimum, std::min(maximum, value));
}

double mean_value(const std::vector<double>& values) {
    if (values.empty()) {
        return 0.0;
    }
    double sum = 0.0;
    for (const double value : values) {
        sum += value;
    }
    return sum / static_cast<double>(values.size());
}

double nearest_rank_percentile(std::vector<double> values, double percentile) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const double clamped = std::max(0.0, std::min(1.0, percentile));
    const std::size_t index = static_cast<std::size_t>(
        std::ceil(clamped * static_cast<double>(values.size())) - 1.0);
    return values[std::min(index, values.size() - 1u)];
}

double vector_magnitude(double x, double y) {
    return std::sqrt((x * x) + (y * y));
}

double vector_alignment(
    double ax,
    double ay,
    double bx,
    double by,
    double min_magnitude);

double vector_turn_degrees(
    double previous_x,
    double previous_y,
    double current_x,
    double current_y,
    double min_magnitude);

void reset_benchmark_short_plan(
    BenchmarkShortTermOutputPlan& plan,
    double output_x,
    double output_y,
    double expected_dx,
    double expected_dy) {
    plan.has_output = true;
    plan.output_x = output_x;
    plan.output_y = output_y;
    plan.shaped_ticks = 0;
    plan.has_expected = true;
    plan.expected_dx = expected_dx;
    plan.expected_dy = expected_dy;
}

void apply_benchmark_short_plan(
    BenchmarkShortTermOutputPlan& plan,
    controller_native::NativeControllerOutputComponents& components,
    double expected_dx,
    double expected_dy,
    double reticle_speed) {
    constexpr double kVectorDeadzone = 0.015;
    constexpr double kManualYieldMagnitude = 0.18;
    constexpr double kManualYieldAlignment = 0.25;
    constexpr double kTargetJumpResetPx = 24.0;
    constexpr double kFarErrorResetPx = 96.0;
    constexpr double kBrakeNearErrorPx = 18.0;
    constexpr double kBrakeFarErrorPx = 36.0;
    constexpr double kNearHorizonSeconds = 0.022;
    constexpr double kFarHorizonSeconds = 0.016;
    constexpr double kCrossingFraction = 0.72;

    const double raw_x = components.final_stick.x;
    const double raw_y = -components.final_stick.y;
    const double manual_x = components.manual_stick.x;
    const double manual_y = -components.manual_stick.y;
    const double raw_assist_x = raw_x - manual_x;
    const double raw_assist_y = raw_y - manual_y;
    const double manual_magnitude = vector_magnitude(manual_x, manual_y);
    const double error_radius = std::hypot(expected_dx, expected_dy);
    const double output_magnitude = vector_magnitude(raw_x, raw_y);

    const bool manual_opposes_plan =
        plan.has_output &&
        manual_magnitude >= kManualYieldMagnitude &&
        vector_alignment(
            manual_x,
            manual_y,
            plan.output_x,
            plan.output_y,
            kVectorDeadzone) < -0.15;
    const bool manual_fights_raw =
        manual_magnitude >= kManualYieldMagnitude &&
        vector_alignment(manual_x, manual_y, raw_x, raw_y, kVectorDeadzone) <
            kManualYieldAlignment;
    const bool manual_is_correcting = manual_opposes_plan || manual_fights_raw;
    const bool target_jump =
        plan.has_expected &&
        std::hypot(expected_dx - plan.expected_dx, expected_dy - plan.expected_dy) >
            kTargetJumpResetPx;
    if (manual_is_correcting || target_jump || error_radius > kFarErrorResetPx ||
        output_magnitude < kVectorDeadzone) {
        reset_benchmark_short_plan(
            plan,
            raw_assist_x,
            raw_assist_y,
            expected_dx,
            expected_dy);
        return;
    }

    const auto signum_double = [](double value) {
        if (value > 0.0) {
            return 1.0;
        }
        if (value < 0.0) {
            return -1.0;
        }
        return 0.0;
    };
    const auto planned_axis = [&](double error, double raw, double manual) {
        const double abs_error = std::fabs(error);
        if (abs_error > kBrakeFarErrorPx) {
            return raw;
        }
        const double error_sign = signum_double(error);
        const double raw_sign = signum_double(raw);
        if (error_sign == 0.0 || raw_sign != error_sign ||
            std::fabs(raw) < kVectorDeadzone) {
            return raw;
        }
        const double horizon_seconds =
            abs_error <= kBrakeNearErrorPx ? kNearHorizonSeconds : kFarHorizonSeconds;
        const double allowed_abs =
            (abs_error * kCrossingFraction) /
            std::max(reticle_speed * horizon_seconds, 1.0);
        if (std::fabs(raw) <= allowed_abs) {
            return raw;
        }
        const double raw_assist = raw - manual;
        const double desired = error_sign * allowed_abs;
        double planned_assist = desired - manual;
        if (signum_double(planned_assist) != signum_double(raw_assist)) {
            planned_assist = 0.0;
        } else if (std::fabs(planned_assist) > std::fabs(raw_assist)) {
            planned_assist = raw_assist;
        }
        return manual + planned_assist;
    };

    const double shaped_x = planned_axis(expected_dx, raw_x, manual_x);
    const double shaped_y = planned_axis(expected_dy, raw_y, manual_y);

    ++plan.shaped_ticks;
    plan.has_output = true;
    plan.output_x = shaped_x - manual_x;
    plan.output_y = shaped_y - manual_y;
    plan.has_expected = true;
    plan.expected_dx = expected_dx;
    plan.expected_dy = expected_dy;
    components.final_stick.x =
        clamp_float(static_cast<float>(shaped_x), -1.0f, 1.0f);
    components.final_stick.y =
        clamp_float(static_cast<float>(-shaped_y), -1.0f, 1.0f);
}

double vector_alignment(
    double ax,
    double ay,
    double bx,
    double by,
    double min_magnitude) {
    const double a_mag = vector_magnitude(ax, ay);
    const double b_mag = vector_magnitude(bx, by);
    if (a_mag < min_magnitude || b_mag < min_magnitude) {
        return 0.0;
    }
    return std::max(-1.0, std::min(1.0, ((ax * bx) + (ay * by)) / (a_mag * b_mag)));
}

double vector_turn_degrees(
    double previous_x,
    double previous_y,
    double current_x,
    double current_y,
    double min_magnitude) {
    const double alignment = vector_alignment(
        previous_x,
        previous_y,
        current_x,
        current_y,
        min_magnitude);
    return std::acos(std::max(-1.0, std::min(1.0, alignment))) * (180.0 / 3.14159265358979323846);
}

double alignment_score(double mean_alignment) {
    return std::max(0.0, std::min(100.0, 50.0 * (mean_alignment + 1.0)));
}

double turn_smoothness_score(double p95_turn_degrees) {
    return std::max(0.0, std::min(100.0, 100.0 * (1.0 - (p95_turn_degrees / 180.0))));
}

int signum(double value) {
    if (value > 0.0) {
        return 1;
    }
    if (value < 0.0) {
        return -1;
    }
    return 0;
}

struct OvershootStats {
    int count = 0;
    double max_px = 0.0;
};

struct ModeOvershootStats {
    int count = 0;
    int ads_snap_count = 0;
    int body_lock_count = 0;
    int manual_count = 0;
    double max_px = 0.0;
};

OvershootStats axis_overshoot_stats(const std::vector<double>& errors, double threshold_px) {
    OvershootStats stats;
    if (errors.size() < 2) {
        return stats;
    }

    const auto finish_episode = [&](bool active, double peak_abs) {
        if (active && peak_abs > threshold_px) {
            ++stats.count;
            stats.max_px = std::max(stats.max_px, peak_abs);
        }
    };

    bool active = false;
    int active_side = 0;
    double peak_abs = 0.0;
    int previous_sign = signum(errors.front());
    for (std::size_t index = 1; index < errors.size(); ++index) {
        const double current = errors[index];
        const int current_sign = signum(current);
        if (current_sign == 0) {
            previous_sign = current_sign;
            continue;
        }

        const bool crossed =
            previous_sign != 0 && current_sign != previous_sign;
        if (crossed) {
            finish_episode(active, peak_abs);
            active = true;
            active_side = current_sign;
            peak_abs = std::fabs(current);
        } else if (active && current_sign == active_side) {
            peak_abs = std::max(peak_abs, std::fabs(current));
        } else if (active && current_sign != active_side) {
            finish_episode(active, peak_abs);
            active = false;
            active_side = 0;
            peak_abs = 0.0;
        }
        previous_sign = current_sign;
    }
    finish_episode(active, peak_abs);
    return stats;
}

void count_overshoot_mode(ModeOvershootStats& stats, const std::string& mode) {
    if (mode == "ads_snap") {
        ++stats.ads_snap_count;
    } else if (mode == "body_lock") {
        ++stats.body_lock_count;
    } else {
        ++stats.manual_count;
    }
}

ModeOvershootStats axis_mode_overshoot_stats(
    const std::vector<double>& errors,
    const std::vector<std::string>& modes,
    double threshold_px) {
    ModeOvershootStats stats;
    if (errors.size() < 2 || errors.size() != modes.size()) {
        return stats;
    }

    const auto finish_episode = [&](bool active, double peak_abs, const std::string& peak_mode) {
        if (active && peak_abs > threshold_px) {
            ++stats.count;
            stats.max_px = std::max(stats.max_px, peak_abs);
            count_overshoot_mode(stats, peak_mode);
        }
    };

    bool active = false;
    int active_side = 0;
    double peak_abs = 0.0;
    std::string peak_mode = "manual";
    int previous_sign = signum(errors.front());
    for (std::size_t index = 1; index < errors.size(); ++index) {
        const double current = errors[index];
        const int current_sign = signum(current);
        if (current_sign == 0) {
            previous_sign = current_sign;
            continue;
        }

        const bool crossed =
            previous_sign != 0 && current_sign != previous_sign;
        if (crossed) {
            finish_episode(active, peak_abs, peak_mode);
            active = true;
            active_side = current_sign;
            peak_abs = std::fabs(current);
            peak_mode = modes[index];
        } else if (active && current_sign == active_side) {
            const double current_abs = std::fabs(current);
            if (current_abs > peak_abs) {
                peak_abs = current_abs;
                peak_mode = modes[index];
            }
        } else if (active && current_sign != active_side) {
            finish_episode(active, peak_abs, peak_mode);
            active = false;
            active_side = 0;
            peak_abs = 0.0;
            peak_mode = "manual";
        }
        previous_sign = current_sign;
    }
    finish_episode(active, peak_abs, peak_mode);
    return stats;
}

std::vector<RandomFovOvershootEvent> random_fov_axis_overshoot_events(
    const std::vector<RandomFovSample>& samples,
    bool y_axis,
    double threshold_px) {
    std::vector<RandomFovOvershootEvent> events;
    if (samples.size() < 2) {
        return events;
    }

    const auto error_for_sample = [y_axis](const RandomFovSample& sample) {
        return y_axis ? sample.error_y : sample.error_x;
    };
    const auto make_event = [&](double previous_error, const RandomFovSample& crossing_sample) {
        RandomFovOvershootEvent event;
        event.axis = y_axis ? "y" : "x";
        event.segment = crossing_sample.segment;
        event.crossing_tick = crossing_sample.tick;
        event.peak_tick = crossing_sample.tick;
        event.crossing_global_tick = crossing_sample.global_tick;
        event.peak_global_tick = crossing_sample.global_tick;
        event.peak_mode = crossing_sample.mode;
        event.previous_error = previous_error;
        event.crossing_error = error_for_sample(crossing_sample);
        event.peak_error = event.crossing_error;
        event.peak_abs_px = std::fabs(event.crossing_error);
        event.final_x = crossing_sample.final_x;
        event.final_y = crossing_sample.final_y;
        event.manual_x = crossing_sample.manual_x;
        event.manual_y = crossing_sample.manual_y;
        event.ai_aim_x = crossing_sample.ai_aim_x;
        event.ai_aim_y = crossing_sample.ai_aim_y;
        event.dynamics_x = crossing_sample.dynamics_x;
        event.dynamics_y = crossing_sample.dynamics_y;
        event.fov_scale = crossing_sample.fov_scale;
        event.expected_dx = crossing_sample.expected_dx;
        event.expected_dy = crossing_sample.expected_dy;
        event.vision_has_target = crossing_sample.vision_has_target;
        event.vision_aim_authority = crossing_sample.vision_aim_authority;
        event.vision_fire_authority = crossing_sample.vision_fire_authority;
        event.vision_target_tier = crossing_sample.vision_target_tier;
        event.vision_dx = crossing_sample.vision_dx;
        event.vision_dy = crossing_sample.vision_dy;
        event.vision_has_tracker_projection = crossing_sample.vision_has_tracker_projection;
        event.vision_tracker_dx = crossing_sample.vision_tracker_dx;
        event.vision_tracker_dy = crossing_sample.vision_tracker_dy;
        event.vision_age_ms = crossing_sample.vision_age_ms;
        event.target_speed_px_per_sec = crossing_sample.target_speed_px_per_sec;
        event.heading_deg = crossing_sample.heading_deg;
        return event;
    };
    const auto apply_peak = [&](RandomFovOvershootEvent& event, const RandomFovSample& sample) {
        const double error = error_for_sample(sample);
        event.peak_tick = sample.tick;
        event.peak_global_tick = sample.global_tick;
        event.peak_mode = sample.mode;
        event.peak_error = error;
        event.peak_abs_px = std::fabs(error);
        event.final_x = sample.final_x;
        event.final_y = sample.final_y;
        event.manual_x = sample.manual_x;
        event.manual_y = sample.manual_y;
        event.ai_aim_x = sample.ai_aim_x;
        event.ai_aim_y = sample.ai_aim_y;
        event.dynamics_x = sample.dynamics_x;
        event.dynamics_y = sample.dynamics_y;
        event.fov_scale = sample.fov_scale;
        event.expected_dx = sample.expected_dx;
        event.expected_dy = sample.expected_dy;
        event.vision_has_target = sample.vision_has_target;
        event.vision_aim_authority = sample.vision_aim_authority;
        event.vision_fire_authority = sample.vision_fire_authority;
        event.vision_target_tier = sample.vision_target_tier;
        event.vision_dx = sample.vision_dx;
        event.vision_dy = sample.vision_dy;
        event.vision_has_tracker_projection = sample.vision_has_tracker_projection;
        event.vision_tracker_dx = sample.vision_tracker_dx;
        event.vision_tracker_dy = sample.vision_tracker_dy;
        event.vision_age_ms = sample.vision_age_ms;
        event.target_speed_px_per_sec = sample.target_speed_px_per_sec;
        event.heading_deg = sample.heading_deg;
    };
    const auto finish_episode = [&](bool active, const RandomFovOvershootEvent& event) {
        if (active && event.peak_abs_px > threshold_px) {
            events.push_back(event);
        }
    };

    bool active = false;
    int active_side = 0;
    RandomFovOvershootEvent active_event;
    double previous_error = error_for_sample(samples.front());
    int previous_sign = signum(previous_error);
    for (std::size_t index = 1; index < samples.size(); ++index) {
        const double current = error_for_sample(samples[index]);
        const int current_sign = signum(current);
        if (current_sign == 0) {
            previous_error = current;
            previous_sign = current_sign;
            continue;
        }

        const bool crossed =
            previous_sign != 0 && current_sign != previous_sign;
        if (crossed) {
            finish_episode(active, active_event);
            active = true;
            active_side = current_sign;
            active_event = make_event(previous_error, samples[index]);
        } else if (active && current_sign == active_side) {
            const double current_abs = std::fabs(current);
            if (current_abs > active_event.peak_abs_px) {
                apply_peak(active_event, samples[index]);
            }
        } else if (active && current_sign != active_side) {
            finish_episode(active, active_event);
            active = false;
            active_side = 0;
            active_event = RandomFovOvershootEvent{};
        }
        previous_error = current;
        previous_sign = current_sign;
    }
    finish_episode(active, active_event);
    return events;
}

int axis_overshoot_count(const std::vector<double>& errors, double threshold_px) {
    return axis_overshoot_stats(errors, threshold_px).count;
}

double axis_max_overshoot(const std::vector<double>& errors, double threshold_px) {
    return axis_overshoot_stats(errors, threshold_px).max_px;
}

void require_benchmark_check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_near(double actual, double expected, double tolerance, const char* message) {
    if (std::fabs(actual - expected) > tolerance) {
        std::ostringstream out;
        out << message << ": expected " << expected << " got " << actual;
        throw std::runtime_error(out.str());
    }
}

std::vector<ScenarioMetrics> run_selector_intent_suite(unsigned int seed);
std::vector<ScenarioMetrics> run_roi_fallback_suite(unsigned int seed);
ScenarioMetrics run_tracker_random_fov_100hz(
    controller_native::GamepadRuntimeConfig config,
    const CliOptions& options,
    const std::string& name,
    bool enable_dynamics,
    bool enable_short_plan,
    bool pure_ads);
ScenarioMetrics run_ads_diagonal_manual_stress_100hz(
    controller_native::GamepadRuntimeConfig config,
    const std::string& name = "ads_diagonal_manual_stress_100hz",
    bool enable_dynamics = false,
    bool fire_active = false,
    bool simulate_late_fov_occlusion = false,
    bool fresh_timestamp_for_late_position = false,
    bool simulate_err_targets = false,
    unsigned int err_target_seed = 1337);
ScenarioMetrics run_ads_bodylock_moving_chase_100hz(
    controller_native::GamepadRuntimeConfig config,
    unsigned int seed,
    const std::string& name,
    bool enable_dynamics,
    bool fire_active,
    BodylockChaseMotionProfile motion_profile,
    bool occlusion_gap);
ScenarioMetrics run_ads_manual_carry_through_100hz(
    controller_native::GamepadRuntimeConfig config,
    const std::string& name = "ads_manual_carry_through_100hz");
ScenarioMetrics run_ads_bodylock_near_high_output_100hz(
    controller_native::GamepadRuntimeConfig config,
    const std::string& name = "ads_bodylock_near_high_output_100hz");
ScenarioMetrics run_adversarial_controller_authority_100hz(
    controller_native::GamepadRuntimeConfig config,
    const std::string& name = "adversarial_controller_authority_100hz");

void run_self_test() {
    const char* selector_suite_argv[] = {
        "cod_native_gamepad_benchmark",
        "--suite",
        "selector_intent",
        "--selector-intent-seed",
        "2026",
    };
    const CliOptions selector_suite_options = parse_args(
        static_cast<int>(std::size(selector_suite_argv)),
        const_cast<char**>(selector_suite_argv));
    require_benchmark_check(
        selector_suite_options.suite == "selector_intent",
        "CLI should parse selector_intent suite");
    require_benchmark_check(
        selector_suite_options.selector_intent_seed == 2026,
        "CLI should parse selector intent seed");
    const std::vector<ScenarioMetrics> selector_suite =
        run_selector_intent_suite(selector_suite_options.selector_intent_seed);
    require_benchmark_check(
        selector_suite.size() == 3,
        "selector_intent suite should produce three benchmark scenarios");
    require_benchmark_check(
        selector_suite.front().has_selector_intent,
        "selector_intent scenarios should expose selector metrics");
    require_benchmark_check(
        selector_suite.front().selector_intent_seed == 2026,
        "selector_intent scenarios should preserve the requested seed");
    require_benchmark_check(
        selector_suite.front().selector_intent_wrong_target_frames == 0,
        "two-target selector intent benchmark should not choose the wrong plausible target");
    require_benchmark_check(
        selector_suite[1].selector_intent_ping_pong_switches == 0,
        "crossing selector intent benchmark should not ping-pong targets");
    require_benchmark_check(
        selector_suite[2].selector_intent_fire_authority_leaks == 0,
        "weak selector intent benchmark must not leak fire authority");

    const char* roi_suite_argv[] = {
        "cod_native_gamepad_benchmark",
        "--suite",
        "roi_fallback",
        "--roi-fallback-seed",
        "2026",
    };
    const CliOptions roi_suite_options = parse_args(
        static_cast<int>(std::size(roi_suite_argv)),
        const_cast<char**>(roi_suite_argv));
    require_benchmark_check(
        roi_suite_options.suite == "roi_fallback",
        "CLI should parse roi_fallback suite");
    require_benchmark_check(
        roi_suite_options.roi_fallback_seed == 2026,
        "CLI should parse ROI fallback seed");
    const std::vector<ScenarioMetrics> roi_suite =
        run_roi_fallback_suite(roi_suite_options.roi_fallback_seed);
    require_benchmark_check(
        roi_suite.size() == 3,
        "roi_fallback suite should produce three benchmark scenarios");
    require_benchmark_check(
        roi_suite.front().has_roi_fallback,
        "roi_fallback scenarios should expose ROI metrics");
    require_benchmark_check(
        roi_suite.front().roi_fallback_partial_regions > 0,
        "partial color frame ROI benchmark should use partial regions");
    require_benchmark_check(
        roi_suite[1].roi_fallback_external_cue_no_full_frame_frames > 0,
        "external cue ROI benchmark should avoid full color frame requests");
    require_benchmark_check(
        roi_suite[2].roi_fallback_edge_clamped_regions > 0,
        "edge ROI benchmark should report clamped regions");
    for (const ScenarioMetrics& scenario : roi_suite) {
        require_benchmark_check(
            scenario.roi_fallback_target_loss_frames == 0,
            "ROI fallback benchmark should not lose target authority from ROI-only misses");
    }

    controller_native::GamepadRuntimeConfig pure_ads_config;
    pure_ads_config.recoil.enabled = false;
    pure_ads_config.aim_assist_dynamics.enabled = false;
    CliOptions pure_ads_options;
    pure_ads_options.random_fov_ticks = 960;
    pure_ads_options.random_fov_seed = 20260703;
    const ScenarioMetrics pure_ads_metrics = run_tracker_random_fov_100hz(
        pure_ads_config,
        pure_ads_options,
        "ads_pure_random_fov_100hz_self_test",
        false,
        false,
        true);
    require_benchmark_check(
        pure_ads_metrics.has_random_fov &&
            pure_ads_metrics.random_fov_manual_direction_samples == 0,
        "pure ADS random FOV benchmark should not include manual stick direction samples");

    controller_native::GamepadRuntimeConfig moving_config;
    moving_config.recoil.enabled = false;
    const ScenarioMetrics moving_chase = run_ads_bodylock_moving_chase_100hz(
        moving_config,
        20260704,
        "ads_bodylock_moving_chase_100hz_self_test",
        true,
        false,
        BodylockChaseMotionProfile::Smooth,
        false);
    require_benchmark_check(
        moving_chase.has_ads_manual_stress &&
            moving_chase.ads_manual_stress_body_lock_frames > 0,
        "moving chase benchmark should exercise body-lock frames");
    require_benchmark_check(
        moving_chase.ads_manual_stress_vision_samples > 0 &&
            moving_chase.ads_manual_stress_measured_ticks > 0,
        "moving chase benchmark should produce vision and measured ticks");
    require_benchmark_check(
        moving_chase.ads_manual_stress_body_lock_tracking_samples > 0 &&
            moving_chase.ads_manual_stress_body_lock_mean_error_px > 0.0 &&
            moving_chase.ads_manual_stress_body_lock_p95_error_px > 0.0,
        "moving chase benchmark should report body-lock-only tracking error metrics");
    require_benchmark_check(
        moving_chase.ads_manual_stress_body_lock_direction_score >= 50.0,
        "moving chase benchmark should report body-lock output direction quality");
    require_benchmark_check(
        moving_chase.ads_manual_stress_body_lock_turn_samples > 0 &&
            moving_chase.ads_manual_stress_body_lock_turn_smoothness_score > 0.0,
        "moving chase benchmark should report body-lock-only smoothness metrics");
    require_benchmark_check(
        moving_chase.ads_manual_stress_body_lock_sustain_cases > 0 &&
            moving_chase.ads_manual_stress_body_lock_sustain_required_ms > 0.0 &&
            moving_chase.ads_manual_stress_body_lock_sustain_longest_ms > 0.0,
        "moving chase benchmark should report sustained body-lock tracking metrics");
    require_benchmark_check(
        moving_chase.ads_manual_stress_body_lock_close_assist_samples > 0 &&
            moving_chase.ads_manual_stress_body_lock_close_assist_mean_ai_output > 0.0,
        "moving chase benchmark should report body-lock close-assist strength");
    require_benchmark_check(
        moving_chase.ads_manual_stress_body_lock_dropout_frames >= 0 &&
            moving_chase.ads_manual_stress_body_lock_centered_samples >= 0,
        "moving chase benchmark should report body-lock dropout and centered jitter counters");

    const ScenarioMetrics slide_occluded = run_ads_bodylock_moving_chase_100hz(
        moving_config,
        20260704,
        "ads_bodylock_slide_occlusion_chase_100hz_self_test",
        true,
        true,
        BodylockChaseMotionProfile::Slide,
        true);
    require_benchmark_check(
        slide_occluded.ads_manual_stress_vision_dropped_ticks > 0,
        "slide occlusion benchmark should include no-submit vision ticks");
    require_benchmark_check(
        slide_occluded.ads_manual_stress_slide_down_lag_p95_px > 0.0,
        "slide benchmark should measure downward tracking lag");
    require_benchmark_check(
        slide_occluded.ads_manual_stress_occlusion_peak_error_px > 0.0,
        "slide occlusion benchmark should measure peak occlusion error");

    const ScenarioMetrics arc_jump = run_ads_bodylock_moving_chase_100hz(
        moving_config,
        20260704,
        "ads_bodylock_arc_jump_chase_100hz_self_test",
        true,
        false,
        BodylockChaseMotionProfile::ArcJump,
        false);
    require_benchmark_check(
        arc_jump.has_ads_manual_stress &&
            arc_jump.ads_manual_stress_body_lock_frames > 0 &&
            arc_jump.ads_manual_stress_measured_ticks > 0,
        "arc jump benchmark should exercise body-lock movement samples");

    const ScenarioMetrics near_high = run_ads_bodylock_near_high_output_100hz(
        moving_config,
        "ads_bodylock_near_high_output_100hz_self_test");
    require_benchmark_check(
        near_high.has_ads_bodylock_near_high &&
            near_high.ads_bodylock_near_high_near_target_frames > 0 &&
            near_high.ads_bodylock_near_high_body_lock_frames > 0,
        "near-high output benchmark should exercise body-lock near-target frames");
    require_benchmark_check(
        near_high.ads_bodylock_near_high_output_frames > 0 &&
            near_high.ads_bodylock_near_high_brake_inactive_frames > 0,
        "near-high output benchmark should reproduce high output while brake is inactive");
    require_benchmark_check(
        near_high.ads_bodylock_near_high_max_final_output >= 0.45 &&
            near_high.ads_bodylock_near_high_p95_output_delta >= 0.0,
        "near-high output benchmark should report high-output and smoothness metrics");
    require_benchmark_check(
        near_high.ads_bodylock_near_high_close_assist_frames > 0 &&
            near_high.ads_bodylock_near_high_close_assist_mean_output > 0.0,
        "near-high output benchmark should report close-assist strength instead of treating all near-high output as bad");
    require_benchmark_check(
        near_high.ads_bodylock_near_high_centered_frames >= 0 &&
            near_high.ads_bodylock_near_high_centered_jitter_frames >= 0 &&
            near_high.ads_bodylock_near_high_low_output_close_frames >= 0,
        "near-high output benchmark should report centered jitter and close-range dropout counters");

    const ScenarioMetrics carry_through = run_ads_manual_carry_through_100hz(
        moving_config,
        "ads_manual_carry_through_100hz_self_test");
    require_benchmark_check(
        carry_through.has_ads_carry_through &&
            carry_through.ads_carry_through_same_direction_accel_frames > 0,
        "ADS carry-through benchmark should reproduce AI and manual same-direction acceleration");
    require_benchmark_check(
        carry_through.ads_carry_through_sign_flip_events > 0 &&
            carry_through.ads_carry_through_max_overshoot_px <= 25.0,
        "ADS carry-through benchmark should keep overshoot bounded after crossing");
    require_benchmark_check(
        carry_through.ads_carry_through_near_high_output_frames > 0 &&
            carry_through.ads_carry_through_brake_active_frames > 0 &&
            carry_through.ads_carry_through_brake_inactive_near_high_frames <
                carry_through.ads_carry_through_near_high_output_frames,
        "ADS carry-through benchmark should apply carry brake before high-output near-target frames dominate");
    require_benchmark_check(
        carry_through.ads_carry_through_body_lock_frames +
                carry_through.ads_carry_through_manual_frames >
            0,
        "ADS carry-through benchmark should exercise post-ads-snap authority modes");

    const ScenarioMetrics adversarial = run_adversarial_controller_authority_100hz(
        moving_config,
        "adversarial_controller_authority_100hz_self_test");
    require_benchmark_check(
        adversarial.has_adversarial_controller &&
            adversarial.adversarial_controller_ticks > 0,
        "adversarial controller benchmark should report controller stress metrics");
    require_benchmark_check(
        adversarial.adversarial_controller_wrong_target_frames > 0 &&
            adversarial.adversarial_controller_user_fight_frames > 0,
        "adversarial controller benchmark should expose wrong-target user fight");
    require_benchmark_check(
        adversarial.adversarial_controller_invalid_strong_frames > 0 &&
            adversarial.adversarial_controller_stale_high_output_frames > 0,
        "adversarial controller benchmark should expose invalid/stale high authority");
    require_benchmark_check(
        adversarial.adversarial_controller_err_target_frames > 0 &&
            adversarial.adversarial_controller_recovery_frames > 0,
        "adversarial controller benchmark should expose err target and recovery windows");

    const ScenarioMetrics err_late_ads = run_ads_diagonal_manual_stress_100hz(
        moving_config,
        "ads_diagonal_err_target_late_position_fov_occlusion_50hz_dynamic_fire_self_test",
        true,
        true,
        true,
        true,
        true,
        20260704);
    require_benchmark_check(
        err_late_ads.ads_manual_stress_unreliable_high_output_frames > 0 &&
            err_late_ads.ads_manual_stress_unreliable_max_final_output > 0.0,
        "ADS stress benchmark should report high output under unreliable acquisition evidence");
    require_benchmark_check(
        err_late_ads.ads_manual_stress_unreliable_same_direction_frames > 0 &&
            err_late_ads.ads_manual_stress_unreliable_fight_frames >= 0,
        "ADS stress benchmark should report manual/AI stacking and fight during unreliable acquisition");

    const std::vector<double> crossed_then_deepened = {6.0, 2.5, -1.0, -4.0};
    require_benchmark_check(
        axis_overshoot_count(crossed_then_deepened, 2.0) == 1,
        "overshoot count should include crossings that deepen after the crossing sample");
    require_near(
        axis_max_overshoot(crossed_then_deepened, 2.0),
        4.0,
        0.001,
        "max overshoot should include the deepest same-side excursion after crossing");

    const std::vector<double> noise_crossing = {4.0, -1.0, 0.5, -0.75};
    require_benchmark_check(
        axis_overshoot_count(noise_crossing, 2.0) == 0,
        "overshoot count should ignore crossings that stay inside the threshold");
    require_near(
        axis_max_overshoot(noise_crossing, 2.0),
        0.0,
        0.001,
        "max overshoot should ignore threshold-sized noise");
    std::vector<RandomFovSample> diagnostic_samples;
    diagnostic_samples.push_back(RandomFovSample{});
    diagnostic_samples.back().error_x = 6.0;
    diagnostic_samples.back().mode = "body_lock";
    diagnostic_samples.push_back(RandomFovSample{});
    diagnostic_samples.back().error_x = -1.0;
    diagnostic_samples.back().tick = 1;
    diagnostic_samples.back().global_tick = 1;
    diagnostic_samples.back().mode = "body_lock";
    diagnostic_samples.push_back(RandomFovSample{});
    diagnostic_samples.back().error_x = -4.0;
    diagnostic_samples.back().tick = 2;
    diagnostic_samples.back().global_tick = 2;
    diagnostic_samples.back().mode = "body_lock";
    diagnostic_samples.back().final_x = -0.10;
    const std::vector<RandomFovOvershootEvent> diagnostic_events =
        random_fov_axis_overshoot_events(diagnostic_samples, false, 2.0);
    require_benchmark_check(
        diagnostic_events.size() == 1,
        "overshoot detail should use the same episode threshold as overshoot count");
    require_near(
        diagnostic_events.front().peak_abs_px,
        4.0,
        0.001,
        "overshoot detail should report the deepest same-side peak");
    require_near(
        diagnostic_events.front().final_x,
        -0.10,
        0.001,
        "overshoot detail should preserve component context at the peak sample");

    require_near(
        vector_alignment(1.0, 0.0, 2.0, 0.0, 0.001),
        1.0,
        0.001,
        "vector alignment should score same-direction vectors as 1");
    require_near(
        vector_alignment(1.0, 0.0, -2.0, 0.0, 0.001),
        -1.0,
        0.001,
        "vector alignment should score opposite-direction vectors as -1");
    require_near(
        vector_turn_degrees(1.0, 0.0, 0.0, 1.0, 0.001),
        90.0,
        0.001,
        "turn angle should report right-angle direction changes");
    require_near(
        alignment_score(1.0),
        100.0,
        0.001,
        "alignment score should map perfect alignment to 100");
    require_near(
        turn_smoothness_score(90.0),
        50.0,
        0.001,
        "turn smoothness score should treat right-angle changes as mid quality");

    BenchmarkShortTermOutputPlan plan;
    controller_native::NativeControllerOutputComponents planned_components;
    planned_components.final_stick.x = 0.60f;
    planned_components.final_stick.y = 0.0f;
    apply_benchmark_short_plan(plan, planned_components, 6.0, 0.0, 1200.0);
    require_benchmark_check(
        planned_components.final_stick.x > 0.0f &&
            planned_components.final_stick.x < 0.30f,
        "benchmark short plan should brake near-target AI output before crossing");

    BenchmarkShortTermOutputPlan manual_yield_plan;
    controller_native::NativeControllerOutputComponents manual_yield_components;
    manual_yield_components.final_stick.x = 0.50f;
    manual_yield_components.final_stick.y = 0.0f;
    manual_yield_components.manual_stick.x = -0.30f;
    apply_benchmark_short_plan(
        manual_yield_plan,
        manual_yield_components,
        18.0,
        0.0,
        1200.0);
    require_near(
        manual_yield_components.final_stick.x,
        0.50,
        0.001,
        "benchmark short plan should yield immediately to manual correction");

    const std::vector<std::string> mode_sequence = {
        "ads_snap",
        "ads_snap",
        "body_lock",
        "body_lock",
    };
    const ModeOvershootStats mode_overshoot =
        axis_mode_overshoot_stats(crossed_then_deepened, mode_sequence, 2.0);
    require_benchmark_check(
        mode_overshoot.count == 1 && mode_overshoot.body_lock_count == 1,
        "mode overshoot count should attribute the episode to the peak excursion mode");
    require_near(
        ads_lag_stress_fov_scale_for_tick(0),
        1.0,
        0.001,
        "ADS lag stress should start at full FOV scale");
    require_near(
        ads_lag_stress_fov_scale_for_tick(135),
        0.72,
        0.001,
        "ADS lag stress should settle at the narrowed FOV scale");
    require_benchmark_check(
        !ads_lag_stress_drops_vision_tick(139),
        "ADS lag stress should keep vision before the first occlusion window");
    require_benchmark_check(
        ads_lag_stress_drops_vision_tick(145),
        "ADS lag stress should drop vision during the first occlusion window");
    require_benchmark_check(
        !ads_lag_stress_drops_vision_tick(190),
        "ADS lag stress should resume vision after the first occlusion window");
    const AdsErrTargetWindow diagnostic_err_window{30, 45, 70.0, -55.0};
    require_benchmark_check(
        ads_err_target_window_active(diagnostic_err_window, 30),
        "err-target window should be active on its first tick");
    require_benchmark_check(
        !ads_err_target_window_active(diagnostic_err_window, 45),
        "err-target window should be inactive on its end tick");
    require_near(
        ads_err_target_window_offset_radius(diagnostic_err_window),
        std::hypot(70.0, -55.0),
        0.001,
        "err-target window should report its offset radius");

    std::cout << "[NativeGamepadBenchmark] self-test PASS\n";
}

vision_native::Detection selector_intent_detection(
    float target_x,
    float target_y,
    float conf) {
    constexpr float width = 60.0f;
    constexpr float height = 140.0f;
    vision_native::Detection detection;
    detection.x1 = target_x - (width * 0.5f);
    detection.x2 = target_x + (width * 0.5f);
    detection.y1 = target_y - (height * 0.40f);
    detection.y2 = detection.y1 + height;
    detection.conf = conf;
    return detection;
}

vision_native::DetectionBatch selector_intent_batch(
    std::uint64_t frame_id,
    std::initializer_list<vision_native::Detection> detections) {
    vision_native::DetectionBatch batch;
    batch.frame_id = frame_id;
    batch.captured_at_ns = frame_id * 10'000'000ull;
    batch.inferred_at_ns = batch.captured_at_ns + 1'000'000ull;
    batch.frame_width = 640;
    batch.frame_height = 512;
    batch.detections.insert(batch.detections.end(), detections.begin(), detections.end());
    return batch;
}

pipeline_contract::UserAimIntent selector_intent_direction(
    std::uint64_t intent_id,
    float x,
    float y,
    float strength = 1.0f) {
    pipeline_contract::UserAimIntent intent;
    intent.valid = true;
    intent.intent_id = intent_id;
    intent.strength = strength;
    intent.has_direction = true;
    intent.direction.x = x;
    intent.direction.y = y;
    intent.aiming = true;
    return intent;
}

int selector_intent_bucket(const controller_native::ControllerVisionSnapshot& snapshot) {
    if (!snapshot.frame_updated || !snapshot.state.has_target) {
        return 0;
    }
    return snapshot.state.target_x < snapshot.state.screen_center_x ? -1 : 1;
}

void selector_intent_apply_frame_metadata(
    vision_native::VisionResult& result,
    const vision_native::DetectionBatch& batch) {
    result.frame_updated = true;
    result.frame_id = batch.frame_id;
    result.captured_at_ns = batch.captured_at_ns;
    result.inferred_at_ns = batch.inferred_at_ns;
    result.result_at_ns = batch.inferred_at_ns + 1'000'000ull;
}

void selector_intent_record_decision(
    ScenarioMetrics& metrics,
    const vision_native::VisionResult& result,
    const controller_native::ControllerVisionSnapshot& snapshot) {
    ++metrics.selector_intent_ticks;
    if (snapshot.frame_updated && snapshot.state.has_target) {
        ++metrics.selector_intent_target_frames;
    }
    if (result.intent_applied) {
        ++metrics.selector_intent_intent_applied_frames;
    }
    const char* decision = result.intent_decision == nullptr ? "none" : result.intent_decision;
    if (std::strcmp(decision, "ignored_active_lock") == 0) {
        ++metrics.selector_intent_ignored_active_lock_frames;
    } else if (std::strcmp(decision, "delayed_switch_confirm") == 0) {
        ++metrics.selector_intent_delayed_switch_confirm_frames;
    }
}

void selector_intent_finish_rates(ScenarioMetrics& metrics) {
    metrics.frames = metrics.selector_intent_ticks;
    metrics.selector_intent_wrong_target_rate = rate(
        metrics.selector_intent_wrong_target_frames,
        metrics.selector_intent_target_frames);
}

ScenarioMetrics run_selector_intent_two_targets_manual_sweep_100hz(unsigned int seed) {
    constexpr int kTicks = 240;
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> jitter(-1.5f, 1.5f);
    vision_native::VisionTargetSelector selector(640, 512);
    ScenarioMetrics metrics;
    metrics.name = "selector_intent_two_targets_manual_sweep_100hz";
    metrics.has_selector_intent = true;
    metrics.selector_intent_seed = static_cast<int>(seed);

    int previous_bucket = 0;
    int bucket_before_previous = 0;
    for (int tick = 0; tick < kTicks; ++tick) {
        const float left_x =
            260.0f + (std::sin(static_cast<float>(tick) * 0.031f) * 3.0f) + jitter(rng);
        const float right_x =
            380.0f + (std::cos(static_cast<float>(tick) * 0.027f) * 3.0f) + jitter(rng);
        const vision_native::DetectionBatch batch = selector_intent_batch(
            static_cast<std::uint64_t>(tick + 1),
            {
                selector_intent_detection(left_x, 256.0f, 0.92f),
                selector_intent_detection(right_x, 256.0f, 0.92f),
            });
        vision_native::VisionResult result = selector.select(
            batch,
            selector_intent_direction(static_cast<std::uint64_t>(seed) * 1000ull + tick, 1.0f, 0.0f));
        selector_intent_apply_frame_metadata(result, batch);
        const controller_native::ControllerVisionSnapshot snapshot =
            runtime_app::adapt_vision_result(result);
        selector_intent_record_decision(metrics, result, snapshot);

        const int bucket = selector_intent_bucket(snapshot);
        if (tick >= 1 && bucket != 0 && bucket != 1) {
            ++metrics.selector_intent_wrong_target_frames;
        }
        if (bucket != 0 && previous_bucket != 0 && bucket != previous_bucket) {
            ++metrics.selector_intent_switches;
            if (bucket == bucket_before_previous) {
                ++metrics.selector_intent_ping_pong_switches;
            }
        }
        if (bucket != 0) {
            bucket_before_previous = previous_bucket;
            previous_bucket = bucket;
        }
    }

    selector_intent_finish_rates(metrics);
    return metrics;
}

ScenarioMetrics run_selector_intent_crossing_targets_active_lock(unsigned int seed) {
    constexpr int kTicks = 90;
    vision_native::VisionTargetSelector selector(640, 512);
    ScenarioMetrics metrics;
    metrics.name = "selector_intent_crossing_targets_active_lock";
    metrics.has_selector_intent = true;
    metrics.selector_intent_seed = static_cast<int>(seed);

    const vision_native::DetectionBatch lock_batch = selector_intent_batch(
        1,
        {selector_intent_detection(260.0f, 256.0f, 0.92f)});
    selector.select(lock_batch);
    selector.select(lock_batch);

    int previous_bucket = -1;
    int bucket_before_previous = 0;
    for (int tick = 0; tick < kTicks; ++tick) {
        const float challenger_x = 336.0f + std::min(18.0f, static_cast<float>(tick) * 0.20f);
        const vision_native::DetectionBatch batch = selector_intent_batch(
            static_cast<std::uint64_t>(tick + 2),
            {
                selector_intent_detection(260.0f, 256.0f, 0.40f),
                selector_intent_detection(challenger_x, 256.0f, 0.92f),
            });
        vision_native::VisionResult result = selector.select(
            batch,
            selector_intent_direction(static_cast<std::uint64_t>(seed) * 2000ull + tick, 1.0f, 0.0f));
        selector_intent_apply_frame_metadata(result, batch);
        const controller_native::ControllerVisionSnapshot snapshot =
            runtime_app::adapt_vision_result(result);
        selector_intent_record_decision(metrics, result, snapshot);

        const int bucket = selector_intent_bucket(snapshot);
        if (tick >= 2 && bucket != 0 && bucket != 1) {
            ++metrics.selector_intent_wrong_target_frames;
        }
        if (bucket != 0 && previous_bucket != 0 && bucket != previous_bucket) {
            ++metrics.selector_intent_switches;
            if (bucket == bucket_before_previous) {
                ++metrics.selector_intent_ping_pong_switches;
            }
        }
        if (bucket != 0) {
            bucket_before_previous = previous_bucket;
            previous_bucket = bucket;
        }
    }

    selector_intent_finish_rates(metrics);
    return metrics;
}

ScenarioMetrics run_selector_intent_weak_continuation_no_fire(unsigned int seed) {
    constexpr int kTicks = 80;
    vision_native::VisionTargetSelector selector(640, 512);
    ScenarioMetrics metrics;
    metrics.name = "selector_intent_weak_continuation_no_fire";
    metrics.has_selector_intent = true;
    metrics.selector_intent_seed = static_cast<int>(seed);

    const vision_native::DetectionBatch lock_batch = selector_intent_batch(
        1,
        {selector_intent_detection(320.0f, 256.0f, 0.92f)});
    selector.select(lock_batch);
    selector.select(lock_batch);

    for (int tick = 0; tick < kTicks; ++tick) {
        const float weak_x = 320.0f + (std::sin(static_cast<float>(tick) * 0.045f) * 2.0f);
        const vision_native::DetectionBatch batch = selector_intent_batch(
            static_cast<std::uint64_t>(tick + 2),
            {selector_intent_detection(weak_x, 256.0f, 0.30f)});
        vision_native::VisionResult result = selector.select(
            batch,
            selector_intent_direction(static_cast<std::uint64_t>(seed) * 3000ull + tick, 1.0f, 0.0f));
        selector_intent_apply_frame_metadata(result, batch);
        const controller_native::ControllerVisionSnapshot snapshot =
            runtime_app::adapt_vision_result(result);
        selector_intent_record_decision(metrics, result, snapshot);

        if (snapshot.frame_updated && snapshot.state.has_target) {
            if (snapshot.state.fire_authority) {
                ++metrics.selector_intent_fire_authority_leaks;
            } else if (std::strcmp(result.target_source, "associated_weak") == 0) {
                ++metrics.selector_intent_weak_no_fire_frames;
            }
        }
    }

    selector_intent_finish_rates(metrics);
    return metrics;
}

std::vector<ScenarioMetrics> run_selector_intent_suite(unsigned int seed) {
    return {
        run_selector_intent_two_targets_manual_sweep_100hz(seed),
        run_selector_intent_crossing_targets_active_lock(seed),
        run_selector_intent_weak_continuation_no_fire(seed),
    };
}

int roi_area(const vision_native::VisionTargetSelector::FrameRegion& region) {
    return std::max(0, region.right - region.left) *
        std::max(0, region.bottom - region.top);
}

struct RoiColorFrame {
    std::vector<std::uint8_t> pixels;
    vision_native::VisionTargetSelector::ColorFrameView view;
};

RoiColorFrame make_roi_color_frame(
    const vision_native::VisionTargetSelector::FrameRegion& region,
    int frame_width,
    int frame_height,
    bool enemy_colored) {
    RoiColorFrame frame;
    frame.view.width = std::max(0, region.right - region.left);
    frame.view.height = std::max(0, region.bottom - region.top);
    frame.view.origin_x = region.left;
    frame.view.origin_y = region.top;
    frame.view.frame_width = frame_width;
    frame.view.frame_height = frame_height;
    frame.view.row_pitch = frame.view.width * 3;
    frame.view.format = vision_native::PixelFormat::RGB8;
    frame.pixels.assign(
        static_cast<std::size_t>(std::max(0, frame.view.row_pitch * frame.view.height)),
        0);
    for (int y = 0; y < frame.view.height; ++y) {
        for (int x = 0; x < frame.view.width; ++x) {
            const std::size_t offset =
                static_cast<std::size_t>(y * frame.view.row_pitch + x * 3);
            if (enemy_colored && ((x + (y * 2)) % 5 == 0)) {
                frame.pixels[offset + 0] = 255;
                frame.pixels[offset + 1] = 0;
                frame.pixels[offset + 2] = 0;
            } else {
                frame.pixels[offset + 0] = 12;
                frame.pixels[offset + 1] = 12;
                frame.pixels[offset + 2] = 12;
            }
        }
    }
    frame.view.data = frame.pixels.data();
    return frame;
}

vision_native::DetectionBatch roi_single_target_batch(
    std::uint64_t frame_id,
    float target_x,
    float target_y,
    float conf) {
    return selector_intent_batch(
        frame_id,
        {selector_intent_detection(target_x, target_y, conf)});
}

void roi_record_result(
    ScenarioMetrics& metrics,
    const vision_native::VisionTargetSelector& selector,
    const vision_native::DetectionBatch& batch,
    const std::optional<vision_native::VisionTargetSelector::FrameRegion>& region,
    const vision_native::VisionResult& result,
    bool partial_frame,
    bool expected_target) {
    ++metrics.roi_fallback_ticks;
    if (region.has_value()) {
        ++metrics.roi_fallback_requested_regions;
        metrics.roi_fallback_processed_area_ratio +=
            static_cast<double>(roi_area(*region)) / static_cast<double>(640 * 512);
        if (partial_frame) {
            ++metrics.roi_fallback_partial_regions;
        }
        if (region->left == 0 || region->top == 0 || region->right == 640 || region->bottom == 512) {
            ++metrics.roi_fallback_edge_clamped_regions;
        }
    } else if (!batch.has_external_cue && selector.wants_color_frame()) {
        ++metrics.roi_fallback_full_frame_requests;
    }
    if (expected_target && !result.has_target) {
        ++metrics.roi_fallback_target_loss_frames;
    }
}

void roi_finish_rates(ScenarioMetrics& metrics) {
    metrics.frames = metrics.roi_fallback_ticks;
    if (metrics.roi_fallback_requested_regions > 0) {
        metrics.roi_fallback_processed_area_ratio /=
            static_cast<double>(metrics.roi_fallback_requested_regions);
    }
}

ScenarioMetrics run_roi_fallback_partial_color_frame(unsigned int seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> jitter(-2.0f, 2.0f);
    vision_native::VisionTargetSelector selector(640, 512);
    ScenarioMetrics metrics;
    metrics.name = "roi_fallback_partial_color_frame";
    metrics.has_roi_fallback = true;
    metrics.roi_fallback_seed = static_cast<int>(seed);

    for (int tick = 0; tick < 120; ++tick) {
        if (tick > 2 && tick % 40 == 20) {
            vision_native::DetectionBatch miss_batch;
            miss_batch.frame_id = static_cast<std::uint64_t>(tick + 1);
            miss_batch.captured_at_ns = miss_batch.frame_id * 10'000'000ull;
            miss_batch.inferred_at_ns = miss_batch.captured_at_ns + 1'000'000ull;
            miss_batch.frame_width = 640;
            miss_batch.frame_height = 512;
            const auto miss_region = selector.required_color_region(miss_batch);
            RoiColorFrame miss_frame = make_roi_color_frame({0, 0, 8, 8}, 640, 512, false);
            vision_native::VisionResult miss_result =
                selector.select_with_frame(miss_batch, miss_frame.view);
            selector_intent_apply_frame_metadata(miss_result, miss_batch);
            if (miss_result.has_target) {
                ++metrics.roi_fallback_roi_miss_hold_frames;
            }
            roi_record_result(
                metrics,
                selector,
                miss_batch,
                miss_region,
                miss_result,
                false,
                true);
            continue;
        }

        const vision_native::DetectionBatch batch = roi_single_target_batch(
            static_cast<std::uint64_t>(tick + 1),
            320.0f + jitter(rng),
            256.0f + jitter(rng),
            0.45f);
        const auto region = selector.required_color_region(batch);
        if (!region.has_value()) {
            ++metrics.roi_fallback_target_loss_frames;
            ++metrics.roi_fallback_ticks;
            continue;
        }
        RoiColorFrame frame = make_roi_color_frame(*region, 640, 512, true);
        vision_native::VisionResult result = selector.select_with_frame(batch, frame.view);
        selector_intent_apply_frame_metadata(result, batch);
        roi_record_result(metrics, selector, batch, region, result, true, tick >= 1);
    }

    roi_finish_rates(metrics);
    return metrics;
}

ScenarioMetrics run_roi_external_cue_no_full_color_frame(unsigned int seed) {
    vision_native::VisionTargetSelector selector(640, 512);
    ScenarioMetrics metrics;
    metrics.name = "roi_external_cue_no_full_color_frame";
    metrics.has_roi_fallback = true;
    metrics.roi_fallback_seed = static_cast<int>(seed);

    const vision_native::DetectionBatch setup = roi_single_target_batch(1, 320.0f, 256.0f, 0.45f);
    const auto setup_region = selector.required_color_region(setup);
    if (setup_region.has_value()) {
        RoiColorFrame frame = make_roi_color_frame(*setup_region, 640, 512, true);
        selector.select_with_frame(setup, frame.view);
        selector.select_with_frame(setup, frame.view);
    }

    for (int tick = 0; tick < 80; ++tick) {
        vision_native::DetectionBatch cue_batch;
        cue_batch.frame_id = static_cast<std::uint64_t>(tick + 2);
        cue_batch.captured_at_ns = cue_batch.frame_id * 10'000'000ull;
        cue_batch.inferred_at_ns = cue_batch.captured_at_ns + 1'000'000ull;
        cue_batch.frame_width = 640;
        cue_batch.frame_height = 512;
        cue_batch.has_external_cue = true;
        cue_batch.external_cue_x = 320.0f + (std::sin(static_cast<float>(tick) * 0.05f) * 2.0f);
        cue_batch.external_cue_y = 200.0f + (std::cos(static_cast<float>(tick) * 0.05f) * 2.0f);
        cue_batch.external_cue_score = 0.90f;

        const auto region = selector.required_color_region(cue_batch);
        if (!region.has_value()) {
            ++metrics.roi_fallback_external_cue_no_full_frame_frames;
        }
        vision_native::VisionResult result = selector.select(cue_batch);
        selector_intent_apply_frame_metadata(result, cue_batch);
        roi_record_result(metrics, selector, cue_batch, region, result, false, false);
    }

    roi_finish_rates(metrics);
    return metrics;
}

ScenarioMetrics run_roi_boundary_candidate_at_screen_edge(unsigned int seed) {
    vision_native::VisionTargetSelector selector(640, 512);
    ScenarioMetrics metrics;
    metrics.name = "roi_boundary_candidate_at_screen_edge";
    metrics.has_roi_fallback = true;
    metrics.roi_fallback_seed = static_cast<int>(seed);

    for (int tick = 0; tick < 120; ++tick) {
        const float target_x = tick % 2 == 0 ? 10.0f : 630.0f;
        const float target_y = tick % 3 == 0 ? 72.0f : 498.0f;
        const vision_native::DetectionBatch batch = roi_single_target_batch(
            static_cast<std::uint64_t>(tick + 1),
            target_x,
            target_y,
            0.92f);
        const auto region = selector.required_color_region(batch);
        if (region.has_value()) {
            RoiColorFrame frame = make_roi_color_frame(*region, 640, 512, true);
            vision_native::VisionResult result = selector.select_with_frame(batch, frame.view);
            selector_intent_apply_frame_metadata(result, batch);
            roi_record_result(metrics, selector, batch, region, result, true, false);
        } else {
            ++metrics.roi_fallback_ticks;
        }
    }

    roi_finish_rates(metrics);
    return metrics;
}

std::vector<ScenarioMetrics> run_roi_fallback_suite(unsigned int seed) {
    return {
        run_roi_fallback_partial_color_frame(seed),
        run_roi_external_cue_no_full_color_frame(seed),
        run_roi_boundary_candidate_at_screen_edge(seed),
    };
}

void add_axis_sample(AxisStats& stats, float manual, float final_value, float recoil) {
    constexpr float kEps = 0.0001f;
    stats.abs_recoil_sum += std::fabs(recoil);
    stats.abs_final_sum += std::fabs(final_value);
    if (std::fabs(manual) <= kEps) {
        return;
    }
    ++stats.manual_frames;
    if (manual * final_value < -kEps) {
        ++stats.final_opposes_manual;
        stats.manual_preservation_sum += 0.0;
    } else {
        const double ratio =
            std::min(2.0, static_cast<double>(std::fabs(final_value) / std::fabs(manual)));
        stats.manual_preservation_sum += ratio;
    }
    if (std::fabs(final_value) < (std::fabs(manual) * 0.35f) || manual * final_value < -kEps) {
        ++stats.final_blocks_manual;
    }
    if (manual * recoil < -kEps) {
        ++stats.recoil_opposes_manual;
    }
}

void add_frame_sample(
    ScenarioMetrics& metrics,
    const controller_native::NativeControllerOutputComponents& components) {
    ++metrics.frames;
    add_axis_sample(
        metrics.x,
        components.manual_stick.x,
        components.final_stick.x,
        components.recoil_stick.x);
    add_axis_sample(
        metrics.y,
        components.manual_stick.y,
        components.final_stick.y,
        components.recoil_stick.y);
}

void sleep_dt(double dt_ms) {
    if (dt_ms <= 0.0) {
        return;
    }
    std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(dt_ms));
}

struct AdsParityCase {
    const char* family = "single_static_offset";
    double initial_dx = 0.0;
    double initial_dy = 0.0;
    double velocity_x = 0.0;
    double velocity_y = 0.0;
    int decel_start_frame = -1;
    int decel_duration_frames = 0;
    double decel_target_speed_scale = 1.0;
};

struct AdsManualFrame {
    float x = 0.0f;
    float y = 0.0f;
    bool overshoot_recover = false;
};

struct AdsCaseResult {
    int overshoot_events = 0;
    int crossing_events = 0;
    int ads_snap_crossing_events = 0;
    int body_lock_crossing_events = 0;
    int manual_crossing_events = 0;
    int ads_snap_overshoot_events = 0;
    int body_lock_overshoot_events = 0;
    int manual_overshoot_events = 0;
    int overshoot_events_x = 0;
    int overshoot_events_y = 0;
    int time_to_under_20_frame = -1;
    double mean_error_px = 0.0;
    double p95_error_px = 0.0;
    double p99_error_px = 0.0;
    double final_error_px = 0.0;
    double max_single_frame_camera_delta_px = 0.0;
    double max_overshoot_px = 0.0;
};

double ads_velocity_scale_for_frame(const AdsParityCase& scenario, int frame) {
    if (scenario.decel_start_frame < 0 || frame < scenario.decel_start_frame) {
        return 1.0;
    }
    if (scenario.decel_duration_frames <= 1) {
        return scenario.decel_target_speed_scale;
    }
    const int step_index =
        std::min(scenario.decel_duration_frames - 1, frame - scenario.decel_start_frame);
    const double progress = clamp_double(
        static_cast<double>(step_index) /
            static_cast<double>(scenario.decel_duration_frames - 1),
        0.0,
        1.0);
    return 1.0 + ((scenario.decel_target_speed_scale - 1.0) * progress);
}

float ads_manual_axis_x(double error_x, double scale) {
    constexpr double kMaxManualRatio = 0.72;
    constexpr double kFullScaleX = 90.0;
    return static_cast<float>(clamp_double(
        (error_x / kFullScaleX) * kMaxManualRatio * scale,
        -1.0,
        1.0));
}

float ads_manual_axis_y(double error_y, double scale) {
    constexpr double kMaxManualRatio = 0.72;
    constexpr double kFullScaleY = 80.0;
    return static_cast<float>(clamp_double(
        (-error_y / kFullScaleY) * kMaxManualRatio * scale,
        -1.0,
        1.0));
}

AdsManualFrame ads_manual_frame(
    const std::string& profile,
    int frame,
    double error_x,
    double error_y) {
    constexpr double kAlignedScale = 0.62;
    constexpr double kOpposingScale = 0.55;
    constexpr double kRecoverScale = 0.48;
    constexpr double kVerticalTailScale = 0.16;
    AdsManualFrame manual;
    if (profile == "none") {
        return manual;
    }
    if (profile == "aligned_follow") {
        manual.x = ads_manual_axis_x(error_x, kAlignedScale);
        manual.y = ads_manual_axis_y(error_y, kAlignedScale);
        return manual;
    }
    if (profile == "opposing_burst") {
        const bool in_burst = frame >= 4 && frame <= 7;
        const double scale = in_burst ? -kOpposingScale : kAlignedScale;
        manual.x = ads_manual_axis_x(error_x, scale);
        manual.y = ads_manual_axis_y(error_y, scale);
        return manual;
    }

    manual.overshoot_recover = true;
    if (frame < 3) {
        manual.x = ads_manual_axis_x(error_x, kAlignedScale);
        manual.y = ads_manual_axis_y(error_y, kAlignedScale);
    } else if (frame < 6) {
        manual.x = ads_manual_axis_x(error_x, -kRecoverScale);
        manual.y = ads_manual_axis_y(error_y, -kRecoverScale);
    } else {
        manual.x = ads_manual_axis_x(error_x, kRecoverScale * 0.35);
        manual.y = ads_manual_axis_y(error_y, kVerticalTailScale);
    }
    return manual;
}

AdsCaseResult run_ads_parity_case(
    controller_native::GamepadRuntimeConfig config,
    const AdsParityCase& scenario,
    const std::string& profile,
    bool enable_dynamics,
    bool fire_active) {
    constexpr int kSimFrames = 90;
    constexpr double kFrameDt = 1.0 / 60.0;
    constexpr double kReticleSpeed = 1500.0;
    constexpr double kOvershootThresholdPx = 2.0;
    constexpr double kUnderTargetThresholdPx = 20.0;
    constexpr int kUnderTargetConsecutiveFrames = 2;

    config.recoil.enabled = false;
    config.aim_assist_dynamics.enabled = enable_dynamics;

    double simulated_now = 1.0;
    controller_native::NativeGamepadController controller(
        config,
        [&simulated_now]() { return simulated_now; });
    double target_x = scenario.initial_dx;
    double target_y = scenario.initial_dy;
    double reticle_x = 0.0;
    double reticle_y = 0.0;
    int under_target_streak = 0;
    AdsCaseResult result;
    std::vector<double> errors;
    std::vector<double> x_errors;
    std::vector<double> y_errors;
    std::vector<std::string> modes;
    errors.reserve(kSimFrames);
    x_errors.reserve(kSimFrames);
    y_errors.reserve(kSimFrames);
    modes.reserve(kSimFrames);
    bool has_previous_error = false;
    double previous_error_x = 0.0;
    double previous_error_y = 0.0;

    for (int frame = 0; frame < kSimFrames; ++frame) {
        simulated_now = 1.0 + (static_cast<double>(frame) * kFrameDt);
        const double velocity_scale = ads_velocity_scale_for_frame(scenario, frame);
        target_x += scenario.velocity_x * velocity_scale * kFrameDt;
        target_y += scenario.velocity_y * velocity_scale * kFrameDt;

        const double error_x_before = target_x - reticle_x;
        const double error_y_before = target_y - reticle_y;
        const AdsManualFrame manual =
            ads_manual_frame(profile, frame, error_x_before, error_y_before);
        controller.submit_vision_state(
            benchmark_target_state(
                static_cast<float>(error_x_before),
                static_cast<float>(error_y_before),
                simulated_now));
        controller.build_output(aiming_state(manual.x, manual.y, fire_active));
        const controller_native::NativeControllerOutputComponents& components =
            controller.last_output_components();
        const double reticle_delta_x =
            static_cast<double>(components.final_stick.x) * kReticleSpeed * kFrameDt;
        const double reticle_delta_y =
            -static_cast<double>(components.final_stick.y) * kReticleSpeed * kFrameDt;
        reticle_x += reticle_delta_x;
        reticle_y += reticle_delta_y;
        result.max_single_frame_camera_delta_px = std::max(
            result.max_single_frame_camera_delta_px,
            std::hypot(reticle_delta_x, reticle_delta_y));

        const double error_x_after = target_x - reticle_x;
        const double error_y_after = target_y - reticle_y;
        const double radial_after = std::hypot(error_x_after, error_y_after);
        const std::string& mode = controller.last_ai_aim_mode();
        const auto record_crossing = [&](double previous, double current) {
            if (previous == 0.0 || current == 0.0 ||
                previous * current >= 0.0 ||
                std::fabs(current) <= kOvershootThresholdPx) {
                return;
            }
            ++result.crossing_events;
            if (mode == "ads_snap") {
                ++result.ads_snap_crossing_events;
            } else if (mode == "body_lock") {
                ++result.body_lock_crossing_events;
            } else {
                ++result.manual_crossing_events;
            }
        };
        if (has_previous_error) {
            record_crossing(previous_error_x, error_x_after);
            record_crossing(previous_error_y, error_y_after);
        }
        previous_error_x = error_x_after;
        previous_error_y = error_y_after;
        has_previous_error = true;
        x_errors.push_back(error_x_after);
        y_errors.push_back(error_y_after);
        modes.push_back(mode);
        errors.push_back(radial_after);
        if (radial_after <= kUnderTargetThresholdPx) {
            ++under_target_streak;
            if (result.time_to_under_20_frame < 0 &&
                under_target_streak >= kUnderTargetConsecutiveFrames) {
                result.time_to_under_20_frame =
                    frame - kUnderTargetConsecutiveFrames + 1;
            }
        } else {
            under_target_streak = 0;
        }
    }

    const ModeOvershootStats x_overshoot =
        axis_mode_overshoot_stats(x_errors, modes, kOvershootThresholdPx);
    const ModeOvershootStats y_overshoot =
        axis_mode_overshoot_stats(y_errors, modes, kOvershootThresholdPx);
    result.overshoot_events = x_overshoot.count + y_overshoot.count;
    result.overshoot_events_x = x_overshoot.count;
    result.overshoot_events_y = y_overshoot.count;
    result.ads_snap_overshoot_events =
        x_overshoot.ads_snap_count + y_overshoot.ads_snap_count;
    result.body_lock_overshoot_events =
        x_overshoot.body_lock_count + y_overshoot.body_lock_count;
    result.manual_overshoot_events =
        x_overshoot.manual_count + y_overshoot.manual_count;
    result.max_overshoot_px = std::max(x_overshoot.max_px, y_overshoot.max_px);
    result.mean_error_px = mean_value(errors);
    result.p95_error_px = nearest_rank_percentile(errors, 0.95);
    result.p99_error_px = nearest_rank_percentile(errors, 0.99);
    result.final_error_px = errors.empty() ? 0.0 : errors.back();
    return result;
}

ScenarioMetrics run_ads_python_parity_60hz(
    controller_native::GamepadRuntimeConfig config,
    const std::string& name = "ads_python_parity_60hz",
    bool enable_dynamics = false,
    bool fire_active = false) {
    ScenarioMetrics metrics;
    metrics.name = name;
    metrics.has_ads_parity = true;
    metrics.ads_parity_frames_per_case = 90;
    metrics.ads_parity_frame_dt_ms = 1000.0 / 60.0;
    metrics.ads_parity_target_sample_hz = 60.0;

    const std::vector<AdsParityCase> scenarios = {
        {"single_static_offset", 160.0, 16.0, 0.0, 0.0, -1, 0, 1.0},
        {"single_static_offset", -80.0, -48.0, 0.0, 0.0, -1, 0, 1.0},
        {"single_strafe_then_decel", 120.0, -24.0, -320.0, 0.0, 18, 12, 0.35},
        {"single_diagonal_then_decel", -100.0, 50.0, 226.0, -226.0, 24, 18, 0.15},
    };
    const std::vector<std::string> profiles = {
        "none",
        "aligned_follow",
        "opposing_burst",
        "overshoot_recover",
    };

    std::vector<double> mean_errors;
    std::vector<double> p95_errors;
    std::vector<double> p99_errors;
    std::vector<double> final_errors;
    std::vector<double> time_to_under_ms;
    for (const AdsParityCase& scenario : scenarios) {
        for (const std::string& profile : profiles) {
            const AdsCaseResult result =
                run_ads_parity_case(config, scenario, profile, enable_dynamics, fire_active);
            ++metrics.ads_parity_cases;
            metrics.ads_parity_overshoot_events += result.overshoot_events;
            metrics.ads_parity_crossing_events += result.crossing_events;
            metrics.ads_parity_ads_snap_crossing_events +=
                result.ads_snap_crossing_events;
            metrics.ads_parity_body_lock_crossing_events +=
                result.body_lock_crossing_events;
            metrics.ads_parity_manual_crossing_events +=
                result.manual_crossing_events;
            metrics.ads_parity_ads_snap_overshoot_events +=
                result.ads_snap_overshoot_events;
            metrics.ads_parity_body_lock_overshoot_events +=
                result.body_lock_overshoot_events;
            metrics.ads_parity_manual_overshoot_events +=
                result.manual_overshoot_events;
            metrics.ads_parity_overshoot_events_x += result.overshoot_events_x;
            metrics.ads_parity_overshoot_events_y += result.overshoot_events_y;
            if (profile == "none") {
                metrics.ads_parity_overshoot_events_none += result.overshoot_events;
            } else if (profile == "aligned_follow") {
                metrics.ads_parity_overshoot_events_aligned_follow += result.overshoot_events;
            } else if (profile == "opposing_burst") {
                metrics.ads_parity_overshoot_events_opposing_burst += result.overshoot_events;
            } else if (profile == "overshoot_recover") {
                metrics.ads_parity_overshoot_events_overshoot_recover += result.overshoot_events;
            }
            if (result.overshoot_events > 0) {
                ++metrics.ads_parity_overshoot_cases;
            }
            if (profile == "overshoot_recover") {
                metrics.ads_parity_overshoot_recover_events +=
                    result.overshoot_events;
                if (result.overshoot_events > 0) {
                    ++metrics.ads_parity_overshoot_recover_cases;
                }
            }
            if (result.time_to_under_20_frame >= 0) {
                ++metrics.ads_parity_under_20_cases;
                time_to_under_ms.push_back(
                    static_cast<double>(result.time_to_under_20_frame) *
                    metrics.ads_parity_frame_dt_ms);
            }
            metrics.ads_parity_max_single_frame_camera_delta_px = std::max(
                metrics.ads_parity_max_single_frame_camera_delta_px,
                result.max_single_frame_camera_delta_px);
            metrics.ads_parity_max_overshoot_px = std::max(
                metrics.ads_parity_max_overshoot_px,
                result.max_overshoot_px);
            mean_errors.push_back(result.mean_error_px);
            p95_errors.push_back(result.p95_error_px);
            p99_errors.push_back(result.p99_error_px);
            final_errors.push_back(result.final_error_px);
        }
    }

    metrics.frames = metrics.ads_parity_cases * metrics.ads_parity_frames_per_case;
    metrics.ads_parity_mean_error_px = mean_value(mean_errors);
    metrics.ads_parity_p95_error_px = mean_value(p95_errors);
    metrics.ads_parity_p99_error_px = mean_value(p99_errors);
    metrics.ads_parity_final_error_px = mean_value(final_errors);
    metrics.ads_parity_mean_time_to_under_20_ms = mean_value(time_to_under_ms);
    return metrics;
}

ScenarioMetrics run_bodylock_edge_escape(
    const controller_native::GamepadRuntimeConfig& config,
    int frames,
    double dt_ms) {
    ScenarioMetrics metrics;
    metrics.name = "bodylock_edge_escape";
    int blocked = 0;
    for (float sign : {-1.0f, 1.0f}) {
        controller_native::NativeGamepadController controller(config);
        const int warmup_frames = std::min(15, std::max(1, frames / 4));
        for (int frame = 0; frame < frames; ++frame) {
            const double now = current_seconds();
            controller_native::NativeControllerVisionState target =
                target_state(24.0f * sign, -18.0f, now);
            controller.submit_vision_state(target);
            const float manual_x = frame < warmup_frames ? 0.0f : -0.50f * sign;
            const controller_native::PhysicalGamepadState physical =
                aiming_state(manual_x, 0.0f);
            controller.build_output(physical);
            const controller_native::NativeControllerOutputComponents& components =
                controller.last_output_components();
            add_frame_sample(metrics, components);
            if (std::fabs(manual_x) > 0.0f &&
                (manual_x * components.final_stick.x <= 0.0f ||
                 std::fabs(components.final_stick.x) < std::fabs(manual_x) * 0.30f)) {
                ++blocked;
            }
            sleep_dt(dt_ms);
        }
    }
    metrics.blocked_frames = blocked;
    return metrics;
}

ScenarioMetrics run_ads_wrong_input_recovery(
    const controller_native::GamepadRuntimeConfig& config,
    int frames,
    double dt_ms) {
    ScenarioMetrics metrics;
    metrics.name = "ads_wrong_input_recovery";
    controller_native::NativeGamepadController controller(config);
    const int wrong_frames = std::min(18, std::max(1, frames / 5));
    int recovery_frames = -1;
    for (int frame = 0; frame < frames; ++frame) {
        const double now = current_seconds();
        controller.submit_vision_state(target_state(62.0f, -16.0f, now));
        const float manual_x = frame < wrong_frames ? -0.30f : 0.30f;
        const controller_native::PhysicalGamepadState physical = aiming_state(manual_x, 0.0f);
        controller.build_output(physical);
        const controller_native::NativeControllerOutputComponents& components =
            controller.last_output_components();
        add_frame_sample(metrics, components);
        if (frame >= wrong_frames && recovery_frames < 0 && components.final_stick.x > 0.05f) {
            recovery_frames = frame - wrong_frames;
        }
        sleep_dt(dt_ms);
    }
    metrics.recovery_frames = recovery_frames;
    return metrics;
}

ScenarioMetrics run_recoil_manual_conflict(
    const controller_native::GamepadRuntimeConfig& config,
    int frames,
    double dt_ms) {
    ScenarioMetrics metrics;
    metrics.name = "recoil_manual_conflict";
    controller_native::NativeGamepadController controller(config);
    for (int frame = 0; frame < frames; ++frame) {
        const double now = current_seconds();
        controller.submit_vision_state(target_state(10.0f, -10.0f, now));
        controller_native::PhysicalGamepadState physical = aiming_state(0.08f, 0.12f);
        physical.right_trigger = 1.0f;
        controller.build_output(physical);
        add_frame_sample(metrics, controller.last_output_components());
        sleep_dt(dt_ms);
    }
    return metrics;
}

ScenarioMetrics run_tracker_random_fov_100hz(
    controller_native::GamepadRuntimeConfig config,
    const CliOptions& options,
    const std::string& name = "tracker_random_fov_100hz",
    bool enable_dynamics = false,
    bool enable_short_plan = false,
    bool pure_ads = false) {
    ScenarioMetrics metrics;
    metrics.name = name;
    metrics.has_random_fov = true;
    metrics.random_fov_min_scale = options.random_fov_min_scale;
    metrics.random_fov_max_scale = options.random_fov_max_scale;
    metrics.random_fov_ai_force_scale = options.random_fov_ai_force_scale;
    if (options.random_fov_ticks <= 0) {
        return metrics;
    }

    constexpr double kControllerHz = 1000.0;
    constexpr double kVisionHz = 100.0;
    constexpr double kMetricHz = 60.0;
    constexpr double kDtSeconds = 1.0 / kControllerHz;
    constexpr int kVisionIntervalTicks =
        static_cast<int>(kControllerHz / kVisionHz);
    const int kMetricIntervalTicks = std::max(
        1,
        static_cast<int>(std::round(kControllerHz / kMetricHz)));
    constexpr double kDirectionDeadzonePx = 6.0;
    constexpr double kOutputDeadzone = 0.015;
    const double reticle_speed =
        std::max(1.0f, config.ai_aim.target_projection_reticle_speed_px_per_sec);

    config.recoil.enabled = false;
    config.aim_assist_dynamics.enabled = enable_dynamics;
    config.ai_aim.ads_snap_window_ms =
        std::max(config.ai_aim.ads_snap_window_ms, options.random_fov_ticks + 20);
    config.ai_aim.target_max_age_ms = std::max(config.ai_aim.target_max_age_ms, 80.0f);
    config.ai_aim.target_projection_max_age_ms =
        std::max(config.ai_aim.target_projection_max_age_ms, 40.0f);
    const float force_scale = static_cast<float>(options.random_fov_ai_force_scale);
    config.ai_aim.max_ai_force *= force_scale;
    config.ai_aim.max_ai_force_y *= force_scale;
    config.ai_aim.ads_snap_max_ai_force *= force_scale;
    config.ai_aim.ads_snap_max_ai_force_y *= force_scale;
    config.ai_aim.body_lock_max_ai_force *= force_scale;
    config.ai_aim.body_lock_opposing_boost_max_ai_force *= force_scale;
    config.ai_aim.body_lock_max_ai_force_y *= force_scale;

    double simulated_now = 1.0;
    std::mt19937 rng(options.random_fov_seed);
    std::uniform_real_distribution<double> fov_distribution(
        options.random_fov_min_scale,
        options.random_fov_max_scale);
    std::uniform_int_distribution<int> hold_distribution(45, 115);
    std::uniform_real_distribution<double> phase_distribution(0.0, 6.28318530717958647692);
    std::uniform_real_distribution<double> amplitude_distribution(0.85, 1.15);
    std::uniform_real_distribution<double> initial_dx_distribution(-60.0, 60.0);
    std::uniform_real_distribution<double> initial_dy_distribution(-45.0, 45.0);
    std::uniform_real_distribution<double> speed_distribution(220.0, 420.0);
    std::uniform_real_distribution<double> heading_distribution(-170.0, 170.0);
    std::uniform_real_distribution<double> turn_delta_distribution(-120.0, 120.0);
    std::uniform_real_distribution<double> second_turn_delta_distribution(-100.0, 100.0);
    std::uniform_real_distribution<double> steady_turn_speed_scale_distribution(0.92, 1.04);
    std::uniform_real_distribution<double> turn_decel_speed_scale_distribution(0.88, 1.02);
    std::uniform_real_distribution<double> soft_decel_scale_distribution(0.12, 0.65);
    std::uniform_real_distribution<double> resume_scale_distribution(0.70, 1.05);
    std::uniform_real_distribution<double> probability_distribution(0.0, 1.0);
    const auto frame_to_tick = [](int frame) {
        return static_cast<int>(std::round(
            static_cast<double>(frame) * (1000.0 / 60.0)));
    };
    auto random_int = [&rng](int min_value, int max_value) {
        return std::uniform_int_distribution<int>(min_value, max_value)(rng);
    };

    int fov_transition_ticks = std::max(
        1,
        static_cast<int>(std::round(
            std::max(1.0f, config.ai_aim.ads_snap_fov_transition_ms))));
    double fov_sum = 0.0;
    double abs_expected_dx_sum = 0.0;
    double abs_final_x_sum = 0.0;
    double abs_ai_aim_x_sum = 0.0;
    double target_alignment_sum = 0.0;
    double manual_alignment_sum = 0.0;
    std::vector<double> turn_degrees;
    std::vector<double> output_deltas;
    std::vector<double> residual_errors;
    residual_errors.reserve(static_cast<std::size_t>(options.random_fov_ticks));
    turn_degrees.reserve(static_cast<std::size_t>(options.random_fov_ticks));
    output_deltas.reserve(static_cast<std::size_t>(options.random_fov_ticks));
    constexpr double kOvershootThresholdPx = 2.0;
    constexpr int kReferenceScenarioCount = 24;
    const int segment_count =
        std::max(1, std::min(kReferenceScenarioCount, options.random_fov_ticks));
    const int base_segment_ticks = options.random_fov_ticks / segment_count;
    const int extra_segment_ticks = options.random_fov_ticks % segment_count;
    int total_overshoot_events = 0;
    int total_overshoot_events_x = 0;
    int total_overshoot_events_y = 0;
    int total_overshoot_ads_snap = 0;
    int total_overshoot_body_lock = 0;
    int total_overshoot_manual = 0;
    double max_overshoot_px = 0.0;
    int global_tick = 0;

    for (int segment = 0; segment < segment_count; ++segment) {
        const int segment_ticks =
            base_segment_ticks + (segment < extra_segment_ticks ? 1 : 0);
        if (segment_ticks <= 0) {
            continue;
        }
        controller_native::NativeGamepadController controller(
            config,
            [&simulated_now]() { return simulated_now; });
        const double manual_phase_x = phase_distribution(rng);
        const double manual_phase_y = phase_distribution(rng);
        const double manual_noise_x = 0.055 * amplitude_distribution(rng);
        const double manual_noise_y = 0.035 * amplitude_distribution(rng);
        double base_speed_px_per_sec = speed_distribution(rng);
        double current_speed_px_per_sec = base_speed_px_per_sec;
        double current_heading_deg = heading_distribution(rng);
        int first_turn_tick = -1;
        int second_turn_tick = -1;
        double first_turn_delta_deg = 0.0;
        double second_turn_delta_deg = 0.0;
        double first_turn_speed_scale = 1.0;
        double second_turn_speed_scale = 1.0;
        int decel_start_tick = -1;
        int decel_duration_ticks = 1;
        double decel_target_speed_scale = 1.0;
        int resume_start_tick = -1;
        int resume_duration_ticks = 1;
        double resume_target_speed_scale = 1.0;
        bool decel_active = false;
        bool resume_active = false;
        double transition_start_speed_scale = 1.0;
        double transition_end_speed_scale = 1.0;
        int transition_start_tick = 0;
        int transition_duration_ticks = 1;
        const int phase1_slot = segment % kReferenceScenarioCount;
        if (phase1_slot < 8) {
            first_turn_tick = frame_to_tick(random_int(18, 72));
            first_turn_delta_deg = turn_delta_distribution(rng);
            first_turn_speed_scale = steady_turn_speed_scale_distribution(rng);
            if (probability_distribution(rng) < 0.5) {
                const int first_frame =
                    static_cast<int>(std::round(first_turn_tick / (1000.0 / 60.0)));
                const int second_frame =
                    random_int(first_frame + 18, std::min(150, first_frame + 70));
                second_turn_tick = frame_to_tick(second_frame);
                second_turn_delta_deg = second_turn_delta_distribution(rng);
                second_turn_speed_scale = steady_turn_speed_scale_distribution(rng);
            }
        } else if (phase1_slot < 16) {
            const int turn_frame = random_int(16, 60);
            first_turn_tick = frame_to_tick(turn_frame);
            first_turn_delta_deg = turn_delta_distribution(rng);
            first_turn_speed_scale = turn_decel_speed_scale_distribution(rng);
            const int decel_frame_min = std::min(150, turn_frame + 18);
            const int decel_frame_max = std::min(170, turn_frame + 80);
            decel_start_tick = frame_to_tick(
                random_int(decel_frame_min, std::max(decel_frame_min, decel_frame_max)));
            decel_duration_ticks = frame_to_tick(random_int(10, 30));
            decel_target_speed_scale =
                probability_distribution(rng) < 0.35 ? 0.0 : soft_decel_scale_distribution(rng);
        } else {
            const int decel_frame = random_int(18, 72);
            decel_start_tick = frame_to_tick(decel_frame);
            decel_duration_ticks = frame_to_tick(random_int(10, 30));
            decel_target_speed_scale =
                probability_distribution(rng) < 0.35 ? 0.0 : soft_decel_scale_distribution(rng);
            if (probability_distribution(rng) < 0.875) {
                const int resume_frame_min =
                    std::min(160, decel_frame + static_cast<int>(
                        std::round(decel_duration_ticks / (1000.0 / 60.0))) + 12);
                const int resume_frame_max =
                    std::min(176, decel_frame + static_cast<int>(
                        std::round(decel_duration_ticks / (1000.0 / 60.0))) + 70);
                resume_start_tick = frame_to_tick(
                    random_int(resume_frame_min, std::max(resume_frame_min, resume_frame_max)));
                resume_duration_ticks = frame_to_tick(random_int(12, 36));
                resume_target_speed_scale = resume_scale_distribution(rng);
            }
        }

        double fov_scale = 1.0;
        double fov_start = 1.0;
        double fov_target = fov_distribution(rng);
        int fov_transition_start_tick = 0;
        int next_fov_change_tick = fov_transition_ticks + hold_distribution(rng);
        ++metrics.random_fov_change_events;

        double target_x_position = initial_dx_distribution(rng);
        double target_y_position = initial_dy_distribution(rng);
        double reticle_x_position = 0.0;
        double reticle_y_position = 0.0;
        double previous_vision_dx = 0.0;
        bool has_previous_vision_dx = false;
        double previous_output_x = 0.0;
        double previous_output_y = 0.0;
        bool has_previous_output = false;
        BenchmarkShortTermOutputPlan short_plan;
        std::vector<double> segment_residual_x_errors;
        std::vector<double> segment_residual_y_errors;
        std::vector<std::string> segment_modes;
        std::vector<RandomFovSample> segment_samples;
        segment_residual_x_errors.reserve(
            static_cast<std::size_t>(segment_ticks));
        segment_residual_y_errors.reserve(
            static_cast<std::size_t>(segment_ticks));
        segment_modes.reserve(static_cast<std::size_t>(segment_ticks));
        segment_samples.reserve(static_cast<std::size_t>(segment_ticks));

        const int measure_start_tick = std::min(
            segment_ticks - 1,
            std::max(0, segment_ticks / 3));

        for (int tick = 0; tick < segment_ticks; ++tick, ++global_tick) {
            simulated_now = 1.0 + (static_cast<double>(global_tick) * kDtSeconds);
            if (tick >= next_fov_change_tick) {
                fov_start = fov_scale;
                fov_target = fov_distribution(rng);
                fov_transition_start_tick = tick;
                next_fov_change_tick =
                    tick + fov_transition_ticks + hold_distribution(rng);
                ++metrics.random_fov_change_events;
            }
            const double fov_progress =
                static_cast<double>(tick - fov_transition_start_tick) /
                static_cast<double>(fov_transition_ticks);
            fov_scale = lerp(fov_start, fov_target, fov_progress);

            if (tick == first_turn_tick) {
                current_heading_deg = std::fmod(
                    current_heading_deg + first_turn_delta_deg + 360.0,
                    360.0);
                current_speed_px_per_sec = base_speed_px_per_sec * first_turn_speed_scale;
            }
            if (tick == second_turn_tick) {
                current_heading_deg = std::fmod(
                    current_heading_deg + second_turn_delta_deg + 360.0,
                    360.0);
                current_speed_px_per_sec = base_speed_px_per_sec * second_turn_speed_scale;
            }
            if (tick == decel_start_tick) {
                decel_active = true;
                resume_active = false;
                transition_start_tick = tick;
                transition_duration_ticks = std::max(1, decel_duration_ticks);
                transition_start_speed_scale =
                    base_speed_px_per_sec <= 0.0
                        ? 0.0
                        : current_speed_px_per_sec / base_speed_px_per_sec;
                transition_end_speed_scale = decel_target_speed_scale;
            }
            if (tick == resume_start_tick) {
                resume_active = true;
                decel_active = false;
                transition_start_tick = tick;
                transition_duration_ticks = std::max(1, resume_duration_ticks);
                transition_start_speed_scale =
                    base_speed_px_per_sec <= 0.0
                        ? 0.0
                        : current_speed_px_per_sec / base_speed_px_per_sec;
                transition_end_speed_scale = resume_target_speed_scale;
            }
            if (decel_active || resume_active) {
                const double progress = static_cast<double>(tick - transition_start_tick) /
                    static_cast<double>(transition_duration_ticks);
                const double speed_scale = lerp(
                    transition_start_speed_scale,
                    transition_end_speed_scale,
                    progress);
                current_speed_px_per_sec = base_speed_px_per_sec * speed_scale;
                if (progress >= 1.0) {
                    decel_active = false;
                    resume_active = false;
                    current_speed_px_per_sec =
                        base_speed_px_per_sec * transition_end_speed_scale;
                }
            }
            const double t = static_cast<double>(tick) * kDtSeconds;
            const double heading_rad =
                current_heading_deg * 3.14159265358979323846 / 180.0;
            target_x_position +=
                std::cos(heading_rad) * current_speed_px_per_sec * kDtSeconds;
            target_y_position +=
                std::sin(heading_rad) * current_speed_px_per_sec * kDtSeconds;
            const double expected_dx =
                (target_x_position - reticle_x_position) * fov_scale;
            const double expected_dy =
                (target_y_position - reticle_y_position) * fov_scale;
            const bool vision_tick = tick % kVisionIntervalTicks == 0;
            if (vision_tick) {
                controller.submit_vision_state(
                    target_state(
                        static_cast<float>(expected_dx),
                        static_cast<float>(expected_dy),
                        simulated_now));
                ++metrics.random_fov_vision_samples;
                if (has_previous_vision_dx &&
                    std::fabs(expected_dx - previous_vision_dx) > 18.0) {
                    ++metrics.random_fov_large_vision_jumps;
                }
                previous_vision_dx = expected_dx;
                has_previous_vision_dx = true;
            } else {
                ++metrics.random_fov_tracker_only_ticks;
            }

            float manual_x = clamp_float(
                static_cast<float>((expected_dx / 90.0) * 0.72),
                -0.72f,
                0.72f);
            float manual_y = clamp_float(
                static_cast<float>((-expected_dy / 80.0) * 0.72),
                -0.72f,
                0.72f);
            manual_x = clamp_float(
                manual_x + static_cast<float>(
                    manual_noise_x * std::sin((t * 31.0) + manual_phase_x)),
                -0.72f,
                0.72f);
            manual_y = clamp_float(
                manual_y + static_cast<float>(
                    manual_noise_y * std::sin((t * 19.0) + manual_phase_y)),
                -0.72f,
                0.72f);
            if (pure_ads) {
                manual_x = 0.0f;
                manual_y = 0.0f;
            }

            controller.build_output(aiming_state(manual_x, manual_y));
            const controller_native::NativeControllerOutputComponents& raw_components =
                controller.last_output_components();
            controller_native::NativeControllerOutputComponents components =
                raw_components;
            if (enable_short_plan) {
                apply_benchmark_short_plan(
                    short_plan,
                    components,
                    expected_dx,
                    expected_dy,
                    reticle_speed);
            }
            add_frame_sample(metrics, components);
            const double output_move_x = components.final_stick.x;
            const double output_move_y = -components.final_stick.y;
            const double manual_move_x = manual_x;
            const double manual_move_y = -manual_y;

            const int expected_dir = direction(expected_dx, kDirectionDeadzonePx);
            const int output_dir = direction(components.final_stick.x, kOutputDeadzone);
            const bool output_insensitive =
                expected_dir != 0 && std::fabs(components.final_stick.x) < kOutputDeadzone;
            if (vision_tick) {
                if (expected_dir != 0 && output_dir != 0 && output_dir != expected_dir) {
                    ++metrics.random_fov_fresh_wrong_direction;
                }
                if (output_insensitive) {
                    ++metrics.random_fov_fresh_insensitive;
                }
            } else {
                if (expected_dir != 0 && output_dir != 0 && output_dir != expected_dir) {
                    ++metrics.random_fov_tracker_wrong_direction;
                }
                if (output_insensitive) {
                    ++metrics.random_fov_tracker_insensitive;
                }
            }

            fov_sum += fov_scale;
            abs_expected_dx_sum += std::fabs(expected_dx);
            abs_final_x_sum += std::fabs(components.final_stick.x);
            abs_ai_aim_x_sum += std::fabs(components.ai_aim_stick.x);
            const bool metric_sample_tick =
                tick >= measure_start_tick &&
                ((tick - measure_start_tick) % kMetricIntervalTicks) == 0;
            if (metric_sample_tick) {
                constexpr double kVectorDeadzone = 0.015;
                target_alignment_sum += vector_alignment(
                    output_move_x,
                    output_move_y,
                    expected_dx,
                    expected_dy,
                    kVectorDeadzone);
                ++metrics.random_fov_direction_samples;

                if (vector_magnitude(manual_move_x, manual_move_y) >= kVectorDeadzone) {
                    manual_alignment_sum += vector_alignment(
                        output_move_x,
                        output_move_y,
                        manual_move_x,
                        manual_move_y,
                        kVectorDeadzone);
                    ++metrics.random_fov_manual_direction_samples;
                }
                if (has_previous_output) {
                    const double previous_mag =
                        vector_magnitude(previous_output_x, previous_output_y);
                    const double current_mag =
                        vector_magnitude(output_move_x, output_move_y);
                    if (previous_mag >= kVectorDeadzone &&
                        current_mag >= kVectorDeadzone) {
                        const double turn_degrees_value = vector_turn_degrees(
                            previous_output_x,
                            previous_output_y,
                            output_move_x,
                            output_move_y,
                            kVectorDeadzone);
                        const double output_delta = vector_magnitude(
                            output_move_x - previous_output_x,
                            output_move_y - previous_output_y);
                        turn_degrees.push_back(turn_degrees_value);
                        output_deltas.push_back(output_delta);
                        RandomFovTurnEvent turn_event;
                        turn_event.segment = segment;
                        turn_event.tick = tick;
                        turn_event.global_tick = global_tick;
                        turn_event.mode = controller.last_ai_aim_mode();
                        turn_event.turn_degrees = turn_degrees_value;
                        turn_event.output_delta = output_delta;
                        turn_event.previous_output_x = previous_output_x;
                        turn_event.previous_output_y = previous_output_y;
                        turn_event.output_x = output_move_x;
                        turn_event.output_y = output_move_y;
                        turn_event.manual_x = manual_x;
                        turn_event.manual_y = manual_y;
                        turn_event.ai_aim_x = components.ai_aim_stick.x;
                        turn_event.ai_aim_y = components.ai_aim_stick.y;
                        turn_event.dynamics_x = components.dynamic_adjustment_stick.x;
                        turn_event.dynamics_y = components.dynamic_adjustment_stick.y;
                        turn_event.fov_scale = fov_scale;
                        turn_event.expected_dx = expected_dx;
                        turn_event.expected_dy = expected_dy;
                        turn_event.target_speed_px_per_sec = current_speed_px_per_sec;
                        turn_event.heading_deg = current_heading_deg;
                        metrics.random_fov_turn_details.push_back(std::move(turn_event));
                        ++metrics.random_fov_turn_samples;
                    }
                }
                previous_output_x = output_move_x;
                previous_output_y = output_move_y;
                has_previous_output = true;
            }
            reticle_x_position +=
                components.final_stick.x * reticle_speed * kDtSeconds;
            reticle_y_position +=
                -components.final_stick.y * reticle_speed * kDtSeconds;
            const double residual_dx =
                (target_x_position - reticle_x_position) * fov_scale;
            const double residual_dy =
                (target_y_position - reticle_y_position) * fov_scale;
            if (metric_sample_tick) {
                residual_errors.push_back(std::hypot(residual_dx, residual_dy));
                segment_residual_x_errors.push_back(residual_dx);
                segment_residual_y_errors.push_back(residual_dy);
                segment_modes.push_back(controller.last_ai_aim_mode());
                RandomFovSample sample;
                sample.segment = segment;
                sample.tick = tick;
                sample.global_tick = global_tick;
                sample.error_x = residual_dx;
                sample.error_y = residual_dy;
                sample.mode = controller.last_ai_aim_mode();
                sample.final_x = components.final_stick.x;
                sample.final_y = components.final_stick.y;
                sample.manual_x = components.manual_stick.x;
                sample.manual_y = components.manual_stick.y;
                sample.ai_aim_x = components.ai_aim_stick.x;
                sample.ai_aim_y = components.ai_aim_stick.y;
                sample.dynamics_x = components.dynamic_adjustment_stick.x;
                sample.dynamics_y = components.dynamic_adjustment_stick.y;
                sample.fov_scale = fov_scale;
                sample.expected_dx = expected_dx;
                sample.expected_dy = expected_dy;
                copy_frame_vision_to_sample(
                    sample,
                    controller.last_frame_vision_state(),
                    simulated_now);
                sample.target_speed_px_per_sec = current_speed_px_per_sec;
                sample.heading_deg = current_heading_deg;
                segment_samples.push_back(std::move(sample));
                ++metrics.random_fov_measured_ticks;
            }
        }

        const ModeOvershootStats segment_x_overshoot =
            axis_mode_overshoot_stats(
                segment_residual_x_errors,
                segment_modes,
                kOvershootThresholdPx);
        const ModeOvershootStats segment_y_overshoot =
            axis_mode_overshoot_stats(
                segment_residual_y_errors,
                segment_modes,
                kOvershootThresholdPx);
        total_overshoot_events_x += segment_x_overshoot.count;
        total_overshoot_events_y += segment_y_overshoot.count;
        total_overshoot_events +=
            segment_x_overshoot.count + segment_y_overshoot.count;
        total_overshoot_ads_snap +=
            segment_x_overshoot.ads_snap_count + segment_y_overshoot.ads_snap_count;
        total_overshoot_body_lock +=
            segment_x_overshoot.body_lock_count + segment_y_overshoot.body_lock_count;
        total_overshoot_manual +=
            segment_x_overshoot.manual_count + segment_y_overshoot.manual_count;
        max_overshoot_px = std::max(
            max_overshoot_px,
            std::max(segment_x_overshoot.max_px, segment_y_overshoot.max_px));
        std::vector<RandomFovOvershootEvent> segment_x_details =
            random_fov_axis_overshoot_events(
                segment_samples,
                false,
                kOvershootThresholdPx);
        std::vector<RandomFovOvershootEvent> segment_y_details =
            random_fov_axis_overshoot_events(
                segment_samples,
                true,
                kOvershootThresholdPx);
        metrics.random_fov_overshoot_details.insert(
            metrics.random_fov_overshoot_details.end(),
            segment_x_details.begin(),
            segment_x_details.end());
        metrics.random_fov_overshoot_details.insert(
            metrics.random_fov_overshoot_details.end(),
            segment_y_details.begin(),
            segment_y_details.end());
    }

    const double safe_ticks = static_cast<double>(std::max(1, options.random_fov_ticks));
    metrics.random_fov_mean_scale =
        fov_sum / safe_ticks;
    metrics.random_fov_mean_abs_expected_dx =
        abs_expected_dx_sum / safe_ticks;
    metrics.random_fov_mean_abs_final_x =
        abs_final_x_sum / safe_ticks;
    metrics.random_fov_mean_abs_ai_aim_x = abs_ai_aim_x_sum / safe_ticks;
    metrics.random_fov_output_per_100px_error =
        metrics.random_fov_mean_abs_expected_dx <= 0.001
            ? 0.0
            : metrics.random_fov_mean_abs_final_x /
                (metrics.random_fov_mean_abs_expected_dx / 100.0);
    metrics.random_fov_mean_error_px = mean_value(residual_errors);
    metrics.random_fov_p95_error_px =
        nearest_rank_percentile(residual_errors, 0.95);
    metrics.random_fov_p99_error_px =
        nearest_rank_percentile(residual_errors, 0.99);
    metrics.random_fov_mean_target_alignment =
        metrics.random_fov_direction_samples <= 0
            ? 0.0
            : target_alignment_sum /
                static_cast<double>(metrics.random_fov_direction_samples);
    metrics.random_fov_mean_manual_alignment =
        metrics.random_fov_manual_direction_samples <= 0
            ? 0.0
            : manual_alignment_sum /
                static_cast<double>(metrics.random_fov_manual_direction_samples);
    metrics.random_fov_direction_score =
        alignment_score(metrics.random_fov_mean_target_alignment);
    metrics.random_fov_manual_direction_score =
        alignment_score(metrics.random_fov_mean_manual_alignment);
    metrics.random_fov_mean_turn_degrees = mean_value(turn_degrees);
    metrics.random_fov_p95_turn_degrees =
        nearest_rank_percentile(turn_degrees, 0.95);
    metrics.random_fov_turn_smoothness_score =
        turn_smoothness_score(metrics.random_fov_p95_turn_degrees);
    metrics.random_fov_mean_output_delta = mean_value(output_deltas);
    metrics.random_fov_p95_output_delta =
        nearest_rank_percentile(output_deltas, 0.95);
    metrics.random_fov_overshoot_events = total_overshoot_events;
    metrics.random_fov_overshoot_events_x = total_overshoot_events_x;
    metrics.random_fov_overshoot_events_y = total_overshoot_events_y;
    metrics.random_fov_overshoot_ads_snap = total_overshoot_ads_snap;
    metrics.random_fov_overshoot_body_lock = total_overshoot_body_lock;
    metrics.random_fov_overshoot_manual = total_overshoot_manual;
    metrics.random_fov_max_overshoot_px = max_overshoot_px;
    return metrics;
}

ScenarioMetrics run_ads_fov_settle_130ms(
    controller_native::GamepadRuntimeConfig config,
    const CliOptions& options) {
    ScenarioMetrics metrics;
    metrics.name = "ads_fov_settle_130ms";
    metrics.has_ads_settle = true;

    constexpr double kControllerHz = 1000.0;
    constexpr double kVisionHz = 100.0;
    constexpr double kDtSeconds = 1.0 / kControllerHz;
    constexpr int kVisionIntervalTicks =
        static_cast<int>(kControllerHz / kVisionHz);
    constexpr double kSettleThresholdPx = 3.0;
    constexpr int kSettleHoldTicks = 10;
    constexpr double kOvershootThresholdPx = 2.0;

    config.recoil.enabled = false;
    config.aim_assist_dynamics.enabled = false;
    config.ai_aim.target_max_age_ms = std::max(config.ai_aim.target_max_age_ms, 80.0f);
    config.ai_aim.target_projection_max_age_ms =
        std::max(config.ai_aim.target_projection_max_age_ms, 40.0f);
    const float force_scale = static_cast<float>(options.random_fov_ai_force_scale);
    config.ai_aim.max_ai_force *= force_scale;
    config.ai_aim.max_ai_force_y *= force_scale;
    config.ai_aim.ads_snap_max_ai_force *= force_scale;
    config.ai_aim.ads_snap_max_ai_force_y *= force_scale;
    config.ai_aim.body_lock_max_ai_force *= force_scale;
    config.ai_aim.body_lock_opposing_boost_max_ai_force *= force_scale;
    config.ai_aim.body_lock_max_ai_force_y *= force_scale;

    const int snap_window_ms = std::max(1, config.ai_aim.ads_snap_window_ms);
    const int total_ticks = std::max(240, snap_window_ms + 110);
    const double reticle_speed =
        std::max(1.0f, config.ai_aim.target_projection_reticle_speed_px_per_sec);
    const double fov_start = 1.0;
    const double fov_end =
        std::max(0.05f, std::min(2.0f, config.ai_aim.ads_snap_fov_scale));
    const double fov_transition_ms =
        std::max(1.0f, config.ai_aim.ads_snap_fov_transition_ms);
    const double target_x_position = 72.0;
    const double target_y_position = -12.0;
    double reticle_x_position = 0.0;
    double reticle_y_position = 0.0;
    int settle_hold = 0;

    std::vector<double> residual_errors;
    std::vector<double> residual_x_errors;
    std::vector<double> residual_y_errors;
    residual_errors.reserve(static_cast<std::size_t>(total_ticks));
    residual_x_errors.reserve(static_cast<std::size_t>(total_ticks));
    residual_y_errors.reserve(static_cast<std::size_t>(total_ticks));

    double simulated_now = 1.0;
    controller_native::NativeGamepadController controller(
        config,
        [&simulated_now]() { return simulated_now; });
    for (int tick = 0; tick < total_ticks; ++tick) {
        simulated_now = 1.0 + (static_cast<double>(tick) * kDtSeconds);
        const double expected_dx = target_x_position - reticle_x_position;
        const double expected_dy = target_y_position - reticle_y_position;
        const bool vision_tick = tick % kVisionIntervalTicks == 0;
        if (vision_tick) {
            controller.submit_vision_state(
                target_state(
                    static_cast<float>(expected_dx),
                    static_cast<float>(expected_dy),
                    simulated_now));
            ++metrics.ads_settle_vision_samples;
        }

        controller.build_output(aiming_state(0.0f, 0.0f));
        const controller_native::NativeControllerOutputComponents& components =
            controller.last_output_components();
        add_frame_sample(metrics, components);

        reticle_x_position += components.final_stick.x * reticle_speed * kDtSeconds;
        reticle_y_position += -components.final_stick.y * reticle_speed * kDtSeconds;
        const double residual_dx = target_x_position - reticle_x_position;
        const double residual_dy = target_y_position - reticle_y_position;
        const double residual_radius = std::hypot(residual_dx, residual_dy);
        residual_x_errors.push_back(residual_dx);
        residual_y_errors.push_back(residual_dy);
        residual_errors.push_back(residual_radius);
        ++metrics.ads_settle_measured_ticks;

        if (residual_radius <= kSettleThresholdPx) {
            ++settle_hold;
            if (metrics.ads_settle_settled_tick < 0 && settle_hold >= kSettleHoldTicks) {
                metrics.ads_settle_settled_tick = tick - kSettleHoldTicks + 1;
            }
        } else {
            settle_hold = 0;
        }

    }

    const OvershootStats x_overshoot =
        axis_overshoot_stats(residual_x_errors, kOvershootThresholdPx);
    const OvershootStats y_overshoot =
        axis_overshoot_stats(residual_y_errors, kOvershootThresholdPx);
    metrics.ads_settle_ticks = total_ticks;
    metrics.ads_settle_initial_dx = target_x_position;
    metrics.ads_settle_initial_dy = target_y_position;
    metrics.ads_settle_fov_start_scale = fov_start;
    metrics.ads_settle_fov_end_scale = fov_end;
    metrics.ads_settle_fov_transition_ms = fov_transition_ms;
    metrics.ads_settle_snap_window_ms = static_cast<double>(snap_window_ms);
    metrics.ads_settle_ai_force_scale = options.random_fov_ai_force_scale;
    metrics.ads_settle_mean_error_px = mean_value(residual_errors);
    metrics.ads_settle_p95_error_px =
        nearest_rank_percentile(residual_errors, 0.95);
    metrics.ads_settle_p99_error_px =
        nearest_rank_percentile(residual_errors, 0.99);
    metrics.ads_settle_overshoot_events_x = x_overshoot.count;
    metrics.ads_settle_overshoot_events_y = y_overshoot.count;
    metrics.ads_settle_overshoot_events = x_overshoot.count + y_overshoot.count;
    metrics.ads_settle_max_overshoot_x_px = x_overshoot.max_px;
    metrics.ads_settle_max_overshoot_y_px = y_overshoot.max_px;
    metrics.ads_settle_max_overshoot_px =
        std::max(x_overshoot.max_px, y_overshoot.max_px);
    metrics.ads_settle_final_error_px =
        residual_errors.empty() ? 0.0 : residual_errors.back();
    metrics.ads_settle_min_abs_x_px =
        residual_x_errors.empty() ? 0.0 : std::fabs(*std::min_element(
            residual_x_errors.begin(),
            residual_x_errors.end(),
            [](double left, double right) {
                return std::fabs(left) < std::fabs(right);
            }));
    metrics.ads_settle_min_abs_y_px =
        residual_y_errors.empty() ? 0.0 : std::fabs(*std::min_element(
            residual_y_errors.begin(),
            residual_y_errors.end(),
            [](double left, double right) {
                return std::fabs(left) < std::fabs(right);
            }));
    return metrics;
}

ScenarioMetrics run_ads_diagonal_manual_stress_100hz(
    controller_native::GamepadRuntimeConfig config,
    const std::string& name,
    bool enable_dynamics,
    bool fire_active,
    bool simulate_late_fov_occlusion,
    bool fresh_timestamp_for_late_position,
    bool simulate_err_targets,
    unsigned int err_target_seed) {
    ScenarioMetrics metrics;
    metrics.name = name;
    metrics.has_ads_manual_stress = true;

    constexpr double kControllerHz = 1000.0;
    const double kVisionHz = simulate_late_fov_occlusion ? 50.0 : 100.0;
    constexpr double kDtSeconds = 1.0 / kControllerHz;
    const int kVisionIntervalTicks =
        std::max(1, static_cast<int>(std::round(kControllerHz / kVisionHz)));
    constexpr int kTicksPerCase = 520;
    constexpr double kReticleSpeed = 1500.0;
    constexpr double kOvershootThresholdPx = 2.0;
    constexpr double kLargeOvershootThresholdPx = 50.0;
    constexpr double kVectorDeadzone = 0.015;
    constexpr double kErrTargetRecoveredThresholdPx = 20.0;
    const int kVisionLagTicks = simulate_late_fov_occlusion ? 20 : 0;
    metrics.ads_manual_stress_vision_hz = kVisionHz;
    metrics.ads_manual_stress_fov_change_events =
        simulate_late_fov_occlusion ? 2 : 0;

    struct StressCase {
        double initial_dx = 0.0;
        double initial_dy = 0.0;
        double aligned_x = 0.0;
        double aligned_y = 0.0;
        double reverse_x = 0.0;
        double reverse_y = 0.0;
    };

    const std::vector<StressCase> cases = {
        {96.0, -72.0, 0.92, 0.82, -0.72, -0.64},
        {-96.0, -72.0, -0.92, 0.82, 0.72, -0.64},
        {96.0, 72.0, 0.92, -0.82, -0.72, 0.64},
        {-96.0, 72.0, -0.92, -0.82, 0.72, 0.64},
    };
    struct VisionHistorySample {
        double dx = 0.0;
        double dy = 0.0;
        double observed_at = 0.0;
    };

    config.recoil.enabled = false;
    config.aim_assist_dynamics.enabled = enable_dynamics;
    config.ai_aim.target_max_age_ms = std::max(config.ai_aim.target_max_age_ms, 80.0f);
    config.ai_aim.target_projection_max_age_ms =
        std::max(config.ai_aim.target_projection_max_age_ms, 40.0f);

    std::vector<double> residual_errors;
    std::vector<double> final_errors;
    std::vector<double> turn_degrees;
    residual_errors.reserve(cases.size() * kTicksPerCase);
    final_errors.reserve(cases.size());
    turn_degrees.reserve(cases.size() * kTicksPerCase);

    double target_alignment_sum = 0.0;
    double manual_alignment_sum = 0.0;
    int target_alignment_samples = 0;
    int manual_alignment_samples = 0;
    int global_tick = 0;
    double fov_scale_sum = 0.0;
    std::mt19937 err_target_rng(err_target_seed);
    std::uniform_int_distribution<int> err_duration_distribution(34, 68);
    std::uniform_real_distribution<double> err_magnitude_distribution(76.0, 150.0);
    std::uniform_real_distribution<double> err_angle_distribution(
        0.0,
        6.28318530717958647692);
    std::vector<double> err_recovery_ms;
    constexpr double kUnreliableHighOutputThreshold = 0.45;
    constexpr double kManualAiIntentThreshold = 0.10;
    constexpr double kUnreliableSameDirectionAlignment = 0.35;
    constexpr double kUnreliableFightAlignment = -0.35;
    constexpr double kFreshTargetAgeMs = 45.0;
    const int acquisition_guard_ticks =
        static_cast<int>(
            std::ceil(
                (static_cast<double>(config.ai_aim.ads_snap_window_ms) + 120.0) /
                1000.0 *
                kControllerHz));
    double unreliable_final_output_sum = 0.0;
    int unreliable_final_output_samples = 0;

    for (std::size_t case_index = 0; case_index < cases.size(); ++case_index) {
        const StressCase& stress = cases[case_index];
        double simulated_now = 1.0;
        controller_native::NativeGamepadController controller(
            config,
            [&simulated_now]() { return simulated_now; });
        const double target_x_position = stress.initial_dx;
        const double target_y_position = stress.initial_dy;
        double reticle_x_position = 0.0;
        double reticle_y_position = 0.0;
        double previous_output_x = 0.0;
        double previous_output_y = 0.0;
        bool has_previous_output = false;
        double previous_reported_vision_dx = 0.0;
        bool has_previous_reported_vision_dx = false;
        std::vector<double> x_errors;
        std::vector<double> y_errors;
        std::vector<std::string> modes;
        std::vector<RandomFovSample> samples;
        std::vector<VisionHistorySample> vision_history;
        std::vector<AdsErrTargetWindow> err_windows;
        x_errors.reserve(kTicksPerCase);
        y_errors.reserve(kTicksPerCase);
        modes.reserve(kTicksPerCase);
        samples.reserve(kTicksPerCase);
        vision_history.reserve(kTicksPerCase);
        if (simulate_err_targets) {
            const std::array<std::pair<int, int>, 3> start_ranges = {{
                {72, 124},
                {186, 246},
                {318, 398},
            }};
            for (const auto& start_range : start_ranges) {
                std::uniform_int_distribution<int> start_distribution(
                    start_range.first,
                    start_range.second);
                const int start_tick = start_distribution(err_target_rng);
                const int duration_ticks = err_duration_distribution(err_target_rng);
                const double magnitude = err_magnitude_distribution(err_target_rng);
                const double angle = err_angle_distribution(err_target_rng);
                AdsErrTargetWindow window;
                window.start_tick = start_tick;
                window.end_tick =
                    std::min(kTicksPerCase - 1, start_tick + duration_ticks);
                window.offset_dx = std::cos(angle) * magnitude;
                window.offset_dy = std::sin(angle) * magnitude;
                metrics.ads_manual_stress_max_err_target_offset_px = std::max(
                    metrics.ads_manual_stress_max_err_target_offset_px,
                    ads_err_target_window_offset_radius(window));
                err_windows.push_back(window);
            }
            metrics.ads_manual_stress_err_target_windows +=
                static_cast<int>(err_windows.size());
        }

        for (int tick = 0; tick < kTicksPerCase; ++tick, ++global_tick) {
            simulated_now = 1.0 + (static_cast<double>(global_tick) * kDtSeconds);
            const double fov_scale = simulate_late_fov_occlusion
                ? ads_lag_stress_fov_scale_for_tick(tick)
                : 1.0;
            const double expected_dx = target_x_position - reticle_x_position;
            const double expected_dy = target_y_position - reticle_y_position;
            const double expected_vision_dx = expected_dx * fov_scale;
            const double expected_vision_dy = expected_dy * fov_scale;
            fov_scale_sum += fov_scale;
            vision_history.push_back(
                VisionHistorySample{expected_vision_dx, expected_vision_dy, simulated_now});
            if (tick % kVisionIntervalTicks == 0) {
                if (simulate_late_fov_occlusion &&
                    ads_lag_stress_drops_vision_tick(tick)) {
                    ++metrics.ads_manual_stress_vision_dropped_ticks;
                } else {
                    const int report_index = std::max(
                        0,
                        static_cast<int>(vision_history.size()) - 1 - kVisionLagTicks);
                    const VisionHistorySample& report = vision_history[report_index];
                    const double reported_observed_at =
                        fresh_timestamp_for_late_position
                            ? simulated_now
                            : report.observed_at;
                    double reported_dx = report.dx;
                    double reported_dy = report.dy;
                    for (const AdsErrTargetWindow& window : err_windows) {
                        if (ads_err_target_window_active(window, tick)) {
                            reported_dx += window.offset_dx;
                            reported_dy += window.offset_dy;
                            ++metrics.ads_manual_stress_err_target_samples;
                            break;
                        }
                    }
                    controller.submit_vision_state(
                        benchmark_target_state(
                            static_cast<float>(reported_dx),
                            static_cast<float>(reported_dy),
                            reported_observed_at));
                    ++metrics.ads_manual_stress_vision_samples;
                    if (kVisionLagTicks > 0) {
                        ++metrics.ads_manual_stress_delayed_submissions;
                    }
                    metrics.ads_manual_stress_max_report_age_ms = std::max(
                        metrics.ads_manual_stress_max_report_age_ms,
                        (simulated_now - reported_observed_at) * 1000.0);
                    if (has_previous_reported_vision_dx &&
                        std::fabs(reported_dx - previous_reported_vision_dx) > 18.0) {
                        ++metrics.ads_manual_stress_large_vision_jumps;
                    }
                    previous_reported_vision_dx = reported_dx;
                    has_previous_reported_vision_dx = true;
                }
            } else if (simulate_late_fov_occlusion) {
                ++metrics.ads_manual_stress_vision_dropped_ticks;
            }
            if (!simulate_late_fov_occlusion && tick % kVisionIntervalTicks == 0) {
                double reported_dx = expected_vision_dx;
                double reported_dy = expected_vision_dy;
                for (const AdsErrTargetWindow& window : err_windows) {
                    if (ads_err_target_window_active(window, tick)) {
                        reported_dx += window.offset_dx;
                        reported_dy += window.offset_dy;
                        ++metrics.ads_manual_stress_err_target_samples;
                        break;
                    }
                }
                controller.submit_vision_state(
                    benchmark_target_state(
                        static_cast<float>(reported_dx),
                        static_cast<float>(reported_dy),
                        simulated_now));
                ++metrics.ads_manual_stress_vision_samples;
                if (has_previous_reported_vision_dx &&
                    std::fabs(reported_dx - previous_reported_vision_dx) > 18.0) {
                    ++metrics.ads_manual_stress_large_vision_jumps;
                }
                previous_reported_vision_dx = reported_dx;
                has_previous_reported_vision_dx = true;
            }

            float manual_x = 0.0f;
            float manual_y = 0.0f;
            if (tick < 170) {
                manual_x = static_cast<float>(stress.aligned_x);
                manual_y = static_cast<float>(stress.aligned_y);
            } else if (tick < 260) {
                manual_x = static_cast<float>(stress.reverse_x);
                manual_y = static_cast<float>(stress.reverse_y);
            } else if (tick < 340) {
                manual_x = static_cast<float>(stress.aligned_x * 0.18);
                manual_y = static_cast<float>(stress.aligned_y * 0.18);
            }

            controller.build_output(aiming_state(manual_x, manual_y, fire_active));
            const controller_native::NativeControllerOutputComponents& components =
                controller.last_output_components();
            add_frame_sample(metrics, components);

            const double reticle_delta_x =
                static_cast<double>(components.final_stick.x) * kReticleSpeed * kDtSeconds;
            const double reticle_delta_y =
                -static_cast<double>(components.final_stick.y) * kReticleSpeed * kDtSeconds;
            reticle_x_position += reticle_delta_x;
            reticle_y_position += reticle_delta_y;
            metrics.ads_manual_stress_max_single_frame_camera_delta_px = std::max(
                metrics.ads_manual_stress_max_single_frame_camera_delta_px,
                std::hypot(reticle_delta_x, reticle_delta_y));

            const double residual_dx = target_x_position - reticle_x_position;
            const double residual_dy = target_y_position - reticle_y_position;
            const double scaled_residual_dx = residual_dx * fov_scale;
            const double scaled_residual_dy = residual_dy * fov_scale;
            const double residual_radius =
                std::hypot(scaled_residual_dx, scaled_residual_dy);
            const double output_move_x = components.final_stick.x;
            const double output_move_y = -components.final_stick.y;
            const double manual_move_x = manual_x;
            const double manual_move_y = -manual_y;
            const double ai_move_x = components.ai_aim_stick.x;
            const double ai_move_y = -components.ai_aim_stick.y;
            const double final_output_magnitude =
                vector_magnitude(output_move_x, output_move_y);
            const double manual_magnitude =
                vector_magnitude(manual_move_x, manual_move_y);
            const double ai_magnitude = vector_magnitude(ai_move_x, ai_move_y);
            bool err_target_active = false;
            for (const AdsErrTargetWindow& window : err_windows) {
                if (ads_err_target_window_active(window, tick)) {
                    err_target_active = true;
                    break;
                }
            }
            const controller_native::NativeControllerVisionState& frame_vision =
                controller.last_frame_vision_state();
            const double frame_vision_age_ms =
                frame_vision.has_target
                    ? std::max(
                          0.0,
                          (simulated_now - frame_vision.observed_at_seconds) * 1000.0)
                    : 0.0;
            const bool no_fresh_target =
                !frame_vision.has_target || frame_vision_age_ms >= kFreshTargetAgeMs;
            const bool unreliable_acquisition =
                (tick < acquisition_guard_ticks &&
                 (simulate_late_fov_occlusion || no_fresh_target)) ||
                err_target_active;
            if (unreliable_acquisition) {
                unreliable_final_output_sum += final_output_magnitude;
                ++unreliable_final_output_samples;
                metrics.ads_manual_stress_unreliable_max_final_output = std::max(
                    metrics.ads_manual_stress_unreliable_max_final_output,
                    final_output_magnitude);
                const bool high_output =
                    final_output_magnitude >= kUnreliableHighOutputThreshold;
                if (high_output) {
                    ++metrics.ads_manual_stress_unreliable_high_output_frames;
                    if (no_fresh_target) {
                        ++metrics
                              .ads_manual_stress_unreliable_no_fresh_target_high_output_frames;
                    }
                    if (err_target_active) {
                        ++metrics
                              .ads_manual_stress_unreliable_err_target_high_output_frames;
                    }
                }
                if (manual_magnitude >= kManualAiIntentThreshold &&
                    ai_magnitude >= kManualAiIntentThreshold) {
                    const double manual_ai_alignment = vector_alignment(
                        manual_move_x,
                        manual_move_y,
                        ai_move_x,
                        ai_move_y,
                        kVectorDeadzone);
                    if (manual_ai_alignment >= kUnreliableSameDirectionAlignment) {
                        ++metrics.ads_manual_stress_unreliable_same_direction_frames;
                    } else if (manual_ai_alignment <= kUnreliableFightAlignment) {
                        ++metrics.ads_manual_stress_unreliable_fight_frames;
                    }
                }
            }
            for (AdsErrTargetWindow& window : err_windows) {
                const bool err_active = ads_err_target_window_active(window, tick);
                if (err_active) {
                    window.was_active = true;
                    continue;
                }
                if (window.was_active &&
                    !window.recovery_pending &&
                    !window.recovery_recorded &&
                    tick >= window.end_tick) {
                    window.recovery_pending = true;
                    window.recovery_start_tick = tick;
                    window.peak_recovery_error_px = residual_radius;
                }
                if (window.recovery_pending) {
                    window.peak_recovery_error_px = std::max(
                        window.peak_recovery_error_px,
                        residual_radius);
                    if (residual_radius <= kErrTargetRecoveredThresholdPx) {
                        const double recovery_ms =
                            static_cast<double>(tick - window.recovery_start_tick) *
                            kDtSeconds * 1000.0;
                        err_recovery_ms.push_back(recovery_ms);
                        ++metrics.ads_manual_stress_err_target_recovered_windows;
                        metrics.ads_manual_stress_max_err_target_recovery_error_px =
                            std::max(
                                metrics.ads_manual_stress_max_err_target_recovery_error_px,
                                window.peak_recovery_error_px);
                        window.recovery_pending = false;
                        window.recovery_recorded = true;
                    }
                }
            }
            residual_errors.push_back(residual_radius);
            x_errors.push_back(scaled_residual_dx);
            y_errors.push_back(scaled_residual_dy);
            modes.push_back(controller.last_ai_aim_mode());
            ++metrics.ads_manual_stress_measured_ticks;

            target_alignment_sum += vector_alignment(
                output_move_x,
                output_move_y,
                scaled_residual_dx,
                scaled_residual_dy,
                kVectorDeadzone);
            ++target_alignment_samples;
            if (vector_magnitude(manual_move_x, manual_move_y) >= kVectorDeadzone) {
                manual_alignment_sum += vector_alignment(
                    output_move_x,
                    output_move_y,
                    manual_move_x,
                    manual_move_y,
                    kVectorDeadzone);
                ++manual_alignment_samples;
            }
            if (has_previous_output) {
                const double previous_mag =
                    vector_magnitude(previous_output_x, previous_output_y);
                const double current_mag =
                    vector_magnitude(output_move_x, output_move_y);
                if (previous_mag >= kVectorDeadzone &&
                    current_mag >= kVectorDeadzone) {
                    turn_degrees.push_back(vector_turn_degrees(
                        previous_output_x,
                        previous_output_y,
                        output_move_x,
                        output_move_y,
                        kVectorDeadzone));
                }
            }
            previous_output_x = output_move_x;
            previous_output_y = output_move_y;
            has_previous_output = true;

            RandomFovSample sample;
            sample.segment = static_cast<int>(case_index);
            sample.tick = tick;
            sample.global_tick = global_tick;
            sample.error_x = scaled_residual_dx;
            sample.error_y = scaled_residual_dy;
            sample.mode = controller.last_ai_aim_mode();
            sample.final_x = components.final_stick.x;
            sample.final_y = components.final_stick.y;
            sample.manual_x = components.manual_stick.x;
            sample.manual_y = components.manual_stick.y;
            sample.ai_aim_x = components.ai_aim_stick.x;
            sample.ai_aim_y = components.ai_aim_stick.y;
            sample.dynamics_x = components.dynamic_adjustment_stick.x;
            sample.dynamics_y = components.dynamic_adjustment_stick.y;
            sample.fov_scale = fov_scale;
            sample.expected_dx = expected_vision_dx;
            sample.expected_dy = expected_vision_dy;
            copy_frame_vision_to_sample(
                sample,
                controller.last_frame_vision_state(),
                simulated_now);
            samples.push_back(std::move(sample));
        }
        for (const AdsErrTargetWindow& window : err_windows) {
            if (window.recovery_pending) {
                metrics.ads_manual_stress_max_err_target_recovery_error_px = std::max(
                    metrics.ads_manual_stress_max_err_target_recovery_error_px,
                    window.peak_recovery_error_px);
            }
        }

        const ModeOvershootStats x_overshoot =
            axis_mode_overshoot_stats(x_errors, modes, kOvershootThresholdPx);
        const ModeOvershootStats y_overshoot =
            axis_mode_overshoot_stats(y_errors, modes, kOvershootThresholdPx);
        metrics.ads_manual_stress_overshoot_events_x += x_overshoot.count;
        metrics.ads_manual_stress_overshoot_events_y += y_overshoot.count;
        metrics.ads_manual_stress_overshoot_events +=
            x_overshoot.count + y_overshoot.count;
        metrics.ads_manual_stress_overshoot_ads_snap +=
            x_overshoot.ads_snap_count + y_overshoot.ads_snap_count;
        metrics.ads_manual_stress_overshoot_body_lock +=
            x_overshoot.body_lock_count + y_overshoot.body_lock_count;
        metrics.ads_manual_stress_overshoot_manual +=
            x_overshoot.manual_count + y_overshoot.manual_count;
        metrics.ads_manual_stress_max_overshoot_px = std::max(
            metrics.ads_manual_stress_max_overshoot_px,
            std::max(x_overshoot.max_px, y_overshoot.max_px));
        final_errors.push_back(std::hypot(x_errors.back(), y_errors.back()));

        std::vector<RandomFovOvershootEvent> x_details =
            random_fov_axis_overshoot_events(samples, false, kOvershootThresholdPx);
        std::vector<RandomFovOvershootEvent> y_details =
            random_fov_axis_overshoot_events(samples, true, kOvershootThresholdPx);
        for (const RandomFovOvershootEvent& event : x_details) {
            if (event.peak_abs_px >= kLargeOvershootThresholdPx) {
                ++metrics.ads_manual_stress_large_overshoot_events;
            }
        }
        for (const RandomFovOvershootEvent& event : y_details) {
            if (event.peak_abs_px >= kLargeOvershootThresholdPx) {
                ++metrics.ads_manual_stress_large_overshoot_events;
            }
        }
        metrics.ads_manual_stress_overshoot_details.insert(
            metrics.ads_manual_stress_overshoot_details.end(),
            x_details.begin(),
            x_details.end());
        metrics.ads_manual_stress_overshoot_details.insert(
            metrics.ads_manual_stress_overshoot_details.end(),
            y_details.begin(),
            y_details.end());
    }

    metrics.ads_manual_stress_cases = static_cast<int>(cases.size());
    metrics.ads_manual_stress_ticks_per_case = kTicksPerCase;
    metrics.ads_manual_stress_mean_error_px = mean_value(residual_errors);
    metrics.ads_manual_stress_p95_error_px =
        nearest_rank_percentile(residual_errors, 0.95);
    metrics.ads_manual_stress_p99_error_px =
        nearest_rank_percentile(residual_errors, 0.99);
    metrics.ads_manual_stress_final_error_px = mean_value(final_errors);
    metrics.ads_manual_stress_mean_err_target_recovery_ms =
        mean_value(err_recovery_ms);
    metrics.ads_manual_stress_p95_err_target_recovery_ms =
        nearest_rank_percentile(err_recovery_ms, 0.95);
    const double total_ticks =
        static_cast<double>(std::max(1, metrics.ads_manual_stress_measured_ticks));
    metrics.ads_manual_stress_mean_fov_scale = fov_scale_sum / total_ticks;
    metrics.ads_manual_stress_mean_target_alignment =
        target_alignment_samples <= 0
            ? 0.0
            : target_alignment_sum / static_cast<double>(target_alignment_samples);
    metrics.ads_manual_stress_mean_manual_alignment =
        manual_alignment_samples <= 0
            ? 0.0
            : manual_alignment_sum / static_cast<double>(manual_alignment_samples);
    metrics.ads_manual_stress_unreliable_mean_final_output =
        unreliable_final_output_samples <= 0
            ? 0.0
            : unreliable_final_output_sum /
                static_cast<double>(unreliable_final_output_samples);
    metrics.ads_manual_stress_direction_score =
        alignment_score(metrics.ads_manual_stress_mean_target_alignment);
    metrics.ads_manual_stress_manual_direction_score =
        alignment_score(metrics.ads_manual_stress_mean_manual_alignment);
    metrics.ads_manual_stress_p95_turn_degrees =
        nearest_rank_percentile(turn_degrees, 0.95);
    metrics.ads_manual_stress_turn_smoothness_score =
        turn_smoothness_score(metrics.ads_manual_stress_p95_turn_degrees);
    return metrics;
}

ScenarioMetrics run_ads_manual_carry_through_100hz(
    controller_native::GamepadRuntimeConfig config,
    const std::string& name) {
    ScenarioMetrics metrics;
    metrics.name = name;
    metrics.has_ads_carry_through = true;

    constexpr double kControllerHz = 1000.0;
    constexpr double kVisionHz = 100.0;
    constexpr double kDtSeconds = 1.0 / kControllerHz;
    constexpr int kVisionIntervalTicks = 10;
    constexpr int kTicks = 360;
    constexpr double kInitialDx = 96.0;
    constexpr double kNearTargetPx = 36.0;
    constexpr double kHighOutputThreshold = 0.40;
    constexpr double kAccelDeadzone = 0.05;
    constexpr double kOvershootThresholdPx = 2.0;

    config.recoil.enabled = false;
    config.aim_assist_dynamics.enabled = false;
    config.ai_aim.target_max_age_ms = std::max(config.ai_aim.target_max_age_ms, 100.0f);
    config.ai_aim.target_projection_max_age_ms =
        std::max(config.ai_aim.target_projection_max_age_ms, 80.0f);
    config.ai_aim.ads_snap_window_ms =
        std::max(config.ai_aim.ads_snap_window_ms, 180);
    config.ai_aim.body_lock_activation_box_px =
        std::max(config.ai_aim.body_lock_activation_box_px, 190.0f);
    config.ai_aim.body_lock_box_tolerance_px =
        std::max(config.ai_aim.body_lock_box_tolerance_px, 28.0f);
    config.ai_aim.body_lock_confidence_frames =
        std::max(config.ai_aim.body_lock_confidence_frames, 1);
    config.ai_aim.body_lock_smoothing =
        std::min(config.ai_aim.body_lock_smoothing, 0.10f);
    config.ai_aim.body_lock_max_ai_force =
        std::max(config.ai_aim.body_lock_max_ai_force, 0.72f);
    config.ai_aim.body_lock_opposing_boost_max_ai_force =
        std::max(config.ai_aim.body_lock_opposing_boost_max_ai_force, 0.78f);
    config.ai_aim.body_lock_max_ai_force_y =
        std::max(config.ai_aim.body_lock_max_ai_force_y, 0.76f);

    const double reticle_speed = std::max(
        1.0f,
        config.ai_aim.target_projection_reticle_speed_px_per_sec);
    metrics.ads_carry_through_initial_dx = kInitialDx;
    metrics.ads_carry_through_snap_window_ms =
        static_cast<double>(config.ai_aim.ads_snap_window_ms);

    double simulated_now = 1.0;
    controller_native::NativeGamepadController controller(
        config,
        [&simulated_now]() { return simulated_now; });

    double reticle_x_position = 0.0;
    std::vector<double> residual_errors;
    std::vector<double> x_errors;
    std::vector<std::string> modes;
    residual_errors.reserve(kTicks);
    x_errors.reserve(kTicks);
    modes.reserve(kTicks);

    int previous_error_sign = 0;
    bool has_previous_error_sign = false;
    bool after_first_crossing = false;

    for (int tick = 0; tick < kTicks; ++tick) {
        simulated_now = 1.0 + (static_cast<double>(tick) * kDtSeconds);
        const double expected_dx = kInitialDx - reticle_x_position;
        if (tick % kVisionIntervalTicks == 0) {
            controller.submit_vision_state(benchmark_target_state(
                static_cast<float>(expected_dx),
                0.0f,
                simulated_now));
            ++metrics.ads_carry_through_vision_samples;
        }

        float manual_x = 0.0f;
        if (tick < 170) {
            manual_x = 0.92f;
        } else if (tick < 260) {
            manual_x = 0.62f;
        } else if (tick < 320) {
            manual_x = -0.34f;
        }

        controller.build_output(aiming_state(manual_x, 0.0f, false));
        const controller_native::NativeControllerOutputComponents& components =
            controller.last_output_components();
        add_frame_sample(metrics, components);

        const std::string mode = controller.last_ai_aim_mode();
        if (mode == "ads_snap") {
            ++metrics.ads_carry_through_ads_snap_frames;
        } else if (mode == "body_lock") {
            ++metrics.ads_carry_through_body_lock_frames;
        } else {
            ++metrics.ads_carry_through_manual_frames;
        }

        const int manual_sign =
            direction(static_cast<double>(components.manual_stick.x), kAccelDeadzone);
        const int ai_sign =
            direction(static_cast<double>(components.ai_aim_stick.x), kAccelDeadzone);
        if (manual_sign != 0 && manual_sign == ai_sign) {
            ++metrics.ads_carry_through_same_direction_accel_frames;
        }
        if (components.ads_brake_active) {
            ++metrics.ads_carry_through_brake_active_frames;
        }
        metrics.ads_carry_through_max_brake_error_px = std::max(
            metrics.ads_carry_through_max_brake_error_px,
            std::hypot(
                static_cast<double>(components.ads_brake_error_px.x),
                static_cast<double>(components.ads_brake_error_px.y)));

        const double reticle_delta_x =
            static_cast<double>(components.final_stick.x) *
            reticle_speed *
            kDtSeconds;
        reticle_x_position += reticle_delta_x;
        metrics.ads_manual_stress_max_single_frame_camera_delta_px = std::max(
            metrics.ads_manual_stress_max_single_frame_camera_delta_px,
            std::fabs(reticle_delta_x));

        const double residual_dx = kInitialDx - reticle_x_position;
        const double abs_residual_dx = std::fabs(residual_dx);
        residual_errors.push_back(abs_residual_dx);
        x_errors.push_back(residual_dx);
        modes.push_back(mode);

        if (abs_residual_dx <= kNearTargetPx) {
            ++metrics.ads_carry_through_near_target_frames;
            metrics.ads_carry_through_max_near_final_x = std::max(
                metrics.ads_carry_through_max_near_final_x,
                std::fabs(static_cast<double>(components.final_stick.x)));
            if (std::fabs(static_cast<double>(components.final_stick.x)) >=
                kHighOutputThreshold) {
                ++metrics.ads_carry_through_near_high_output_frames;
                if (!components.ads_brake_active) {
                    ++metrics.ads_carry_through_brake_inactive_near_high_frames;
                }
            }
        }

        const int current_error_sign = signum(residual_dx);
        if (has_previous_error_sign &&
            previous_error_sign != 0 &&
            current_error_sign != 0 &&
            current_error_sign != previous_error_sign) {
            ++metrics.ads_carry_through_sign_flip_events;
            after_first_crossing = true;
        }
        if (after_first_crossing) {
            metrics.ads_carry_through_max_overshoot_px = std::max(
                metrics.ads_carry_through_max_overshoot_px,
                abs_residual_dx);
        }
        previous_error_sign = current_error_sign;
        has_previous_error_sign = current_error_sign != 0;
    }

    const ModeOvershootStats x_overshoot =
        axis_mode_overshoot_stats(x_errors, modes, kOvershootThresholdPx);
    metrics.ads_carry_through_max_overshoot_px = std::max(
        metrics.ads_carry_through_max_overshoot_px,
        x_overshoot.max_px);
    metrics.ads_carry_through_ticks = kTicks;
    metrics.ads_carry_through_mean_error_px = mean_value(residual_errors);
    metrics.ads_carry_through_p95_error_px =
        nearest_rank_percentile(residual_errors, 0.95);
    metrics.ads_carry_through_final_error_px =
        residual_errors.empty() ? 0.0 : residual_errors.back();
    return metrics;
}

ScenarioMetrics run_ads_bodylock_near_high_output_100hz(
    controller_native::GamepadRuntimeConfig config,
    const std::string& name) {
    ScenarioMetrics metrics;
    metrics.name = name;
    metrics.has_ads_bodylock_near_high = true;

    constexpr double kControllerHz = 1000.0;
    constexpr double kDtSeconds = 1.0 / kControllerHz;
    constexpr int kVisionIntervalTicks = 10;
    constexpr int kTicks = 720;
    constexpr double kNearTargetPx = 80.0;
    constexpr double kHighOutputThreshold = 0.45;
    constexpr double kOutputSpikeDelta = 0.30;
    constexpr double kVectorDeadzone = 0.015;
    constexpr double kCloseAssistMinPx = 18.0;
    constexpr double kCloseAssistMaxPx = 80.0;
    constexpr double kCloseAssistLowOutputThreshold = 0.08;
    constexpr double kCenteredPx = 16.0;
    constexpr double kCenteredJitterDelta = 0.12;

    config.recoil.enabled = false;
    config.aim_assist_dynamics.enabled = true;
    config.ai_aim.target_max_age_ms = std::max(config.ai_aim.target_max_age_ms, 160.0f);
    config.ai_aim.target_projection_max_age_ms =
        std::max(config.ai_aim.target_projection_max_age_ms, 180.0f);
    config.ai_aim.ads_snap_window_ms = std::max(config.ai_aim.ads_snap_window_ms, 180);
    config.ai_aim.body_lock_box_tolerance_px =
        std::max(config.ai_aim.body_lock_box_tolerance_px, 28.0f);
    config.ai_aim.body_lock_activation_box_px =
        std::max(config.ai_aim.body_lock_activation_box_px, 190.0f);
    config.ai_aim.body_lock_confidence_frames =
        std::max(config.ai_aim.body_lock_confidence_frames, 1);

    metrics.ads_bodylock_near_high_initial_dx = 76.0;
    metrics.ads_bodylock_near_high_snap_window_ms =
        static_cast<double>(config.ai_aim.ads_snap_window_ms);

    double simulated_now = 1.0;
    controller_native::NativeGamepadController controller(
        config,
        [&simulated_now]() { return simulated_now; });

    std::vector<double> errors;
    std::vector<double> output_deltas;
    std::vector<double> turn_degrees;
    std::vector<double> centered_output_deltas;
    errors.reserve(kTicks);
    output_deltas.reserve(kTicks);
    turn_degrees.reserve(kTicks);
    centered_output_deltas.reserve(kTicks);
    double close_assist_output_sum = 0.0;

    double previous_output_x = 0.0;
    double previous_output_y = 0.0;
    bool has_previous_output = false;
    int previous_x_sign = 0;
    int previous_y_sign = 0;
    const int acquisition_expired_tick =
        static_cast<int>(
            std::ceil(
                (static_cast<double>(config.ai_aim.ads_snap_window_ms) + 120.0) /
                1000.0 *
                kControllerHz));

    for (int tick = 0; tick < kTicks; ++tick) {
        simulated_now = 1.0 + (static_cast<double>(tick) * kDtSeconds);
        const double seconds = static_cast<double>(tick) * kDtSeconds;
        const double wave = std::sin(seconds * 19.0);
        const double fast_wave = std::sin(seconds * 47.0);
        const double reported_dx =
            tick < 160 ? 76.0 - (0.10 * static_cast<double>(tick))
                       : 64.0 + (12.0 * wave);
        const double reported_dy =
            tick < 160 ? -24.0 + (0.05 * static_cast<double>(tick))
                       : -18.0 + (10.0 * fast_wave);

        if (tick % kVisionIntervalTicks == 0) {
            controller.submit_vision_state(benchmark_target_state(
                static_cast<float>(reported_dx),
                static_cast<float>(reported_dy),
                simulated_now));
            ++metrics.ads_bodylock_near_high_vision_samples;
        }

        const float manual_x = tick >= 360 && tick < 520
            ? static_cast<float>(clamp_double(0.18 * std::sin(seconds * 9.0), -0.24, 0.24))
            : 0.0f;
        const float manual_y = tick >= 360 && tick < 520
            ? static_cast<float>(clamp_double(-0.14 * std::cos(seconds * 11.0), -0.20, 0.20))
            : 0.0f;
        controller.build_output(aiming_state(manual_x, manual_y, false));
        const controller_native::NativeControllerOutputComponents& components =
            controller.last_output_components();
        add_frame_sample(metrics, components);

        const std::string mode = controller.last_ai_aim_mode();
        if (mode == "ads_snap") {
            ++metrics.ads_bodylock_near_high_ads_snap_frames;
        } else if (mode == "body_lock") {
            ++metrics.ads_bodylock_near_high_body_lock_frames;
        } else if (mode == "manual") {
            ++metrics.ads_bodylock_near_high_manual_frames;
        }

        const double error_radius = std::hypot(reported_dx, reported_dy);
        errors.push_back(error_radius);
        const double output_x = components.final_stick.x;
        const double output_y = -components.final_stick.y;
        const double output_magnitude = vector_magnitude(output_x, output_y);
        metrics.ads_bodylock_near_high_max_final_output = std::max(
            metrics.ads_bodylock_near_high_max_final_output,
            output_magnitude);

        if (error_radius <= kNearTargetPx) {
            ++metrics.ads_bodylock_near_high_near_target_frames;
            if (output_magnitude >= kHighOutputThreshold) {
                ++metrics.ads_bodylock_near_high_output_frames;
                if (components.ads_brake_active) {
                    ++metrics.ads_bodylock_near_high_brake_active_frames;
                } else {
                    ++metrics.ads_bodylock_near_high_brake_inactive_frames;
                }
                if (tick >= acquisition_expired_tick &&
                    mode == "body_lock" &&
                    !components.ads_brake_active) {
                    ++metrics.ads_bodylock_near_high_acquisition_expired_high_frames;
                }
            }
        }
        const bool close_assist_band =
            error_radius > kCloseAssistMinPx && error_radius <= kCloseAssistMaxPx;
        if (close_assist_band && mode == "body_lock") {
            ++metrics.ads_bodylock_near_high_close_assist_frames;
            close_assist_output_sum += output_magnitude;
        }
        if (close_assist_band &&
            (mode != "body_lock" ||
             output_magnitude < kCloseAssistLowOutputThreshold)) {
            ++metrics.ads_bodylock_near_high_low_output_close_frames;
        }
        if (error_radius <= kCenteredPx) {
            ++metrics.ads_bodylock_near_high_centered_frames;
        }

        if (has_previous_output) {
            const double previous_magnitude =
                vector_magnitude(previous_output_x, previous_output_y);
            if (previous_magnitude >= kVectorDeadzone &&
                output_magnitude >= kVectorDeadzone) {
                const double output_delta = vector_magnitude(
                    output_x - previous_output_x,
                    output_y - previous_output_y);
                output_deltas.push_back(output_delta);
                if (error_radius <= kCenteredPx) {
                    centered_output_deltas.push_back(output_delta);
                    if (output_delta >= kCenteredJitterDelta) {
                        ++metrics.ads_bodylock_near_high_centered_jitter_frames;
                    }
                }
                if (output_delta > kOutputSpikeDelta) {
                    ++metrics.ads_bodylock_near_high_output_spikes;
                }
                turn_degrees.push_back(vector_turn_degrees(
                    previous_output_x,
                    previous_output_y,
                    output_x,
                    output_y,
                    kVectorDeadzone));
                const int current_x_sign = signum(output_x);
                const int current_y_sign = signum(output_y);
                if ((previous_x_sign != 0 && current_x_sign != 0 &&
                     previous_x_sign != current_x_sign) ||
                    (previous_y_sign != 0 && current_y_sign != 0 &&
                     previous_y_sign != current_y_sign)) {
                    ++metrics.ads_bodylock_near_high_chatter_events;
                }
                previous_x_sign = current_x_sign;
                previous_y_sign = current_y_sign;
            }
        } else {
            previous_x_sign = signum(output_x);
            previous_y_sign = signum(output_y);
        }
        previous_output_x = output_x;
        previous_output_y = output_y;
        has_previous_output = true;
    }

    metrics.ads_bodylock_near_high_ticks = kTicks;
    metrics.ads_bodylock_near_high_mean_error_px = mean_value(errors);
    metrics.ads_bodylock_near_high_p95_error_px =
        nearest_rank_percentile(errors, 0.95);
    metrics.ads_bodylock_near_high_final_error_px =
        errors.empty() ? 0.0 : errors.back();
    metrics.ads_bodylock_near_high_p95_output_delta =
        nearest_rank_percentile(output_deltas, 0.95);
    metrics.ads_bodylock_near_high_centered_p95_output_delta =
        nearest_rank_percentile(centered_output_deltas, 0.95);
    metrics.ads_bodylock_near_high_p95_turn_degrees =
        nearest_rank_percentile(turn_degrees, 0.95);
    metrics.ads_bodylock_near_high_turn_smoothness_score =
        turn_smoothness_score(metrics.ads_bodylock_near_high_p95_turn_degrees);
    metrics.ads_bodylock_near_high_close_assist_mean_output =
        metrics.ads_bodylock_near_high_close_assist_frames <= 0
            ? 0.0
            : close_assist_output_sum /
                static_cast<double>(metrics.ads_bodylock_near_high_close_assist_frames);
    return metrics;
}

ScenarioMetrics run_adversarial_controller_authority_100hz(
    controller_native::GamepadRuntimeConfig config,
    const std::string& name) {
    ScenarioMetrics metrics;
    metrics.name = name;
    metrics.has_adversarial_controller = true;

    constexpr double kControllerHz = 1000.0;
    constexpr double kDtSeconds = 1.0 / kControllerHz;
    constexpr int kVisionIntervalTicks = 10;
    constexpr int kTicks = 720;
    constexpr double kHighOutputThreshold = 0.45;
    constexpr double kFightAlignment = -0.25;
    constexpr double kStaleMs = 80.0;
    constexpr double kVectorDeadzone = 0.015;

    config.recoil.enabled = false;
    config.aim_assist_dynamics.enabled = true;
    config.ai_aim.target_max_age_ms = std::max(config.ai_aim.target_max_age_ms, 220.0f);
    config.ai_aim.target_projection_max_age_ms =
        std::max(config.ai_aim.target_projection_max_age_ms, 260.0f);
    config.ai_aim.ads_snap_window_ms = std::max(config.ai_aim.ads_snap_window_ms, 180);
    config.ai_aim.body_lock_confidence_frames =
        std::max(config.ai_aim.body_lock_confidence_frames, 1);

    double simulated_now = 1.0;
    controller_native::NativeGamepadController controller(
        config,
        [&simulated_now]() { return simulated_now; });

    std::vector<double> intended_errors;
    std::vector<double> manual_ai_alignments;
    intended_errors.reserve(kTicks);
    manual_ai_alignments.reserve(kTicks);

    int last_submit_tick = -1;
    for (int tick = 0; tick < kTicks; ++tick) {
        simulated_now = 1.0 + (static_cast<double>(tick) * kDtSeconds);

        double intended_dx = -42.0;
        double intended_dy = 18.0;
        double submitted_dx = intended_dx;
        double submitted_dy = intended_dy;
        float manual_x = -0.32f;
        float manual_y = 0.14f;
        bool submit_vision = (tick % kVisionIntervalTicks) == 0;
        bool wrong_target_phase = false;
        bool invalid_strong_phase = false;
        bool err_target_phase = false;
        bool recovery_phase = false;
        const char* tier = "observed_strong";

        if (tick < 120) {
            intended_dx = -78.0 + (0.24 * static_cast<double>(tick));
            intended_dy = 30.0 - (0.06 * static_cast<double>(tick));
            submitted_dx = intended_dx;
            submitted_dy = intended_dy;
            manual_x = -0.30f;
            manual_y = 0.12f;
        } else if (tick < 260) {
            intended_dx = -38.0;
            intended_dy = 20.0;
            submitted_dx = 68.0;
            submitted_dy = -28.0;
            manual_x = -0.58f;
            manual_y = 0.22f;
            wrong_target_phase = true;
        } else if (tick < 400) {
            intended_dx = 34.0 + (0.12 * static_cast<double>(tick - 260));
            intended_dy = 10.0;
            submitted_dx = 34.0;
            submitted_dy = 10.0;
            manual_x = 0.48f;
            manual_y = 0.08f;
            submit_vision = tick == 260;
        } else if (tick < 520) {
            intended_dx = 64.0;
            intended_dy = 16.0;
            submitted_dx = -26.0;
            submitted_dy = -10.0;
            manual_x = 0.58f;
            manual_y = 0.14f;
            invalid_strong_phase = true;
            tier = "suspected_corpse";
        } else if (tick < 620) {
            intended_dx = -32.0;
            intended_dy = 18.0;
            submitted_dx = 82.0;
            submitted_dy = -52.0;
            manual_x = -0.54f;
            manual_y = 0.24f;
            err_target_phase = true;
        } else {
            intended_dx = -28.0;
            intended_dy = 14.0;
            submitted_dx = intended_dx;
            submitted_dy = intended_dy;
            manual_x = -0.34f;
            manual_y = 0.12f;
            recovery_phase = true;
        }

        if (submit_vision) {
            controller_native::NativeControllerVisionState state =
                benchmark_target_state(
                    static_cast<float>(submitted_dx),
                    static_cast<float>(submitted_dy),
                    simulated_now);
            state.target_tier = tier;
            state.aim_authority = true;
            state.fire_authority = !invalid_strong_phase;
            controller.submit_vision_state(state);
            last_submit_tick = tick;
            ++metrics.adversarial_controller_vision_samples;
        }

        controller.build_output(aiming_state(manual_x, manual_y, false));
        const controller_native::NativeControllerOutputComponents& components =
            controller.last_output_components();
        const controller_native::NativeControllerVisionState& frame_state =
            controller.last_frame_vision_state();
        add_frame_sample(metrics, components);

        const double final_output = vector_magnitude(
            components.final_stick.x,
            components.final_stick.y);
        metrics.adversarial_controller_max_final_output = std::max(
            metrics.adversarial_controller_max_final_output,
            final_output);

        const double intended_error = std::hypot(intended_dx, intended_dy);
        intended_errors.push_back(intended_error);
        if (intended_error <= 60.0 && final_output >= kHighOutputThreshold) {
            ++metrics.adversarial_controller_near_high_output_frames;
        }
        if (frame_state.has_tracker_projection) {
            ++metrics.adversarial_controller_projected_frames;
        }

        const double manual_ai_alignment = vector_alignment(
            components.manual_stick.x,
            components.manual_stick.y,
            components.ai_aim_stick.x,
            components.ai_aim_stick.y,
            kVectorDeadzone);
        manual_ai_alignments.push_back(manual_ai_alignment);
        const double manual_final_alignment = vector_alignment(
            components.manual_stick.x,
            components.manual_stick.y,
            components.final_stick.x,
            components.final_stick.y,
            kVectorDeadzone);
        if (manual_ai_alignment <= kFightAlignment ||
            manual_final_alignment <= kFightAlignment) {
            ++metrics.adversarial_controller_user_fight_frames;
        }

        if (wrong_target_phase) {
            ++metrics.adversarial_controller_wrong_target_frames;
        }
        if (invalid_strong_phase && final_output >= 0.25) {
            ++metrics.adversarial_controller_invalid_strong_frames;
        }
        const double stale_age_ms = last_submit_tick >= 0
            ? static_cast<double>(tick - last_submit_tick) * kDtSeconds * 1000.0
            : 0.0;
        metrics.adversarial_controller_max_stale_age_ms = std::max(
            metrics.adversarial_controller_max_stale_age_ms,
            stale_age_ms);
        if (stale_age_ms >= kStaleMs && final_output >= 0.25) {
            ++metrics.adversarial_controller_stale_high_output_frames;
        }
        if (err_target_phase && final_output >= 0.25) {
            ++metrics.adversarial_controller_err_target_frames;
        }
        if (recovery_phase) {
            ++metrics.adversarial_controller_recovery_frames;
        }
    }

    metrics.adversarial_controller_ticks = kTicks;
    metrics.adversarial_controller_p95_error_px =
        nearest_rank_percentile(intended_errors, 0.95);
    metrics.adversarial_controller_mean_manual_ai_alignment =
        mean_value(manual_ai_alignments);
    return metrics;
}

ScenarioMetrics run_ads_bodylock_moving_chase_100hz(
    controller_native::GamepadRuntimeConfig config,
    unsigned int seed,
    const std::string& name,
    bool enable_dynamics,
    bool fire_active,
    BodylockChaseMotionProfile motion_profile,
    bool occlusion_gap) {
    ScenarioMetrics metrics;
    metrics.name = name;
    metrics.has_ads_manual_stress = true;

    constexpr double kControllerHz = 1000.0;
    constexpr double kVisionHz = 100.0;
    constexpr double kDtSeconds = 1.0 / kControllerHz;
    constexpr int kVisionIntervalTicks = 10;
    constexpr int kTicksPerCase = 680;
    constexpr double kReticleSpeed = 1500.0;
    constexpr double kOvershootThresholdPx = 2.0;
    constexpr double kLargeOvershootThresholdPx = 50.0;
    constexpr double kVectorDeadzone = 0.015;
    constexpr double kBodyLockSpikeDelta = 0.30;
    constexpr double kBodyLockSustainErrorPx = 45.0;
    constexpr int kBodyLockSustainRequiredTicks = 180;
    constexpr int kSlideStartTick = 210;
    constexpr int kSlideEndTick = 410;
    constexpr int kOcclusionStartTick = 285;
    constexpr int kOcclusionEndTick = 405;
    constexpr double kRecoverThresholdPx = 22.0;
    constexpr double kBodyLockCloseAssistMinPx = 18.0;
    constexpr double kBodyLockCloseAssistMaxPx = 72.0;
    constexpr double kBodyLockCloseAssistLowOutput = 0.06;
    constexpr double kBodyLockCenteredPx = 14.0;
    constexpr double kBodyLockCenteredJitterDelta = 0.10;
    constexpr double kPi = 3.14159265358979323846;

    struct MovingCase {
        double start_dx = 0.0;
        double start_dy = 0.0;
        double velocity_x = 0.0;
        double velocity_y = 0.0;
        double wave_x = 0.0;
        double wave_y = 0.0;
    };

    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> start_x_distribution(82.0, 132.0);
    std::uniform_real_distribution<double> start_y_distribution(-42.0, 34.0);
    std::uniform_real_distribution<double> speed_distribution(130.0, 240.0);
    std::uniform_real_distribution<double> vertical_distribution(-35.0, 55.0);
    std::uniform_real_distribution<double> wave_distribution(8.0, 22.0);
    std::vector<MovingCase> cases;
    cases.reserve(4);
    for (int index = 0; index < 4; ++index) {
        const double side = index % 2 == 0 ? 1.0 : -1.0;
        MovingCase moving;
        moving.start_dx = side * start_x_distribution(rng);
        moving.start_dy = start_y_distribution(rng);
        moving.velocity_x = -side * speed_distribution(rng);
        moving.velocity_y = vertical_distribution(rng);
        moving.wave_x = side * wave_distribution(rng);
        moving.wave_y = wave_distribution(rng);
        cases.push_back(moving);
    }

    config.recoil.enabled = false;
    config.aim_assist_dynamics.enabled = enable_dynamics;
    config.ai_aim.target_max_age_ms = std::max(config.ai_aim.target_max_age_ms, 160.0f);
    config.ai_aim.target_projection_max_age_ms =
        std::max(config.ai_aim.target_projection_max_age_ms, 180.0f);
    config.ai_aim.ads_snap_window_ms = std::max(config.ai_aim.ads_snap_window_ms, 180);
    config.ai_aim.body_lock_box_tolerance_px =
        std::max(config.ai_aim.body_lock_box_tolerance_px, 28.0f);
    config.ai_aim.body_lock_activation_box_px =
        std::max(config.ai_aim.body_lock_activation_box_px, 190.0f);
    config.ai_aim.body_lock_confidence_frames =
        std::max(config.ai_aim.body_lock_confidence_frames, 1);

    const auto smooth_step = [](double value) {
        const double t = std::max(0.0, std::min(1.0, value));
        return t * t * (3.0 - (2.0 * t));
    };
    const auto moving_body_state = [](
        float dx,
        float dy,
        double now,
        float body_width,
        float body_height) {
        controller_native::NativeControllerVisionState state =
            target_state(dx, dy, now);
        const float ratio = 0.40f;
        state.body_x1 = state.target_x - (body_width * 0.5f);
        state.body_x2 = state.target_x + (body_width * 0.5f);
        state.body_y1 = state.target_y - (body_height * ratio);
        state.body_y2 = state.body_y1 + body_height;
        return state;
    };

    std::vector<double> residual_errors;
    std::vector<double> final_errors;
    std::vector<double> turn_degrees;
    std::vector<double> body_lock_turn_degrees;
    std::vector<double> body_lock_output_deltas;
    std::vector<double> body_lock_centered_output_deltas;
    std::vector<double> slide_down_lags;
    std::vector<double> slide_recovery_ms;
    std::vector<double> body_lock_tracking_errors;
    residual_errors.reserve(cases.size() * kTicksPerCase);
    final_errors.reserve(cases.size());
    turn_degrees.reserve(cases.size() * kTicksPerCase);
    body_lock_turn_degrees.reserve(cases.size() * kTicksPerCase);
    body_lock_output_deltas.reserve(cases.size() * kTicksPerCase);
    body_lock_centered_output_deltas.reserve(cases.size() * kTicksPerCase);
    slide_down_lags.reserve(cases.size() * (kSlideEndTick - kSlideStartTick));
    body_lock_tracking_errors.reserve(cases.size() * kTicksPerCase);

    double target_alignment_sum = 0.0;
    double manual_alignment_sum = 0.0;
    double body_lock_target_alignment_sum = 0.0;
    int target_alignment_samples = 0;
    int manual_alignment_samples = 0;
    int body_lock_target_alignment_samples = 0;
    int global_tick = 0;
    double fov_scale_sum = 0.0;
    double body_lock_close_assist_ai_output_sum = 0.0;
    int longest_body_lock_sustain_ticks = 0;

    for (std::size_t case_index = 0; case_index < cases.size(); ++case_index) {
        const MovingCase& moving = cases[case_index];
        double simulated_now = 1.0;
        controller_native::NativeGamepadController controller(
            config,
            [&simulated_now]() { return simulated_now; });
        double reticle_x_position = 0.0;
        double reticle_y_position = 0.0;
        double previous_output_x = 0.0;
        double previous_output_y = 0.0;
        bool has_previous_output = false;
        double previous_body_lock_output_x = 0.0;
        double previous_body_lock_output_y = 0.0;
        bool has_previous_body_lock_output = false;
        double previous_reported_dx = 0.0;
        double previous_reported_dy = 0.0;
        bool has_previous_report = false;
        bool recover_pending = false;
        int recover_start_tick = -1;
        int current_body_lock_sustain_ticks = 0;
        int case_longest_body_lock_sustain_ticks = 0;
        bool case_has_body_lock = false;
        bool case_sustain_passed = false;

        std::vector<double> x_errors;
        std::vector<double> y_errors;
        std::vector<std::string> modes;
        std::vector<RandomFovSample> samples;
        x_errors.reserve(kTicksPerCase);
        y_errors.reserve(kTicksPerCase);
        modes.reserve(kTicksPerCase);
        samples.reserve(kTicksPerCase);

        for (int tick = 0; tick < kTicksPerCase; ++tick, ++global_tick) {
            simulated_now = 1.0 + (static_cast<double>(global_tick) * kDtSeconds);
            const double seconds = static_cast<double>(tick) * kDtSeconds;
            const bool slide_motion =
                motion_profile == BodylockChaseMotionProfile::Slide;
            const bool crouch_cycle_motion =
                motion_profile == BodylockChaseMotionProfile::CrouchCycle;
            const bool jump_motion =
                motion_profile == BodylockChaseMotionProfile::Jump ||
                motion_profile == BodylockChaseMotionProfile::ArcJump;
            const bool arc_jump_motion =
                motion_profile == BodylockChaseMotionProfile::ArcJump;
            const double slide_ratio = slide_motion
                ? smooth_step(
                    static_cast<double>(tick - kSlideStartTick) /
                    static_cast<double>(kSlideEndTick - kSlideStartTick))
                : 0.0;
            const double slide_release_ratio = slide_motion
                ? smooth_step(
                    static_cast<double>(tick - kSlideEndTick) /
                    static_cast<double>(kTicksPerCase - kSlideEndTick))
                : 0.0;
            const double slide_shape = slide_ratio * (1.0 - (0.35 * slide_release_ratio));
            const double crouch_shape = crouch_cycle_motion
                ? 0.5 * (1.0 - std::cos(seconds * kPi * 8.0))
                : 0.0;
            const double jump_window = jump_motion
                ? std::max(
                    0.0,
                    std::min(
                        1.0,
                        static_cast<double>(tick - 155) / 320.0))
                : 0.0;
            const double jump_shape =
                jump_motion ? std::sin(jump_window * kPi) : 0.0;
            const double slide_dir = moving.velocity_x >= 0.0 ? 1.0 : -1.0;
            const double target_x_position =
                moving.start_dx +
                (moving.velocity_x * seconds) +
                (moving.wave_x * std::sin(seconds * 10.0)) +
                (slide_motion ? slide_dir * 72.0 * slide_shape : 0.0) +
                (arc_jump_motion ? slide_dir * 58.0 * jump_shape : 0.0);
            const double target_y_position =
                moving.start_dy +
                (moving.velocity_y * seconds) +
                (moving.wave_y * std::sin(seconds * 7.0 + 0.6)) +
                (slide_motion ? 92.0 * slide_shape : 0.0) +
                (crouch_cycle_motion ? 45.0 * crouch_shape : 0.0) -
                (jump_motion ? 95.0 * jump_shape : 0.0);
            const double pose_compress = std::max(slide_shape, crouch_shape);
            const double body_height = slide_motion
                ? lerp(180.0, 82.0, slide_shape)
                : (crouch_cycle_motion ? lerp(180.0, 112.0, pose_compress) : 180.0);
            const double body_width = slide_motion
                ? lerp(84.0, 112.0, slide_shape)
                : (crouch_cycle_motion ? lerp(84.0, 98.0, pose_compress) : 84.0);
            const double expected_dx = target_x_position - reticle_x_position;
            const double expected_dy = target_y_position - reticle_y_position;
            fov_scale_sum += 1.0;

            const bool in_occlusion =
                occlusion_gap &&
                tick >= kOcclusionStartTick &&
                tick < kOcclusionEndTick;
            if (in_occlusion) {
                ++metrics.ads_manual_stress_vision_dropped_ticks;
            }
            if (tick % kVisionIntervalTicks == 0 && !in_occlusion) {
                controller.submit_vision_state(
                    moving_body_state(
                        static_cast<float>(expected_dx),
                        static_cast<float>(expected_dy),
                        simulated_now,
                        static_cast<float>(body_width),
                        static_cast<float>(body_height)));
                ++metrics.ads_manual_stress_vision_samples;
                if (has_previous_report &&
                    std::hypot(
                        expected_dx - previous_reported_dx,
                        expected_dy - previous_reported_dy) > 18.0) {
                    ++metrics.ads_manual_stress_large_vision_jumps;
                }
                previous_reported_dx = expected_dx;
                previous_reported_dy = expected_dy;
                has_previous_report = true;
            }

            const double desired_move_x =
                (expected_dx * 0.0045) + (moving.velocity_x / 900.0);
            const double desired_move_y =
                (expected_dy * 0.0040) +
                ((moving.velocity_y +
                     (slide_motion ? 180.0 * slide_shape : 0.0) -
                     (jump_motion ? 130.0 * jump_shape : 0.0)) /
                    950.0);
            float manual_x = static_cast<float>(clamp_double(desired_move_x, -0.42, 0.42));
            float manual_y = static_cast<float>(clamp_double(-desired_move_y, -0.42, 0.42));
            if (tick < 70) {
                manual_x *= 0.35f;
                manual_y *= 0.35f;
            }
            if (occlusion_gap && in_occlusion) {
                manual_x *= 0.70f;
                manual_y *= 0.70f;
            }

            controller.build_output(aiming_state(manual_x, manual_y, fire_active));
            const controller_native::NativeControllerOutputComponents& components =
                controller.last_output_components();
            add_frame_sample(metrics, components);
            const std::string mode = controller.last_ai_aim_mode();
            if (mode == "body_lock") {
                ++metrics.ads_manual_stress_body_lock_frames;
            }

            const double reticle_delta_x =
                static_cast<double>(components.final_stick.x) * kReticleSpeed * kDtSeconds;
            const double reticle_delta_y =
                -static_cast<double>(components.final_stick.y) * kReticleSpeed * kDtSeconds;
            reticle_x_position += reticle_delta_x;
            reticle_y_position += reticle_delta_y;
            metrics.ads_manual_stress_max_single_frame_camera_delta_px = std::max(
                metrics.ads_manual_stress_max_single_frame_camera_delta_px,
                std::hypot(reticle_delta_x, reticle_delta_y));

            const double residual_dx = target_x_position - reticle_x_position;
            const double residual_dy = target_y_position - reticle_y_position;
            const double residual_radius = std::hypot(residual_dx, residual_dy);
            residual_errors.push_back(residual_radius);
            x_errors.push_back(residual_dx);
            y_errors.push_back(residual_dy);
            modes.push_back(mode);
            ++metrics.ads_manual_stress_measured_ticks;
            if (in_occlusion) {
                metrics.ads_manual_stress_occlusion_peak_error_px = std::max(
                    metrics.ads_manual_stress_occlusion_peak_error_px,
                    residual_radius);
            }
            if (slide_motion && tick >= kSlideStartTick && tick < kSlideEndTick) {
                slide_down_lags.push_back(std::max(0.0, residual_dy));
            }
            if (occlusion_gap && tick == kOcclusionEndTick) {
                recover_pending = true;
                recover_start_tick = tick;
            }
            if (recover_pending && residual_radius <= kRecoverThresholdPx) {
                slide_recovery_ms.push_back(
                    static_cast<double>(tick - recover_start_tick) * kDtSeconds * 1000.0);
                recover_pending = false;
            }

            const double output_move_x = components.final_stick.x;
            const double output_move_y = -components.final_stick.y;
            const double manual_move_x = manual_x;
            const double manual_move_y = -manual_y;
            target_alignment_sum += vector_alignment(
                output_move_x,
                output_move_y,
                residual_dx,
                residual_dy,
                kVectorDeadzone);
            ++target_alignment_samples;
            if (vector_magnitude(manual_move_x, manual_move_y) >= kVectorDeadzone) {
                manual_alignment_sum += vector_alignment(
                    output_move_x,
                    output_move_y,
                    manual_move_x,
                    manual_move_y,
                    kVectorDeadzone);
                ++manual_alignment_samples;
            }
            if (mode == "body_lock") {
                body_lock_tracking_errors.push_back(residual_radius);
                case_has_body_lock = true;
                body_lock_target_alignment_sum += vector_alignment(
                    output_move_x,
                    output_move_y,
                    residual_dx,
                    residual_dy,
                    kVectorDeadzone);
                ++body_lock_target_alignment_samples;
                bool body_lock_output_spiked = false;
                bool has_body_lock_output_delta = false;
                double body_lock_output_delta = 0.0;
                if (has_previous_body_lock_output) {
                    const double previous_body_lock_mag = vector_magnitude(
                        previous_body_lock_output_x,
                        previous_body_lock_output_y);
                    const double current_body_lock_mag =
                        vector_magnitude(output_move_x, output_move_y);
                    if (previous_body_lock_mag >= kVectorDeadzone &&
                        current_body_lock_mag >= kVectorDeadzone) {
                        body_lock_turn_degrees.push_back(vector_turn_degrees(
                            previous_body_lock_output_x,
                            previous_body_lock_output_y,
                            output_move_x,
                            output_move_y,
                            kVectorDeadzone));
                        const double output_delta = vector_magnitude(
                            output_move_x - previous_body_lock_output_x,
                            output_move_y - previous_body_lock_output_y);
                        body_lock_output_delta = output_delta;
                        has_body_lock_output_delta = true;
                        body_lock_output_deltas.push_back(output_delta);
                        if (output_delta > kBodyLockSpikeDelta) {
                            ++metrics.ads_manual_stress_body_lock_output_spikes;
                            body_lock_output_spiked = true;
                        }
                        const int previous_x_sign =
                            signum(previous_body_lock_output_x);
                        const int current_x_sign = signum(output_move_x);
                        const int previous_y_sign =
                            signum(previous_body_lock_output_y);
                        const int current_y_sign = signum(output_move_y);
                        if ((previous_x_sign != 0 && current_x_sign != 0 &&
                             previous_x_sign != current_x_sign) ||
                            (previous_y_sign != 0 && current_y_sign != 0 &&
                             previous_y_sign != current_y_sign)) {
                            ++metrics.ads_manual_stress_body_lock_chatter_events;
                        }
                    }
                }
                const double body_lock_ai_output = vector_magnitude(
                    components.ai_aim_stick.x,
                    components.ai_aim_stick.y);
                const bool close_assist_band =
                    residual_radius > kBodyLockCloseAssistMinPx &&
                    residual_radius <= kBodyLockCloseAssistMaxPx;
                if (close_assist_band) {
                    ++metrics.ads_manual_stress_body_lock_close_assist_samples;
                    body_lock_close_assist_ai_output_sum += body_lock_ai_output;
                    if (body_lock_ai_output < kBodyLockCloseAssistLowOutput) {
                        ++metrics.ads_manual_stress_body_lock_low_output_close_frames;
                    }
                }
                if (residual_radius <= kBodyLockCenteredPx) {
                    ++metrics.ads_manual_stress_body_lock_centered_samples;
                    if (has_body_lock_output_delta) {
                        body_lock_centered_output_deltas.push_back(body_lock_output_delta);
                        if (body_lock_output_delta >= kBodyLockCenteredJitterDelta) {
                            ++metrics
                                  .ads_manual_stress_body_lock_centered_jitter_frames;
                        }
                    }
                }
                if (residual_radius <= kBodyLockSustainErrorPx &&
                    !body_lock_output_spiked) {
                    ++current_body_lock_sustain_ticks;
                    ++metrics.ads_manual_stress_body_lock_sustain_good_frames;
                    case_longest_body_lock_sustain_ticks = std::max(
                        case_longest_body_lock_sustain_ticks,
                        current_body_lock_sustain_ticks);
                    if (current_body_lock_sustain_ticks >=
                        kBodyLockSustainRequiredTicks) {
                        case_sustain_passed = true;
                    }
                } else {
                    current_body_lock_sustain_ticks = 0;
                }
                previous_body_lock_output_x = output_move_x;
                previous_body_lock_output_y = output_move_y;
                has_previous_body_lock_output = true;
            } else {
                const bool close_assist_band =
                    residual_radius > kBodyLockCloseAssistMinPx &&
                    residual_radius <= kBodyLockCloseAssistMaxPx;
                if (close_assist_band && tick >= 70) {
                    ++metrics.ads_manual_stress_body_lock_dropout_frames;
                    ++metrics.ads_manual_stress_body_lock_low_output_close_frames;
                }
                current_body_lock_sustain_ticks = 0;
                has_previous_body_lock_output = false;
            }
            if (has_previous_output) {
                const double previous_mag =
                    vector_magnitude(previous_output_x, previous_output_y);
                const double current_mag =
                    vector_magnitude(output_move_x, output_move_y);
                if (previous_mag >= kVectorDeadzone &&
                    current_mag >= kVectorDeadzone) {
                    turn_degrees.push_back(vector_turn_degrees(
                        previous_output_x,
                        previous_output_y,
                        output_move_x,
                        output_move_y,
                        kVectorDeadzone));
                }
            }
            previous_output_x = output_move_x;
            previous_output_y = output_move_y;
            has_previous_output = true;

            RandomFovSample sample;
            sample.segment = static_cast<int>(case_index);
            sample.tick = tick;
            sample.global_tick = global_tick;
            sample.error_x = residual_dx;
            sample.error_y = residual_dy;
            sample.mode = mode;
            sample.final_x = components.final_stick.x;
            sample.final_y = components.final_stick.y;
            sample.manual_x = components.manual_stick.x;
            sample.manual_y = components.manual_stick.y;
            sample.ai_aim_x = components.ai_aim_stick.x;
            sample.ai_aim_y = components.ai_aim_stick.y;
            sample.dynamics_x = components.dynamic_adjustment_stick.x;
            sample.dynamics_y = components.dynamic_adjustment_stick.y;
            sample.fov_scale = 1.0;
            sample.expected_dx = expected_dx;
            sample.expected_dy = expected_dy;
            sample.target_speed_px_per_sec = std::hypot(
                moving.velocity_x +
                    (slide_motion ? slide_dir * 360.0 * slide_shape : 0.0) +
                    (arc_jump_motion ? slide_dir * 180.0 * jump_shape : 0.0),
                moving.velocity_y +
                    (slide_motion ? 300.0 * slide_shape : 0.0) -
                    (jump_motion ? 260.0 * jump_shape : 0.0));
            sample.heading_deg = std::atan2(
                moving.velocity_y +
                    (slide_motion ? 300.0 * slide_shape : 0.0) -
                    (jump_motion ? 260.0 * jump_shape : 0.0),
                moving.velocity_x +
                    (slide_motion ? slide_dir * 360.0 * slide_shape : 0.0) +
                    (arc_jump_motion ? slide_dir * 180.0 * jump_shape : 0.0)) *
                180.0 / kPi;
            copy_frame_vision_to_sample(
                sample,
                controller.last_frame_vision_state(),
                simulated_now);
            samples.push_back(std::move(sample));
        }

        const ModeOvershootStats x_overshoot =
            axis_mode_overshoot_stats(x_errors, modes, kOvershootThresholdPx);
        const ModeOvershootStats y_overshoot =
            axis_mode_overshoot_stats(y_errors, modes, kOvershootThresholdPx);
        metrics.ads_manual_stress_overshoot_events_x += x_overshoot.count;
        metrics.ads_manual_stress_overshoot_events_y += y_overshoot.count;
        metrics.ads_manual_stress_overshoot_events +=
            x_overshoot.count + y_overshoot.count;
        metrics.ads_manual_stress_overshoot_ads_snap +=
            x_overshoot.ads_snap_count + y_overshoot.ads_snap_count;
        metrics.ads_manual_stress_overshoot_body_lock +=
            x_overshoot.body_lock_count + y_overshoot.body_lock_count;
        metrics.ads_manual_stress_overshoot_manual +=
            x_overshoot.manual_count + y_overshoot.manual_count;
        metrics.ads_manual_stress_max_overshoot_px = std::max(
            metrics.ads_manual_stress_max_overshoot_px,
            std::max(x_overshoot.max_px, y_overshoot.max_px));
        final_errors.push_back(std::hypot(x_errors.back(), y_errors.back()));

        std::vector<RandomFovOvershootEvent> x_details =
            random_fov_axis_overshoot_events(samples, false, kOvershootThresholdPx);
        std::vector<RandomFovOvershootEvent> y_details =
            random_fov_axis_overshoot_events(samples, true, kOvershootThresholdPx);
        for (const RandomFovOvershootEvent& event : x_details) {
            if (event.peak_abs_px >= kLargeOvershootThresholdPx) {
                ++metrics.ads_manual_stress_large_overshoot_events;
            }
        }
        for (const RandomFovOvershootEvent& event : y_details) {
            if (event.peak_abs_px >= kLargeOvershootThresholdPx) {
                ++metrics.ads_manual_stress_large_overshoot_events;
            }
        }
        metrics.ads_manual_stress_overshoot_details.insert(
            metrics.ads_manual_stress_overshoot_details.end(),
            x_details.begin(),
            x_details.end());
        metrics.ads_manual_stress_overshoot_details.insert(
            metrics.ads_manual_stress_overshoot_details.end(),
            y_details.begin(),
            y_details.end());
        if (case_has_body_lock) {
            ++metrics.ads_manual_stress_body_lock_sustain_cases;
            if (case_sustain_passed) {
                ++metrics.ads_manual_stress_body_lock_sustain_passes;
            }
            longest_body_lock_sustain_ticks = std::max(
                longest_body_lock_sustain_ticks,
                case_longest_body_lock_sustain_ticks);
        }
    }

    metrics.ads_manual_stress_cases = static_cast<int>(cases.size());
    metrics.ads_manual_stress_ticks_per_case = kTicksPerCase;
    metrics.ads_manual_stress_vision_hz = kVisionHz;
    metrics.ads_manual_stress_mean_error_px = mean_value(residual_errors);
    metrics.ads_manual_stress_p95_error_px =
        nearest_rank_percentile(residual_errors, 0.95);
    metrics.ads_manual_stress_p99_error_px =
        nearest_rank_percentile(residual_errors, 0.99);
    metrics.ads_manual_stress_final_error_px = mean_value(final_errors);
    const double total_ticks =
        static_cast<double>(std::max(1, metrics.ads_manual_stress_measured_ticks));
    metrics.ads_manual_stress_mean_fov_scale = fov_scale_sum / total_ticks;
    metrics.ads_manual_stress_body_lock_ratio =
        static_cast<double>(metrics.ads_manual_stress_body_lock_frames) / total_ticks;
    metrics.ads_manual_stress_body_lock_tracking_samples =
        static_cast<int>(body_lock_tracking_errors.size());
    metrics.ads_manual_stress_body_lock_mean_error_px =
        mean_value(body_lock_tracking_errors);
    metrics.ads_manual_stress_body_lock_p95_error_px =
        nearest_rank_percentile(body_lock_tracking_errors, 0.95);
    metrics.ads_manual_stress_body_lock_direction_score =
        body_lock_target_alignment_samples <= 0
            ? 0.0
            : alignment_score(
                body_lock_target_alignment_sum /
                static_cast<double>(body_lock_target_alignment_samples));
    metrics.ads_manual_stress_body_lock_turn_samples =
        static_cast<int>(body_lock_turn_degrees.size());
    metrics.ads_manual_stress_body_lock_p95_turn_degrees =
        nearest_rank_percentile(body_lock_turn_degrees, 0.95);
    metrics.ads_manual_stress_body_lock_turn_smoothness_score =
        turn_smoothness_score(
            metrics.ads_manual_stress_body_lock_p95_turn_degrees);
    metrics.ads_manual_stress_body_lock_p95_output_delta =
        nearest_rank_percentile(body_lock_output_deltas, 0.95);
    metrics.ads_manual_stress_body_lock_close_assist_mean_ai_output =
        metrics.ads_manual_stress_body_lock_close_assist_samples <= 0
            ? 0.0
            : body_lock_close_assist_ai_output_sum /
                static_cast<double>(
                    metrics.ads_manual_stress_body_lock_close_assist_samples);
    metrics.ads_manual_stress_body_lock_centered_p95_output_delta =
        nearest_rank_percentile(body_lock_centered_output_deltas, 0.95);
    metrics.ads_manual_stress_body_lock_sustain_required_ms =
        static_cast<double>(kBodyLockSustainRequiredTicks) * kDtSeconds * 1000.0;
    metrics.ads_manual_stress_body_lock_sustain_error_px =
        kBodyLockSustainErrorPx;
    metrics.ads_manual_stress_body_lock_sustain_longest_ms =
        static_cast<double>(longest_body_lock_sustain_ticks) * kDtSeconds * 1000.0;
    metrics.ads_manual_stress_body_lock_sustain_pass_rate = rate(
        metrics.ads_manual_stress_body_lock_sustain_passes,
        metrics.ads_manual_stress_body_lock_sustain_cases);
    metrics.ads_manual_stress_body_lock_sustain_score =
        metrics.ads_manual_stress_body_lock_sustain_pass_rate * 100.0;
    metrics.ads_manual_stress_slide_down_lag_p95_px =
        nearest_rank_percentile(slide_down_lags, 0.95);
    metrics.ads_manual_stress_slide_recover_ms =
        nearest_rank_percentile(slide_recovery_ms, 0.95);
    metrics.ads_manual_stress_mean_target_alignment =
        target_alignment_samples <= 0
            ? 0.0
            : target_alignment_sum / static_cast<double>(target_alignment_samples);
    metrics.ads_manual_stress_mean_manual_alignment =
        manual_alignment_samples <= 0
            ? 0.0
            : manual_alignment_sum / static_cast<double>(manual_alignment_samples);
    metrics.ads_manual_stress_direction_score =
        alignment_score(metrics.ads_manual_stress_mean_target_alignment);
    metrics.ads_manual_stress_manual_direction_score =
        alignment_score(metrics.ads_manual_stress_mean_manual_alignment);
    metrics.ads_manual_stress_p95_turn_degrees =
        nearest_rank_percentile(turn_degrees, 0.95);
    metrics.ads_manual_stress_turn_smoothness_score =
        turn_smoothness_score(metrics.ads_manual_stress_p95_turn_degrees);
    return metrics;
}

std::string escape_json(const std::string& value) {
    std::ostringstream out;
    for (const char ch : value) {
        switch (ch) {
        case '\\':
            out << "\\\\";
            break;
        case '"':
            out << "\\\"";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\r':
            out << "\\r";
            break;
        case '\t':
            out << "\\t";
            break;
        default:
            out << ch;
            break;
        }
    }
    return out.str();
}

void write_axis_json(std::ostream& out, const AxisStats& stats, int frames, const char* indent) {
    out
        << indent << "\"manual_frames\": " << stats.manual_frames << ",\n"
        << indent << "\"final_opposes_manual_rate\": " << rate(stats.final_opposes_manual, stats.manual_frames) << ",\n"
        << indent << "\"final_blocks_manual_rate\": " << rate(stats.final_blocks_manual, stats.manual_frames) << ",\n"
        << indent << "\"mean_manual_preservation_ratio\": " << mean_ratio(stats) << ",\n"
        << indent << "\"recoil_opposes_manual_rate\": " << rate(stats.recoil_opposes_manual, stats.manual_frames) << ",\n"
        << indent << "\"mean_abs_recoil\": " << mean_abs_recoil(stats, frames) << ",\n"
        << indent << "\"mean_abs_final\": " << mean_abs_final(stats, frames) << "\n";
}

void write_random_fov_overshoot_details_json(
    std::ostream& out,
    const std::vector<RandomFovOvershootEvent>& events,
    const char* indent) {
    out << indent << "[";
    if (!events.empty()) {
        out << "\n";
    }
    for (std::size_t index = 0; index < events.size(); ++index) {
        const RandomFovOvershootEvent& event = events[index];
        out
            << indent << "  {\n"
            << indent << "    \"axis\": \"" << escape_json(event.axis) << "\",\n"
            << indent << "    \"segment\": " << event.segment << ",\n"
            << indent << "    \"crossing_tick\": " << event.crossing_tick << ",\n"
            << indent << "    \"peak_tick\": " << event.peak_tick << ",\n"
            << indent << "    \"crossing_global_tick\": " << event.crossing_global_tick << ",\n"
            << indent << "    \"peak_global_tick\": " << event.peak_global_tick << ",\n"
            << indent << "    \"peak_mode\": \"" << escape_json(event.peak_mode) << "\",\n"
            << indent << "    \"previous_error\": " << event.previous_error << ",\n"
            << indent << "    \"crossing_error\": " << event.crossing_error << ",\n"
            << indent << "    \"peak_error\": " << event.peak_error << ",\n"
            << indent << "    \"peak_abs_px\": " << event.peak_abs_px << ",\n"
            << indent << "    \"final_x\": " << event.final_x << ",\n"
            << indent << "    \"final_y\": " << event.final_y << ",\n"
            << indent << "    \"manual_x\": " << event.manual_x << ",\n"
            << indent << "    \"manual_y\": " << event.manual_y << ",\n"
            << indent << "    \"ai_aim_x\": " << event.ai_aim_x << ",\n"
            << indent << "    \"ai_aim_y\": " << event.ai_aim_y << ",\n"
            << indent << "    \"dynamics_x\": " << event.dynamics_x << ",\n"
            << indent << "    \"dynamics_y\": " << event.dynamics_y << ",\n"
            << indent << "    \"fov_scale\": " << event.fov_scale << ",\n"
            << indent << "    \"expected_dx\": " << event.expected_dx << ",\n"
            << indent << "    \"expected_dy\": " << event.expected_dy << ",\n"
            << indent << "    \"vision_has_target\": "
            << (event.vision_has_target ? "true" : "false") << ",\n"
            << indent << "    \"vision_aim_authority\": "
            << (event.vision_aim_authority ? "true" : "false") << ",\n"
            << indent << "    \"vision_fire_authority\": "
            << (event.vision_fire_authority ? "true" : "false") << ",\n"
            << indent << "    \"vision_target_tier\": \""
            << escape_json(event.vision_target_tier) << "\",\n"
            << indent << "    \"vision_dx\": " << event.vision_dx << ",\n"
            << indent << "    \"vision_dy\": " << event.vision_dy << ",\n"
            << indent << "    \"vision_has_tracker_projection\": "
            << (event.vision_has_tracker_projection ? "true" : "false") << ",\n"
            << indent << "    \"vision_tracker_dx\": " << event.vision_tracker_dx << ",\n"
            << indent << "    \"vision_tracker_dy\": " << event.vision_tracker_dy << ",\n"
            << indent << "    \"vision_age_ms\": " << event.vision_age_ms << ",\n"
            << indent << "    \"target_speed_px_per_sec\": " << event.target_speed_px_per_sec << ",\n"
            << indent << "    \"heading_deg\": " << event.heading_deg << "\n"
            << indent << "  }" << (index + 1 == events.size() ? "\n" : ",\n");
    }
    out << indent << "]";
}

void write_random_fov_turn_details_json(
    std::ostream& out,
    const std::vector<RandomFovTurnEvent>& events,
    const char* indent) {
    out << indent << "[";
    if (!events.empty()) {
        out << "\n";
    }
    for (std::size_t index = 0; index < events.size(); ++index) {
        const RandomFovTurnEvent& event = events[index];
        out
            << indent << "  {\n"
            << indent << "    \"segment\": " << event.segment << ",\n"
            << indent << "    \"tick\": " << event.tick << ",\n"
            << indent << "    \"global_tick\": " << event.global_tick << ",\n"
            << indent << "    \"mode\": \"" << escape_json(event.mode) << "\",\n"
            << indent << "    \"turn_degrees\": " << event.turn_degrees << ",\n"
            << indent << "    \"output_delta\": " << event.output_delta << ",\n"
            << indent << "    \"previous_output_x\": " << event.previous_output_x << ",\n"
            << indent << "    \"previous_output_y\": " << event.previous_output_y << ",\n"
            << indent << "    \"output_x\": " << event.output_x << ",\n"
            << indent << "    \"output_y\": " << event.output_y << ",\n"
            << indent << "    \"manual_x\": " << event.manual_x << ",\n"
            << indent << "    \"manual_y\": " << event.manual_y << ",\n"
            << indent << "    \"ai_aim_x\": " << event.ai_aim_x << ",\n"
            << indent << "    \"ai_aim_y\": " << event.ai_aim_y << ",\n"
            << indent << "    \"dynamics_x\": " << event.dynamics_x << ",\n"
            << indent << "    \"dynamics_y\": " << event.dynamics_y << ",\n"
            << indent << "    \"fov_scale\": " << event.fov_scale << ",\n"
            << indent << "    \"expected_dx\": " << event.expected_dx << ",\n"
            << indent << "    \"expected_dy\": " << event.expected_dy << ",\n"
            << indent << "    \"target_speed_px_per_sec\": " << event.target_speed_px_per_sec << ",\n"
            << indent << "    \"heading_deg\": " << event.heading_deg << "\n"
            << indent << "  }" << (index + 1 == events.size() ? "\n" : ",\n");
    }
    out << indent << "]";
}

void write_json(
    const CliOptions& options,
    const controller_native::RuntimeConfig& config,
    const std::vector<ScenarioMetrics>& scenarios) {
    const std::filesystem::path parent = options.output_path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    std::ofstream out(options.output_path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("failed to open output: " + options.output_path.string());
    }
    out << std::fixed << std::setprecision(6);
    out
        << "{\n"
        << "  \"run_key\": \"" << escape_json(options.run_key) << "\",\n"
        << "  \"suite\": \"" << escape_json(options.suite) << "\",\n"
        << "  \"config_path\": \"" << escape_json(options.config_path.string()) << "\",\n"
        << "  \"recoil_state_override\": \"" << escape_json(options.recoil_state_path.string()) << "\",\n"
        << "  \"frames_per_case\": " << options.frames << ",\n"
        << "  \"dt_ms\": " << options.dt_ms << ",\n"
        << "  \"random_fov_ticks\": " << options.random_fov_ticks << ",\n"
        << "  \"random_fov_seed\": " << options.random_fov_seed << ",\n"
        << "  \"selector_intent_seed\": " << options.selector_intent_seed << ",\n"
        << "  \"roi_fallback_seed\": " << options.roi_fallback_seed << ",\n"
        << "  \"random_fov_ai_force_scale\": " << options.random_fov_ai_force_scale << ",\n"
        << "  \"metadata\": {\n"
        << "    \"offline\": true,\n"
        << "    \"uses_capture\": false,\n"
        << "    \"uses_vigem\": false,\n"
        << "    \"recoil_recognizer_state_path\": \""
        << escape_json(config.gamepad.recoil.recognizer_state_path) << "\",\n"
        << "    \"recoil_profile_directory\": \""
        << escape_json(config.gamepad.recoil.profile_directory) << "\",\n"
        << "    \"recoil_profile_amount\": " << config.gamepad.recoil.profile_amount << ",\n"
        << "    \"recoil_profile_x_amount\": " << config.gamepad.recoil.profile_x_amount << ",\n"
        << "    \"recoil_feedback_amount\": " << config.gamepad.recoil.feedback_amount << "\n"
        << "  },\n"
        << "  \"scenarios\": [\n";
    for (std::size_t index = 0; index < scenarios.size(); ++index) {
        const ScenarioMetrics& scenario = scenarios[index];
        out
            << "    {\n"
            << "      \"name\": \"" << escape_json(scenario.name) << "\",\n"
            << "      \"frames\": " << scenario.frames << ",\n"
            << "      \"blocked_frames\": " << scenario.blocked_frames << ",\n"
            << "      \"recovery_frames\": " << scenario.recovery_frames << ",\n"
            << "      \"x\": {\n";
        write_axis_json(out, scenario.x, scenario.frames, "        ");
        out
            << "      },\n"
            << "      \"y\": {\n";
        write_axis_json(out, scenario.y, scenario.frames, "        ");
        out
            << "      }";
        if (scenario.has_random_fov) {
            out
                << ",\n"
                << "      \"random_fov\": {\n"
                << "        \"controller_hz\": 1000.000000,\n"
                << "        \"vision_hz\": 100.000000,\n"
                << "        \"vision_samples\": " << scenario.random_fov_vision_samples << ",\n"
                << "        \"tracker_only_ticks\": " << scenario.random_fov_tracker_only_ticks << ",\n"
                << "        \"measured_ticks\": " << scenario.random_fov_measured_ticks << ",\n"
                << "        \"fov_change_events\": " << scenario.random_fov_change_events << ",\n"
                << "        \"large_vision_jumps\": " << scenario.random_fov_large_vision_jumps << ",\n"
                << "        \"fov_min_scale\": " << scenario.random_fov_min_scale << ",\n"
                << "        \"fov_max_scale\": " << scenario.random_fov_max_scale << ",\n"
                << "        \"ai_force_scale\": " << scenario.random_fov_ai_force_scale << ",\n"
                << "        \"mean_fov_scale\": " << scenario.random_fov_mean_scale << ",\n"
                << "        \"mean_abs_expected_dx\": " << scenario.random_fov_mean_abs_expected_dx << ",\n"
                << "        \"mean_abs_final_x\": " << scenario.random_fov_mean_abs_final_x << ",\n"
                << "        \"mean_abs_ai_aim_x\": " << scenario.random_fov_mean_abs_ai_aim_x << ",\n"
                << "        \"output_per_100px_error\": " << scenario.random_fov_output_per_100px_error << ",\n"
                << "        \"mean_error_px\": " << scenario.random_fov_mean_error_px << ",\n"
                << "        \"p95_error_px\": " << scenario.random_fov_p95_error_px << ",\n"
                << "        \"p99_error_px\": " << scenario.random_fov_p99_error_px << ",\n"
                << "        \"direction_samples\": " << scenario.random_fov_direction_samples << ",\n"
                << "        \"manual_direction_samples\": "
                << scenario.random_fov_manual_direction_samples << ",\n"
                << "        \"turn_samples\": " << scenario.random_fov_turn_samples << ",\n"
                << "        \"mean_target_alignment\": "
                << scenario.random_fov_mean_target_alignment << ",\n"
                << "        \"mean_manual_alignment\": "
                << scenario.random_fov_mean_manual_alignment << ",\n"
                << "        \"direction_score\": " << scenario.random_fov_direction_score << ",\n"
                << "        \"manual_direction_score\": "
                << scenario.random_fov_manual_direction_score << ",\n"
                << "        \"mean_turn_degrees\": "
                << scenario.random_fov_mean_turn_degrees << ",\n"
                << "        \"p95_turn_degrees\": "
                << scenario.random_fov_p95_turn_degrees << ",\n"
                << "        \"turn_smoothness_score\": "
                << scenario.random_fov_turn_smoothness_score << ",\n"
                << "        \"mean_output_delta\": "
                << scenario.random_fov_mean_output_delta << ",\n"
                << "        \"p95_output_delta\": "
                << scenario.random_fov_p95_output_delta << ",\n"
                << "        \"turn_details\": ";
            write_random_fov_turn_details_json(
                out,
                scenario.random_fov_turn_details,
                "        ");
            out
                << ",\n"
                << "        \"overshoot_events\": " << scenario.random_fov_overshoot_events << ",\n"
                << "        \"overshoot_events_x\": " << scenario.random_fov_overshoot_events_x << ",\n"
                << "        \"overshoot_events_y\": " << scenario.random_fov_overshoot_events_y << ",\n"
                << "        \"overshoot_ads_snap\": " << scenario.random_fov_overshoot_ads_snap << ",\n"
                << "        \"overshoot_body_lock\": " << scenario.random_fov_overshoot_body_lock << ",\n"
                << "        \"overshoot_manual\": " << scenario.random_fov_overshoot_manual << ",\n"
                << "        \"max_overshoot_px\": " << scenario.random_fov_max_overshoot_px << ",\n"
                << "        \"overshoot_details\": ";
            write_random_fov_overshoot_details_json(
                out,
                scenario.random_fov_overshoot_details,
                "        ");
            out
                << ",\n"
                << "        \"fresh_wrong_direction_rate\": "
                << rate(
                    scenario.random_fov_fresh_wrong_direction,
                    scenario.random_fov_vision_samples) << ",\n"
                << "        \"tracker_wrong_direction_rate\": "
                << rate(
                    scenario.random_fov_tracker_wrong_direction,
                    scenario.random_fov_tracker_only_ticks) << ",\n"
                << "        \"fresh_insensitive_rate\": "
                << rate(
                    scenario.random_fov_fresh_insensitive,
                    scenario.random_fov_vision_samples) << ",\n"
                << "        \"tracker_insensitive_rate\": "
                << rate(
                    scenario.random_fov_tracker_insensitive,
                    scenario.random_fov_tracker_only_ticks) << "\n"
                << "      }";
        }
        if (scenario.has_ads_settle) {
            out
                << ",\n"
                << "      \"ads_settle\": {\n"
                << "        \"controller_hz\": 1000.000000,\n"
                << "        \"vision_hz\": 100.000000,\n"
                << "        \"ticks\": " << scenario.ads_settle_ticks << ",\n"
                << "        \"measured_ticks\": " << scenario.ads_settle_measured_ticks << ",\n"
                << "        \"vision_samples\": " << scenario.ads_settle_vision_samples << ",\n"
                << "        \"initial_dx\": " << scenario.ads_settle_initial_dx << ",\n"
                << "        \"initial_dy\": " << scenario.ads_settle_initial_dy << ",\n"
                << "        \"fov_start_scale\": " << scenario.ads_settle_fov_start_scale << ",\n"
                << "        \"fov_end_scale\": " << scenario.ads_settle_fov_end_scale << ",\n"
                << "        \"fov_transition_ms\": " << scenario.ads_settle_fov_transition_ms << ",\n"
                << "        \"snap_window_ms\": " << scenario.ads_settle_snap_window_ms << ",\n"
                << "        \"ai_force_scale\": " << scenario.ads_settle_ai_force_scale << ",\n"
                << "        \"settled_tick\": " << scenario.ads_settle_settled_tick << ",\n"
                << "        \"mean_error_px\": " << scenario.ads_settle_mean_error_px << ",\n"
                << "        \"p95_error_px\": " << scenario.ads_settle_p95_error_px << ",\n"
                << "        \"p99_error_px\": " << scenario.ads_settle_p99_error_px << ",\n"
                << "        \"final_error_px\": " << scenario.ads_settle_final_error_px << ",\n"
                << "        \"min_abs_x_px\": " << scenario.ads_settle_min_abs_x_px << ",\n"
                << "        \"min_abs_y_px\": " << scenario.ads_settle_min_abs_y_px << ",\n"
                << "        \"overshoot_events\": " << scenario.ads_settle_overshoot_events << ",\n"
                << "        \"overshoot_events_x\": " << scenario.ads_settle_overshoot_events_x << ",\n"
                << "        \"overshoot_events_y\": " << scenario.ads_settle_overshoot_events_y << ",\n"
                << "        \"max_overshoot_px\": " << scenario.ads_settle_max_overshoot_px << ",\n"
                << "        \"max_overshoot_x_px\": " << scenario.ads_settle_max_overshoot_x_px << ",\n"
                << "        \"max_overshoot_y_px\": " << scenario.ads_settle_max_overshoot_y_px << "\n"
                << "      }";
        }
        if (scenario.has_ads_parity) {
            out
                << ",\n"
                << "      \"ads_python_parity\": {\n"
                << "        \"controller_hz\": 60.000000,\n"
                << "        \"target_sample_hz\": " << scenario.ads_parity_target_sample_hz << ",\n"
                << "        \"frame_dt_ms\": " << scenario.ads_parity_frame_dt_ms << ",\n"
                << "        \"cases\": " << scenario.ads_parity_cases << ",\n"
                << "        \"frames_per_case\": " << scenario.ads_parity_frames_per_case << ",\n"
                << "        \"overshoot_cases\": " << scenario.ads_parity_overshoot_cases << ",\n"
                << "        \"overshoot_events\": " << scenario.ads_parity_overshoot_events << ",\n"
                << "        \"overshoot_events_x\": " << scenario.ads_parity_overshoot_events_x << ",\n"
                << "        \"overshoot_events_y\": " << scenario.ads_parity_overshoot_events_y << ",\n"
                << "        \"crossing_events\": " << scenario.ads_parity_crossing_events << ",\n"
                << "        \"ads_snap_crossing_events\": "
                << scenario.ads_parity_ads_snap_crossing_events << ",\n"
                << "        \"body_lock_crossing_events\": "
                << scenario.ads_parity_body_lock_crossing_events << ",\n"
                << "        \"manual_crossing_events\": "
                << scenario.ads_parity_manual_crossing_events << ",\n"
                << "        \"ads_snap_overshoot_events\": "
                << scenario.ads_parity_ads_snap_overshoot_events << ",\n"
                << "        \"body_lock_overshoot_events\": "
                << scenario.ads_parity_body_lock_overshoot_events << ",\n"
                << "        \"manual_overshoot_events\": "
                << scenario.ads_parity_manual_overshoot_events << ",\n"
                << "        \"overshoot_events_none\": "
                << scenario.ads_parity_overshoot_events_none << ",\n"
                << "        \"overshoot_events_aligned_follow\": "
                << scenario.ads_parity_overshoot_events_aligned_follow << ",\n"
                << "        \"overshoot_events_opposing_burst\": "
                << scenario.ads_parity_overshoot_events_opposing_burst << ",\n"
                << "        \"overshoot_events_overshoot_recover\": "
                << scenario.ads_parity_overshoot_events_overshoot_recover << ",\n"
                << "        \"overshoot_recover_cases\": "
                << scenario.ads_parity_overshoot_recover_cases << ",\n"
                << "        \"overshoot_recover_events\": "
                << scenario.ads_parity_overshoot_recover_events << ",\n"
                << "        \"under_20_cases\": " << scenario.ads_parity_under_20_cases << ",\n"
                << "        \"mean_time_to_under_20_ms\": "
                << scenario.ads_parity_mean_time_to_under_20_ms << ",\n"
                << "        \"mean_error_px\": " << scenario.ads_parity_mean_error_px << ",\n"
                << "        \"p95_error_px\": " << scenario.ads_parity_p95_error_px << ",\n"
                << "        \"p99_error_px\": " << scenario.ads_parity_p99_error_px << ",\n"
                << "        \"final_error_px\": " << scenario.ads_parity_final_error_px << ",\n"
                << "        \"max_single_frame_camera_delta_px\": "
                << scenario.ads_parity_max_single_frame_camera_delta_px << ",\n"
                << "        \"max_overshoot_px\": " << scenario.ads_parity_max_overshoot_px << "\n"
                << "      }";
        }
        if (scenario.has_ads_manual_stress) {
            out
                << ",\n"
                << "      \"ads_manual_stress\": {\n"
                << "        \"controller_hz\": 1000.000000,\n"
                << "        \"vision_hz\": "
                << scenario.ads_manual_stress_vision_hz << ",\n"
                << "        \"vision_samples\": "
                << scenario.ads_manual_stress_vision_samples << ",\n"
                << "        \"vision_no_submit_ticks\": "
                << scenario.ads_manual_stress_vision_dropped_ticks << ",\n"
                << "        \"delayed_submissions\": "
                << scenario.ads_manual_stress_delayed_submissions << ",\n"
                << "        \"fov_change_events\": "
                << scenario.ads_manual_stress_fov_change_events << ",\n"
                << "        \"large_vision_jumps\": "
                << scenario.ads_manual_stress_large_vision_jumps << ",\n"
                << "        \"err_target_windows\": "
                << scenario.ads_manual_stress_err_target_windows << ",\n"
                << "        \"err_target_samples\": "
                << scenario.ads_manual_stress_err_target_samples << ",\n"
                << "        \"err_target_recovered_windows\": "
                << scenario.ads_manual_stress_err_target_recovered_windows << ",\n"
                << "        \"max_err_target_offset_px\": "
                << scenario.ads_manual_stress_max_err_target_offset_px << ",\n"
                << "        \"mean_err_target_recovery_ms\": "
                << scenario.ads_manual_stress_mean_err_target_recovery_ms << ",\n"
                << "        \"p95_err_target_recovery_ms\": "
                << scenario.ads_manual_stress_p95_err_target_recovery_ms << ",\n"
                << "        \"max_err_target_recovery_error_px\": "
                << scenario.ads_manual_stress_max_err_target_recovery_error_px << ",\n"
                << "        \"mean_fov_scale\": "
                << scenario.ads_manual_stress_mean_fov_scale << ",\n"
                << "        \"max_report_age_ms\": "
                << scenario.ads_manual_stress_max_report_age_ms << ",\n"
                << "        \"cases\": " << scenario.ads_manual_stress_cases << ",\n"
                << "        \"ticks_per_case\": "
                << scenario.ads_manual_stress_ticks_per_case << ",\n"
                << "        \"measured_ticks\": "
                << scenario.ads_manual_stress_measured_ticks << ",\n"
                << "        \"mean_error_px\": "
                << scenario.ads_manual_stress_mean_error_px << ",\n"
                << "        \"p95_error_px\": "
                << scenario.ads_manual_stress_p95_error_px << ",\n"
                << "        \"p99_error_px\": "
                << scenario.ads_manual_stress_p99_error_px << ",\n"
                << "        \"final_error_px\": "
                << scenario.ads_manual_stress_final_error_px << ",\n"
                << "        \"overshoot_events\": "
                << scenario.ads_manual_stress_overshoot_events << ",\n"
                << "        \"overshoot_events_x\": "
                << scenario.ads_manual_stress_overshoot_events_x << ",\n"
                << "        \"overshoot_events_y\": "
                << scenario.ads_manual_stress_overshoot_events_y << ",\n"
                << "        \"overshoot_ads_snap\": "
                << scenario.ads_manual_stress_overshoot_ads_snap << ",\n"
                << "        \"overshoot_body_lock\": "
                << scenario.ads_manual_stress_overshoot_body_lock << ",\n"
                << "        \"overshoot_manual\": "
                << scenario.ads_manual_stress_overshoot_manual << ",\n"
                << "        \"large_overshoot_events_50px\": "
                << scenario.ads_manual_stress_large_overshoot_events << ",\n"
                << "        \"max_overshoot_px\": "
                << scenario.ads_manual_stress_max_overshoot_px << ",\n"
                << "        \"max_single_frame_camera_delta_px\": "
                << scenario.ads_manual_stress_max_single_frame_camera_delta_px << ",\n"
                << "        \"mean_target_alignment\": "
                << scenario.ads_manual_stress_mean_target_alignment << ",\n"
                << "        \"mean_manual_alignment\": "
                << scenario.ads_manual_stress_mean_manual_alignment << ",\n"
                << "        \"direction_score\": "
                << scenario.ads_manual_stress_direction_score << ",\n"
                << "        \"manual_direction_score\": "
                << scenario.ads_manual_stress_manual_direction_score << ",\n"
                << "        \"p95_turn_degrees\": "
                << scenario.ads_manual_stress_p95_turn_degrees << ",\n"
                << "        \"turn_smoothness_score\": "
                << scenario.ads_manual_stress_turn_smoothness_score << ",\n"
                << "        \"body_lock_frames\": "
                << scenario.ads_manual_stress_body_lock_frames << ",\n"
                << "        \"body_lock_ratio\": "
                << scenario.ads_manual_stress_body_lock_ratio << ",\n"
                << "        \"body_lock_tracking_samples\": "
                << scenario.ads_manual_stress_body_lock_tracking_samples << ",\n"
                << "        \"body_lock_mean_error_px\": "
                << scenario.ads_manual_stress_body_lock_mean_error_px << ",\n"
                << "        \"body_lock_p95_error_px\": "
                << scenario.ads_manual_stress_body_lock_p95_error_px << ",\n"
                << "        \"body_lock_direction_score\": "
                << scenario.ads_manual_stress_body_lock_direction_score << ",\n"
                << "        \"body_lock_chatter_events\": "
                << scenario.ads_manual_stress_body_lock_chatter_events << ",\n"
                << "        \"body_lock_output_spikes\": "
                << scenario.ads_manual_stress_body_lock_output_spikes << ",\n"
                << "        \"body_lock_turn_samples\": "
                << scenario.ads_manual_stress_body_lock_turn_samples << ",\n"
                << "        \"body_lock_close_assist_samples\": "
                << scenario.ads_manual_stress_body_lock_close_assist_samples << ",\n"
                << "        \"body_lock_low_output_close_frames\": "
                << scenario.ads_manual_stress_body_lock_low_output_close_frames << ",\n"
                << "        \"body_lock_dropout_frames\": "
                << scenario.ads_manual_stress_body_lock_dropout_frames << ",\n"
                << "        \"body_lock_centered_samples\": "
                << scenario.ads_manual_stress_body_lock_centered_samples << ",\n"
                << "        \"body_lock_centered_jitter_frames\": "
                << scenario.ads_manual_stress_body_lock_centered_jitter_frames << ",\n"
                << "        \"body_lock_p95_output_delta\": "
                << scenario.ads_manual_stress_body_lock_p95_output_delta << ",\n"
                << "        \"body_lock_p95_turn_degrees\": "
                << scenario.ads_manual_stress_body_lock_p95_turn_degrees << ",\n"
                << "        \"body_lock_turn_smoothness_score\": "
                << scenario.ads_manual_stress_body_lock_turn_smoothness_score << ",\n"
                << "        \"body_lock_close_assist_mean_ai_output\": "
                << scenario.ads_manual_stress_body_lock_close_assist_mean_ai_output << ",\n"
                << "        \"body_lock_centered_p95_output_delta\": "
                << scenario.ads_manual_stress_body_lock_centered_p95_output_delta << ",\n"
                << "        \"body_lock_sustain_cases\": "
                << scenario.ads_manual_stress_body_lock_sustain_cases << ",\n"
                << "        \"body_lock_sustain_passes\": "
                << scenario.ads_manual_stress_body_lock_sustain_passes << ",\n"
                << "        \"body_lock_sustain_good_frames\": "
                << scenario.ads_manual_stress_body_lock_sustain_good_frames << ",\n"
                << "        \"body_lock_sustain_required_ms\": "
                << scenario.ads_manual_stress_body_lock_sustain_required_ms << ",\n"
                << "        \"body_lock_sustain_error_px\": "
                << scenario.ads_manual_stress_body_lock_sustain_error_px << ",\n"
                << "        \"body_lock_sustain_longest_ms\": "
                << scenario.ads_manual_stress_body_lock_sustain_longest_ms << ",\n"
                << "        \"body_lock_sustain_pass_rate\": "
                << scenario.ads_manual_stress_body_lock_sustain_pass_rate << ",\n"
                << "        \"body_lock_sustain_score\": "
                << scenario.ads_manual_stress_body_lock_sustain_score << ",\n"
                << "        \"occlusion_peak_error_px\": "
                << scenario.ads_manual_stress_occlusion_peak_error_px << ",\n"
                << "        \"slide_down_lag_p95_px\": "
                << scenario.ads_manual_stress_slide_down_lag_p95_px << ",\n"
                << "        \"slide_recover_ms\": "
                << scenario.ads_manual_stress_slide_recover_ms << ",\n"
                << "        \"unreliable_high_output_frames\": "
                << scenario.ads_manual_stress_unreliable_high_output_frames << ",\n"
                << "        \"unreliable_same_direction_frames\": "
                << scenario.ads_manual_stress_unreliable_same_direction_frames << ",\n"
                << "        \"unreliable_fight_frames\": "
                << scenario.ads_manual_stress_unreliable_fight_frames << ",\n"
                << "        \"unreliable_no_fresh_target_high_output_frames\": "
                << scenario
                       .ads_manual_stress_unreliable_no_fresh_target_high_output_frames
                << ",\n"
                << "        \"unreliable_err_target_high_output_frames\": "
                << scenario.ads_manual_stress_unreliable_err_target_high_output_frames
                << ",\n"
                << "        \"unreliable_max_final_output\": "
                << scenario.ads_manual_stress_unreliable_max_final_output << ",\n"
                << "        \"unreliable_mean_final_output\": "
                << scenario.ads_manual_stress_unreliable_mean_final_output << ",\n"
                << "        \"overshoot_details\": ";
            write_random_fov_overshoot_details_json(
                out,
                scenario.ads_manual_stress_overshoot_details,
                "        ");
            out
                << "\n"
                << "      }";
        }
        if (scenario.has_ads_bodylock_near_high) {
            out
                << ",\n"
                << "      \"ads_bodylock_near_high\": {\n"
                << "        \"controller_hz\": 1000.000000,\n"
                << "        \"vision_hz\": 100.000000,\n"
                << "        \"initial_dx\": "
                << scenario.ads_bodylock_near_high_initial_dx << ",\n"
                << "        \"snap_window_ms\": "
                << scenario.ads_bodylock_near_high_snap_window_ms << ",\n"
                << "        \"ticks\": "
                << scenario.ads_bodylock_near_high_ticks << ",\n"
                << "        \"vision_samples\": "
                << scenario.ads_bodylock_near_high_vision_samples << ",\n"
                << "        \"ads_snap_frames\": "
                << scenario.ads_bodylock_near_high_ads_snap_frames << ",\n"
                << "        \"body_lock_frames\": "
                << scenario.ads_bodylock_near_high_body_lock_frames << ",\n"
                << "        \"manual_frames\": "
                << scenario.ads_bodylock_near_high_manual_frames << ",\n"
                << "        \"near_target_frames\": "
                << scenario.ads_bodylock_near_high_near_target_frames << ",\n"
                << "        \"near_high_output_frames\": "
                << scenario.ads_bodylock_near_high_output_frames << ",\n"
                << "        \"brake_active_frames\": "
                << scenario.ads_bodylock_near_high_brake_active_frames << ",\n"
                << "        \"brake_inactive_frames\": "
                << scenario.ads_bodylock_near_high_brake_inactive_frames << ",\n"
                << "        \"acquisition_expired_high_frames\": "
                << scenario.ads_bodylock_near_high_acquisition_expired_high_frames << ",\n"
                << "        \"chatter_events\": "
                << scenario.ads_bodylock_near_high_chatter_events << ",\n"
                << "        \"output_spikes\": "
                << scenario.ads_bodylock_near_high_output_spikes << ",\n"
                << "        \"close_assist_frames\": "
                << scenario.ads_bodylock_near_high_close_assist_frames << ",\n"
                << "        \"low_output_close_frames\": "
                << scenario.ads_bodylock_near_high_low_output_close_frames << ",\n"
                << "        \"centered_frames\": "
                << scenario.ads_bodylock_near_high_centered_frames << ",\n"
                << "        \"centered_jitter_frames\": "
                << scenario.ads_bodylock_near_high_centered_jitter_frames << ",\n"
                << "        \"mean_error_px\": "
                << scenario.ads_bodylock_near_high_mean_error_px << ",\n"
                << "        \"p95_error_px\": "
                << scenario.ads_bodylock_near_high_p95_error_px << ",\n"
                << "        \"final_error_px\": "
                << scenario.ads_bodylock_near_high_final_error_px << ",\n"
                << "        \"max_final_output\": "
                << scenario.ads_bodylock_near_high_max_final_output << ",\n"
                << "        \"p95_output_delta\": "
                << scenario.ads_bodylock_near_high_p95_output_delta << ",\n"
                << "        \"p95_turn_degrees\": "
                << scenario.ads_bodylock_near_high_p95_turn_degrees << ",\n"
                << "        \"turn_smoothness_score\": "
                << scenario.ads_bodylock_near_high_turn_smoothness_score << ",\n"
                << "        \"close_assist_mean_output\": "
                << scenario.ads_bodylock_near_high_close_assist_mean_output << ",\n"
                << "        \"centered_p95_output_delta\": "
                << scenario.ads_bodylock_near_high_centered_p95_output_delta << "\n"
                << "      }";
        }
        if (scenario.has_ads_carry_through) {
            out
                << ",\n"
                << "      \"ads_carry_through\": {\n"
                << "        \"controller_hz\": 1000.000000,\n"
                << "        \"vision_hz\": 100.000000,\n"
                << "        \"initial_dx\": "
                << scenario.ads_carry_through_initial_dx << ",\n"
                << "        \"snap_window_ms\": "
                << scenario.ads_carry_through_snap_window_ms << ",\n"
                << "        \"ticks\": "
                << scenario.ads_carry_through_ticks << ",\n"
                << "        \"vision_samples\": "
                << scenario.ads_carry_through_vision_samples << ",\n"
                << "        \"ads_snap_frames\": "
                << scenario.ads_carry_through_ads_snap_frames << ",\n"
                << "        \"body_lock_frames\": "
                << scenario.ads_carry_through_body_lock_frames << ",\n"
                << "        \"manual_frames\": "
                << scenario.ads_carry_through_manual_frames << ",\n"
                << "        \"same_direction_accel_frames\": "
                << scenario.ads_carry_through_same_direction_accel_frames << ",\n"
                << "        \"near_target_frames\": "
                << scenario.ads_carry_through_near_target_frames << ",\n"
                << "        \"near_high_output_frames\": "
                << scenario.ads_carry_through_near_high_output_frames << ",\n"
                << "        \"brake_active_frames\": "
                << scenario.ads_carry_through_brake_active_frames << ",\n"
                << "        \"brake_inactive_near_high_frames\": "
                << scenario.ads_carry_through_brake_inactive_near_high_frames << ",\n"
                << "        \"sign_flip_events\": "
                << scenario.ads_carry_through_sign_flip_events << ",\n"
                << "        \"mean_error_px\": "
                << scenario.ads_carry_through_mean_error_px << ",\n"
                << "        \"p95_error_px\": "
                << scenario.ads_carry_through_p95_error_px << ",\n"
                << "        \"final_error_px\": "
                << scenario.ads_carry_through_final_error_px << ",\n"
                << "        \"max_overshoot_px\": "
                << scenario.ads_carry_through_max_overshoot_px << ",\n"
                << "        \"max_near_final_x\": "
                << scenario.ads_carry_through_max_near_final_x << ",\n"
                << "        \"max_brake_error_px\": "
                << scenario.ads_carry_through_max_brake_error_px << "\n"
                << "      }";
        }
        if (scenario.has_adversarial_controller) {
            out
                << ",\n"
                << "      \"adversarial_controller\": {\n"
                << "        \"controller_hz\": 1000.000000,\n"
                << "        \"vision_hz\": 100.000000,\n"
                << "        \"ticks\": "
                << scenario.adversarial_controller_ticks << ",\n"
                << "        \"vision_samples\": "
                << scenario.adversarial_controller_vision_samples << ",\n"
                << "        \"wrong_target_frames\": "
                << scenario.adversarial_controller_wrong_target_frames << ",\n"
                << "        \"user_fight_frames\": "
                << scenario.adversarial_controller_user_fight_frames << ",\n"
                << "        \"invalid_strong_frames\": "
                << scenario.adversarial_controller_invalid_strong_frames << ",\n"
                << "        \"stale_high_output_frames\": "
                << scenario.adversarial_controller_stale_high_output_frames << ",\n"
                << "        \"err_target_frames\": "
                << scenario.adversarial_controller_err_target_frames << ",\n"
                << "        \"recovery_frames\": "
                << scenario.adversarial_controller_recovery_frames << ",\n"
                << "        \"near_high_output_frames\": "
                << scenario.adversarial_controller_near_high_output_frames << ",\n"
                << "        \"projected_frames\": "
                << scenario.adversarial_controller_projected_frames << ",\n"
                << "        \"max_final_output\": "
                << scenario.adversarial_controller_max_final_output << ",\n"
                << "        \"p95_error_px\": "
                << scenario.adversarial_controller_p95_error_px << ",\n"
                << "        \"max_stale_age_ms\": "
                << scenario.adversarial_controller_max_stale_age_ms << ",\n"
                << "        \"mean_manual_ai_alignment\": "
                << scenario.adversarial_controller_mean_manual_ai_alignment << "\n"
                << "      }";
        }
        if (scenario.has_selector_intent) {
            out
                << ",\n"
                << "      \"selector_intent\": {\n"
                << "        \"controller_hz\": 100.000000,\n"
                << "        \"seed\": " << scenario.selector_intent_seed << ",\n"
                << "        \"ticks\": " << scenario.selector_intent_ticks << ",\n"
                << "        \"target_frames\": " << scenario.selector_intent_target_frames << ",\n"
                << "        \"wrong_target_frames\": "
                << scenario.selector_intent_wrong_target_frames << ",\n"
                << "        \"wrong_target_rate\": "
                << scenario.selector_intent_wrong_target_rate << ",\n"
                << "        \"switches\": " << scenario.selector_intent_switches << ",\n"
                << "        \"ping_pong_switches\": "
                << scenario.selector_intent_ping_pong_switches << ",\n"
                << "        \"fire_authority_leaks\": "
                << scenario.selector_intent_fire_authority_leaks << ",\n"
                << "        \"intent_applied_frames\": "
                << scenario.selector_intent_intent_applied_frames << ",\n"
                << "        \"ignored_active_lock_frames\": "
                << scenario.selector_intent_ignored_active_lock_frames << ",\n"
                << "        \"delayed_switch_confirm_frames\": "
                << scenario.selector_intent_delayed_switch_confirm_frames << ",\n"
                << "        \"weak_no_fire_frames\": "
                << scenario.selector_intent_weak_no_fire_frames << "\n"
                << "      }";
        }
        if (scenario.has_roi_fallback) {
            out
                << ",\n"
                << "      \"roi_fallback\": {\n"
                << "        \"seed\": " << scenario.roi_fallback_seed << ",\n"
                << "        \"ticks\": " << scenario.roi_fallback_ticks << ",\n"
                << "        \"requested_regions\": "
                << scenario.roi_fallback_requested_regions << ",\n"
                << "        \"partial_regions\": "
                << scenario.roi_fallback_partial_regions << ",\n"
                << "        \"full_frame_requests\": "
                << scenario.roi_fallback_full_frame_requests << ",\n"
                << "        \"external_cue_no_full_frame_frames\": "
                << scenario.roi_fallback_external_cue_no_full_frame_frames << ",\n"
                << "        \"roi_miss_hold_frames\": "
                << scenario.roi_fallback_roi_miss_hold_frames << ",\n"
                << "        \"edge_clamped_regions\": "
                << scenario.roi_fallback_edge_clamped_regions << ",\n"
                << "        \"target_loss_frames\": "
                << scenario.roi_fallback_target_loss_frames << ",\n"
                << "        \"processed_area_ratio\": "
                << scenario.roi_fallback_processed_area_ratio << "\n"
                << "      }";
        }
        out
            << "\n"
            << "    }" << (index + 1 == scenarios.size() ? "\n" : ",\n");
    }
    out << "  ]\n}\n";
}

void print_summary(
    const CliOptions& options,
    const std::vector<ScenarioMetrics>& scenarios) {
    std::cout << "[NativeGamepadBenchmark] run_key=" << options.run_key << "\n";
    for (const ScenarioMetrics& scenario : scenarios) {
        std::cout
            << "  " << scenario.name
            << " frames=" << scenario.frames
            << " x_oppose=" << std::fixed << std::setprecision(3)
            << rate(scenario.x.final_opposes_manual, scenario.x.manual_frames)
            << " x_block=" << rate(scenario.x.final_blocks_manual, scenario.x.manual_frames)
            << " x_preserve=" << mean_ratio(scenario.x)
            << " y_oppose=" << rate(scenario.y.final_opposes_manual, scenario.y.manual_frames)
            << " y_block=" << rate(scenario.y.final_blocks_manual, scenario.y.manual_frames)
            << " y_preserve=" << mean_ratio(scenario.y);
        if (scenario.blocked_frames > 0) {
            std::cout << " blocked_frames=" << scenario.blocked_frames;
        }
        if (scenario.recovery_frames >= 0) {
            std::cout << " recovery_frames=" << scenario.recovery_frames;
        }
        if (scenario.has_random_fov) {
            std::cout
                << " vision_hz=100"
                << " force_scale=" << scenario.random_fov_ai_force_scale
                << " measured=" << scenario.random_fov_measured_ticks
                << " fov_events=" << scenario.random_fov_change_events
                << " large_jumps=" << scenario.random_fov_large_vision_jumps
                << " fresh_wrong="
                << rate(
                    scenario.random_fov_fresh_wrong_direction,
                    scenario.random_fov_vision_samples)
                << " tracker_wrong="
                << rate(
                    scenario.random_fov_tracker_wrong_direction,
                    scenario.random_fov_tracker_only_ticks)
                << " fresh_idle="
                << rate(
                    scenario.random_fov_fresh_insensitive,
                    scenario.random_fov_vision_samples)
                << " tracker_idle="
                << rate(
                    scenario.random_fov_tracker_insensitive,
                    scenario.random_fov_tracker_only_ticks)
                << " mean_err=" << scenario.random_fov_mean_error_px
                << " p95_err=" << scenario.random_fov_p95_error_px
                << " p99_err=" << scenario.random_fov_p99_error_px
                << " dir_score=" << scenario.random_fov_direction_score
                << " manual_dir_score=" << scenario.random_fov_manual_direction_score
                << " smooth_score=" << scenario.random_fov_turn_smoothness_score
                << " p95_turn=" << scenario.random_fov_p95_turn_degrees
                << " p95_delta=" << scenario.random_fov_p95_output_delta
                << " overshoots=" << scenario.random_fov_overshoot_events
                << " over_x=" << scenario.random_fov_overshoot_events_x
                << " over_y=" << scenario.random_fov_overshoot_events_y
                << " ads_over=" << scenario.random_fov_overshoot_ads_snap
                << " body_over=" << scenario.random_fov_overshoot_body_lock
                << " manual_over=" << scenario.random_fov_overshoot_manual
                << " max_over=" << scenario.random_fov_max_overshoot_px
                << " mean_dx=" << scenario.random_fov_mean_abs_expected_dx
                << " mean_out_x=" << scenario.random_fov_mean_abs_final_x
                << " out_per_100px=" << scenario.random_fov_output_per_100px_error;
        }
        if (scenario.has_ads_settle) {
            std::cout
                << " ticks=" << scenario.ads_settle_ticks
                << " snap_ms=" << scenario.ads_settle_snap_window_ms
                << " force_scale=" << scenario.ads_settle_ai_force_scale
                << " fov=" << scenario.ads_settle_fov_start_scale
                << "->" << scenario.ads_settle_fov_end_scale
                << " settled=" << scenario.ads_settle_settled_tick
                << " mean_err=" << scenario.ads_settle_mean_error_px
                << " p95_err=" << scenario.ads_settle_p95_error_px
                << " final_err=" << scenario.ads_settle_final_error_px
                << " overshoots=" << scenario.ads_settle_overshoot_events
                << " max_over=" << scenario.ads_settle_max_overshoot_px;
        }
        if (scenario.has_ads_parity) {
            std::cout
                << " cases=" << scenario.ads_parity_cases
                << " hz=60"
                << " mean_err=" << scenario.ads_parity_mean_error_px
                << " p95_err=" << scenario.ads_parity_p95_error_px
                << " final_err=" << scenario.ads_parity_final_error_px
                << " under20_cases=" << scenario.ads_parity_under_20_cases
                << " overshoot_cases=" << scenario.ads_parity_overshoot_cases
                << " overshoots=" << scenario.ads_parity_overshoot_events
                << " over_x=" << scenario.ads_parity_overshoot_events_x
                << " over_y=" << scenario.ads_parity_overshoot_events_y
                << " crossings=" << scenario.ads_parity_crossing_events
                << " ads_cross=" << scenario.ads_parity_ads_snap_crossing_events
                << " body_cross=" << scenario.ads_parity_body_lock_crossing_events
                << " ads_over=" << scenario.ads_parity_ads_snap_overshoot_events
                << " body_over=" << scenario.ads_parity_body_lock_overshoot_events
                << " manual_over=" << scenario.ads_parity_manual_overshoot_events
                << " profile_over=[none:" << scenario.ads_parity_overshoot_events_none
                << ",aligned:" << scenario.ads_parity_overshoot_events_aligned_follow
                << ",opposing:" << scenario.ads_parity_overshoot_events_opposing_burst
                << ",recover:" << scenario.ads_parity_overshoot_events_overshoot_recover
                << "]"
                << " overshoot_recover_events="
                << scenario.ads_parity_overshoot_recover_events
                << " max_over=" << scenario.ads_parity_max_overshoot_px
                << " max_frame_delta="
                << scenario.ads_parity_max_single_frame_camera_delta_px;
        }
        if (scenario.has_ads_manual_stress) {
            std::cout
                << " cases=" << scenario.ads_manual_stress_cases
                << " hz=1000/" << scenario.ads_manual_stress_vision_hz
                << " vision_samples=" << scenario.ads_manual_stress_vision_samples
                << " no_submit_ticks="
                << scenario.ads_manual_stress_vision_dropped_ticks
                << " delayed="
                << scenario.ads_manual_stress_delayed_submissions
                << " fov_events="
                << scenario.ads_manual_stress_fov_change_events
                << " large_jumps="
                << scenario.ads_manual_stress_large_vision_jumps
                << " err_windows="
                << scenario.ads_manual_stress_err_target_windows
                << " err_samples="
                << scenario.ads_manual_stress_err_target_samples
                << " err_recovered="
                << scenario.ads_manual_stress_err_target_recovered_windows
                << " err_offset="
                << scenario.ads_manual_stress_max_err_target_offset_px
                << " err_recover_mean_ms="
                << scenario.ads_manual_stress_mean_err_target_recovery_ms
                << " err_recover_p95_ms="
                << scenario.ads_manual_stress_p95_err_target_recovery_ms
                << " err_peak="
                << scenario.ads_manual_stress_max_err_target_recovery_error_px
                << " mean_fov="
                << scenario.ads_manual_stress_mean_fov_scale
                << " max_report_age_ms="
                << scenario.ads_manual_stress_max_report_age_ms
                << " mean_err=" << scenario.ads_manual_stress_mean_error_px
                << " p95_err=" << scenario.ads_manual_stress_p95_error_px
                << " p99_err=" << scenario.ads_manual_stress_p99_error_px
                << " final_err=" << scenario.ads_manual_stress_final_error_px
                << " overshoots=" << scenario.ads_manual_stress_overshoot_events
                << " over_x=" << scenario.ads_manual_stress_overshoot_events_x
                << " over_y=" << scenario.ads_manual_stress_overshoot_events_y
                << " ads_over=" << scenario.ads_manual_stress_overshoot_ads_snap
                << " body_over=" << scenario.ads_manual_stress_overshoot_body_lock
                << " manual_over=" << scenario.ads_manual_stress_overshoot_manual
                << " large50=" << scenario.ads_manual_stress_large_overshoot_events
                << " max_over=" << scenario.ads_manual_stress_max_overshoot_px
                << " max_frame_delta="
                << scenario.ads_manual_stress_max_single_frame_camera_delta_px
                << " dir_score=" << scenario.ads_manual_stress_direction_score
                << " manual_dir_score="
                << scenario.ads_manual_stress_manual_direction_score
                << " smooth_score="
                << scenario.ads_manual_stress_turn_smoothness_score
                << " p95_turn=" << scenario.ads_manual_stress_p95_turn_degrees
                << " body_lock_frames="
                << scenario.ads_manual_stress_body_lock_frames
                << " body_lock_ratio="
                << scenario.ads_manual_stress_body_lock_ratio
                << " body_lock_samples="
                << scenario.ads_manual_stress_body_lock_tracking_samples
                << " body_lock_mean_err="
                << scenario.ads_manual_stress_body_lock_mean_error_px
                << " body_lock_p95_err="
                << scenario.ads_manual_stress_body_lock_p95_error_px
                << " body_lock_dir_score="
                << scenario.ads_manual_stress_body_lock_direction_score
                << " body_lock_chatter="
                << scenario.ads_manual_stress_body_lock_chatter_events
                << " body_lock_spikes="
                << scenario.ads_manual_stress_body_lock_output_spikes
                << " body_lock_close_samples="
                << scenario.ads_manual_stress_body_lock_close_assist_samples
                << " body_lock_close_ai="
                << scenario.ads_manual_stress_body_lock_close_assist_mean_ai_output
                << " body_lock_low_close="
                << scenario.ads_manual_stress_body_lock_low_output_close_frames
                << " body_lock_dropout="
                << scenario.ads_manual_stress_body_lock_dropout_frames
                << " body_lock_centered="
                << scenario.ads_manual_stress_body_lock_centered_samples
                << " body_lock_centered_jitter="
                << scenario.ads_manual_stress_body_lock_centered_jitter_frames
                << " body_lock_p95_delta="
                << scenario.ads_manual_stress_body_lock_p95_output_delta
                << " body_lock_centered_p95_delta="
                << scenario.ads_manual_stress_body_lock_centered_p95_output_delta
                << " body_lock_p95_turn="
                << scenario.ads_manual_stress_body_lock_p95_turn_degrees
                << " body_lock_smooth="
                << scenario.ads_manual_stress_body_lock_turn_smoothness_score
                << " body_lock_sustain="
                << scenario.ads_manual_stress_body_lock_sustain_passes
                << "/" << scenario.ads_manual_stress_body_lock_sustain_cases
                << " body_lock_sustain_score="
                << scenario.ads_manual_stress_body_lock_sustain_score
                << " body_lock_longest_ms="
                << scenario.ads_manual_stress_body_lock_sustain_longest_ms
                << " occlusion_peak="
                << scenario.ads_manual_stress_occlusion_peak_error_px
                << " slide_down_lag_p95="
                << scenario.ads_manual_stress_slide_down_lag_p95_px
                << " slide_recover_ms="
                << scenario.ads_manual_stress_slide_recover_ms
                << " unreliable_high="
                << scenario.ads_manual_stress_unreliable_high_output_frames
                << " unreliable_same_dir="
                << scenario.ads_manual_stress_unreliable_same_direction_frames
                << " unreliable_fight="
                << scenario.ads_manual_stress_unreliable_fight_frames
                << " unreliable_no_fresh_high="
                << scenario
                       .ads_manual_stress_unreliable_no_fresh_target_high_output_frames
                << " unreliable_err_high="
                << scenario.ads_manual_stress_unreliable_err_target_high_output_frames
                << " unreliable_max_final="
                << scenario.ads_manual_stress_unreliable_max_final_output
                << " unreliable_mean_final="
                << scenario.ads_manual_stress_unreliable_mean_final_output;
        }
        if (scenario.has_ads_bodylock_near_high) {
            std::cout
                << " near_ticks=" << scenario.ads_bodylock_near_high_ticks
                << " near_modes=[ads:"
                << scenario.ads_bodylock_near_high_ads_snap_frames
                << ",body:" << scenario.ads_bodylock_near_high_body_lock_frames
                << ",manual:" << scenario.ads_bodylock_near_high_manual_frames
                << "]"
                << " near_frames="
                << scenario.ads_bodylock_near_high_near_target_frames
                << " near_high="
                << scenario.ads_bodylock_near_high_output_frames
                << " brake_active="
                << scenario.ads_bodylock_near_high_brake_active_frames
                << " brake_gap="
                << scenario.ads_bodylock_near_high_brake_inactive_frames
                << " expired_high="
                << scenario.ads_bodylock_near_high_acquisition_expired_high_frames
                << " chatter="
                << scenario.ads_bodylock_near_high_chatter_events
                << " spikes="
                << scenario.ads_bodylock_near_high_output_spikes
                << " close_assist="
                << scenario.ads_bodylock_near_high_close_assist_frames
                << " close_mean="
                << scenario.ads_bodylock_near_high_close_assist_mean_output
                << " low_close="
                << scenario.ads_bodylock_near_high_low_output_close_frames
                << " centered="
                << scenario.ads_bodylock_near_high_centered_frames
                << " centered_jitter="
                << scenario.ads_bodylock_near_high_centered_jitter_frames
                << " p95_delta="
                << scenario.ads_bodylock_near_high_p95_output_delta
                << " centered_p95_delta="
                << scenario.ads_bodylock_near_high_centered_p95_output_delta
                << " p95_turn="
                << scenario.ads_bodylock_near_high_p95_turn_degrees
                << " smooth="
                << scenario.ads_bodylock_near_high_turn_smoothness_score
                << " max_final="
                << scenario.ads_bodylock_near_high_max_final_output;
        }
        if (scenario.has_ads_carry_through) {
            std::cout
                << " carry_ticks=" << scenario.ads_carry_through_ticks
                << " carry_modes=[ads:" << scenario.ads_carry_through_ads_snap_frames
                << ",body:" << scenario.ads_carry_through_body_lock_frames
                << ",manual:" << scenario.ads_carry_through_manual_frames
                << "]"
                << " same_dir_accel="
                << scenario.ads_carry_through_same_direction_accel_frames
                << " near_high="
                << scenario.ads_carry_through_near_high_output_frames
                << " brake_active="
                << scenario.ads_carry_through_brake_active_frames
                << " brake_gap="
                << scenario.ads_carry_through_brake_inactive_near_high_frames
                << " flips=" << scenario.ads_carry_through_sign_flip_events
                << " max_over="
                << scenario.ads_carry_through_max_overshoot_px
                << " final_err="
                << scenario.ads_carry_through_final_error_px;
        }
        if (scenario.has_adversarial_controller) {
            std::cout
                << " adv_ticks=" << scenario.adversarial_controller_ticks
                << " adv_wrong=" << scenario.adversarial_controller_wrong_target_frames
                << " adv_fight=" << scenario.adversarial_controller_user_fight_frames
                << " adv_invalid=" << scenario.adversarial_controller_invalid_strong_frames
                << " adv_stale_high="
                << scenario.adversarial_controller_stale_high_output_frames
                << " adv_err=" << scenario.adversarial_controller_err_target_frames
                << " adv_recovery=" << scenario.adversarial_controller_recovery_frames
                << " adv_near_high="
                << scenario.adversarial_controller_near_high_output_frames
                << " adv_projected="
                << scenario.adversarial_controller_projected_frames
                << " adv_max_final="
                << scenario.adversarial_controller_max_final_output
                << " adv_p95_err="
                << scenario.adversarial_controller_p95_error_px
                << " adv_align="
                << scenario.adversarial_controller_mean_manual_ai_alignment;
        }
        if (scenario.has_selector_intent) {
            std::cout
                << " selector_hz=100"
                << " seed=" << scenario.selector_intent_seed
                << " ticks=" << scenario.selector_intent_ticks
                << " targets=" << scenario.selector_intent_target_frames
                << " wrong=" << scenario.selector_intent_wrong_target_frames
                << " wrong_rate=" << scenario.selector_intent_wrong_target_rate
                << " switches=" << scenario.selector_intent_switches
                << " pingpong=" << scenario.selector_intent_ping_pong_switches
                << " fire_leaks=" << scenario.selector_intent_fire_authority_leaks
                << " applied=" << scenario.selector_intent_intent_applied_frames
                << " ignored_lock="
                << scenario.selector_intent_ignored_active_lock_frames
                << " delayed_switch="
                << scenario.selector_intent_delayed_switch_confirm_frames
                << " weak_no_fire=" << scenario.selector_intent_weak_no_fire_frames;
        }
        if (scenario.has_roi_fallback) {
            std::cout
                << " roi_seed=" << scenario.roi_fallback_seed
                << " ticks=" << scenario.roi_fallback_ticks
                << " requested=" << scenario.roi_fallback_requested_regions
                << " partial=" << scenario.roi_fallback_partial_regions
                << " full_requests=" << scenario.roi_fallback_full_frame_requests
                << " external_no_full="
                << scenario.roi_fallback_external_cue_no_full_frame_frames
                << " miss_hold=" << scenario.roi_fallback_roi_miss_hold_frames
                << " edge_clamp=" << scenario.roi_fallback_edge_clamped_regions
                << " target_loss=" << scenario.roi_fallback_target_loss_frames
                << " area_ratio=" << scenario.roi_fallback_processed_area_ratio;
        }
        std::cout << "\n";
    }
    std::cout << "[NativeGamepadBenchmark] artifact=" << options.output_path.string() << "\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const CliOptions options = parse_args(argc, argv);
        if (options.self_test) {
            run_self_test();
            return 0;
        }

        controller_native::RuntimeConfig runtime_config;
        std::vector<ScenarioMetrics> scenarios;
        if (options.suite == "selector_intent") {
            scenarios = run_selector_intent_suite(options.selector_intent_seed);
        } else if (options.suite == "roi_fallback") {
            scenarios = run_roi_fallback_suite(options.roi_fallback_seed);
        } else {
            runtime_config = controller_native::load_runtime_config(options.config_path);
            if (!options.recoil_state_path.empty()) {
                runtime_config.gamepad.recoil.recognizer_state_path =
                    options.recoil_state_path.string();
            }

            scenarios.push_back(run_bodylock_edge_escape(
                runtime_config.gamepad,
                options.frames,
                options.dt_ms));
            scenarios.push_back(run_ads_wrong_input_recovery(
                runtime_config.gamepad,
                options.frames,
                options.dt_ms));
            scenarios.push_back(run_recoil_manual_conflict(
                runtime_config.gamepad,
                options.frames,
                options.dt_ms));
            scenarios.push_back(run_ads_python_parity_60hz(runtime_config.gamepad));
            scenarios.push_back(run_ads_python_parity_60hz(
                runtime_config.gamepad,
                "ads_python_parity_60hz_dynamic",
                true,
                false));
            scenarios.push_back(run_ads_python_parity_60hz(
                runtime_config.gamepad,
                "ads_python_parity_60hz_dynamic_fire",
                true,
                true));
            scenarios.push_back(run_ads_fov_settle_130ms(runtime_config.gamepad, options));
            scenarios.push_back(
                run_ads_diagonal_manual_stress_100hz(runtime_config.gamepad));
            scenarios.push_back(run_ads_diagonal_manual_stress_100hz(
                runtime_config.gamepad,
                "ads_diagonal_manual_stress_100hz_dynamic",
                true,
                false));
            scenarios.push_back(run_ads_diagonal_manual_stress_100hz(
                runtime_config.gamepad,
                "ads_diagonal_manual_stress_100hz_dynamic_fire",
                true,
                true));
            scenarios.push_back(run_ads_diagonal_manual_stress_100hz(
                runtime_config.gamepad,
                "ads_diagonal_late_vision_fov_occlusion_50hz",
                false,
                false,
                true));
            scenarios.push_back(run_ads_diagonal_manual_stress_100hz(
                runtime_config.gamepad,
                "ads_diagonal_late_vision_fov_occlusion_50hz_dynamic_fire",
                true,
                true,
                true));
            scenarios.push_back(run_ads_diagonal_manual_stress_100hz(
                runtime_config.gamepad,
                "ads_diagonal_late_position_fresh_timestamp_fov_occlusion_50hz_dynamic_fire",
                true,
                true,
                true,
                true));
            scenarios.push_back(run_ads_diagonal_manual_stress_100hz(
                runtime_config.gamepad,
                "ads_diagonal_err_target_recovery_100hz_dynamic_fire",
                true,
                true,
                false,
                false,
                true,
                options.random_fov_seed));
            scenarios.push_back(run_ads_diagonal_manual_stress_100hz(
                runtime_config.gamepad,
                "ads_diagonal_err_target_late_position_fov_occlusion_50hz_dynamic_fire",
                true,
                true,
                true,
                true,
                true,
                options.random_fov_seed));
            scenarios.push_back(
                run_ads_manual_carry_through_100hz(runtime_config.gamepad));
            scenarios.push_back(
                run_ads_bodylock_near_high_output_100hz(runtime_config.gamepad));
            scenarios.push_back(
                run_adversarial_controller_authority_100hz(runtime_config.gamepad));
            scenarios.push_back(run_ads_bodylock_moving_chase_100hz(
                runtime_config.gamepad,
                options.random_fov_seed,
                "ads_bodylock_moving_chase_100hz_dynamic",
                true,
                false,
                BodylockChaseMotionProfile::Smooth,
                false));
            scenarios.push_back(run_ads_bodylock_moving_chase_100hz(
                runtime_config.gamepad,
                options.random_fov_seed,
                "ads_bodylock_slide_visible_chase_100hz_dynamic",
                true,
                false,
                BodylockChaseMotionProfile::Slide,
                false));
            scenarios.push_back(run_ads_bodylock_moving_chase_100hz(
                runtime_config.gamepad,
                options.random_fov_seed,
                "ads_bodylock_slide_occlusion_chase_100hz_dynamic_fire",
                true,
                true,
                BodylockChaseMotionProfile::Slide,
                true));
            scenarios.push_back(run_ads_bodylock_moving_chase_100hz(
                runtime_config.gamepad,
                options.random_fov_seed,
                "ads_bodylock_crouch_cycle_chase_100hz_dynamic",
                true,
                false,
                BodylockChaseMotionProfile::CrouchCycle,
                false));
            scenarios.push_back(run_ads_bodylock_moving_chase_100hz(
                runtime_config.gamepad,
                options.random_fov_seed,
                "ads_bodylock_jump_chase_100hz_dynamic",
                true,
                false,
                BodylockChaseMotionProfile::Jump,
                false));
            scenarios.push_back(run_ads_bodylock_moving_chase_100hz(
                runtime_config.gamepad,
                options.random_fov_seed,
                "ads_bodylock_arc_jump_chase_100hz_dynamic",
                true,
                false,
                BodylockChaseMotionProfile::ArcJump,
                false));
            if (options.random_fov_ticks > 0) {
                scenarios.push_back(run_tracker_random_fov_100hz(
                    runtime_config.gamepad,
                    options));
                scenarios.push_back(run_tracker_random_fov_100hz(
                    runtime_config.gamepad,
                    options,
                    "tracker_random_fov_100hz_short_plan",
                    false,
                    true));
                scenarios.push_back(run_tracker_random_fov_100hz(
                    runtime_config.gamepad,
                    options,
                    "tracker_random_fov_100hz_dynamic",
                    true));
                scenarios.push_back(run_tracker_random_fov_100hz(
                    runtime_config.gamepad,
                    options,
                    "tracker_random_fov_100hz_dynamic_short_plan",
                    true,
                    true));
                scenarios.push_back(run_tracker_random_fov_100hz(
                    runtime_config.gamepad,
                    options,
                    "ads_pure_random_fov_100hz_dynamic",
                    true,
                    false,
                    true));
                scenarios.push_back(run_tracker_random_fov_100hz(
                    runtime_config.gamepad,
                    options,
                    "ads_pure_random_fov_100hz_dynamic_short_plan",
                    true,
                    true,
                    true));
            }
        }

        write_json(options, runtime_config, scenarios);
        print_summary(options, scenarios);
        return 0;
    } catch (const std::exception& exc) {
        std::cerr << "[NativeGamepadBenchmark][Error] " << exc.what() << "\n";
        return 1;
    }
}
