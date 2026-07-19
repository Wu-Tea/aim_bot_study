#pragma once

#include "sustained_aimlab_trace.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace controller_native::sustained_aimlab {

enum class BranchPolicy : std::uint8_t {
    ActualMix,
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

ReplayReference record_reference(
    const ScenarioScript& script,
    ManualProfile manual_profile,
    BenchmarkCohort cohort,
    const ReplayControllerFactory& factory);

BranchResult replay_branch(
    const ReplayReference& reference,
    const ReplayRequest& request,
    const ReplayControllerFactory& factory);

const char* to_string(BranchPolicy policy) noexcept;

}  // namespace controller_native::sustained_aimlab
