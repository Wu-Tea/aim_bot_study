#pragma once

#include "sustained_aimlab_scenario.h"
#include "sustained_aimlab_score.h"

#include <cstdint>
#include <functional>

namespace controller_native::sustained_aimlab {

struct ControllerObservation {
    int now_ms = 0;
    bool target_present = false;
    bool fresh_vision = false;
    std::uint64_t frame_id = 0;
    std::uint64_t target_id = 0;
    Vec2d observed_error_px;
    Vec2d manual_stick;
};

struct ControllerStepResult {
    Vec2d final_stick;
    Vec2d requested_assist_stick;
    Vec2d shaped_assist_stick;
    Vec2d predicted_terminal_error_px;
    double radial_closing_velocity_px_per_sec = 0.0;
    bool bodylock_mode = false;
    bool target_observed = false;
    bool tracker_reliable = false;
    int intent_fusion_candidate = 0;
    float intent_fusion_manual_weight = 1.0f;
    float intent_fusion_ai_weight = 0.0f;
    bool intent_fusion_fallback = false;
    bool intent_fusion_manual_escape = false;
};

struct SimulationTraceFrame {
    int absolute_ms = 0;
    int target_elapsed_ms = -1;
    bool target_active = false;
    bool fresh_vision = false;
    std::uint64_t target_id = 0;
    MotionProfile motion = MotionProfile::ConstantHorizontal;
    Vec2d true_error_before_px;
    Vec2d true_error_after_px;
    Vec2d target_velocity_px_per_second;
    ControllerObservation input;
    ControllerStepResult output;
};

using SimulationTraceObserver =
    std::function<void(const SimulationTraceFrame&)>;

using ControllerStep = std::function<ControllerStepResult(
    const ControllerObservation&)>;

BenchmarkResult run_simulation(
    const ScenarioScript& script,
    ManualProfile manual_profile,
    ControllerStep controller_step,
    BenchmarkCohort cohort = BenchmarkCohort::AdsAcquire,
    SimulationTraceObserver trace_observer = {});

}  // namespace controller_native::sustained_aimlab
