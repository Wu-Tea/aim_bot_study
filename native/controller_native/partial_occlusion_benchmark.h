#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace controller_native::partial_occlusion {

enum class ScenarioKind {
    Combat,
    HumanErrors,
    PracticalStress,
    DestructiveStress,
};

enum class HumanErrorKind {
    None,
    StaleDirection,
    WrongX,
    WrongY,
    CrossingInertia,
    WrongBoth,
    MixedAxes,
};

enum class StressTier {
    None,
    Practical,
    Destructive,
};

enum class VisionPhase {
    FullObserved,
    PartialObserved,
    Missing,
    BiasedReacquisition,
    StableRecovery,
};

struct ScenarioCase {
    int index = 0;
    int direction_x = 1;
    int direction_y = 1;
    int observation_gap_ms = 60;
    HumanErrorKind error_kind = HumanErrorKind::None;
    double target_velocity_x_px_per_sec = 0.0;
    double target_velocity_y_px_per_sec = 0.0;
    double left_stick_x = 0.0;
    double full_body_top_y_px = -72.0;
    double full_body_width_px = 84.0;
    double full_body_height_px = 180.0;
    double partial_body_height_px = 108.0;
    double truth_aim_height_ratio = 0.365;
    int full_observed_ms = 140;
    int partial_observed_ms = 50;
    int biased_reacquisition_ms = 50;
    int stable_recovery_ms = 260;
    int manual_reaction_ms = 55;
    int error_hold_ms = 0;
    int error_onset_ms = -1;
    double manual_magnitude_cap = 0.45;
    StressTier stress_tier = StressTier::None;
    double truth_motion_amplitude_x_px_per_sec = 0.0;
    double truth_motion_amplitude_y_px_per_sec = 0.0;
    double observation_jitter_x_px = 0.0;
    double observation_jitter_y_px = 0.0;
    double observation_jump_px = 0.0;
};

struct ScenarioDefinition {
    std::string name;
    std::uint32_t seed = 1337;
    std::vector<ScenarioCase> cases;
};

struct ManualProfileSample {
    double x = 0.0;
    double y = 0.0;
};

struct DisturbanceSample {
    double x = 0.0;
    double y = 0.0;
};

struct PartialOcclusionMetrics {
    int cases = 0;
    int measured_frames = 0;
    int vision_samples = 0;
    int missing_vision_samples = 0;
    int ads_snap_frames = 0;
    int body_lock_frames = 0;
    int manual_frames = 0;
    int mode_changes = 0;
    int output_spikes = 0;
    int correct_manual_opposition_frames = 0;
    int wrong_manual_high_force_frames = 0;
    int axis_intervention_x_frames = 0;
    int axis_intervention_y_frames = 0;
    double mean_error_px = 0.0;
    double p95_error_px = 0.0;
    double final_error_px = 0.0;
    double peak_error_px = 0.0;
    double max_overshoot_x_px = 0.0;
    double max_overshoot_y_px = 0.0;
    double occlusion_peak_error_px = 0.0;
    double mean_recovery_ms = 0.0;
    double p95_output_delta = 0.0;
    double peak_geometry_bias_px = 0.0;
    double error_window_mean_px = 0.0;
    double error_window_p95_px = 0.0;
    double error_window_peak_px = 0.0;
    double error_window_recovery_ms = 0.0;
    double max_observation_offset_px = 0.0;
    double peak_manual_error_x = 0.0;
    double peak_manual_error_y = 0.0;
    int manual_error_active_frames = 0;
};

struct PartialOcclusionScore {
    int formula_version = 1;
    double tracking = 0.0;
    double overshoot = 0.0;
    double recovery = 0.0;
    double smoothness = 0.0;
    double intent = 0.0;
    double overall = 0.0;
};

struct BenchmarkMetadata {
    std::uint32_t seed = 1337;
    std::string config_path;
    double aim_height_ratio = 0.365;
    double ads_strength_scale = 0.0;
    double ads_vertical_strength_scale = 0.0;
    double ads_range_px = 0.0;
    int ads_snap_duration_ms = 0;
    double bodylock_strength = 0.0;
    double bodylock_vertical_strength = 0.0;
    double bodylock_activation_range_px = 0.0;
    double bodylock_tolerance_px = 0.0;
};

struct CaseReport {
    std::string name;
    PartialOcclusionMetrics metrics;
    PartialOcclusionScore score;
};

struct ScenarioReport {
    std::string name;
    PartialOcclusionMetrics metrics;
    PartialOcclusionScore score;
    std::vector<CaseReport> cases;
};

ScenarioDefinition build_scenario(ScenarioKind kind, std::uint32_t seed = 1337);

ManualProfileSample sample_manual_profile(
    const ScenarioCase& value,
    ManualProfileSample historical,
    ManualProfileSample ideal,
    int elapsed_in_error_ms) noexcept;

DisturbanceSample sample_truth_velocity_disturbance(
    const ScenarioCase& value,
    std::uint32_t seed,
    int elapsed_ms) noexcept;

DisturbanceSample sample_observation_disturbance(
    const ScenarioCase& value,
    std::uint32_t seed,
    int elapsed_ms) noexcept;

double truth_chest_y_px(
    double full_body_top_y_px,
    double full_body_height_px,
    double aim_height_ratio) noexcept;

double observed_chest_y_px(
    double observed_body_top_y_px,
    double observed_body_height_px,
    double aim_height_ratio) noexcept;

PartialOcclusionScore score_metrics(const PartialOcclusionMetrics& metrics) noexcept;

std::string render_report_json(
    const BenchmarkMetadata& metadata,
    const std::vector<ScenarioReport>& scenarios);

const char* to_string(HumanErrorKind value) noexcept;

}  // namespace controller_native::partial_occlusion
