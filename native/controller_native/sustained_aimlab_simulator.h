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
};

using ControllerStep = std::function<ControllerStepResult(
    const ControllerObservation&)>;

BenchmarkResult run_simulation(
    const ScenarioScript& script,
    ManualProfile manual_profile,
    ControllerStep controller_step,
    BenchmarkCohort cohort = BenchmarkCohort::AdsAcquire);

}  // namespace controller_native::sustained_aimlab
