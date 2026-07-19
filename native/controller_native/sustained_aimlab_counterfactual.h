#pragma once

#include "sustained_aimlab_trace.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace controller_native::sustained_aimlab {

enum class BranchPolicy : std::uint8_t {
    ActualMix,
    Neutral,
    ManualOnly,
    AiOnly,
    ManualPlusAi25,
    ManualPlusAi50,
    ManualPlusAi75,
};

struct BranchSchedule {
    int start_ms = 0;
    int duration_ms = 0;
    BranchPolicy policy = BranchPolicy::ActualMix;
};

using ReplayControllerFactory =
    std::function<ControllerStep(const BranchSchedule&)>;

struct ReplayRequest {
    int branch_at_ms = 0;
    int substitution_ms = 40;
    int horizon_ms = 160;
    BranchPolicy policy = BranchPolicy::ActualMix;
};

struct ReplayReference {
    ScenarioScript script;
    ManualProfile manual_profile = ManualProfile::Pure;
    BenchmarkCohort cohort = BenchmarkCohort::AdsAcquire;
    std::vector<SimulationTraceFrame> trace;
};

struct BranchResult {
    BranchPolicy policy = BranchPolicy::ActualMix;
    bool prebranch_verified = false;
    int first_divergence_ms = -1;
    double error_area_px_ms = 0.0;
    double final_error_px = 0.0;
    double path_px = 0.0;
    int correction_reversals = 0;
    int time_to_acquire_ms = -1;
    int settle_ms = -1;
    int false_interrupt_ms = 0;
    int reacquire_delay_ms = 0;
    std::vector<SimulationTraceFrame> trace;
};

enum class OutcomeClass : std::uint8_t {
    LocalHelpfulGlobalHelpful,
    LocalHelpfulGlobalHarmful,
    LocalHarmfulGlobalHelpful,
    LocalHarmfulGlobalHarmful,
};

struct AnalysisBudget {
    int substitution_ms = 80;
    int stable_horizon_ms = 500;
};

struct CounterfactualMetrics {
    double instant_progress_px = 0.0;
    double regret_40_px_ms = 0.0;
    double regret_80_px_ms = 0.0;
    double regret_160_px_ms = 0.0;
    double future_burden_px_ms = 0.0;
    int future_settle_delay_ms = 0;
    double extra_path_px = 0.0;
    int correction_reversals = 0;
    int manual_helped_but_suppressed_ms = 0;
    int ai_helped_but_suppressed_ms = 0;
    int both_harmful_ms = 0;
    int destructive_stack_ms = 0;
    int wrong_way_commit_ms = 0;
    int false_interrupt_ms = 0;
    int reacquire_delay_ms = 0;
    OutcomeClass classification = OutcomeClass::LocalHarmfulGlobalHarmful;
};

struct CounterfactualEpisode {
    int branch_at_ms = 0;
    std::vector<BranchResult> candidates;
    BranchResult causal_oracle;
    BranchResult hindsight_oracle;
    CounterfactualMetrics actual;
};

ReplayReference record_reference(
    const ScenarioScript& script,
    ManualProfile manual_profile,
    BenchmarkCohort cohort,
    const ReplayControllerFactory& factory);

BranchResult replay_branch(
    const ReplayReference& reference,
    const ReplayRequest& request,
    const ReplayControllerFactory& factory);

CounterfactualEpisode analyze_episode(
    const ReplayReference& reference,
    int branch_at_ms,
    const ReplayControllerFactory& factory,
    const AnalysisBudget& budget = {});

const char* to_string(BranchPolicy policy) noexcept;
const char* to_string(OutcomeClass outcome) noexcept;

}  // namespace controller_native::sustained_aimlab
