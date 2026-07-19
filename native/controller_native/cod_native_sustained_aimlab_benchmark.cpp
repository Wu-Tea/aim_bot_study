#include "native_gamepad_controller.h"
#include "runtime_config.h"
#include "sustained_aimlab_counterfactual.h"
#include "sustained_aimlab_simulator.h"
#include "sustained_aimlab_trace.h"

#include "common_native/authority_types.h"
#include "pipeline_contract/target_snapshot.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using controller_native::ControllerVisionSnapshot;
using controller_native::GamepadRuntimeConfig;
using controller_native::NativeGamepadController;
using controller_native::BenchmarkIntentFusionMode;
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
    std::string counterfactual = "off";
    std::string intent_fusion = "legacy";
};

struct CounterfactualEpisodeSummary {
    int branch_at_ms = 0;
    std::string source;
    std::string source_kind;
    BranchPolicy causal_policy = BranchPolicy::ActualMix;
    BranchPolicy hindsight_policy = BranchPolicy::ActualMix;
    CounterfactualMetrics metrics;
    double actual_error_area_px_ms = 0.0;
    double causal_error_area_px_ms = 0.0;
    double hindsight_error_area_px_ms = 0.0;
};

struct CounterfactualRunSummary {
    std::string mode = "off";
    int fixed_anchors_detected = 0;
    int fixed_anchors_analyzed = 0;
    int fixed_anchors_skipped = 0;
    int dynamic_episodes_detected = 0;
    int dynamic_episodes_analyzed = 0;
    int dynamic_episodes_skipped = 0;
    double regret_40_px_ms = 0.0;
    double regret_80_px_ms = 0.0;
    double regret_160_px_ms = 0.0;
    double future_burden_px_ms = 0.0;
    int future_settle_delay_ms = 0;
    double causal_error_area_gap_px_ms = 0.0;
    double hindsight_headroom_px_ms = 0.0;
    int manual_helped_but_suppressed_ms = 0;
    int ai_helped_but_suppressed_ms = 0;
    int both_harmful_ms = 0;
    int destructive_stack_ms = 0;
    int wrong_way_commit_ms = 0;
    std::vector<CounterfactualEpisodeSummary> worst_episodes;
};

struct FusionRunSummary {
    std::array<std::uint64_t, 6> candidate_ticks{};
    std::uint64_t fallback_ticks = 0;
    std::uint64_t manual_escape_ticks = 0;
    double manual_weight_sum = 0.0;
    double ai_weight_sum = 0.0;
    std::uint64_t ticks = 0;
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
        } else if (argument == "--counterfactual" && index + 1 < argc) {
            options.counterfactual = argv[++index];
        } else if (argument == "--intent-fusion" && index + 1 < argc) {
            options.intent_fusion = argv[++index];
        } else if (argument == "--help") {
            std::cout
                << "Usage: cod_native_sustained_aimlab_benchmark "
                << "[--config PATH] [--seed N ...] [--profile pure|mixed|both] "
                << "[--cohort ads|bodylock|both] "
                << "[--target-profile ordinary|small] [--camera-response PX] "
                << "[--slowdown-edge N] [--slowdown-center N] "
                << "[--output PATH] [--revision HASH] [--dirty] "
                << "[--duration-ms N] [--smoke] "
                << "[--counterfactual off|quick|full] "
                << "[--intent-fusion legacy|vector]\n";
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
    if (options.counterfactual != "off" &&
        options.counterfactual != "quick" &&
        options.counterfactual != "full") {
        throw std::runtime_error(
            "counterfactual mode must be off, quick, or full");
    }
    if (options.intent_fusion != "legacy" &&
        options.intent_fusion != "vector") {
        throw std::runtime_error(
            "intent fusion mode must be legacy or vector");
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

const char* anchor_name(AnchorKind kind) {
    switch (kind) {
    case AnchorKind::TargetReverse: return "target_reverse";
    case AnchorKind::TargetStop: return "target_stop";
    case AnchorKind::JumpApex: return "jump_apex";
    case AnchorKind::FallTransition: return "fall_transition";
    case AnchorKind::ObservationLoss: return "observation_loss";
    case AnchorKind::ObservationRecovery: return "observation_recovery";
    case AnchorKind::AdsSettled: return "ads_settled";
    case AnchorKind::AdsBodylockHandoff: return "ads_bodylock_handoff";
    }
    return "unknown";
}

const char* conflict_name(ConflictKind kind) {
    switch (kind) {
    case ConflictKind::ManualAiOpposition: return "manual_ai_opposition";
    case ConflictKind::NearZeroCancellation: return "near_zero_cancellation";
    case ConflictKind::StallRing: return "stall_ring";
    case ConflictKind::DirectionReversal: return "direction_reversal";
    case ConflictKind::CircleExit: return "circle_exit";
    case ConflictKind::FalseInterruption: return "false_interruption";
    case ConflictKind::WrongWayCommit: return "wrong_way_commit";
    case ConflictKind::DestructiveStack: return "destructive_stack";
    }
    return "unknown";
}

void write_counterfactual_summary(
    std::ostream& out, const CounterfactualRunSummary& summary) {
    out << "{\"mode\":" << json_string(summary.mode)
        << ",\"fixed_anchors_detected\":" << summary.fixed_anchors_detected
        << ",\"fixed_anchors_analyzed\":" << summary.fixed_anchors_analyzed
        << ",\"fixed_anchors_skipped\":" << summary.fixed_anchors_skipped
        << ",\"dynamic_episodes_detected\":"
        << summary.dynamic_episodes_detected
        << ",\"dynamic_episodes_analyzed\":"
        << summary.dynamic_episodes_analyzed
        << ",\"dynamic_episodes_skipped\":"
        << summary.dynamic_episodes_skipped
        << ",\"analyzed_episodes\":"
        << summary.fixed_anchors_analyzed + summary.dynamic_episodes_analyzed
        << ",\"skipped_episodes\":"
        << summary.fixed_anchors_skipped + summary.dynamic_episodes_skipped
        << ",\"regret_40_px_ms\":" << summary.regret_40_px_ms
        << ",\"regret_80_px_ms\":" << summary.regret_80_px_ms
        << ",\"regret_160_px_ms\":" << summary.regret_160_px_ms
        << ",\"future_burden_px_ms\":" << summary.future_burden_px_ms
        << ",\"future_settle_delay_ms\":"
        << summary.future_settle_delay_ms
        << ",\"causal_error_area_gap_px_ms\":"
        << summary.causal_error_area_gap_px_ms
        << ",\"hindsight_headroom_px_ms\":"
        << summary.hindsight_headroom_px_ms
        << ",\"manual_helped_but_suppressed_ms\":"
        << summary.manual_helped_but_suppressed_ms
        << ",\"ai_helped_but_suppressed_ms\":"
        << summary.ai_helped_but_suppressed_ms
        << ",\"both_harmful_ms\":" << summary.both_harmful_ms
        << ",\"destructive_stack_ms\":" << summary.destructive_stack_ms
        << ",\"wrong_way_commit_ms\":" << summary.wrong_way_commit_ms
        << ",\"worst_episodes\":[";
    for (std::size_t index = 0; index < summary.worst_episodes.size(); ++index) {
        if (index) out << ',';
        const auto& episode = summary.worst_episodes[index];
        out << "{\"branch_at_ms\":" << episode.branch_at_ms
            << ",\"source\":" << json_string(episode.source)
            << ",\"source_kind\":" << json_string(episode.source_kind)
            << ",\"causal_policy\":"
            << json_string(to_string(episode.causal_policy))
            << ",\"hindsight_policy\":"
            << json_string(to_string(episode.hindsight_policy))
            << ",\"classification\":"
            << json_string(to_string(episode.metrics.classification))
            << ",\"instant_progress_px\":"
            << episode.metrics.instant_progress_px
            << ",\"regret_80_px_ms\":"
            << episode.metrics.regret_80_px_ms
            << ",\"future_burden_px_ms\":"
            << episode.metrics.future_burden_px_ms
            << ",\"actual_error_area_px_ms\":"
            << episode.actual_error_area_px_ms
            << ",\"causal_error_area_px_ms\":"
            << episode.causal_error_area_px_ms
            << ",\"hindsight_error_area_px_ms\":"
            << episode.hindsight_error_area_px_ms << '}';
    }
    out << "]}";
}

void write_report(
    const std::filesystem::path& output_path,
    const CliOptions& options,
    const BenchmarkConfig& config,
    std::uint64_t config_fingerprint,
    const std::vector<BenchmarkResult>& results,
    const std::vector<CounterfactualRunSummary>& counterfactual_results,
    const std::vector<FusionRunSummary>& fusion_results) {
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
        << "  \"intent_fusion\": {\"schema_version\": 1, \"mode\": "
        << json_string(options.intent_fusion)
        << ", \"candidate_set_version\": 1},\n"
        << "  \"counterfactual_conflict\": {\"schema_version\": 1, "
        << "\"candidate_set_version\": 1, \"mode\": "
        << json_string(options.counterfactual)
        << ", \"primary_oracle\": \"causal_oracle\", "
        << "\"headroom_oracle\": \"hindsight_oracle\", "
        << "\"per_kind_replay_budget\": "
        << (options.counterfactual == "full" ? 4 :
            options.counterfactual == "quick" ? 1 : 0)
        << ", \"global_harm_ratio\": 1.01"
        << ", \"local_horizons_ms\": [40,80,160], "
        << "\"stable_horizon_ms\": "
        << (options.counterfactual == "full" ? 500 :
            options.counterfactual == "quick" ? 160 : 0) << "},\n"
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
            << ", \"settled_targets\": " << result.settled_targets
            << ", \"unsettled_targets\": " << result.unsettled_targets
            << ", \"center_cross_events\": " << result.center_cross_events
            << ", \"max_post_cross_error_px\": " << result.max_post_cross_error_px
            << ", \"p95_post_cross_error_px\": " << result.p95_post_cross_error_px
            << ", \"overshoot_area_px_ms\": " << result.overshoot_area_px_ms
            << ", \"continued_push_after_cross_ms\": " << result.continued_push_after_cross_ms
            << ", \"correction_reversal_events\": " << result.correction_reversal_events
            << ", \"circle_exit_events\": " << result.circle_exit_events
            << ", \"stall_ring_ms\": " << result.stall_ring_ms
            << ", \"direction_discontinuities\": " << result.direction_discontinuities
            << ", \"max_error_px\": " << result.max_error_px
            << ", \"median_first_entry_to_settle_ms\": " << result.median_first_entry_to_settle_ms
            << ", \"p95_first_entry_to_settle_ms\": " << result.p95_first_entry_to_settle_ms
            << ", \"handoff_count\": " << result.handoff_count
            << ", \"max_handoff_residual_px\": " << result.max_handoff_residual_px
            << ", \"max_abs_handoff_closing_speed_px_per_sec\": "
            << result.max_abs_handoff_closing_speed_px_per_sec
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
                << ",\"false_stop_events\":" << target.false_stop_events
                << ",\"settled\":" << (target.settled ? "true" : "false")
                << ",\"center_cross_events\":" << target.center_cross_events
                << ",\"max_post_cross_error_px\":" << target.max_post_cross_error_px
                << ",\"overshoot_area_px_ms\":" << target.overshoot_area_px_ms
                << ",\"continued_push_after_cross_ms\":" << target.continued_push_after_cross_ms
                << ",\"brake_start_distance_px\":" << target.brake_start_distance_px
                << ",\"time_to_zero_radial_speed_ms\":" << target.time_to_zero_radial_speed_ms
                << ",\"first_entry_to_settle_ms\":" << target.first_entry_to_settle_ms
                << ",\"correction_reversal_events\":" << target.correction_reversal_events
                << ",\"circle_exit_events\":" << target.circle_exit_events
                << ",\"stall_ring_ms\":" << target.stall_ring_ms
                << ",\"direction_discontinuities\":" << target.direction_discontinuities
                << ",\"max_error_px\":" << target.max_error_px
                << ",\"handoff_residual_px\":" << target.handoff_residual_px
                << ",\"handoff_closing_speed_px_per_sec\":"
                << target.handoff_closing_speed_px_per_sec << '}';
        }
        const FusionRunSummary& fusion = fusion_results.at(run_index);
        out << "],\"intent_fusion\":{\"candidate_ticks\":[";
        for (std::size_t index = 0; index < fusion.candidate_ticks.size(); ++index) {
            if (index != 0) out << ',';
            out << fusion.candidate_ticks[index];
        }
        const double divisor = fusion.ticks > 0
            ? static_cast<double>(fusion.ticks) : 1.0;
        out << "],\"fallback_ticks\":" << fusion.fallback_ticks
            << ",\"manual_escape_ticks\":" << fusion.manual_escape_ticks
            << ",\"mean_manual_weight\":"
            << fusion.manual_weight_sum / divisor
            << ",\"mean_ai_weight\":" << fusion.ai_weight_sum / divisor
            << "},\"counterfactual\":";
        if (run_index < counterfactual_results.size()) {
            write_counterfactual_summary(out, counterfactual_results[run_index]);
        } else {
            write_counterfactual_summary(out, CounterfactualRunSummary{});
        }
        out << '}' << (run_index + 1 == results.size() ? "\n" : ",\n");
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

class NativeReplayAdapter {
public:
    NativeReplayAdapter(
        GamepadRuntimeConfig source_config,
        BranchSchedule schedule,
        BenchmarkIntentFusionMode intent_fusion_mode,
        std::shared_ptr<bool> saw_assisted_mode)
        : schedule_(schedule),
          config_(std::move(source_config)),
          controller_(config_, [this] { return now_seconds_; }),
          saw_assisted_mode_(std::move(saw_assisted_mode)) {
        physical_.connected = true;
        physical_.left_trigger = 1.0f;
        controller_.set_benchmark_intent_fusion_mode(intent_fusion_mode);
        controller_.set_benchmark_mix_transform(
            [this](float manual_x, float manual_y,
                   float mixed_x, float mixed_y,
                   const controller_native::NativeControllerOutputComponents& components) {
                const bool active = now_ms_ >= schedule_.start_ms &&
                    now_ms_ < schedule_.start_ms + schedule_.duration_ms;
                if (!active) return pipeline_contract::Vec2f{mixed_x, mixed_y};
                const auto ai = components.shaped_assist_stick;
                switch (schedule_.policy) {
                case BranchPolicy::ActualMix:
                    return pipeline_contract::Vec2f{mixed_x, mixed_y};
                case BranchPolicy::Neutral:
                    return pipeline_contract::Vec2f{};
                case BranchPolicy::ManualOnly:
                    return pipeline_contract::Vec2f{manual_x, manual_y};
                case BranchPolicy::AiOnly:
                    return pipeline_contract::Vec2f{ai.x, ai.y};
                case BranchPolicy::ManualPlusAi25:
                    return pipeline_contract::Vec2f{
                        manual_x + ai.x * 0.25f,
                        manual_y + ai.y * 0.25f};
                case BranchPolicy::ManualPlusAi50:
                    return pipeline_contract::Vec2f{
                        manual_x + ai.x * 0.50f,
                        manual_y + ai.y * 0.50f};
                case BranchPolicy::ManualPlusAi75:
                    return pipeline_contract::Vec2f{
                        manual_x + ai.x * 0.75f,
                        manual_y + ai.y * 0.75f};
                }
                return pipeline_contract::Vec2f{mixed_x, mixed_y};
            });
    }

    ControllerStepResult step(const ControllerObservation& input) {
        now_ms_ = input.now_ms;
        now_seconds_ = static_cast<double>(input.now_ms) / 1000.0;
        physical_.right_x = static_cast<float>(input.manual_stick.x);
        physical_.right_y = static_cast<float>(input.manual_stick.y);
        if (input.fresh_vision) {
            controller_.submit_vision_snapshot(snapshot_from(input, now_seconds_));
        }
        const auto output = controller_.build_output(physical_);
        const auto& components = controller_.last_output_components();
        const std::string& mode = controller_.last_ai_aim_mode();
        if (saw_assisted_mode_ &&
            (mode == "ads_snap" || mode == "body_lock")) {
            *saw_assisted_mode_ = true;
        }
        const auto& vision = controller_.last_frame_vision_state();
        const auto& plan = controller_.last_target_plan();
        return ControllerStepResult{
            {output.right_x, output.right_y},
            {components.requested_assist_stick.x,
             components.requested_assist_stick.y},
            {components.shaped_assist_stick.x,
             components.shaped_assist_stick.y},
            {plan.predicted_terminal_error_px.x,
             plan.predicted_terminal_error_px.y},
            plan.radial_closing_velocity_px_per_sec,
            mode == "body_lock",
            vision.current_observed_target_present,
            vision.has_target,
            components.intent_fusion_candidate,
            components.intent_fusion_manual_weight,
            components.intent_fusion_ai_weight,
            components.intent_fusion_fallback,
            components.intent_fusion_manual_escape,
        };
    }

private:
    BranchSchedule schedule_;
    int now_ms_ = 0;
    double now_seconds_ = 0.0;
    GamepadRuntimeConfig config_;
    NativeGamepadController controller_;
    PhysicalGamepadState physical_;
    std::shared_ptr<bool> saw_assisted_mode_;
};

ReplayControllerFactory make_native_factory(
    GamepadRuntimeConfig config,
    BenchmarkIntentFusionMode intent_fusion_mode,
    std::shared_ptr<bool> saw_assisted_mode) {
    config.recoil.enabled = false;
    return [config = std::move(config),
            intent_fusion_mode,
            saw_assisted_mode = std::move(saw_assisted_mode)](
               const BranchSchedule& schedule) {
        auto state = std::make_shared<NativeReplayAdapter>(
            config, schedule, intent_fusion_mode, saw_assisted_mode);
        return [state](const ControllerObservation& input) {
            return state->step(input);
        };
    };
}

FusionRunSummary summarize_fusion(const ReplayReference& reference) {
    FusionRunSummary summary;
    for (const auto& frame : reference.trace) {
        const auto& output = frame.output;
        const int candidate = std::clamp(output.intent_fusion_candidate, 0, 5);
        ++summary.candidate_ticks[static_cast<std::size_t>(candidate)];
        if (output.intent_fusion_fallback) ++summary.fallback_ticks;
        if (output.intent_fusion_manual_escape) ++summary.manual_escape_ticks;
        summary.manual_weight_sum += output.intent_fusion_manual_weight;
        summary.ai_weight_sum += output.intent_fusion_ai_weight;
        ++summary.ticks;
    }
    return summary;
}

const BranchResult& actual_branch(const CounterfactualEpisode& episode) {
    const auto found = std::find_if(
        episode.candidates.begin(), episode.candidates.end(),
        [](const BranchResult& branch) {
            return branch.policy == BranchPolicy::ActualMix;
        });
    if (found == episode.candidates.end()) {
        throw std::runtime_error("counterfactual episode lost actual branch");
    }
    return *found;
}

void accumulate_episode(
    CounterfactualRunSummary& summary,
    const CounterfactualEpisode& episode,
    std::string source,
    std::string source_kind) {
    const BranchResult& actual = actual_branch(episode);
    summary.regret_40_px_ms += episode.actual.regret_40_px_ms;
    summary.regret_80_px_ms += episode.actual.regret_80_px_ms;
    summary.regret_160_px_ms += episode.actual.regret_160_px_ms;
    summary.future_burden_px_ms += episode.actual.future_burden_px_ms;
    summary.future_settle_delay_ms += episode.actual.future_settle_delay_ms;
    summary.causal_error_area_gap_px_ms +=
        actual.error_area_px_ms - episode.causal_oracle.error_area_px_ms;
    summary.hindsight_headroom_px_ms +=
        actual.error_area_px_ms - episode.hindsight_oracle.error_area_px_ms;
    summary.manual_helped_but_suppressed_ms +=
        episode.actual.manual_helped_but_suppressed_ms;
    summary.ai_helped_but_suppressed_ms +=
        episode.actual.ai_helped_but_suppressed_ms;
    summary.both_harmful_ms += episode.actual.both_harmful_ms;
    summary.destructive_stack_ms += episode.actual.destructive_stack_ms;
    summary.wrong_way_commit_ms += episode.actual.wrong_way_commit_ms;
    summary.worst_episodes.push_back({
        episode.branch_at_ms,
        std::move(source),
        std::move(source_kind),
        episode.causal_oracle.policy,
        episode.hindsight_oracle.policy,
        episode.actual,
        actual.error_area_px_ms,
        episode.causal_oracle.error_area_px_ms,
        episode.hindsight_oracle.error_area_px_ms,
    });
}

CounterfactualRunSummary analyze_reference(
    const ReplayReference& reference,
    const ReplayControllerFactory& factory,
    const std::string& mode) {
    CounterfactualRunSummary summary;
    summary.mode = mode;
    if (mode == "off") return summary;
    const std::size_t per_kind_limit = mode == "full" ? 4 : 1;
    AnalysisBudget budget;
    budget.substitution_ms = 80;
    budget.stable_horizon_ms = mode == "full" ? 500 : 160;

    auto anchors = generate_fixed_anchors(reference.script, reference.trace);
    anchors.erase(
        std::remove_if(anchors.begin(), anchors.end(),
            [](const EvaluationPoint& point) { return point.absolute_ms < 0; }),
        anchors.end());
    summary.fixed_anchors_detected = static_cast<int>(anchors.size());
    std::map<AnchorKind, std::size_t> anchors_per_kind;
    for (const EvaluationPoint& anchor : anchors) {
        if (anchors_per_kind[anchor.kind]++ >= per_kind_limit) continue;
        const auto episode = analyze_episode(
            reference, anchor.absolute_ms, factory, budget);
        accumulate_episode(
            summary, episode, "fixed_anchor", anchor_name(anchor.kind));
        ++summary.fixed_anchors_analyzed;
    }
    summary.fixed_anchors_skipped =
        summary.fixed_anchors_detected - summary.fixed_anchors_analyzed;

    const auto detected = detect_conflict_episodes(
        reference.trace, ConflictConfig{});
    const auto selected = select_episode_budget(detected, per_kind_limit);
    summary.dynamic_episodes_detected = static_cast<int>(detected.size());
    for (const ConflictEpisode& conflict : selected) {
        const int branch_at_ms = conflict.peak_ms >= 0
            ? conflict.peak_ms : conflict.start_ms;
        const auto episode = analyze_episode(
            reference, branch_at_ms, factory, budget);
        accumulate_episode(
            summary, episode, "dynamic_conflict", conflict_name(conflict.kind));
        ++summary.dynamic_episodes_analyzed;
    }
    summary.dynamic_episodes_skipped =
        summary.dynamic_episodes_detected - summary.dynamic_episodes_analyzed;

    std::stable_sort(
        summary.worst_episodes.begin(), summary.worst_episodes.end(),
        [](const CounterfactualEpisodeSummary& left,
           const CounterfactualEpisodeSummary& right) {
            if (left.metrics.future_burden_px_ms !=
                right.metrics.future_burden_px_ms) {
                return left.metrics.future_burden_px_ms >
                    right.metrics.future_burden_px_ms;
            }
            if (left.metrics.regret_80_px_ms != right.metrics.regret_80_px_ms) {
                return left.metrics.regret_80_px_ms > right.metrics.regret_80_px_ms;
            }
            return left.branch_at_ms < right.branch_at_ms;
        });
    if (summary.worst_episodes.size() > 5) {
        summary.worst_episodes.resize(5);
    }
    return summary;
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
        << " settled=" << result.settled_targets
        << " cross=" << result.center_cross_events
        << " post_cross_max=" << result.max_post_cross_error_px
        << " overshoot_area=" << result.overshoot_area_px_ms
        << " continued_push_ms=" << result.continued_push_after_cross_ms
        << " stall_ring_ms=" << result.stall_ring_ms
        << " handoff_residual_max=" << result.max_handoff_residual_px
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
        std::vector<CounterfactualRunSummary> counterfactual_results;
        std::vector<FusionRunSummary> fusion_results;
        const BenchmarkIntentFusionMode intent_fusion_mode =
            options.intent_fusion == "vector"
            ? BenchmarkIntentFusionMode::CausalVector
            : BenchmarkIntentFusionMode::LegacyAxis;
        for (const std::uint32_t seed : options.seeds) {
            const ScenarioScript script = generate_script(seed, benchmark_config);
            for (const ManualProfile profile : profiles) {
                for (const BenchmarkCohort cohort : cohorts) {
                    auto saw_assisted_mode = std::make_shared<bool>(false);
                    const ReplayControllerFactory factory = make_native_factory(
                        runtime.gamepad, intent_fusion_mode, saw_assisted_mode);
                    ReplayReference reference = record_reference(
                        script, profile, cohort, factory);
                    BenchmarkResult result = reference.benchmark_result;
                    fusion_results.push_back(summarize_fusion(reference));
                    if (options.smoke) {
                        validate_smoke(
                            result, options.duration_ms, *saw_assisted_mode);
                    }
                    print_summary(result);
                    counterfactual_results.push_back(analyze_reference(
                        reference, factory, options.counterfactual));
                    results.push_back(std::move(result));
                }
            }
        }
        write_report(
            options.output_path,
            options,
            benchmark_config,
            file_fingerprint(options.config_path),
            results,
            counterfactual_results,
            fusion_results);
        std::cout << "cod_native_sustained_aimlab_benchmark PASS\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "cod_native_sustained_aimlab_benchmark FAIL: "
                  << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
