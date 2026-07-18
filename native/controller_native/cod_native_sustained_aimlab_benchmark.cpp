#include "native_gamepad_controller.h"
#include "runtime_config.h"
#include "sustained_aimlab_simulator.h"

#include "common_native/authority_types.h"
#include "pipeline_contract/target_snapshot.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using controller_native::ControllerVisionSnapshot;
using controller_native::GamepadRuntimeConfig;
using controller_native::NativeGamepadController;
using controller_native::PhysicalGamepadState;
using controller_native::RuntimeConfig;
using namespace controller_native::sustained_aimlab;

struct CliOptions {
    std::filesystem::path config_path = "config.toml";
    std::vector<std::uint32_t> seeds;
    std::filesystem::path output_path;
    std::string profile = "both";
    std::string cohort = "ads";
    std::string target_profile = "ordinary";
    double camera_response = 500.0;
    double slowdown_edge = 0.50;
    double slowdown_center = 0.40;
    std::string revision = "unknown";
    bool dirty = false;
    int duration_ms = 60'000;
    bool smoke = false;
};

CliOptions parse_args(int argc, char** argv) {
    CliOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--config" && index + 1 < argc) {
            options.config_path = argv[++index];
        } else if (argument == "--seed" && index + 1 < argc) {
            options.seeds.push_back(
                static_cast<std::uint32_t>(std::stoul(argv[++index])));
        } else if (argument == "--output" && index + 1 < argc) {
            options.output_path = argv[++index];
        } else if (argument == "--profile" && index + 1 < argc) {
            options.profile = argv[++index];
        } else if (argument == "--cohort" && index + 1 < argc) {
            options.cohort = argv[++index];
        } else if (argument == "--target-profile" && index + 1 < argc) {
            options.target_profile = argv[++index];
        } else if (argument == "--camera-response" && index + 1 < argc) {
            options.camera_response = std::stod(argv[++index]);
        } else if (argument == "--slowdown-edge" && index + 1 < argc) {
            options.slowdown_edge = std::stod(argv[++index]);
        } else if (argument == "--slowdown-center" && index + 1 < argc) {
            options.slowdown_center = std::stod(argv[++index]);
        } else if (argument == "--revision" && index + 1 < argc) {
            options.revision = argv[++index];
        } else if (argument == "--dirty") {
            options.dirty = true;
        } else if (argument == "--duration-ms" && index + 1 < argc) {
            options.duration_ms = std::stoi(argv[++index]);
        } else if (argument == "--smoke") {
            options.smoke = true;
        } else if (argument == "--help") {
            std::cout
                << "Usage: cod_native_sustained_aimlab_benchmark "
                << "[--config PATH] [--seed N ...] [--profile pure|mixed|both] "
                << "[--cohort ads|bodylock|both] "
                << "[--target-profile ordinary|small] [--camera-response PX] "
                << "[--slowdown-edge N] [--slowdown-center N] "
                << "[--output PATH] [--revision HASH] [--dirty] "
                << "[--duration-ms N] [--smoke]\n";
            std::exit(EXIT_SUCCESS);
        } else {
            throw std::runtime_error(
                "unknown or incomplete argument: " + argument);
        }
    }
    if (options.duration_ms <= 0) {
        throw std::runtime_error("duration must be positive");
    }
    if (options.profile != "pure" && options.profile != "mixed" &&
        options.profile != "both") {
        throw std::runtime_error("profile must be pure, mixed, or both");
    }
    if (options.cohort != "ads" && options.cohort != "bodylock" &&
        options.cohort != "both") {
        throw std::runtime_error("cohort must be ads, bodylock, or both");
    }
    if (options.target_profile != "ordinary" &&
        options.target_profile != "small") {
        throw std::runtime_error("target profile must be ordinary or small");
    }
    if (options.camera_response <= 0.0 || options.slowdown_edge <= 0.0 ||
        options.slowdown_edge > 1.0 || options.slowdown_center <= 0.0 ||
        options.slowdown_center > options.slowdown_edge) {
        throw std::runtime_error("invalid plant response or slowdown multipliers");
    }
    if (options.seeds.empty()) {
        options.seeds = {1337, 20260718, 424242};
    }
    return options;
}

std::uint64_t file_fingerprint(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot fingerprint config: " + path.string());
    std::uint64_t hash = 1469598103934665603ULL;
    char byte = 0;
    while (input.get(byte)) {
        hash ^= static_cast<unsigned char>(byte);
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string json_string(const std::string& value) {
    std::ostringstream output;
    output << '"';
    for (const unsigned char ch : value) {
        switch (ch) {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (ch < 0x20) {
                output << "\\u" << std::hex << std::setw(4)
                       << std::setfill('0') << static_cast<int>(ch) << std::dec;
            } else {
                output << ch;
            }
        }
    }
    output << '"';
    return output.str();
}

const char* profile_name(ManualProfile profile) {
    return profile == ManualProfile::Pure ? "pure" : "mixed";
}

const char* cohort_name(BenchmarkCohort cohort) {
    return cohort == BenchmarkCohort::AdsAcquire ? "ads" : "bodylock";
}

const char* motion_name(MotionProfile motion) {
    switch (motion) {
    case MotionProfile::ConstantHorizontal: return "constant_horizontal";
    case MotionProfile::ConstantVertical: return "constant_vertical";
    case MotionProfile::ConstantDiagonal: return "constant_diagonal";
    case MotionProfile::Accelerate: return "accelerate";
    case MotionProfile::Reverse: return "reverse";
    case MotionProfile::JumpFall: return "jump_fall";
    case MotionProfile::Stop: return "stop";
    }
    return "unknown";
}

void write_report(
    const std::filesystem::path& output_path,
    const CliOptions& options,
    const BenchmarkConfig& config,
    std::uint64_t config_fingerprint,
    const std::vector<BenchmarkResult>& results) {
    if (output_path.empty()) return;
    if (std::filesystem::exists(output_path)) {
        throw std::runtime_error("output already exists: " + output_path.string());
    }
    if (!output_path.parent_path().empty()) {
        std::filesystem::create_directories(output_path.parent_path());
    }
    std::filesystem::path partial = output_path;
    partial += ".partial";
    if (std::filesystem::exists(partial)) {
        throw std::runtime_error("partial output already exists: " + partial.string());
    }
    std::ofstream out(partial, std::ios::binary);
    if (!out) throw std::runtime_error("cannot open output: " + partial.string());
    out << std::setprecision(10);
    out << "{\n  \"schema\": \"sustained-aimlab-v1\",\n"
        << "  \"revision\": " << json_string(options.revision) << ",\n"
        << "  \"dirty\": " << (options.dirty ? "true" : "false") << ",\n"
        << "  \"config_path\": " << json_string(options.config_path.string()) << ",\n"
        << "  \"config_fingerprint_fnv1a64\": \"" << config_fingerprint << "\",\n"
        << "  \"simulator\": {\"duration_ms\": " << config.duration_ms
        << ", \"target_profile\": " << json_string(options.target_profile)
        << ", \"tick_ms\": " << config.tick_ms
        << ", \"tracking_window_ms\": " << config.tracking_window_ms
        << ", \"target_radius_px\": " << config.target_radius_px
        << ", \"camera_response_px_per_stick_second\": "
        << config.camera_response_px_per_stick_second
        << ", \"slowdown_edge\": " << config.slowdown_edge_multiplier
        << ", \"slowdown_center\": " << config.slowdown_center_multiplier << "},\n"
        << "  \"runs\": [\n";
    for (std::size_t run_index = 0; run_index < results.size(); ++run_index) {
        const auto& result = results[run_index];
        out << "    {\"seed\": " << result.seed
            << ", \"profile\": " << json_string(profile_name(result.manual_profile))
            << ", \"cohort\": " << json_string(cohort_name(result.cohort))
            << ", \"script_hash\": \"" << result.script_hash << "\""
            << ", \"ticks\": " << result.ticks
            << ", \"acquire_points\": " << result.acquire_points
            << ", \"tracking_points\": " << result.tracking_points
            << ", \"smooth_bonus\": " << result.smooth_bonus
            << ", \"targets_spawned\": " << result.targets_spawned
            << ", \"targets_acquired\": " << result.targets_acquired
            << ", \"targets_missed\": " << result.targets_missed
            << ", \"over_events\": " << result.over_events
            << ", \"undertrack_events\": " << result.undertrack_events
            << ", \"false_interruption_events\": " << result.false_interruption_events
            << ", \"false_stop_events\": " << result.false_stop_events
            << ", \"stale_output_after_stop_events\": " << result.stale_output_after_stop_events
            << ", \"bodylock_entry_failures\": " << result.bodylock_entry_failures
            << ", \"bodylock_active_ms\": " << result.bodylock_active_ms
            << ", \"unexpected_mode_ms\": " << result.unexpected_mode_ms
            << ", \"mean_error_px\": " << result.mean_error_px
            << ", \"p95_error_px\": " << result.p95_error_px
            << ", \"p95_output_delta\": " << result.p95_output_delta
            << ", \"p95_jerk\": " << result.p95_jerk
            << ", \"targets\": [";
        for (std::size_t target_index = 0; target_index < result.targets.size(); ++target_index) {
            const auto& target = result.targets[target_index];
            if (target_index) out << ',';
            out << "{\"id\":" << target.id
                << ",\"motion\":" << json_string(motion_name(target.motion))
                << ",\"deadline_ms\":" << target.deadline_ms
                << ",\"visible_radius_px\":" << target.visible_radius_px
                << ",\"acquired\":" << (target.acquired ? "true" : "false")
                << ",\"first_entry_ms\":" << target.first_entry_ms
                << ",\"bodylock_entry_failed\":" << (target.bodylock_entry_failed ? "true" : "false")
                << ",\"bodylock_entry_ms\":" << target.bodylock_entry_ms
                << ",\"bodylock_active_ms\":" << target.bodylock_active_ms
                << ",\"unexpected_mode_ms\":" << target.unexpected_mode_ms
                << ",\"acquire_points\":" << target.acquire_points
                << ",\"tracking_points\":" << target.tracking_points
                << ",\"smooth_bonus\":" << target.smooth_bonus
                << ",\"over_events\":" << target.over_events
                << ",\"undertrack_events\":" << target.undertrack_events
                << ",\"false_mode_exit_events\":" << target.false_mode_exit_events
                << ",\"false_stop_events\":" << target.false_stop_events << '}';
        }
        out << "]}" << (run_index + 1 == results.size() ? "\n" : ",\n");
    }
    out << "  ]\n}\n";
    out.close();
    if (!out) throw std::runtime_error("failed writing output: " + partial.string());
    std::filesystem::rename(partial, output_path);
}

ControllerVisionSnapshot snapshot_from(
    const ControllerObservation& input,
    double now_seconds) {
    ControllerVisionSnapshot snapshot;
    snapshot.frame_updated = true;
    snapshot.selector_identity_protocol = true;
    snapshot.frame_id = input.frame_id;
    snapshot.selected_observation_id = input.target_present ? input.target_id : 0;
    snapshot.capture_time_seconds = now_seconds;
    snapshot.ready_time_seconds = now_seconds;
    snapshot.state.screen_center_x = 320.0f;
    snapshot.state.screen_center_y = 256.0f;
    snapshot.state.fresh_observation = true;
    if (!input.target_present) return snapshot;

    pipeline_contract::VisionCandidateSnapshot candidate;
    candidate.id = input.target_id;
    candidate.valid = true;
    candidate.has_aim_point = true;
    candidate.aim_point_px = {
        static_cast<float>(320.0 + input.observed_error_px.x),
        static_cast<float>(256.0 + input.observed_error_px.y),
    };
    candidate.confidence = 0.95f;
    candidate.suggested_authority_state =
        common_native::TargetAuthorityState::StrongAssist;
    snapshot.candidates.push_back(candidate);
    return snapshot;
}

BenchmarkResult run_native(
    const ScenarioScript& script,
    const GamepadRuntimeConfig& source_config,
    ManualProfile profile,
    BenchmarkCohort cohort,
    bool& saw_assisted_mode) {
    double now_seconds = 0.0;
    GamepadRuntimeConfig config = source_config;
    config.recoil.enabled = false;
    NativeGamepadController controller(config, [&] { return now_seconds; });
    PhysicalGamepadState physical;
    physical.connected = true;
    physical.left_trigger = 1.0f;

    ControllerStep adapter = [&](const ControllerObservation& input) {
        now_seconds = static_cast<double>(input.now_ms) / 1000.0;
        physical.right_x = static_cast<float>(input.manual_stick.x);
        physical.right_y = static_cast<float>(input.manual_stick.y);
        if (input.fresh_vision) {
            controller.submit_vision_snapshot(snapshot_from(input, now_seconds));
        }
        const auto output = controller.build_output(physical);
        const auto& components = controller.last_output_components();
        const std::string& mode = controller.last_ai_aim_mode();
        if (mode == "ads_snap" || mode == "body_lock") {
            saw_assisted_mode = true;
        }
        const auto& vision = controller.last_frame_vision_state();
        return ControllerStepResult{
            {output.right_x, output.right_y},
            {components.requested_assist_stick.x,
             components.requested_assist_stick.y},
            {components.shaped_assist_stick.x,
             components.shaped_assist_stick.y},
            mode == "body_lock",
            vision.current_observed_target_present,
            vision.has_target,
        };
    };
    return run_simulation(script, profile, std::move(adapter), cohort);
}

void validate_smoke(
    const BenchmarkResult& result,
    int expected_ticks,
    bool saw_assisted_mode) {
    const bool finite = std::isfinite(result.acquire_points) &&
        std::isfinite(result.tracking_points) &&
        std::isfinite(result.smooth_bonus);
    if (!finite || result.acquire_points < 0.0 ||
        result.tracking_points < 0.0 || result.smooth_bonus < 0.0) {
        throw std::runtime_error("smoke run produced invalid scores");
    }
    if (result.ticks != expected_ticks) {
        throw std::runtime_error("smoke run tick count mismatch");
    }
    if (result.script_hash == 0) {
        throw std::runtime_error("smoke run lost scenario hash");
    }
    if (!saw_assisted_mode) {
        throw std::runtime_error("smoke run never entered an assisted aim mode");
    }
}

void print_summary(const BenchmarkResult& result) {
    std::cout
        << "seed=" << result.seed
        << " ticks=" << result.ticks
        << " cohort=" << cohort_name(result.cohort)
        << " script_hash=" << result.script_hash
        << " acquire_points=" << result.acquire_points
        << " tracking_points=" << result.tracking_points
        << " smooth_bonus=" << result.smooth_bonus
        << " acquired=" << result.targets_acquired
        << " missed=" << result.targets_missed
        << " over=" << result.over_events
        << " undertrack=" << result.undertrack_events
        << " false_interrupt=" << result.false_interruption_events
        << " false_stop=" << result.false_stop_events
        << " bodylock_entry_fail=" << result.bodylock_entry_failures
        << " bodylock_active_ms=" << result.bodylock_active_ms
        << " unexpected_mode_ms=" << result.unexpected_mode_ms
        << "\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const CliOptions options = parse_args(argc, argv);
        const RuntimeConfig runtime =
            controller_native::load_runtime_config(options.config_path);
        BenchmarkConfig benchmark_config;
        benchmark_config.duration_ms = options.duration_ms;
        benchmark_config.target_profile = options.target_profile == "small"
            ? TargetProfile::SmallVisible
            : TargetProfile::Ordinary;
        benchmark_config.camera_response_px_per_stick_second =
            options.camera_response;
        benchmark_config.slowdown_edge_multiplier = options.slowdown_edge;
        benchmark_config.slowdown_center_multiplier = options.slowdown_center;
        std::vector<ManualProfile> profiles;
        if (options.profile != "mixed") profiles.push_back(ManualProfile::Pure);
        if (options.profile != "pure") profiles.push_back(ManualProfile::Mixed);
        std::vector<BenchmarkCohort> cohorts;
        if (options.cohort != "bodylock") cohorts.push_back(BenchmarkCohort::AdsAcquire);
        if (options.cohort != "ads") cohorts.push_back(BenchmarkCohort::BodyLockFollow);
        std::vector<BenchmarkResult> results;
        for (const std::uint32_t seed : options.seeds) {
            const ScenarioScript script = generate_script(seed, benchmark_config);
            for (const ManualProfile profile : profiles) {
                for (const BenchmarkCohort cohort : cohorts) {
                    bool saw_assisted_mode = false;
                    BenchmarkResult result = run_native(
                        script, runtime.gamepad, profile, cohort, saw_assisted_mode);
                    if (options.smoke) {
                        validate_smoke(result, options.duration_ms, saw_assisted_mode);
                    }
                    print_summary(result);
                    results.push_back(std::move(result));
                }
            }
        }
        write_report(
            options.output_path,
            options,
            benchmark_config,
            file_fingerprint(options.config_path),
            results);
        std::cout << "cod_native_sustained_aimlab_benchmark PASS\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "cod_native_sustained_aimlab_benchmark FAIL: "
                  << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
