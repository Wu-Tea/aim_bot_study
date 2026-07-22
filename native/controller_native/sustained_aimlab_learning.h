#pragma once

#include "sustained_aimlab_simulator.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace controller_native::sustained_aimlab {

enum class LearningPolicy : std::uint8_t {
    Baseline,
    ResetEachRound,
    RetainAcrossRounds,
};

struct LearningExperimentConfig {
    int rounds = 5;
    int round_duration_ms = 60'000;
    int control_response_delay_ms = 45;
    double camera_response_px_per_stick_second = 500.0;
    LearningPolicy policy = LearningPolicy::RetainAcrossRounds;
};

struct LearningRoundResult {
    int round_index = 0;
    std::uint32_t seed = 0;
    std::uint64_t script_hash = 0;
    double total_points = 0.0;
    double acquire_points = 0.0;
    double tracking_points = 0.0;
    double smooth_bonus = 0.0;
    double started_with_response_confidence = 0.0;
    double ended_with_response_confidence = 0.0;
    double ended_with_delay_confidence = 0.0;
    double selected_delay_ms = 0.0;
    std::uint64_t accepted_updates = 0;
    int first_accepted_update_ms = -1;
    std::uint64_t valid_rollout_decisions = 0;
    std::uint64_t changed_scale_decisions = 0;
    std::uint64_t harmful_release_candidates = 0;
    double mean_selected_scale = 1.0;
};

struct LearningExperimentResult {
    LearningPolicy policy = LearningPolicy::Baseline;
    bool ground_truth_used_for_policy = false;
    bool control_history_reset_each_round = true;
    int round_duration_ms = 0;
    int plant_delay_ms = 0;
    double camera_response_px_per_stick_second = 0.0;
    ManualProfile manual_profile = ManualProfile::Pure;
    BenchmarkCohort cohort = BenchmarkCohort::AdsAcquire;
    std::string revision = "unknown";
    bool dirty = false;
    std::string config_path;
    std::uint64_t config_fingerprint_fnv1a64 = 0;
    std::vector<LearningRoundResult> rounds;
};

using RoundControllerFactory = std::function<ControllerStep()>;

LearningExperimentResult run_learning_experiment(
    const LearningExperimentConfig& config,
    std::uint32_t first_seed,
    ManualProfile manual_profile,
    BenchmarkCohort cohort,
    RoundControllerFactory controller_factory);

std::string learning_experiment_to_json(
    const LearningExperimentResult& result);

}  // namespace controller_native::sustained_aimlab
