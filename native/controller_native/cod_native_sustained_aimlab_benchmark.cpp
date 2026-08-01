#include "native_gamepad_controller.h"
#include "native_benchmark_controller_adapter.h"
#include "runtime_config.h"
#include "sustained_aimlab_counterfactual.h"
#include "sustained_aimlab_simulator.h"
#include "sustained_aimlab_learning.h"
#include "sustained_aimlab_trace.h"

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
using controller_native::BenchmarkRemainingWorkMode;
using controller_native::PhysicalGamepadState;
using controller_native::RuntimeConfig;
using controller_native::benchmark_adapter::AssistedModeCoverage;
using controller_native::benchmark_adapter::make_native_factory;
using namespace controller_native::sustained_aimlab;

struct CliOptions {
    std::filesystem::path config_path = "config.toml";
    std::vector<std::uint32_t> seeds;
    std::filesystem::path output_path;
    std::string profile = "both";
    std::string cohort = "ads";
    std::string scenario = "baseline";
    std::string target_profile = "ordinary";
    double camera_response = 500.0;
    double manual_input_scale = 1.0;
    double slowdown_edge = 0.50;
    double slowdown_center = 0.40;
    int short_occlusion_ms = 0;
    int vision_hz = 0;
    int vision_result_delay_ms = 0;
    std::string vision_disturbance = "off";
    std::string benchmark_recoil = "off";
    std::string firing_body_geometry_stabilizer = "on";
    std::string firing_disturbance_observer = "on";
    std::string revision = "unknown";
    bool dirty = false;
    int duration_ms = 60'000;
    bool smoke = false;
    std::string counterfactual = "off";
    std::string intent_fusion = "legacy";
    std::string remaining_work = "current";
    double remaining_work_scale = 1.0;
    int target_slot_ms = 0;
    double tracker_velocity_alpha = -1.0;
    std::string left_strafe = "off";
    std::string vertical_motion = "off";
    std::string target_motion = "moving";
    std::string player_action_cues = "on";
    std::string player_motion_oracle = "off";
    std::string player_motion_model = "causal";
    std::string ads_timing = "config";
    int learning_rounds = 0;
    int learning_delay_ms = 45;
    std::string learning_policy = "retain";
};

struct CounterfactualEpisodeSummary {
    int branch_at_ms = 0;
    std::uint64_t target_id = 0;
    int fusion_candidate = -1;
    double fusion_manual_weight = 1.0;
    double fusion_ai_weight = 0.0;
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
    std::array<std::uint64_t, controller_native::kFusionCandidateCount>
        candidate_ticks{};
    std::uint64_t fallback_ticks = 0;
    std::uint64_t manual_escape_ticks = 0;
    double manual_weight_sum = 0.0;
    double ai_weight_sum = 0.0;
    std::uint64_t ticks = 0;
};

struct PulseAmplificationSummary {
    std::uint64_t micro_input_ticks = 0;
    std::uint64_t same_direction_stack_ticks = 0;
    std::uint64_t wrong_near_center_stack_ticks = 0;
    std::uint64_t fresh_vision_output_jump_events = 0;
    std::uint64_t remaining_valid_ticks = 0;
    std::uint64_t remaining_valid_transitions = 0;
    std::uint64_t controller_target_switches = 0;
    std::uint64_t decoy_vision_frames = 0;
    double max_final_to_manual_ratio = 0.0;
    double max_wrong_stack_ratio = 0.0;
    double wrong_stack_output_area = 0.0;
    double max_abs_final_x = 0.0;
    double max_fresh_vision_output_jump = 0.0;
    double max_remaining_work_step_px = 0.0;
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
        } else if (argument == "--scenario" && index + 1 < argc) {
            options.scenario = argv[++index];
        } else if (argument == "--target-profile" && index + 1 < argc) {
            options.target_profile = argv[++index];
        } else if (argument == "--camera-response" && index + 1 < argc) {
            options.camera_response = std::stod(argv[++index]);
        } else if (argument == "--manual-input-scale" &&
                   index + 1 < argc) {
            options.manual_input_scale = std::stod(argv[++index]);
        } else if (argument == "--slowdown-edge" && index + 1 < argc) {
            options.slowdown_edge = std::stod(argv[++index]);
        } else if (argument == "--slowdown-center" && index + 1 < argc) {
            options.slowdown_center = std::stod(argv[++index]);
        } else if (argument == "--short-occlusion-ms" && index + 1 < argc) {
            options.short_occlusion_ms = std::stoi(argv[++index]);
        } else if (argument == "--vision-hz" && index + 1 < argc) {
            options.vision_hz = std::stoi(argv[++index]);
        } else if (argument == "--vision-result-delay-ms" &&
                   index + 1 < argc) {
            options.vision_result_delay_ms = std::stoi(argv[++index]);
        } else if (
            argument == "--vision-disturbance" && index + 1 < argc) {
            options.vision_disturbance = argv[++index];
        } else if (argument == "--benchmark-recoil" &&
                   index + 1 < argc) {
            options.benchmark_recoil = argv[++index];
        } else if (argument == "--firing-body-geometry" &&
                   index + 1 < argc) {
            options.firing_body_geometry_stabilizer = argv[++index];
        } else if (argument == "--firing-disturbance-observer" &&
                   index + 1 < argc) {
            options.firing_disturbance_observer = argv[++index];
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
        } else if (argument == "--remaining-work" && index + 1 < argc) {
            options.remaining_work = argv[++index];
        } else if (argument == "--remaining-work-scale" &&
                   index + 1 < argc) {
            options.remaining_work_scale = std::stod(argv[++index]);
        } else if (argument == "--target-slot-ms" && index + 1 < argc) {
            options.target_slot_ms = std::stoi(argv[++index]);
        } else if (argument == "--tracker-velocity-alpha" &&
                   index + 1 < argc) {
            options.tracker_velocity_alpha = std::stod(argv[++index]);
        } else if (argument == "--left-strafe" && index + 1 < argc) {
            options.left_strafe = argv[++index];
        } else if (argument == "--vertical-motion" && index + 1 < argc) {
            options.vertical_motion = argv[++index];
        } else if (argument == "--target-motion" && index + 1 < argc) {
            options.target_motion = argv[++index];
        } else if (argument == "--player-action-cues" && index + 1 < argc) {
            options.player_action_cues = argv[++index];
        } else if (
            argument == "--player-motion-oracle" && index + 1 < argc) {
            options.player_motion_oracle = argv[++index];
        } else if (
            argument == "--player-motion-model" && index + 1 < argc) {
            options.player_motion_model = argv[++index];
        } else if (argument == "--ads-timing" && index + 1 < argc) {
            options.ads_timing = argv[++index];
        } else if (argument == "--learning-rounds" && index + 1 < argc) {
            options.learning_rounds = std::stoi(argv[++index]);
        } else if (argument == "--learning-delay-ms" && index + 1 < argc) {
            options.learning_delay_ms = std::stoi(argv[++index]);
        } else if (argument == "--learning-policy" && index + 1 < argc) {
            options.learning_policy = argv[++index];
        } else if (argument == "--help") {
            std::cout
                << "Usage: cod_native_sustained_aimlab_benchmark "
                << "[--config PATH] [--seed N ...] "
                   "[--profile pure|mixed|scripted|obsolete|recover|arc|both] "
                << "[--cohort ads|bodylock|both] "
                << "[--scenario baseline|compound_directional] "
                << "[--target-profile ordinary|small|near] [--camera-response PX] "
                << "[--manual-input-scale 0..2] "
                << "[--slowdown-edge N] [--slowdown-center N] "
                << "[--short-occlusion-ms 0|24|36|48] "
                << "[--vision-hz 0|1..1000] "
                << "[--vision-disturbance off|gun-kick|gun-kick-adversarial|"
                   "camera-recoil|gun-kick-plus-recoil|"
                   "body-box-deformation|body-box-plus-recoil|"
                   "horizontal-aim-bias] "
                << "[--benchmark-recoil off|on] "
                << "[--firing-body-geometry off|on] "
                << "[--firing-disturbance-observer off|on] "
                << "[--output PATH] [--revision HASH] [--dirty] "
                << "[--duration-ms N] [--smoke] "
                << "[--counterfactual off|quick|full] "
                << "[--intent-fusion legacy|vector|vector-baseline] "
                << "[--remaining-work current|delivered|integrated|integrated-ai] "
                << "[--remaining-work-scale 0..1.5] "
                << "[--target-slot-ms 0|N] "
                << "[--tracker-velocity-alpha 0..1] "
                << "[--left-strafe off|full-reversal|both] "
                << "[--vertical-motion off|slide|jump|random|all] "
                << "[--target-motion stationary|moving] "
                << "[--player-action-cues on|off] "
                << "[--player-motion-oracle off|state|full|forecast] "
                << "[--player-motion-model current|state|forecast|causal] "
                << "[--ads-timing config|coupled|decoupled160|decoupled180|"
                   "decoupled200|decoupled|hard30|ramp30|hard30-160|ramp30-160] "
                << "[--learning-rounds N --learning-delay-ms N "
                << "--learning-policy baseline|reset|retain]\n";
            std::exit(EXIT_SUCCESS);
        } else {
            throw std::runtime_error(
                "unknown or incomplete argument: " + argument);
        }
    }
    if (options.duration_ms <= 0) {
        throw std::runtime_error("duration must be positive");
    }
    if (options.vision_hz < 0 || options.vision_hz > 1000) {
        throw std::runtime_error("vision hz must be 0 or 1..1000");
    }
    if (options.vision_result_delay_ms < 0 ||
        options.vision_result_delay_ms > 100) {
        throw std::runtime_error("vision result delay must be 0..100 ms");
    }
    if (options.vision_disturbance != "off" &&
        options.vision_disturbance != "gun-kick" &&
        options.vision_disturbance != "gun-kick-adversarial" &&
        options.vision_disturbance != "camera-recoil" &&
        options.vision_disturbance != "gun-kick-plus-recoil" &&
        options.vision_disturbance != "body-box-deformation" &&
        options.vision_disturbance != "body-box-plus-recoil" &&
        options.vision_disturbance != "dropout-decoy" &&
        options.vision_disturbance != "horizontal-aim-bias") {
        throw std::runtime_error(
            "vision disturbance must be off, gun-kick, "
            "gun-kick-adversarial, camera-recoil, or "
            "gun-kick-plus-recoil, body-box-deformation, or "
            "body-box-plus-recoil, dropout-decoy, or horizontal-aim-bias");
    }
    if (options.benchmark_recoil != "off" &&
        options.benchmark_recoil != "on") {
        throw std::runtime_error(
            "benchmark recoil must be off or on");
    }
    if (options.firing_body_geometry_stabilizer != "off" &&
        options.firing_body_geometry_stabilizer != "on") {
        throw std::runtime_error(
            "firing body geometry stabilizer must be off or on");
    }
    if (options.firing_disturbance_observer != "off" &&
        options.firing_disturbance_observer != "on") {
        throw std::runtime_error(
            "firing disturbance observer must be off or on");
    }
    if (options.profile != "pure" && options.profile != "mixed" &&
        options.profile != "scripted" &&
        options.profile != "obsolete" &&
        options.profile != "recover" &&
        options.profile != "arc" &&
        options.profile != "micro" &&
        options.profile != "both") {
        throw std::runtime_error(
            "profile must be pure, mixed, scripted, obsolete, recover, arc, "
            "micro, or both");
    }
    if (options.cohort != "ads" && options.cohort != "bodylock" &&
        options.cohort != "both") {
        throw std::runtime_error("cohort must be ads, bodylock, or both");
    }
    if (options.scenario != "baseline" &&
        options.scenario != "compound_directional") {
        throw std::runtime_error(
            "scenario must be baseline or compound_directional");
    }
    if (options.target_profile != "ordinary" &&
        options.target_profile != "small" &&
        options.target_profile != "near") {
        throw std::runtime_error(
            "target profile must be ordinary, small, or near");
    }
    if (options.counterfactual != "off" &&
        options.counterfactual != "quick" &&
        options.counterfactual != "full") {
        throw std::runtime_error(
            "counterfactual mode must be off, quick, or full");
    }
    if (options.intent_fusion != "legacy" &&
        options.intent_fusion != "vector" &&
        options.intent_fusion != "vector-baseline") {
        throw std::runtime_error(
            "intent fusion mode must be legacy, vector, or vector-baseline");
    }
    if (options.remaining_work != "current" &&
        options.remaining_work != "delivered" &&
        options.remaining_work != "integrated" &&
        options.remaining_work != "integrated-ai") {
        throw std::runtime_error(
            "remaining work mode must be current, delivered, integrated, "
            "or integrated-ai");
    }
    if (options.remaining_work_scale < 0.0 ||
        options.remaining_work_scale > 1.5) {
        throw std::runtime_error(
            "remaining work scale must be in [0,1.5]");
    }
    if (options.target_slot_ms < 0) {
        throw std::runtime_error("target slot must be zero or positive");
    }
    if (options.tracker_velocity_alpha != -1.0 &&
        (options.tracker_velocity_alpha < 0.0 ||
         options.tracker_velocity_alpha > 1.0)) {
        throw std::runtime_error(
            "tracker velocity alpha must be in [0,1]");
    }
    if (options.left_strafe != "off" &&
        options.left_strafe != "full-reversal" &&
        options.left_strafe != "both") {
        throw std::runtime_error(
            "left strafe mode must be off, full-reversal, or both");
    }
    if (options.vertical_motion != "off" &&
        options.vertical_motion != "slide" &&
        options.vertical_motion != "jump" &&
        options.vertical_motion != "random" &&
        options.vertical_motion != "all") {
        throw std::runtime_error(
            "vertical motion must be off, slide, jump, random, or all");
    }
    if (options.target_motion != "stationary" &&
        options.target_motion != "moving") {
        throw std::runtime_error(
            "target motion must be stationary or moving");
    }
    if (options.player_action_cues != "on" &&
        options.player_action_cues != "off") {
        throw std::runtime_error(
            "player action cues must be on or off");
    }
    if (options.player_motion_oracle != "off" &&
        options.player_motion_oracle != "state" &&
        options.player_motion_oracle != "full" &&
        options.player_motion_oracle != "forecast") {
        throw std::runtime_error(
            "player motion oracle must be off, state, full, or forecast");
    }
    if (options.player_motion_model != "current" &&
        options.player_motion_model != "state" &&
        options.player_motion_model != "forecast" &&
        options.player_motion_model != "causal") {
        throw std::runtime_error(
            "player motion model must be current, state, forecast, or causal");
    }
    if (options.ads_timing != "config" &&
        options.ads_timing != "coupled" &&
        options.ads_timing != "decoupled160" &&
        options.ads_timing != "decoupled180" &&
        options.ads_timing != "decoupled200" &&
        options.ads_timing != "decoupled" &&
        options.ads_timing != "hard30" &&
        options.ads_timing != "ramp30" &&
        options.ads_timing != "hard30-160" &&
        options.ads_timing != "ramp30-160") {
        throw std::runtime_error(
            "invalid ADS timing profile");
    }
    if (options.learning_rounds < 0 || options.learning_delay_ms < 0 ||
        (options.learning_policy != "baseline" &&
         options.learning_policy != "reset" &&
         options.learning_policy != "retain")) {
        throw std::runtime_error("invalid learning experiment arguments");
    }
    if (options.learning_rounds > 0 &&
        (options.profile == "both" || options.cohort == "both" ||
         options.seeds.size() > 1 || options.left_strafe != "off" ||
         options.vertical_motion != "off" ||
         options.target_motion != "moving")) {
        throw std::runtime_error(
            "learning mode requires one seed, one profile, one cohort, "
            "and left strafe off");
    }
    if (options.camera_response <= 0.0 || options.slowdown_edge <= 0.0 ||
        options.slowdown_edge > 1.0 || options.slowdown_center <= 0.0 ||
        options.slowdown_center > options.slowdown_edge) {
        throw std::runtime_error("invalid plant response or slowdown multipliers");
    }
    if (!std::isfinite(options.manual_input_scale) ||
        options.manual_input_scale < 0.0 ||
        options.manual_input_scale > 2.0) {
        throw std::runtime_error("manual input scale must be in [0,2]");
    }
    if (options.learning_rounds > 0 &&
        std::fabs(options.manual_input_scale - 1.0) > 1.0e-12) {
        throw std::runtime_error(
            "manual input scale is not supported by learning mode");
    }
    if (options.short_occlusion_ms != 0 &&
        options.short_occlusion_ms != 24 &&
        options.short_occlusion_ms != 36 &&
        options.short_occlusion_ms != 48) {
        throw std::runtime_error(
            "short occlusion must be 0, 24, 36, or 48 ms");
    }
    if (options.seeds.empty()) {
        options.seeds = {1337, 20260718, 424242};
    }
    return options;
}

GamepadRuntimeConfig apply_ads_timing_profile(
    GamepadRuntimeConfig config,
    const std::string& profile) {
    if (profile == "config") return config;
    config.ai_aim.ads_snap_window_ms = 120;
    config.ai_aim.ads_max_acquisition_ms =
        profile == "coupled" ? 120.0f :
        (profile == "decoupled160" ||
         profile == "hard30-160" ||
         profile == "ramp30-160") ? 160.0f :
        profile == "decoupled180" ? 180.0f :
        profile == "decoupled200" ? 200.0f : 220.0f;
    config.ai_aim.ads_start_delay_ms =
        (profile == "hard30" || profile == "hard30-160") ? 30.0f : 0.0f;
    config.ai_aim.ads_start_ramp_ms =
        (profile == "ramp30" || profile == "ramp30-160") ? 30.0f : 0.0f;
    return config;
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
    switch (profile) {
    case ManualProfile::Pure: return "pure";
    case ManualProfile::Mixed: return "mixed";
    case ManualProfile::Scripted: return "scripted";
    case ManualProfile::ObsoleteAfterCrossing: return "obsolete";
    case ManualProfile::WrongThenCorrect: return "recover";
    case ManualProfile::ArcRecovery: return "arc";
    case ManualProfile::MicroCorrection: return "micro";
    }
    return "unknown";
}

const char* cohort_name(BenchmarkCohort cohort) {
    return cohort == BenchmarkCohort::AdsAcquire ? "ads" : "bodylock";
}

const char* player_strafe_name(PlayerStrafeMode mode) {
    return mode == PlayerStrafeMode::FullReversal
        ? "full_reversal" : "off";
}

const char* player_vertical_motion_name(PlayerVerticalMotionMode mode) {
    switch (mode) {
    case PlayerVerticalMotionMode::Off: return "off";
    case PlayerVerticalMotionMode::Slide: return "slide";
    case PlayerVerticalMotionMode::Jump: return "jump";
    case PlayerVerticalMotionMode::Random: return "random";
    }
    return "unknown";
}

const char* motion_name(MotionProfile motion) {
    return to_string(motion);
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
            << ",\"target_id\":" << episode.target_id
            << ",\"fusion_candidate\":" << episode.fusion_candidate
            << ",\"fusion_manual_weight\":" << episode.fusion_manual_weight
            << ",\"fusion_ai_weight\":" << episode.fusion_ai_weight
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
    const GamepadRuntimeConfig& gamepad_config,
    std::uint64_t config_fingerprint,
    const std::vector<BenchmarkResult>& results,
    const std::vector<CounterfactualRunSummary>& counterfactual_results,
    const std::vector<FusionRunSummary>& fusion_results,
    const std::vector<PulseAmplificationSummary>& pulse_results) {
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
    out << "{\n  \"schema\": \"sustained-aimlab-v4\",\n"
        << "  \"revision\": " << json_string(options.revision) << ",\n"
        << "  \"dirty\": " << (options.dirty ? "true" : "false") << ",\n"
        << "  \"config_path\": " << json_string(options.config_path.string()) << ",\n"
        << "  \"config_fingerprint_fnv1a64\": \"" << config_fingerprint << "\",\n"
        << "  \"ads_timing\": {\"profile\": "
        << json_string(options.ads_timing)
        << ", \"arrival_horizon_ms\": "
        << gamepad_config.ai_aim.ads_snap_window_ms
        << ", \"ownership_ceiling_ms\": "
        << gamepad_config.ai_aim.ads_max_acquisition_ms
        << ", \"start_delay_ms\": "
        << gamepad_config.ai_aim.ads_start_delay_ms
        << ", \"start_ramp_ms\": "
        << gamepad_config.ai_aim.ads_start_ramp_ms
        << ", \"fallback_range_gated\": true},\n"
        << "  \"simulator\": {\"duration_ms\": " << config.duration_ms
        << ", \"scenario\": " << json_string(to_string(config.scenario_profile))
        << ", \"target_profile\": " << json_string(options.target_profile)
        << ", \"tick_ms\": " << config.tick_ms
        << ", \"vision_interval_ms\": " << config.vision_interval_ms
        << ", \"vision_hz_requested\": " << options.vision_hz
        << ", \"vision_result_delay_ms\": "
        << config.vision_result_delay_ms
        << ", \"vision_disturbance\": "
        << json_string(options.vision_disturbance)
        << ", \"benchmark_recoil\": "
        << json_string(options.benchmark_recoil)
        << ", \"firing_body_geometry_stabilizer\": "
        << json_string(options.firing_body_geometry_stabilizer)
        << ", \"firing_disturbance_observer\": "
        << json_string(options.firing_disturbance_observer)
        << ", \"tracking_window_ms\": " << config.tracking_window_ms
        << ", \"target_slot_ms\": " << config.fixed_target_slot_ms
        << ", \"target_radius_px\": " << config.target_radius_px
        << ", \"camera_response_px_per_stick_second\": "
        << config.camera_response_px_per_stick_second
        << ", \"manual_input_scale\": " << config.manual_input_scale
        << ", \"slowdown_edge\": " << config.slowdown_edge_multiplier
        << ", \"slowdown_center\": " << config.slowdown_center_multiplier
        << ", \"left_strafe_request\": "
        << json_string(options.left_strafe)
        << ", \"vertical_motion_request\": "
        << json_string(options.vertical_motion)
        << ", \"target_motion\": "
        << json_string(options.target_motion)
        << ", \"player_action_cues\": "
        << json_string(options.player_action_cues)
        << ", \"player_motion_oracle\": "
        << json_string(options.player_motion_oracle)
        << ", \"player_motion_model\": "
        << json_string(options.player_motion_model)
        << ", \"player_top_speed_px_per_second\": ["
        << kPlayerStrafeMinTopSpeedPxPerSecond << ','
        << kPlayerStrafeMaxTopSpeedPxPerSecond
        << "], \"player_time_constant_ms\": ["
        << kPlayerStrafeMinTimeConstantMs << ','
        << kPlayerStrafeMaxTimeConstantMs << ']'
        << ", \"short_occlusion_ms\": "
        << config.short_occlusion_duration_ms << "},\n"
        << "  \"intent_fusion\": {\"schema_version\": 1, \"mode\": "
        << json_string(options.intent_fusion)
        << ", \"candidate_set_version\": 4},\n"
        << "  \"remaining_work\": {\"schema_version\": 1, \"mode\": "
        << json_string(options.remaining_work)
        << ", \"capture_anchor\": \"vision_capture\", "
           "\"delivery_signal\": "
        << json_string(
               options.remaining_work == "integrated-ai"
               ? "shaped_assist"
               : "final_pre_recoil")
        << ", \"scale\": "
        << options.remaining_work_scale << "},\n"
        << "  \"tracker\": {\"velocity_alpha_override\": ";
    if (options.tracker_velocity_alpha >= 0.0) {
        out << options.tracker_velocity_alpha;
    } else {
        out << "null";
    }
    out << "},\n"
        << "  \"counterfactual_conflict\": {\"schema_version\": 1, "
        << "\"candidate_set_version\": 4, \"mode\": "
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
            << ", \"left_strafe\": "
            << json_string(player_strafe_name(result.player_strafe_mode))
            << ", \"vertical_motion\": "
            << json_string(player_vertical_motion_name(
                result.player_vertical_motion_mode))
            << ", \"script_hash\": \"" << result.script_hash << "\""
            << ", \"ticks\": " << result.ticks
            << ", \"left_strafe_active_ms\": "
            << result.left_strafe_active_ms
            << ", \"left_strafe_reversals\": "
            << result.left_strafe_reversals
            << ", \"max_abs_left_x\": " << result.max_abs_left_x
            << ", \"min_sampled_player_top_speed_px_per_second\": "
            << result.min_sampled_player_top_speed_px_per_second
            << ", \"max_sampled_player_top_speed_px_per_second\": "
            << result.max_sampled_player_top_speed_px_per_second
            << ", \"max_abs_player_speed_px_per_second\": "
            << result.max_abs_player_speed_px_per_second
            << ", \"player_vertical_active_ms\": "
            << result.player_vertical_active_ms
            << ", \"player_slide_events\": "
            << result.player_slide_events
            << ", \"player_jump_events\": "
            << result.player_jump_events
            << ", \"max_abs_player_vertical_offset_px\": "
            << result.max_abs_player_vertical_offset_px
            << ", \"max_abs_player_vertical_speed_px_per_second\": "
            << result.max_abs_player_vertical_speed_px_per_second
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
            << ", \"maximum_vertical_overshoot_px\": "
            << result.maximum_vertical_overshoot_px
            << ", \"post_cross_error_area_px_ms\": "
            << result.post_cross_error_area_px_ms
            << ", \"post_cross_wrong_way_output_integral\": "
            << result.post_cross_wrong_way_output_integral
            << ", \"continued_push_after_cross_ms\": " << result.continued_push_after_cross_ms
            << ", \"correction_reversal_events\": " << result.correction_reversal_events
            << ", \"circle_exit_events\": " << result.circle_exit_events
            << ", \"stall_ring_ms\": " << result.stall_ring_ms
            << ", \"direction_discontinuities\": " << result.direction_discontinuities
            << ", \"occluded_direction_discontinuities\": "
            << result.occluded_direction_discontinuities
            << ", \"fresh_vision_direction_discontinuities\": "
            << result.fresh_vision_direction_discontinuities
            << ", \"manual_input_discontinuities\": "
            << result.manual_input_discontinuities
            << ", \"manual_driven_final_discontinuities\": "
            << result.manual_driven_final_discontinuities
            << ", \"controller_residual_discontinuities\": "
            << result.controller_residual_discontinuities
            << ", \"controller_residual_kick_events\": "
            << result.controller_residual_kick_events
            << ", \"requested_assist_discontinuities\": "
            << result.requested_assist_discontinuities
            << ", \"shaped_assist_discontinuities\": "
            << result.shaped_assist_discontinuities
            << ", \"near_center_requested_reversal_events\": "
            << result.near_center_requested_reversal_events
            << ", \"near_center_shaped_reversal_events\": "
            << result.near_center_shaped_reversal_events
            << ", \"near_center_shaped_wrong_way_ms\": "
            << result.near_center_shaped_wrong_way_ms
            << ", \"oscillation_episodes\": "
            << result.oscillation_episodes
            << ", \"oscillation_active_ms\": "
            << result.oscillation_active_ms
            << ", \"oscillation_output_area\": "
            << result.oscillation_output_area
            << ", \"max_error_px\": " << result.max_error_px
            << ", \"median_first_entry_to_settle_ms\": " << result.median_first_entry_to_settle_ms
            << ", \"p95_first_entry_to_settle_ms\": " << result.p95_first_entry_to_settle_ms
            << ", \"median_first_assist_output_ms\": "
            << result.median_first_assist_output_ms
            << ", \"p95_first_assist_output_ms\": "
            << result.p95_first_assist_output_ms
            << ", \"handoff_count\": " << result.handoff_count
            << ", \"max_handoff_residual_px\": " << result.max_handoff_residual_px
            << ", \"max_abs_handoff_closing_speed_px_per_sec\": "
            << result.max_abs_handoff_closing_speed_px_per_sec
            << ", \"handoff_episodes\": " << result.handoff_episodes
            << ", \"handoff_defect_episodes\": "
            << result.handoff_defect_episodes
            << ", \"handoff_defect_rate\": " << result.handoff_defect_rate
            << ", \"post_handoff_local_error_area_px_ms\": "
            << result.post_handoff_local_error_area_px_ms
            << ", \"post_handoff_tail_error_area_px_ms\": "
            << result.post_handoff_tail_error_area_px_ms
            << ", \"p50_post_handoff_rebound_px\": "
            << result.p50_post_handoff_rebound_px
            << ", \"p95_post_handoff_rebound_px\": "
            << result.p95_post_handoff_rebound_px
            << ", \"max_post_handoff_rebound_px\": "
            << result.max_post_handoff_rebound_px
            << ", \"p95_post_handoff_wrong_way_output_integral\": "
            << result.p95_post_handoff_wrong_way_output_integral
            << ", \"occlusion_episodes\": " << result.occlusion_episodes
            << ", \"post_occlusion_samples\": "
            << result.post_occlusion_samples
            << ", \"post_occlusion_error_area_px_ms\": "
            << result.post_occlusion_error_area_px_ms
            << ", \"max_post_occlusion_error_px\": "
            << result.max_post_occlusion_error_px
            << ", \"p95_reveal_to_stable_ms\": "
            << result.p95_reveal_to_stable_ms
            << ", \"mean_error_px\": " << result.mean_error_px
            << ", \"p95_error_px\": " << result.p95_error_px
            << ", \"p95_output_delta\": " << result.p95_output_delta
            << ", \"p95_jerk\": " << result.p95_jerk
            << ", \"p95_controller_residual_delta\": "
            << result.p95_controller_residual_delta
            << ", \"p95_controller_residual_jerk\": "
            << result.p95_controller_residual_jerk
            << ", \"p99_controller_residual_delta\": "
            << result.p99_controller_residual_delta
            << ", \"max_controller_residual_delta\": "
            << result.max_controller_residual_delta
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
                << ",\"first_assist_output_ms\":"
                << target.first_assist_output_ms
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
                << ",\"maximum_vertical_overshoot_px\":"
                << target.maximum_vertical_overshoot_px
                << ",\"post_cross_error_area_px_ms\":"
                << target.post_cross_error_area_px_ms
                << ",\"post_cross_wrong_way_output_integral\":"
                << target.post_cross_wrong_way_output_integral
                << ",\"continued_push_after_cross_ms\":" << target.continued_push_after_cross_ms
                << ",\"brake_start_distance_px\":" << target.brake_start_distance_px
                << ",\"time_to_zero_radial_speed_ms\":" << target.time_to_zero_radial_speed_ms
                << ",\"first_entry_to_settle_ms\":" << target.first_entry_to_settle_ms
                << ",\"correction_reversal_events\":" << target.correction_reversal_events
                << ",\"circle_exit_events\":" << target.circle_exit_events
                << ",\"stall_ring_ms\":" << target.stall_ring_ms
                << ",\"direction_discontinuities\":" << target.direction_discontinuities
                << ",\"near_center_requested_reversal_events\":"
                << target.near_center_requested_reversal_events
                << ",\"near_center_shaped_reversal_events\":"
                << target.near_center_shaped_reversal_events
                << ",\"near_center_shaped_wrong_way_ms\":"
                << target.near_center_shaped_wrong_way_ms
                << ",\"oscillation_episodes\":"
                << target.oscillation_episodes
                << ",\"oscillation_active_ms\":"
                << target.oscillation_active_ms
                << ",\"oscillation_output_area\":"
                << target.oscillation_output_area
                << ",\"max_error_px\":" << target.max_error_px
                << ",\"handoff_observed\":"
                << (target.handoff_observed ? "true" : "false")
                << ",\"handoff_residual_px\":" << target.handoff_residual_px
                << ",\"handoff_closing_speed_px_per_sec\":"
                << target.handoff_closing_speed_px_per_sec
                << ",\"handoff_predicted_crossing\":"
                << (target.handoff_predicted_crossing ? "true" : "false")
                << ",\"handoff_manual_radial\":"
                << target.handoff_manual_radial
                << ",\"handoff_requested_ai_radial\":"
                << target.handoff_requested_ai_radial
                << ",\"handoff_shaped_ai_radial\":"
                << target.handoff_shaped_ai_radial
                << ",\"handoff_final_radial\":"
                << target.handoff_final_radial
                << ",\"post_handoff_local_samples\":"
                << target.post_handoff_local_samples
                << ",\"post_handoff_tail_samples\":"
                << target.post_handoff_tail_samples
                << ",\"post_handoff_local_error_area_px_ms\":"
                << target.post_handoff_local_error_area_px_ms
                << ",\"post_handoff_tail_error_area_px_ms\":"
                << target.post_handoff_tail_error_area_px_ms
                << ",\"post_handoff_max_error_px\":"
                << target.post_handoff_max_error_px
                << ",\"post_handoff_min_error_px\":"
                << target.post_handoff_min_error_px
                << ",\"post_handoff_rebound_px\":"
                << target.post_handoff_rebound_px
                << ",\"post_handoff_wrong_way_output_integral\":"
                << target.post_handoff_wrong_way_output_integral
                << ",\"post_handoff_wrong_way_ms\":"
                << target.post_handoff_wrong_way_ms
                << ",\"post_handoff_manual_wrong_way_ms\":"
                << target.post_handoff_manual_wrong_way_ms
                << ",\"post_handoff_requested_ai_wrong_way_ms\":"
                << target.post_handoff_requested_ai_wrong_way_ms
                << ",\"post_handoff_shaped_ai_wrong_way_ms\":"
                << target.post_handoff_shaped_ai_wrong_way_ms
                << ",\"post_handoff_circle_exit_events\":"
                << target.post_handoff_circle_exit_events
                << ",\"post_handoff_settle_ms\":"
                << target.post_handoff_settle_ms
                << ",\"handoff_defect\":"
                << (target.handoff_defect ? "true" : "false")
                << ",\"occlusion_episodes\":"
                << target.occlusion_episodes
                << ",\"post_occlusion_samples\":"
                << target.post_occlusion_samples
                << ",\"post_occlusion_error_area_px_ms\":"
                << target.post_occlusion_error_area_px_ms
                << ",\"max_post_occlusion_error_px\":"
                << target.max_post_occlusion_error_px
                << ",\"reveal_to_stable_ms\":"
                << target.reveal_to_stable_ms << '}';
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
            << "},\"pulse_amplification\":";
        const PulseAmplificationSummary& pulse = pulse_results.at(run_index);
        out << "{\"micro_input_ticks\":" << pulse.micro_input_ticks
            << ",\"same_direction_stack_ticks\":"
            << pulse.same_direction_stack_ticks
            << ",\"wrong_near_center_stack_ticks\":"
            << pulse.wrong_near_center_stack_ticks
            << ",\"fresh_vision_output_jump_events\":"
            << pulse.fresh_vision_output_jump_events
            << ",\"remaining_valid_ticks\":" << pulse.remaining_valid_ticks
            << ",\"remaining_valid_transitions\":"
            << pulse.remaining_valid_transitions
            << ",\"controller_target_switches\":"
            << pulse.controller_target_switches
            << ",\"decoy_vision_frames\":" << pulse.decoy_vision_frames
            << ",\"max_final_to_manual_ratio\":"
            << pulse.max_final_to_manual_ratio
            << ",\"max_wrong_stack_ratio\":"
            << pulse.max_wrong_stack_ratio
            << ",\"wrong_stack_output_area\":"
            << pulse.wrong_stack_output_area
            << ",\"max_abs_final_x\":" << pulse.max_abs_final_x
            << ",\"max_fresh_vision_output_jump\":"
            << pulse.max_fresh_vision_output_jump
            << ",\"max_remaining_work_step_px\":"
            << pulse.max_remaining_work_step_px
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

FusionRunSummary summarize_fusion(const ReplayReference& reference) {
    FusionRunSummary summary;
    for (const auto& frame : reference.trace) {
        const auto& output = frame.output;
        const int candidate = std::clamp(
            output.intent_fusion_candidate, 0,
            static_cast<int>(summary.candidate_ticks.size() - 1));
        ++summary.candidate_ticks[static_cast<std::size_t>(candidate)];
        if (output.intent_fusion_fallback) ++summary.fallback_ticks;
        if (output.intent_fusion_manual_escape) ++summary.manual_escape_ticks;
        summary.manual_weight_sum += output.intent_fusion_manual_weight;
        summary.ai_weight_sum += output.intent_fusion_ai_weight;
        ++summary.ticks;
    }
    return summary;
}

PulseAmplificationSummary summarize_pulse_amplification(
    const ReplayReference& reference) {
    PulseAmplificationSummary summary;
    bool have_previous = false;
    bool previous_remaining_valid = false;
    double previous_final_x = 0.0;
    Vec2d previous_remaining_work;
    std::uint64_t previous_controller_target_id = 0;
    std::uint64_t previous_script_target_id = 0;
    for (const auto& frame : reference.trace) {
        if (!frame.target_active) {
            have_previous = false;
            previous_controller_target_id = 0;
            previous_script_target_id = 0;
            continue;
        }
        const double manual_x = frame.input.manual_stick.x;
        const double ai_x = frame.output.shaped_assist_stick.x;
        const double final_x = frame.output.final_stick.x;
        if (std::fabs(manual_x) >= 0.015 && std::fabs(manual_x) <= 0.05) {
            ++summary.micro_input_ticks;
            summary.max_final_to_manual_ratio = std::max(
                summary.max_final_to_manual_ratio,
                std::fabs(final_x) / std::max(0.001, std::fabs(manual_x)));
            if (manual_x * ai_x > 0.0 && std::fabs(ai_x) >= 0.05) {
                ++summary.same_direction_stack_ticks;
                if (manual_x * frame.true_error_before_px.x < 0.0 &&
                    std::hypot(frame.true_error_before_px.x,
                               frame.true_error_before_px.y) <= 40.0) {
                    ++summary.wrong_near_center_stack_ticks;
                    summary.max_wrong_stack_ratio = std::max(
                        summary.max_wrong_stack_ratio,
                        std::fabs(final_x) /
                            std::max(0.001, std::fabs(manual_x)));
                    summary.wrong_stack_output_area += std::fabs(final_x);
                }
            }
        }
        summary.max_abs_final_x = std::max(
            summary.max_abs_final_x, std::fabs(final_x));
        if (frame.input.decoy_candidate_present && frame.fresh_vision) {
            ++summary.decoy_vision_frames;
        }
        if (frame.output.remaining_work_valid) {
            ++summary.remaining_valid_ticks;
        }
        if (have_previous) {
            if (frame.output.remaining_work_valid != previous_remaining_valid) {
                ++summary.remaining_valid_transitions;
            }
            const double remaining_step = std::hypot(
                frame.output.remaining_work_px.x - previous_remaining_work.x,
                frame.output.remaining_work_px.y - previous_remaining_work.y);
            summary.max_remaining_work_step_px = std::max(
                summary.max_remaining_work_step_px, remaining_step);
            if (frame.fresh_vision) {
                const double jump = std::fabs(final_x - previous_final_x);
                summary.max_fresh_vision_output_jump = std::max(
                    summary.max_fresh_vision_output_jump, jump);
                if (jump >= 0.05) {
                    ++summary.fresh_vision_output_jump_events;
                }
            }
        }
        if (previous_script_target_id == frame.target_id &&
            previous_controller_target_id != 0 &&
            frame.output.controller_target_id != 0 &&
            frame.output.controller_target_id !=
                previous_controller_target_id) {
            ++summary.controller_target_switches;
        }
        have_previous = true;
        previous_remaining_valid = frame.output.remaining_work_valid;
        previous_remaining_work = frame.output.remaining_work_px;
        previous_final_x = final_x;
        previous_controller_target_id = frame.output.controller_target_id;
        previous_script_target_id = frame.target_id;
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
    const ReplayReference& reference,
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
    const auto& branch_frame = reference.trace.at(
        static_cast<std::size_t>(episode.branch_at_ms));
    summary.worst_episodes.push_back({
        episode.branch_at_ms,
        branch_frame.target_id,
        branch_frame.output.intent_fusion_candidate,
        branch_frame.output.intent_fusion_manual_weight,
        branch_frame.output.intent_fusion_ai_weight,
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
            summary, reference, episode,
            "fixed_anchor", anchor_name(anchor.kind));
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
            summary, reference, episode,
            "dynamic_conflict", conflict_name(conflict.kind));
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
    const AssistedModeCoverage& coverage) {
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
    if (!coverage.saw_assisted_mode) {
        throw std::runtime_error("smoke run never entered an assisted aim mode");
    }
    if (result.cohort == BenchmarkCohort::AdsAcquire &&
        result.targets_spawned > 1 && coverage.ads_target_ids.size() < 2) {
        throw std::runtime_error(
            "ADS smoke run did not create a fresh scope epoch for later targets");
    }
}

void print_summary(const BenchmarkResult& result) {
    std::cout
        << "seed=" << result.seed
        << " ticks=" << result.ticks
        << " profile=" << profile_name(result.manual_profile)
        << " cohort=" << cohort_name(result.cohort)
        << " left_strafe=" << player_strafe_name(result.player_strafe_mode)
        << " vertical_motion=" << player_vertical_motion_name(
            result.player_vertical_motion_mode)
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
        << " handoffs=" << result.handoff_episodes
        << " handoff_defects=" << result.handoff_defect_episodes
        << " defect_rate=" << result.handoff_defect_rate
        << " handoff_rebound_p95=" << result.p95_post_handoff_rebound_px
        << " handoff_wrong_way_p95="
        << result.p95_post_handoff_wrong_way_output_integral
        << "\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const CliOptions options = parse_args(argc, argv);
        const RuntimeConfig runtime =
            controller_native::load_runtime_config(options.config_path);
        const GamepadRuntimeConfig gamepad_config =
            apply_ads_timing_profile(runtime.gamepad, options.ads_timing);
        if (options.learning_rounds > 0) {
            LearningExperimentConfig learning;
            learning.rounds = options.learning_rounds;
            learning.round_duration_ms = options.duration_ms;
            learning.control_response_delay_ms = options.learning_delay_ms;
            learning.camera_response_px_per_stick_second =
                options.camera_response;
            learning.policy = options.learning_policy == "baseline"
                ? LearningPolicy::Baseline
                : options.learning_policy == "reset"
                    ? LearningPolicy::ResetEachRound
                    : LearningPolicy::RetainAcrossRounds;
            const ManualProfile profile = options.profile == "mixed"
                ? ManualProfile::Mixed : ManualProfile::Pure;
            const BenchmarkCohort cohort = options.cohort == "bodylock"
                ? BenchmarkCohort::BodyLockFollow
                : BenchmarkCohort::AdsAcquire;
            const BenchmarkIntentFusionMode fusion =
                options.intent_fusion == "vector"
                    ? BenchmarkIntentFusionMode::CausalVector
                    : options.intent_fusion == "vector-baseline"
                        ? BenchmarkIntentFusionMode::CausalVectorBaseline
                        : BenchmarkIntentFusionMode::LegacyAxis;
            const ReplayControllerFactory native_factory = make_native_factory(
                gamepad_config, cohort, fusion);
            auto report = run_learning_experiment(
                learning, options.seeds.front(), profile, cohort,
                [native_factory] { return native_factory(BranchSchedule{}); });
            report.revision = options.revision;
            report.dirty = options.dirty;
            report.config_path = options.config_path.string();
            report.config_fingerprint_fnv1a64 =
                file_fingerprint(options.config_path);
            const std::string json = learning_experiment_to_json(report);
            if (options.output_path.empty()) {
                std::cout << json << '\n';
            } else {
                if (!options.output_path.parent_path().empty()) {
                    std::filesystem::create_directories(
                        options.output_path.parent_path());
                }
                if (std::filesystem::exists(options.output_path)) {
                    throw std::runtime_error(
                        "output already exists: " + options.output_path.string());
                }
                std::ofstream output(options.output_path, std::ios::binary);
                if (!output) throw std::runtime_error("cannot write learning report");
                output << json << '\n';
            }
            std::cout << "cod_native_sustained_aimlab_benchmark PASS\n";
            return EXIT_SUCCESS;
        }
        BenchmarkConfig benchmark_config;
        benchmark_config.duration_ms = options.duration_ms;
        benchmark_config.scenario_profile =
            options.scenario == "compound_directional"
            ? ScenarioProfile::CompoundDirectional
            : ScenarioProfile::Baseline;
        benchmark_config.target_profile =
            options.target_profile == "small"
            ? TargetProfile::SmallVisible
            : options.target_profile == "near"
                ? TargetProfile::NearCrosshair
                : TargetProfile::Ordinary;
        benchmark_config.camera_response_px_per_stick_second =
            options.camera_response;
        benchmark_config.manual_input_scale = options.manual_input_scale;
        benchmark_config.slowdown_edge_multiplier = options.slowdown_edge;
        benchmark_config.slowdown_center_multiplier = options.slowdown_center;
        benchmark_config.short_occlusion_duration_ms =
            options.short_occlusion_ms;
        benchmark_config.fixed_target_slot_ms = options.target_slot_ms;
        benchmark_config.vision_interval_ms = options.vision_hz > 0
            ? std::max(1, static_cast<int>(std::lround(
                1000.0 / static_cast<double>(options.vision_hz))))
            : 0;
        benchmark_config.vision_result_delay_ms =
            options.vision_result_delay_ms;
        benchmark_config.vision_disturbance =
            options.vision_disturbance == "dropout-decoy"
            ? VisionDisturbanceProfile::TargetDropoutDecoy
            : options.vision_disturbance == "horizontal-aim-bias"
            ? VisionDisturbanceProfile::HorizontalAimBiasRecovery
            : options.vision_disturbance == "body-box-plus-recoil"
            ? VisionDisturbanceProfile::
                  BodyBoxDeformationAndCameraRecoil
            : options.vision_disturbance == "body-box-deformation"
                ? VisionDisturbanceProfile::BodyBoxDeformation
            : options.vision_disturbance == "gun-kick-plus-recoil"
            ? VisionDisturbanceProfile::GunKickAndCameraRecoil
            : options.vision_disturbance == "camera-recoil"
                ? VisionDisturbanceProfile::CameraRecoil
                : options.vision_disturbance == "gun-kick-adversarial"
                    ? VisionDisturbanceProfile::GunKickAdversarial
            : options.vision_disturbance == "gun-kick"
                ? VisionDisturbanceProfile::GunKick
                : VisionDisturbanceProfile::Off;
        benchmark_config.obsolete_vertical_fixture =
            options.profile == "obsolete";
        benchmark_config.target_motion_enabled =
            options.target_motion == "moving";
        benchmark_config.player_action_cues_enabled =
            options.player_action_cues == "on";
        benchmark_config.player_motion_oracle_enabled =
            options.player_motion_oracle != "off";
        benchmark_config.player_motion_rate_oracle_enabled =
            options.player_motion_oracle == "full" ||
            options.player_motion_oracle == "forecast";
        benchmark_config.player_motion_forecast_oracle_enabled =
            options.player_motion_oracle == "forecast";
        std::vector<ManualProfile> profiles;
        if (options.profile == "obsolete") {
            profiles.push_back(ManualProfile::ObsoleteAfterCrossing);
        } else if (options.profile == "recover") {
            profiles.push_back(ManualProfile::WrongThenCorrect);
        } else if (options.profile == "arc") {
            profiles.push_back(ManualProfile::ArcRecovery);
        } else if (options.profile == "scripted") {
            profiles.push_back(ManualProfile::Scripted);
        } else if (options.profile == "micro") {
            profiles.push_back(ManualProfile::MicroCorrection);
        } else {
            if (options.profile != "mixed") profiles.push_back(ManualProfile::Pure);
            if (options.profile != "pure") profiles.push_back(ManualProfile::Mixed);
        }
        std::vector<BenchmarkCohort> cohorts;
        if (options.cohort != "bodylock") cohorts.push_back(BenchmarkCohort::AdsAcquire);
        if (options.cohort != "ads") cohorts.push_back(BenchmarkCohort::BodyLockFollow);
        std::vector<PlayerStrafeMode> player_strafe_modes;
        if (options.left_strafe != "full-reversal") {
            player_strafe_modes.push_back(PlayerStrafeMode::Off);
        }
        if (options.left_strafe != "off") {
            player_strafe_modes.push_back(PlayerStrafeMode::FullReversal);
        }
        std::vector<PlayerVerticalMotionMode> player_vertical_motion_modes;
        if (options.vertical_motion == "off" ||
            options.vertical_motion == "all") {
            player_vertical_motion_modes.push_back(
                PlayerVerticalMotionMode::Off);
        }
        if (options.vertical_motion == "slide" ||
            options.vertical_motion == "all") {
            player_vertical_motion_modes.push_back(
                PlayerVerticalMotionMode::Slide);
        }
        if (options.vertical_motion == "jump" ||
            options.vertical_motion == "all") {
            player_vertical_motion_modes.push_back(
                PlayerVerticalMotionMode::Jump);
        }
        if (options.vertical_motion == "random" ||
            options.vertical_motion == "all") {
            player_vertical_motion_modes.push_back(
                PlayerVerticalMotionMode::Random);
        }
        std::vector<BenchmarkResult> results;
        std::vector<CounterfactualRunSummary> counterfactual_results;
        std::vector<FusionRunSummary> fusion_results;
        std::vector<PulseAmplificationSummary> pulse_results;
        const BenchmarkIntentFusionMode intent_fusion_mode =
            options.intent_fusion == "vector"
                ? BenchmarkIntentFusionMode::CausalVector
                : options.intent_fusion == "vector-baseline"
                    ? BenchmarkIntentFusionMode::CausalVectorBaseline
                    : BenchmarkIntentFusionMode::LegacyAxis;
        const BenchmarkRemainingWorkMode remaining_work_mode =
            options.remaining_work == "delivered"
                ? BenchmarkRemainingWorkMode::DeliveredAdjusted
                : options.remaining_work == "integrated-ai"
                    ? BenchmarkRemainingWorkMode::
                          ControllerIntegratedAssistOnly
                : options.remaining_work == "integrated"
                    ? BenchmarkRemainingWorkMode::ControllerIntegrated
                : BenchmarkRemainingWorkMode::CurrentError;
        for (const std::uint32_t seed : options.seeds) {
            const ScenarioScript script = generate_script(seed, benchmark_config);
            for (const ManualProfile profile : profiles) {
                for (const BenchmarkCohort cohort : cohorts) {
                    for (const PlayerStrafeMode player_strafe_mode :
                         player_strafe_modes) {
                        for (const PlayerVerticalMotionMode vertical_mode :
                             player_vertical_motion_modes) {
                            auto coverage =
                                std::make_shared<AssistedModeCoverage>();
                            const ReplayControllerFactory factory =
                                make_native_factory(
                                    gamepad_config, cohort,
                                    intent_fusion_mode, coverage,
                                    1.0, options.tracker_velocity_alpha,
                                    options.player_motion_model == "state" ||
                                        options.player_motion_model == "causal",
                                     options.player_motion_model == "forecast" ||
                                         options.player_motion_model == "causal",
                                     remaining_work_mode,
                                     options.remaining_work_scale,
                                     options.benchmark_recoil == "on",
                                     options.firing_body_geometry_stabilizer ==
                                         "on",
                                     options.firing_disturbance_observer ==
                                         "on");
                            ReplayReference reference = record_reference(
                                script, profile, cohort, factory,
                                player_strafe_mode, vertical_mode);
                            BenchmarkResult result =
                                reference.benchmark_result;
                            fusion_results.push_back(
                                summarize_fusion(reference));
                            pulse_results.push_back(
                                summarize_pulse_amplification(reference));
                            if (options.smoke) {
                                validate_smoke(
                                    result, options.duration_ms, *coverage);
                            }
                            print_summary(result);
                            counterfactual_results.push_back(
                                analyze_reference(
                                    reference, factory,
                                    options.counterfactual));
                            results.push_back(std::move(result));
                        }
                    }
                }
            }
        }
        write_report(
            options.output_path,
            options,
            benchmark_config,
            gamepad_config,
            file_fingerprint(options.config_path),
            results,
            counterfactual_results,
            fusion_results,
            pulse_results);
        std::cout << "cod_native_sustained_aimlab_benchmark PASS\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "cod_native_sustained_aimlab_benchmark FAIL: "
                  << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
