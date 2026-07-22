#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace controller_native::causal_response {

struct Matrix2 {
    std::array<std::array<double, 2>, 2> values{{{1.0, 0.0}, {0.0, 1.0}}};
};

struct PlantConfig {
    Matrix2 right_response{{{{900.0, 40.0}, {-20.0, 760.0}}}};
    Matrix2 left_response{{{{-180.0, 0.0}, {0.0, -80.0}}}};
    int delay_ms = 45;
    int duration_ms = 4000;
    int tick_ms = 5;
};

struct CohortResult {
    std::string name;
    double cumulative_error_px_ms = 0.0;
    double terminal_error_px = 0.0;
    double reverse_burden_stick_ms = 0.0;
    double jerk_stick = 0.0;
    double hindsight_headroom_px_ms = 0.0;
    std::array<double, 4> hindsight_headroom_by_horizon_px_ms{};
    double aggressive_gain_px_ms = 0.0;
    double aggressive_regret_px_ms = 0.0;
    int wrong_way_ms = 0;
    int interruption_ms = 0;
    int rollout_decisions = 0;
    int rollout_top1_agreements = 0;
    double rollout_causal_gain_px_ms = 0.0;
    double rollout_regret_px_ms = 0.0;
    int rollout_harmful_release_count = 0;
};

struct MutationResult {
    std::string name;
    bool detected = false;
    double evidence = 0.0;
};

struct FixtureReport {
    std::uint32_t seed = 0;
    double ground_truth_delay_ms = 0.0;
    bool controls_depend_on_prior_error = false;
    bool hindsight_only = true;
    std::vector<CohortResult> cohorts;
    std::vector<MutationResult> mutations;
    double hindsight_headroom_px_ms = 0.0;
    std::array<double, 4> hindsight_headroom_by_horizon_px_ms{};
    double single_target_wrong_input_headroom_px_ms = 0.0;
    double single_target_aggressive_gain_px_ms = 0.0;
    double multi_target_conservative_regret_px_ms = 0.0;
    double multi_target_aggressive_regret_px_ms = 0.0;
    double maneuver_absolute_degradation_pp = 0.0;
    double maneuver_relative_degradation_percent = 0.0;
    int rollout_decisions = 0;
    int rollout_top1_agreements = 0;
    double rollout_top1_agreement = 0.0;
    double rollout_causal_gain_px_ms = 0.0;
    double rollout_regret_px_ms = 0.0;
    int rollout_harmful_release_count = 0;
};

FixtureReport run_feedback_fixture(const PlantConfig& config, std::uint32_t seed);
std::string to_json(const FixtureReport& report);

}  // namespace controller_native::causal_response
