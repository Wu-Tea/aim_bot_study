#include "native_gamepad_controller.h"
#include "runtime_config.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

struct CliOptions {
    std::filesystem::path config_path = "config.toml";
    std::string run_key;
    std::filesystem::path output_path;
    std::filesystem::path recoil_state_path;
    int frames = 120;
    double dt_ms = 8.333;
    int random_fov_ticks = 800;
    unsigned int random_fov_seed = 1337;
    double random_fov_min_scale = 0.68;
    double random_fov_max_scale = 1.0;
    double random_fov_ai_force_scale = 1.0;
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
    int random_fov_overshoot_events = 0;
    double random_fov_max_overshoot_px = 0.0;
    double random_fov_ai_force_scale = 1.0;
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
        << "Usage: cod_native_gamepad_benchmark [--config config.toml] "
        << "[--run-key key] [--output path] [--frames n] [--dt-ms ms] "
        << "[--recoil-state path] [--random-fov-ticks n] "
        << "[--random-fov-seed n] [--random-fov-min-scale v] "
        << "[--random-fov-max-scale v] [--random-fov-ai-force-scale v]\n";
}

CliOptions parse_args(int argc, char** argv) {
    CliOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--help" || arg == "-h") {
            print_usage();
            std::exit(0);
        }
        if (arg == "--config" && index + 1 < argc) {
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

controller_native::PhysicalGamepadState aiming_state(float manual_x, float manual_y) {
    controller_native::PhysicalGamepadState state;
    state.connected = true;
    state.left_trigger = 1.0f;
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

float clamp_float(float value, float minimum, float maximum) {
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

double qualifying_overshoot(double previous, double current, double threshold_px) {
    if (previous > 0.0 && current < 0.0 && std::fabs(current) > threshold_px) {
        return std::fabs(current);
    }
    if (previous < 0.0 && current > 0.0 && std::fabs(current) > threshold_px) {
        return std::fabs(current);
    }
    return 0.0;
}

int axis_overshoot_count(const std::vector<double>& errors, double threshold_px) {
    if (errors.size() < 2) {
        return 0;
    }
    int count = 0;
    double previous = errors.front();
    for (std::size_t index = 1; index < errors.size(); ++index) {
        const double current = errors[index];
        if (qualifying_overshoot(previous, current, threshold_px) > 0.0) {
            ++count;
        }
        previous = current;
    }
    return count;
}

double axis_max_overshoot(const std::vector<double>& errors, double threshold_px) {
    if (errors.size() < 2) {
        return 0.0;
    }
    double max_overshoot = 0.0;
    double previous = errors.front();
    for (std::size_t index = 1; index < errors.size(); ++index) {
        const double current = errors[index];
        max_overshoot =
            std::max(max_overshoot, qualifying_overshoot(previous, current, threshold_px));
        previous = current;
    }
    return max_overshoot;
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
    const CliOptions& options) {
    ScenarioMetrics metrics;
    metrics.name = "tracker_random_fov_100hz";
    metrics.has_random_fov = true;
    metrics.random_fov_min_scale = options.random_fov_min_scale;
    metrics.random_fov_max_scale = options.random_fov_max_scale;
    metrics.random_fov_ai_force_scale = options.random_fov_ai_force_scale;
    if (options.random_fov_ticks <= 0) {
        return metrics;
    }

    constexpr double kControllerHz = 1000.0;
    constexpr double kVisionHz = 100.0;
    constexpr double kDtSeconds = 1.0 / kControllerHz;
    constexpr int kVisionIntervalTicks =
        static_cast<int>(kControllerHz / kVisionHz);
    constexpr double kDirectionDeadzonePx = 6.0;
    constexpr double kOutputDeadzone = 0.015;
    const double reticle_speed =
        std::max(1.0f, config.ai_aim.target_projection_reticle_speed_px_per_sec);

    config.recoil.enabled = false;
    config.aim_assist_dynamics.enabled = false;
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

    controller_native::NativeGamepadController controller(config);
    std::mt19937 rng(options.random_fov_seed);
    std::uniform_real_distribution<double> fov_distribution(
        options.random_fov_min_scale,
        options.random_fov_max_scale);
    std::uniform_int_distribution<int> hold_distribution(45, 115);

    double fov_scale = 1.0;
    double fov_start = 1.0;
    double fov_target = fov_distribution(rng);
    int fov_transition_start_tick = 0;
    int fov_transition_ticks = std::max(
        1,
        static_cast<int>(std::round(
            std::max(1.0f, config.ai_aim.ads_snap_fov_transition_ms))));
    int next_fov_change_tick = fov_transition_ticks + hold_distribution(rng);
    metrics.random_fov_change_events = 1;

    double target_x_position = 48.0;
    double target_y_position = -10.0;
    double reticle_x_position = 0.0;
    double reticle_y_position = 0.0;
    double previous_vision_dx = 0.0;
    bool has_previous_vision_dx = false;
    double fov_sum = 0.0;
    double abs_expected_dx_sum = 0.0;
    double abs_final_x_sum = 0.0;
    double abs_ai_aim_x_sum = 0.0;
    std::vector<double> residual_errors;
    residual_errors.reserve(static_cast<std::size_t>(options.random_fov_ticks));
    std::vector<double> residual_x_errors;
    std::vector<double> residual_y_errors;
    residual_x_errors.reserve(static_cast<std::size_t>(options.random_fov_ticks));
    residual_y_errors.reserve(static_cast<std::size_t>(options.random_fov_ticks));

    const int measure_start_tick = std::min(
        options.random_fov_ticks - 1,
        std::max(0, options.random_fov_ticks / 3));

    for (int tick = 0; tick < options.random_fov_ticks; ++tick) {
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

        const double t = static_cast<double>(tick) * kDtSeconds;
        target_x_position +=
            ((210.0 * std::sin(t * 4.3)) + (85.0 * std::sin(t * 11.0))) *
            kDtSeconds;
        target_y_position += (58.0 * std::sin(t * 3.1)) * kDtSeconds;
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
                    current_seconds()));
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
        const int manual_phase = tick % 260;
        if (manual_phase >= 105 && manual_phase < 140) {
            manual_x = clamp_float(-manual_x * 0.75f, -0.72f, 0.72f);
            manual_y = clamp_float(manual_y * 0.45f, -0.72f, 0.72f);
        } else if (manual_phase >= 140 && manual_phase < 160) {
            manual_x = clamp_float(manual_x * 0.40f, -0.72f, 0.72f);
            manual_y = clamp_float(manual_y * 0.65f, -0.72f, 0.72f);
        } else {
            manual_x = clamp_float(
                manual_x + static_cast<float>(0.055 * std::sin(t * 31.0)),
                -0.72f,
                0.72f);
            manual_y = clamp_float(
                manual_y + static_cast<float>(0.035 * std::sin(t * 19.0)),
                -0.72f,
                0.72f);
        }

        controller.build_output(aiming_state(manual_x, manual_y));
        const controller_native::NativeControllerOutputComponents& components =
            controller.last_output_components();
        add_frame_sample(metrics, components);

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
        reticle_x_position += components.final_stick.x * reticle_speed * kDtSeconds;
        reticle_y_position += -components.final_stick.y * reticle_speed * kDtSeconds;
        const double residual_dx =
            (target_x_position - reticle_x_position) * fov_scale;
        const double residual_dy =
            (target_y_position - reticle_y_position) * fov_scale;
        if (tick >= measure_start_tick) {
            residual_errors.push_back(std::hypot(residual_dx, residual_dy));
            residual_x_errors.push_back(residual_dx);
            residual_y_errors.push_back(residual_dy);
            ++metrics.random_fov_measured_ticks;
        }
        sleep_dt(1000.0 / kControllerHz);
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
    constexpr double kOvershootThresholdPx = 2.0;
    metrics.random_fov_overshoot_events =
        axis_overshoot_count(residual_x_errors, kOvershootThresholdPx) +
        axis_overshoot_count(residual_y_errors, kOvershootThresholdPx);
    metrics.random_fov_max_overshoot_px =
        std::max(
            axis_max_overshoot(residual_x_errors, kOvershootThresholdPx),
            axis_max_overshoot(residual_y_errors, kOvershootThresholdPx));
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
        << "  \"config_path\": \"" << escape_json(options.config_path.string()) << "\",\n"
        << "  \"recoil_state_override\": \"" << escape_json(options.recoil_state_path.string()) << "\",\n"
        << "  \"frames_per_case\": " << options.frames << ",\n"
        << "  \"dt_ms\": " << options.dt_ms << ",\n"
        << "  \"random_fov_ticks\": " << options.random_fov_ticks << ",\n"
        << "  \"random_fov_seed\": " << options.random_fov_seed << ",\n"
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
                << "        \"overshoot_events\": " << scenario.random_fov_overshoot_events << ",\n"
                << "        \"max_overshoot_px\": " << scenario.random_fov_max_overshoot_px << ",\n"
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
                << " overshoots=" << scenario.random_fov_overshoot_events
                << " max_over=" << scenario.random_fov_max_overshoot_px
                << " mean_dx=" << scenario.random_fov_mean_abs_expected_dx
                << " mean_out_x=" << scenario.random_fov_mean_abs_final_x
                << " out_per_100px=" << scenario.random_fov_output_per_100px_error;
        }
        std::cout << "\n";
    }
    std::cout << "[NativeGamepadBenchmark] artifact=" << options.output_path.string() << "\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const CliOptions options = parse_args(argc, argv);
        controller_native::RuntimeConfig runtime_config =
            controller_native::load_runtime_config(options.config_path);
        if (!options.recoil_state_path.empty()) {
            runtime_config.gamepad.recoil.recognizer_state_path =
                options.recoil_state_path.string();
        }

        std::vector<ScenarioMetrics> scenarios;
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
        if (options.random_fov_ticks > 0) {
            scenarios.push_back(run_tracker_random_fov_100hz(
                runtime_config.gamepad,
                options));
        }

        write_json(options, runtime_config, scenarios);
        print_summary(options, scenarios);
        return 0;
    } catch (const std::exception& exc) {
        std::cerr << "[NativeGamepadBenchmark][Error] " << exc.what() << "\n";
        return 1;
    }
}
