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
    CompoundDirectional,
};

enum class ScenarioProfile : std::uint8_t {
    Baseline,
    CompoundDirectional,
};

enum class ManualProfile : std::uint8_t {
    Pure,
    Mixed,
    Scripted,
    ObsoleteAfterCrossing,
    WrongThenCorrect,
    ArcRecovery,
};

enum class BenchmarkCohort : std::uint8_t {
    AdsAcquire,
    BodyLockFollow,
};

enum class TargetProfile : std::uint8_t {
    Ordinary,
    SmallVisible,
    NearCrosshair,
};

enum class PlayerStrafeMode : std::uint8_t {
    Off,
    FullReversal,
};

enum class PlayerVerticalMotionMode : std::uint8_t {
    Off,
    Slide,
    Jump,
    Random,
};

enum class VisionDisturbanceProfile : std::uint8_t {
    Off,
    GunKick,
    GunKickAdversarial,
    CameraRecoil,
    GunKickAndCameraRecoil,
    BodyBoxDeformation,
    BodyBoxDeformationAndCameraRecoil,
    HorizontalAimBiasRecovery,
};

enum class PlayerVerticalEvent : std::uint8_t {
    Slide,
    Jump,
};

inline constexpr double kPlayerStrafeMinTopSpeedPxPerSecond = 125.0;
inline constexpr double kPlayerStrafeMaxTopSpeedPxPerSecond = 232.0;
inline constexpr double kPlayerStrafeMinTimeConstantMs = 100.0;
inline constexpr double kPlayerStrafeMaxTimeConstantMs = 180.0;

struct PlayerStrafeScript {
    int initial_direction = 1;
    int onset_ms = 0;
    int reverse_ms = 1;
    int release_ms = 2;
    double top_speed_px_per_second = kPlayerStrafeMinTopSpeedPxPerSecond;
    double time_constant_ms = kPlayerStrafeMinTimeConstantMs;

    bool operator==(const PlayerStrafeScript& other) const noexcept {
        return initial_direction == other.initial_direction &&
            onset_ms == other.onset_ms &&
            reverse_ms == other.reverse_ms &&
            release_ms == other.release_ms &&
            top_speed_px_per_second == other.top_speed_px_per_second &&
            time_constant_ms == other.time_constant_ms;
    }

    bool operator!=(const PlayerStrafeScript& other) const noexcept {
        return !(*this == other);
    }
};

struct PlayerVerticalMotionScript {
    PlayerVerticalEvent random_event = PlayerVerticalEvent::Slide;
    int slide_onset_ms = 80;
    int slide_drop_ms = 100;
    int slide_hold_ms = 180;
    int slide_recover_ms = 140;
    bool slide_instant_recovery = false;
    double slide_depth_px = 40.0;
    int jump_onset_ms = 100;
    int jump_duration_ms = 520;
    double jump_height_px = 44.0;

    bool operator==(const PlayerVerticalMotionScript& other) const noexcept {
        return random_event == other.random_event &&
            slide_onset_ms == other.slide_onset_ms &&
            slide_drop_ms == other.slide_drop_ms &&
            slide_hold_ms == other.slide_hold_ms &&
            slide_recover_ms == other.slide_recover_ms &&
            slide_instant_recovery == other.slide_instant_recovery &&
            slide_depth_px == other.slide_depth_px &&
            jump_onset_ms == other.jump_onset_ms &&
            jump_duration_ms == other.jump_duration_ms &&
            jump_height_px == other.jump_height_px;
    }

    bool operator!=(const PlayerVerticalMotionScript& other) const noexcept {
        return !(*this == other);
    }
};

struct VisionOcclusionBurst {
    int tracking_offset_ms = 0;
    int duration_ms = 0;
};

struct BenchmarkConfig {
    ScenarioProfile scenario_profile = ScenarioProfile::Baseline;
    TargetProfile target_profile = TargetProfile::Ordinary;
    int duration_ms = 60'000;
    int tick_ms = 1;
    int tracking_window_ms = 1'000;
    int inter_target_gap_ms = 50;
    int bodylock_entry_timeout_ms = 250;
    int min_acquire_deadline_ms = 250;
    int max_acquire_deadline_ms = 330;
    // Zero preserves outcome-dependent target replacement. A positive value
    // gives every target the same wall-clock slot, including timeout idle.
    int fixed_target_slot_ms = 0;
    double target_radius_px = 24.0;
    double slowdown_transition_px = 3.0;
    double slowdown_edge_multiplier = 0.50;
    double slowdown_center_multiplier = 0.40;
    double camera_response_px_per_stick_second = 500.0;
    int control_response_delay_ms = 0;
    int frame_width_px = 640;
    int frame_height_px = 512;
    // Zero preserves the historical randomized 10-12 ms cadence.
    // Positive values request a fixed benchmark-only capture interval.
    int vision_interval_ms = 0;
    // Benchmark-only apparent target motion caused by firing/camera kick.
    // This changes Vision observations, not the physical target trajectory.
    VisionDisturbanceProfile vision_disturbance =
        VisionDisturbanceProfile::Off;
    bool obsolete_vertical_fixture = false;
    int short_occlusion_duration_ms = 0;
    bool target_motion_enabled = true;
    bool player_action_cues_enabled = true;
    bool player_motion_oracle_enabled = false;
    bool player_motion_rate_oracle_enabled = false;
    bool player_motion_forecast_oracle_enabled = false;
};

struct VelocityManeuver {
    int at_ms = 0;
    Vec2d velocity_px_per_second;
};

struct TargetScript {
    std::uint64_t id = 0;
    MotionProfile motion = MotionProfile::ConstantHorizontal;
    Vec2d initial_error_px;
    Vec2d initial_velocity_px_per_second;
    Vec2d acceleration_px_per_second_squared;
    int maneuver_at_ms = -1;
    std::vector<VelocityManeuver> velocity_maneuvers;
    int acquire_deadline_ms = 250;
    double visible_radius_px = 24.0;
    PlayerStrafeScript player_strafe;
    PlayerVerticalMotionScript player_vertical;
    std::vector<int> observation_at_ms;
    std::vector<Vec2d> observation_noise_px;
    std::vector<VisionOcclusionBurst> vision_occlusion_bursts;
};

struct ScenarioScript {
    std::uint32_t seed = 0;
    BenchmarkConfig config;
    std::vector<TargetScript> targets;
    std::uint64_t hash = 0;
};

}  // namespace controller_native::sustained_aimlab
