#pragma once

#include "sustained_aimlab_types.h"

namespace controller_native::sustained_aimlab {

ScenarioScript generate_script(
    std::uint32_t seed,
    const BenchmarkConfig& config);

void advance_target(
    const TargetScript& script,
    int target_elapsed_ms,
    double dt_seconds,
    Vec2d& position_error_px,
    Vec2d& velocity_px_per_second);

double aim_slowdown_multiplier(
    double distance_px,
    const BenchmarkConfig& config = BenchmarkConfig{});

double aim_slowdown_multiplier(
    double distance_px,
    double target_radius_px,
    const BenchmarkConfig& config);

const char* to_string(MotionProfile profile) noexcept;

}  // namespace controller_native::sustained_aimlab
