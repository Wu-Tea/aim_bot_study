#pragma once

#include <cstdint>
#include <vector>

namespace controller_native::sustained_aimlab {

struct Vec2d {
    double x = 0.0;
    double y = 0.0;
};

enum class MotionProfile : std::uint8_t {
    ConstantHorizontal,
    ConstantVertical,
    ConstantDiagonal,
    Accelerate,
    Reverse,
    JumpFall,
    Stop,
};

enum class ManualProfile : std::uint8_t {
    Pure,
    Mixed,
};

enum class BenchmarkCohort : std::uint8_t {
    AdsAcquire,
    BodyLockFollow,
};

enum class TargetProfile : std::uint8_t {
    Ordinary,
    SmallVisible,
};

struct BenchmarkConfig {
    TargetProfile target_profile = TargetProfile::Ordinary;
    int duration_ms = 60'000;
    int tick_ms = 1;
    int tracking_window_ms = 1'000;
    int inter_target_gap_ms = 50;
    int bodylock_entry_timeout_ms = 250;
    int min_acquire_deadline_ms = 250;
    int max_acquire_deadline_ms = 330;
    double target_radius_px = 24.0;
    double slowdown_transition_px = 3.0;
    double slowdown_edge_multiplier = 0.50;
    double slowdown_center_multiplier = 0.40;
    double camera_response_px_per_stick_second = 500.0;
    int control_response_delay_ms = 0;
    int frame_width_px = 640;
    int frame_height_px = 512;
};

struct TargetScript {
    std::uint64_t id = 0;
    MotionProfile motion = MotionProfile::ConstantHorizontal;
    Vec2d initial_error_px;
    Vec2d initial_velocity_px_per_second;
    Vec2d acceleration_px_per_second_squared;
    int maneuver_at_ms = -1;
    int acquire_deadline_ms = 250;
    double visible_radius_px = 24.0;
    std::vector<int> observation_at_ms;
    std::vector<Vec2d> observation_noise_px;
};

struct ScenarioScript {
    std::uint32_t seed = 0;
    BenchmarkConfig config;
    std::vector<TargetScript> targets;
    std::uint64_t hash = 0;
};

}  // namespace controller_native::sustained_aimlab
