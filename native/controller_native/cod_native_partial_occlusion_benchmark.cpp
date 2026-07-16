#include "native_gamepad_controller.h"
#include "partial_occlusion_benchmark.h"
#include "runtime_config.h"

#include "pipeline_contract/target_snapshot.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using controller_native::partial_occlusion::HumanErrorKind;
using controller_native::partial_occlusion::PartialOcclusionMetrics;
using controller_native::partial_occlusion::ScenarioCase;
using controller_native::partial_occlusion::ScenarioKind;
using controller_native::partial_occlusion::ScenarioReport;

struct CliOptions {
    std::filesystem::path config_path = "config.toml";
    std::filesystem::path output_path =
        "runs/benchmarks/partial_occlusion_current_seed1337.json";
    std::uint32_t seed = 1337;
};

struct Vec2 {
    double x = 0.0;
    double y = 0.0;
};

struct CaseTrace {
    std::vector<double> errors;
    std::vector<double> output_deltas;
    std::vector<double> recovery_ms;
    double final_error = 0.0;
};

CliOptions parse_args(int argc, char** argv) {
    CliOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--config" && index + 1 < argc) {
            options.config_path = argv[++index];
        } else if (arg == "--output" && index + 1 < argc) {
            options.output_path = argv[++index];
        } else if (arg == "--seed" && index + 1 < argc) {
            options.seed = static_cast<std::uint32_t>(std::stoul(argv[++index]));
        } else if (arg == "--help") {
            std::cout << "Usage: cod_native_partial_occlusion_benchmark "
                      << "[--config path] [--output path] [--seed n]\n";
            std::exit(0);
        } else {
            throw std::runtime_error("unknown or incomplete argument: " + arg);
        }
    }
    return options;
}

double percentile(std::vector<double> values, double fraction) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const std::size_t index = static_cast<std::size_t>(std::ceil(
        std::clamp(fraction, 0.0, 1.0) * values.size())) - 1;
    return values[std::min(index, values.size() - 1)];
}

double alignment(const Vec2& left, const Vec2& right) {
    const double left_length = std::hypot(left.x, left.y);
    const double right_length = std::hypot(right.x, right.y);
    if (left_length <= 0.03 || right_length <= 1e-6) return 0.0;
    return (left.x * right.x + left.y * right.y) /
        (left_length * right_length);
}

bool phase_is_occluded(int elapsed_ms, const ScenarioCase& value) {
    const int occlusion_start = value.full_observed_ms;
    const int occlusion_end =
        value.full_observed_ms + value.partial_observed_ms +
        value.observation_gap_ms + value.biased_reacquisition_ms;
    return elapsed_ms >= occlusion_start && elapsed_ms < occlusion_end;
}

controller_native::NativeControllerVisionState observed_state(
    const ScenarioCase& value,
    const Vec2& true_error,
    int elapsed_ms,
    double now_seconds,
    std::uint64_t sequence,
    double& geometry_bias) {
    controller_native::NativeControllerVisionState state;
    state.vision_sequence = sequence;
    state.selected_observation_id = sequence;
    state.selected_track_id = 1;
    state.selected_backing_frame_id = sequence;
    state.authority_decision_valid = true;
    state.fresh_observation = true;
    state.current_observed_target_present = true;
    state.has_target = true;
    state.aim_authority = true;
    state.fire_authority = false;
    state.target_tier = "observed_strong";
    state.assist_authority_state = pipeline_contract::AssistAuthorityState::ObservedStrong;
    state.assist_authority_reason = pipeline_contract::AssistAuthorityReason::StrongObserved;
    state.observed_at_seconds = now_seconds;
    state.screen_center_x = 320.0f;
    state.screen_center_y = 256.0f;

    const int partial_start = value.full_observed_ms;
    const int missing_start = partial_start + value.partial_observed_ms;
    const int reacquire_start = missing_start + value.observation_gap_ms;
    const int stable_start = reacquire_start + value.biased_reacquisition_ms;
    double body_height = value.full_body_height_px;
    Vec2 observation = true_error;

    if (elapsed_ms >= partial_start && elapsed_ms < missing_start) {
        const double progress = static_cast<double>(elapsed_ms - partial_start) /
            std::max(1, value.partial_observed_ms);
        body_height = value.full_body_height_px +
            (value.partial_body_height_px - value.full_body_height_px) * progress;
    } else if (elapsed_ms >= reacquire_start && elapsed_ms < stable_start) {
        const double progress = static_cast<double>(elapsed_ms - reacquire_start) /
            std::max(1, value.biased_reacquisition_ms);
        body_height = value.partial_body_height_px +
            (value.full_body_height_px - value.partial_body_height_px) * progress;
        observation.x += value.direction_x * 12.0 * (1.0 - progress);
        observation.y += value.direction_y * 9.0 * (1.0 - progress);
    }

    state.dx = static_cast<float>(observation.x);
    state.dy = static_cast<float>(observation.y);
    state.target_x = state.screen_center_x + state.dx;
    state.target_y = state.screen_center_y + state.dy;
    state.has_body_box = true;
    state.body_x1 = state.target_x - static_cast<float>(value.full_body_width_px * 0.5);
    state.body_x2 = state.target_x + static_cast<float>(value.full_body_width_px * 0.5);
    state.body_y1 = state.target_y - static_cast<float>(
        value.full_body_height_px * value.truth_aim_height_ratio);
    state.body_y2 = state.body_y1 + static_cast<float>(body_height);

    const double observed_chest =
        state.body_y1 + body_height * value.truth_aim_height_ratio;
    geometry_bias = std::hypot(
        observation.x - true_error.x,
        observed_chest - state.screen_center_y - true_error.y);
    return state;
}

controller_native::NativeControllerVisionState missing_state(
    double now_seconds,
    std::uint64_t sequence) {
    controller_native::NativeControllerVisionState state;
    state.vision_sequence = sequence;
    state.authority_decision_valid = true;
    state.fresh_observation = false;
    state.current_observed_target_present = false;
    state.has_target = false;
    state.aim_authority = false;
    state.fire_authority = false;
    state.target_tier = "none";
    state.assist_authority_state = pipeline_contract::AssistAuthorityState::Reject;
    state.assist_authority_reason = pipeline_contract::AssistAuthorityReason::None;
    state.observed_at_seconds = now_seconds;
    state.screen_center_x = 320.0f;
    state.screen_center_y = 256.0f;
    return state;
}

Vec2 manual_for(
    const ScenarioCase& value,
    const Vec2& delayed_error,
    int elapsed_ms) {
    Vec2 manual{
        std::clamp(delayed_error.x / 150.0, -value.manual_magnitude_cap, value.manual_magnitude_cap),
        std::clamp(-delayed_error.y / 150.0, -value.manual_magnitude_cap, value.manual_magnitude_cap),
    };
    const double drift = std::sin(elapsed_ms * 0.017) * 0.0118;
    manual.x = std::clamp(manual.x + drift, -value.manual_magnitude_cap, value.manual_magnitude_cap);
    manual.y = std::clamp(manual.y - drift, -value.manual_magnitude_cap, value.manual_magnitude_cap);

    const int error_start = value.full_observed_ms + value.partial_observed_ms;
    const int error_end = error_start + value.observation_gap_ms + value.error_hold_ms;
    if (elapsed_ms < error_start || elapsed_ms >= error_end) return manual;
    switch (value.error_kind) {
        case HumanErrorKind::None:
            break;
        case HumanErrorKind::StaleDirection:
            manual.x = -manual.x;
            manual.y = -manual.y;
            break;
        case HumanErrorKind::WrongX:
            manual.x = -manual.x;
            break;
        case HumanErrorKind::WrongY:
            manual.y = -manual.y;
            break;
        case HumanErrorKind::CrossingInertia:
            manual.x = value.direction_x * value.manual_magnitude_cap;
            manual.y = -value.direction_y * value.manual_magnitude_cap * 0.55;
            break;
    }
    return manual;
}

void accumulate_case(
    const ScenarioCase& value,
    const controller_native::GamepadRuntimeConfig& source_config,
    PartialOcclusionMetrics& metrics,
    CaseTrace& aggregate_trace) {
    constexpr double kDt = 0.001;
    constexpr double kReticleSpeedPxPerSecond = 1500.0;
    constexpr int kVisionIntervalTicks = 10;
    controller_native::GamepadRuntimeConfig config = source_config;
    config.recoil.enabled = false;

    double simulated_now = 10.0 + value.index * 2.0;
    controller_native::NativeGamepadController controller(
        config,
        [&simulated_now]() { return simulated_now; });

    Vec2 target{
        value.direction_x * 92.0,
        value.direction_y * 48.0,
    };
    Vec2 reticle;
    std::vector<Vec2> error_history;
    error_history.reserve(800);
    Vec2 previous_output;
    std::string previous_mode;
    const Vec2 initial_error = target;
    bool recovery_recorded = false;
    int recovery_hold = 0;
    const int stable_start_ms =
        value.full_observed_ms + value.partial_observed_ms +
        value.observation_gap_ms + value.biased_reacquisition_ms;
    const int total_ms = stable_start_ms + value.stable_recovery_ms;
    std::uint64_t sequence = 0;

    double max_wrong_side_x = 0.0;
    double max_wrong_side_y = 0.0;
    double final_error = 0.0;

    for (int tick = 0; tick < total_ms; ++tick) {
        simulated_now += kDt;
        const double ego_velocity_x = -value.left_stick_x * 110.0;
        target.x += (value.target_velocity_x_px_per_sec + ego_velocity_x) * kDt;
        target.y += value.target_velocity_y_px_per_sec * kDt;
        const Vec2 true_error{target.x - reticle.x, target.y - reticle.y};
        error_history.push_back(true_error);

        if (tick % kVisionIntervalTicks == 0) {
            ++sequence;
            ++metrics.vision_samples;
            const int missing_start = value.full_observed_ms + value.partial_observed_ms;
            const int missing_end = missing_start + value.observation_gap_ms;
            if (tick >= missing_start && tick < missing_end) {
                controller.submit_vision_state(missing_state(simulated_now, sequence));
                ++metrics.missing_vision_samples;
            } else {
                double geometry_bias = 0.0;
                controller.submit_vision_state(observed_state(
                    value,
                    true_error,
                    tick,
                    simulated_now,
                    sequence,
                    geometry_bias));
                metrics.peak_geometry_bias_px = std::max(
                    metrics.peak_geometry_bias_px,
                    geometry_bias);
            }
        }

        const int delay_ticks = value.manual_reaction_ms;
        const Vec2 delayed_error = error_history.size() > static_cast<std::size_t>(delay_ticks)
            ? error_history[error_history.size() - delay_ticks - 1]
            : error_history.front();
        const Vec2 manual = manual_for(value, delayed_error, tick);
        controller_native::PhysicalGamepadState physical;
        physical.connected = true;
        physical.left_trigger = 1.0f;
        physical.left_x = static_cast<float>(value.left_stick_x);
        physical.right_x = static_cast<float>(manual.x);
        physical.right_y = static_cast<float>(manual.y);
        controller.build_output(physical);

        const auto& components = controller.last_output_components();
        const Vec2 output{components.final_stick.x, components.final_stick.y};
        const Vec2 ai{components.ai_aim_stick.x, components.ai_aim_stick.y};
        reticle.x += output.x * kReticleSpeedPxPerSecond * kDt;
        reticle.y += -output.y * kReticleSpeedPxPerSecond * kDt;

        const Vec2 error_after{target.x - reticle.x, target.y - reticle.y};
        const double radial_error = std::hypot(error_after.x, error_after.y);
        aggregate_trace.errors.push_back(radial_error);
        final_error = radial_error;
        metrics.peak_error_px = std::max(metrics.peak_error_px, radial_error);
        if (phase_is_occluded(tick, value)) {
            metrics.occlusion_peak_error_px = std::max(
                metrics.occlusion_peak_error_px,
                radial_error);
        }
        if (error_after.x * initial_error.x < 0.0) {
            max_wrong_side_x = std::max(max_wrong_side_x, std::fabs(error_after.x));
        }
        if (error_after.y * initial_error.y < 0.0) {
            max_wrong_side_y = std::max(max_wrong_side_y, std::fabs(error_after.y));
        }

        const double output_delta = std::hypot(
            output.x - previous_output.x,
            output.y - previous_output.y);
        aggregate_trace.output_deltas.push_back(output_delta);
        if (output_delta > 0.20) ++metrics.output_spikes;
        previous_output = output;

        const std::string mode = controller.last_ai_aim_mode();
        if (mode == "ads_snap") ++metrics.ads_snap_frames;
        else if (mode == "body_lock") ++metrics.body_lock_frames;
        else ++metrics.manual_frames;
        if (!previous_mode.empty() && mode != previous_mode) ++metrics.mode_changes;
        previous_mode = mode;

        const Vec2 desired{true_error.x, -true_error.y};
        const double manual_alignment = alignment(manual, desired);
        const double manual_ai_alignment = alignment(manual, ai);
        const double ai_magnitude = std::hypot(ai.x, ai.y);
        if (manual_alignment > 0.25 && manual_ai_alignment < -0.35 && ai_magnitude > 0.05) {
            ++metrics.correct_manual_opposition_frames;
        }
        if (manual_alignment < -0.25 && manual_ai_alignment < -0.35 && ai_magnitude > 0.45) {
            ++metrics.wrong_manual_high_force_frames;
        }

        if (tick >= stable_start_ms && !recovery_recorded) {
            if (radial_error <= 20.0) {
                ++recovery_hold;
                if (recovery_hold >= 20) {
                    aggregate_trace.recovery_ms.push_back(
                        tick - stable_start_ms - recovery_hold + 1);
                    recovery_recorded = true;
                }
            } else {
                recovery_hold = 0;
            }
        }
        ++metrics.measured_frames;
    }

    if (!recovery_recorded) {
        aggregate_trace.recovery_ms.push_back(value.stable_recovery_ms);
    }
    aggregate_trace.final_error += final_error;
    metrics.max_overshoot_x_px = std::max(metrics.max_overshoot_x_px, max_wrong_side_x);
    metrics.max_overshoot_y_px = std::max(metrics.max_overshoot_y_px, max_wrong_side_y);
}

ScenarioReport run_scenario(
    ScenarioKind kind,
    const controller_native::GamepadRuntimeConfig& config,
    std::uint32_t seed) {
    const auto definition = controller_native::partial_occlusion::build_scenario(kind, seed);
    ScenarioReport report;
    report.name = definition.name;
    report.metrics.cases = static_cast<int>(definition.cases.size());
    CaseTrace trace;
    for (const auto& value : definition.cases) {
        accumulate_case(value, config, report.metrics, trace);
    }
    if (!trace.errors.empty()) {
        report.metrics.mean_error_px = std::accumulate(
            trace.errors.begin(), trace.errors.end(), 0.0) / trace.errors.size();
        report.metrics.p95_error_px = percentile(trace.errors, 0.95);
    }
    report.metrics.final_error_px = definition.cases.empty()
        ? 0.0
        : trace.final_error / definition.cases.size();
    report.metrics.mean_recovery_ms = trace.recovery_ms.empty()
        ? 0.0
        : std::accumulate(trace.recovery_ms.begin(), trace.recovery_ms.end(), 0.0) /
            trace.recovery_ms.size();
    report.metrics.p95_output_delta = percentile(trace.output_deltas, 0.95);
    report.score = controller_native::partial_occlusion::score_metrics(report.metrics);
    return report;
}

controller_native::partial_occlusion::BenchmarkMetadata metadata_from(
    const controller_native::RuntimeConfig& config,
    const CliOptions& options) {
    controller_native::partial_occlusion::BenchmarkMetadata metadata;
    metadata.seed = options.seed;
    metadata.config_path = options.config_path.string();
    metadata.aim_height_ratio = config.gamepad.tracker.aim_height_ratio;
    metadata.ads_strength_scale = config.ads.strength_scale;
    metadata.ads_vertical_strength_scale = config.ads.vertical_strength_scale;
    metadata.ads_range_px = config.gamepad.ai_aim.max_pixels;
    metadata.ads_snap_duration_ms = config.gamepad.ai_aim.ads_snap_window_ms;
    metadata.bodylock_strength = config.gamepad.ai_aim.body_lock_max_ai_force;
    metadata.bodylock_vertical_strength = config.gamepad.ai_aim.body_lock_max_ai_force_y;
    metadata.bodylock_activation_range_px = config.gamepad.ai_aim.body_lock_activation_box_px;
    metadata.bodylock_tolerance_px = config.gamepad.ai_aim.body_lock_box_tolerance_px;
    return metadata;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const CliOptions options = parse_args(argc, argv);
        const controller_native::RuntimeConfig config =
            controller_native::load_runtime_config(options.config_path);
        const std::vector<ScenarioReport> reports{
            run_scenario(ScenarioKind::Combat, config.gamepad, options.seed),
            run_scenario(ScenarioKind::HumanErrors, config.gamepad, options.seed),
        };
        const std::string json = controller_native::partial_occlusion::render_report_json(
            metadata_from(config, options),
            reports);
        if (!options.output_path.parent_path().empty()) {
            std::filesystem::create_directories(options.output_path.parent_path());
        }
        std::ofstream output(options.output_path, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("unable to write output: " + options.output_path.string());
        output << json;
        for (const auto& report : reports) {
            std::cout << std::fixed << std::setprecision(2)
                      << report.name
                      << " overall=" << report.score.overall
                      << " tracking=" << report.score.tracking
                      << " overshoot=" << report.score.overshoot
                      << " recovery=" << report.score.recovery
                      << " smoothness=" << report.score.smoothness
                      << " intent=" << report.score.intent
                      << " mean_error_px=" << report.metrics.mean_error_px
                      << " p95_error_px=" << report.metrics.p95_error_px
                      << "\n";
        }
        std::cout << "output=" << options.output_path.string() << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[PartialOcclusionBenchmark][Error] " << error.what() << "\n";
        return 1;
    }
}
