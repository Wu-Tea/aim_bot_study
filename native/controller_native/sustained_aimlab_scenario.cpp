#include "sustained_aimlab_scenario.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <random>
#include <stdexcept>
#include <type_traits>

namespace controller_native::sustained_aimlab {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

double smoothstep(double value) noexcept {
    const double clamped = std::clamp(value, 0.0, 1.0);
    return clamped * clamped * (3.0 - 2.0 * clamped);
}

double sampled_sign(std::mt19937& random) {
    return std::uniform_int_distribution<int>(0, 1)(random) == 0 ? -1.0 : 1.0;
}

Vec2d direction_from_angle(double radians) {
    return {std::cos(radians), std::sin(radians)};
}

Vec2d scaled(Vec2d value, double amount) {
    return {value.x * amount, value.y * amount};
}

void hash_bytes(std::uint64_t& hash, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= kFnvPrime;
    }
}

template <typename Value, std::enable_if_t<std::is_integral_v<Value>, int> = 0>
void hash_integral(std::uint64_t& hash, Value value) {
    const Value stored = value;
    hash_bytes(hash, &stored, sizeof(stored));
}

template <typename Value, std::enable_if_t<std::is_enum_v<Value>, int> = 0>
void hash_integral(std::uint64_t& hash, Value value) {
    using Stored = std::underlying_type_t<Value>;
    const Stored stored = static_cast<Stored>(value);
    hash_bytes(hash, &stored, sizeof(stored));
}

void hash_double(std::uint64_t& hash, double value) {
    static_assert(sizeof(double) == sizeof(std::uint64_t));
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    hash_bytes(hash, &bits, sizeof(bits));
}

void hash_vec(std::uint64_t& hash, Vec2d value) {
    hash_double(hash, value.x);
    hash_double(hash, value.y);
}

void hash_config(std::uint64_t& hash, const BenchmarkConfig& config) {
    // The scenario profile is omitted only for the historical baseline value.
    // Generated player-motion scripts below are always hashed because changing
    // their seeded timing would invalidate paired POV-motion comparisons.
    if (config.scenario_profile != ScenarioProfile::Baseline) {
        hash_integral(hash, config.scenario_profile);
    }
    hash_integral(hash, config.duration_ms);
    hash_integral(hash, config.target_profile);
    hash_integral(hash, config.tick_ms);
    hash_integral(hash, config.tracking_window_ms);
    hash_integral(hash, config.inter_target_gap_ms);
    hash_integral(hash, config.min_acquire_deadline_ms);
    hash_integral(hash, config.max_acquire_deadline_ms);
    if (config.fixed_target_slot_ms > 0) {
        hash_integral(hash, config.fixed_target_slot_ms);
    }
    hash_double(hash, config.target_radius_px);
    hash_double(hash, config.slowdown_transition_px);
    hash_double(hash, config.slowdown_edge_multiplier);
    hash_double(hash, config.slowdown_center_multiplier);
    hash_double(hash, config.camera_response_px_per_stick_second);
    hash_integral(hash, config.control_response_delay_ms);
    hash_integral(hash, config.frame_width_px);
    hash_integral(hash, config.frame_height_px);
    hash_integral(hash, config.vision_interval_ms);
    hash_integral(hash, config.vision_disturbance);
    hash_integral(hash, config.obsolete_vertical_fixture ? 1 : 0);
    hash_integral(hash, config.short_occlusion_duration_ms);
    hash_integral(hash, config.target_motion_enabled ? 1 : 0);
}

std::uint64_t script_hash(const ScenarioScript& script) {
    std::uint64_t hash = kFnvOffset;
    hash_integral(hash, script.seed);
    hash_config(hash, script.config);
    hash_integral(hash, static_cast<std::uint64_t>(script.targets.size()));
    for (const TargetScript& target : script.targets) {
        hash_integral(hash, target.id);
        hash_integral(hash, target.motion);
        hash_vec(hash, target.initial_error_px);
        hash_vec(hash, target.initial_velocity_px_per_second);
        hash_vec(hash, target.acceleration_px_per_second_squared);
        hash_integral(hash, target.maneuver_at_ms);
        if (!target.velocity_maneuvers.empty()) {
            hash_integral(hash,
                static_cast<std::uint64_t>(target.velocity_maneuvers.size()));
            for (const VelocityManeuver& maneuver : target.velocity_maneuvers) {
                hash_integral(hash, maneuver.at_ms);
                hash_vec(hash, maneuver.velocity_px_per_second);
            }
        }
        hash_integral(hash, target.acquire_deadline_ms);
        hash_double(hash, target.visible_radius_px);
        hash_integral(hash, target.player_strafe.initial_direction);
        hash_integral(hash, target.player_strafe.onset_ms);
        hash_integral(hash, target.player_strafe.reverse_ms);
        hash_integral(hash, target.player_strafe.release_ms);
        hash_double(hash, target.player_strafe.top_speed_px_per_second);
        hash_double(hash, target.player_strafe.time_constant_ms);
        hash_integral(hash, target.player_vertical.random_event);
        hash_integral(hash, target.player_vertical.slide_onset_ms);
        hash_integral(hash, target.player_vertical.slide_drop_ms);
        hash_integral(hash, target.player_vertical.slide_hold_ms);
        hash_integral(hash, target.player_vertical.slide_recover_ms);
        hash_integral(
            hash, target.player_vertical.slide_instant_recovery ? 1 : 0);
        hash_double(hash, target.player_vertical.slide_depth_px);
        hash_integral(hash, target.player_vertical.jump_onset_ms);
        hash_integral(hash, target.player_vertical.jump_duration_ms);
        hash_double(hash, target.player_vertical.jump_height_px);
        hash_integral(hash,
            static_cast<std::uint64_t>(target.observation_at_ms.size()));
        for (std::size_t index = 0; index < target.observation_at_ms.size(); ++index) {
            hash_integral(hash, target.observation_at_ms[index]);
            hash_vec(hash, target.observation_noise_px[index]);
        }
        hash_integral(
            hash,
            static_cast<std::uint64_t>(
                target.vision_occlusion_bursts.size()));
        for (const auto& burst : target.vision_occlusion_bursts) {
            hash_integral(hash, burst.tracking_offset_ms);
            hash_integral(hash, burst.duration_ms);
        }
    }
    return hash;
}

void reflect_axis(double lower, double upper, double& position, double& velocity) {
    if (position > upper) {
        position = upper - (position - upper);
        velocity = -std::fabs(velocity);
    } else if (position < lower) {
        position = lower + (lower - position);
        velocity = std::fabs(velocity);
    }
    position = std::clamp(position, lower, upper);
}

}  // namespace

ScenarioScript generate_script(
    std::uint32_t seed,
    const BenchmarkConfig& config) {
    if (config.duration_ms <= 0 || config.tick_ms <= 0 ||
        config.min_acquire_deadline_ms <= 0 ||
        config.max_acquire_deadline_ms < config.min_acquire_deadline_ms ||
        config.fixed_target_slot_ms < 0 ||
        config.target_radius_px <= 0.0 ||
        config.vision_interval_ms < 0) {
        throw std::invalid_argument("invalid sustained AimLab benchmark config");
    }

    ScenarioScript result;
    result.seed = seed;
    result.config = config;

    std::mt19937 random(seed);
    std::uniform_real_distribution<double> angle_distribution(0.0, 2.0 * kPi);
    std::uniform_real_distribution<double> distance_distribution(48.0, 140.0);
    std::uniform_real_distribution<double> near_distance_distribution(8.0, 40.0);
    std::uniform_real_distribution<double> speed_distribution(80.0, 160.0);
    std::uniform_real_distribution<double> acceleration_distribution(60.0, 160.0);
    std::uniform_real_distribution<double> jump_speed_distribution(160.0, 240.0);
    std::uniform_real_distribution<double> noise_distribution(-0.75, 0.75);
    std::uniform_int_distribution<int> deadline_distribution(
        config.min_acquire_deadline_ms, config.max_acquire_deadline_ms);
    std::uniform_int_distribution<int> reverse_time_distribution(80, 180);
    std::uniform_int_distribution<int> stop_time_distribution(80, 200);
    std::uniform_int_distribution<int> compound_dwell_distribution(80, 180);
    std::uniform_real_distribution<double> compound_turn_distribution(
        70.0 * kPi / 180.0, 150.0 * kPi / 180.0);
    std::uniform_int_distribution<int> observation_interval_distribution(10, 12);
    std::uniform_int_distribution<int> profile_offset_distribution(0, 6);
    std::mt19937 strafe_random(seed ^ 0xA17E57AFu);
    std::uniform_int_distribution<int> strafe_onset_distribution(40, 180);
    std::uniform_int_distribution<int> strafe_leg_distribution(180, 420);
    std::uniform_real_distribution<double> strafe_speed_distribution(
        kPlayerStrafeMinTopSpeedPxPerSecond,
        kPlayerStrafeMaxTopSpeedPxPerSecond);
    std::uniform_real_distribution<double> strafe_time_constant_distribution(
        kPlayerStrafeMinTimeConstantMs,
        kPlayerStrafeMaxTimeConstantMs);
    std::mt19937 vertical_random(seed ^ 0x71E271CAu);
    std::uniform_int_distribution<int> vertical_event_distribution(0, 1);
    std::uniform_int_distribution<int> slide_onset_distribution(60, 220);
    std::uniform_int_distribution<int> slide_drop_distribution(70, 140);
    std::uniform_int_distribution<int> slide_hold_distribution(120, 300);
    std::uniform_int_distribution<int> slide_recover_distribution(100, 220);
    std::uniform_real_distribution<double> slide_depth_distribution(32.0, 72.0);
    std::uniform_int_distribution<int> jump_onset_distribution(60, 240);
    std::uniform_int_distribution<int> jump_duration_distribution(420, 700);
    std::uniform_real_distribution<double> jump_height_distribution(28.0, 64.0);
    std::mt19937 occlusion_random(seed ^ 0x5a17c9e3u);
    std::uniform_int_distribution<int> early_occlusion_distribution(80, 180);
    std::uniform_int_distribution<int> late_occlusion_distribution(480, 620);

    const int shortest_cycle_ms = config.min_acquire_deadline_ms +
        config.inter_target_gap_ms;
    const std::size_t target_count = static_cast<std::size_t>(
        std::ceil(static_cast<double>(config.duration_ms) /
                  static_cast<double>(shortest_cycle_ms))) + 1;
    result.targets.reserve(target_count);
    const int profile_offset = profile_offset_distribution(random);

    for (std::size_t index = 0; index < target_count; ++index) {
        TargetScript target;
        target.id = static_cast<std::uint64_t>(index + 1);
        target.player_strafe.initial_direction =
            sampled_sign(strafe_random) < 0.0 ? -1 : 1;
        target.player_strafe.onset_ms =
            strafe_onset_distribution(strafe_random);
        target.player_strafe.reverse_ms =
            target.player_strafe.onset_ms +
            strafe_leg_distribution(strafe_random);
        target.player_strafe.release_ms =
            target.player_strafe.reverse_ms +
            strafe_leg_distribution(strafe_random);
        target.player_strafe.top_speed_px_per_second =
            strafe_speed_distribution(strafe_random);
        target.player_strafe.time_constant_ms =
            strafe_time_constant_distribution(strafe_random);
        target.player_vertical.random_event =
            vertical_event_distribution(vertical_random) == 0
            ? PlayerVerticalEvent::Slide
            : PlayerVerticalEvent::Jump;
        target.player_vertical.slide_onset_ms =
            slide_onset_distribution(vertical_random);
        target.player_vertical.slide_drop_ms =
            slide_drop_distribution(vertical_random);
        target.player_vertical.slide_hold_ms =
            slide_hold_distribution(vertical_random);
        target.player_vertical.slide_recover_ms =
            slide_recover_distribution(vertical_random);
        target.player_vertical.slide_instant_recovery =
            vertical_event_distribution(vertical_random) == 0;
        target.player_vertical.slide_depth_px =
            slide_depth_distribution(vertical_random);
        target.player_vertical.jump_onset_ms =
            jump_onset_distribution(vertical_random);
        target.player_vertical.jump_duration_ms =
            jump_duration_distribution(vertical_random);
        target.player_vertical.jump_height_px =
            jump_height_distribution(vertical_random);
        target.motion =
            config.scenario_profile == ScenarioProfile::CompoundDirectional
            ? MotionProfile::CompoundDirectional
            : static_cast<MotionProfile>((profile_offset + index) % 7);
        const double angle = angle_distribution(random);
        const double distance =
            config.target_profile == TargetProfile::NearCrosshair
            ? near_distance_distribution(random)
            : distance_distribution(random);
        target.initial_error_px = scaled(direction_from_angle(angle), distance);
        if (config.obsolete_vertical_fixture) {
            target.initial_error_px = {
                std::uniform_real_distribution<double>(-6.0, 6.0)(random),
                -std::uniform_real_distribution<double>(100.0, 160.0)(random),
            };
        }
        target.acquire_deadline_ms = deadline_distribution(random);

        const double speed = speed_distribution(random);
        switch (target.motion) {
        case MotionProfile::ConstantHorizontal:
            target.initial_velocity_px_per_second = {sampled_sign(random) * speed, 0.0};
            break;
        case MotionProfile::ConstantVertical:
            target.initial_velocity_px_per_second = {0.0, sampled_sign(random) * speed};
            break;
        case MotionProfile::ConstantDiagonal: {
            constexpr double inverse_sqrt_two = 0.70710678118654752440;
            target.initial_velocity_px_per_second = {
                sampled_sign(random) * speed * inverse_sqrt_two,
                sampled_sign(random) * speed * inverse_sqrt_two,
            };
            break;
        }
        case MotionProfile::Accelerate: {
            const Vec2d direction = direction_from_angle(angle_distribution(random));
            target.initial_velocity_px_per_second = scaled(direction, speed);
            target.acceleration_px_per_second_squared = scaled(
                direction, sampled_sign(random) * acceleration_distribution(random));
            break;
        }
        case MotionProfile::Reverse: {
            const Vec2d direction = direction_from_angle(angle_distribution(random));
            target.initial_velocity_px_per_second = scaled(direction, speed);
            target.maneuver_at_ms = reverse_time_distribution(random);
            break;
        }
        case MotionProfile::JumpFall:
            target.initial_velocity_px_per_second = {
                sampled_sign(random) * speed_distribution(random) * 0.5,
                -jump_speed_distribution(random),
            };
            target.acceleration_px_per_second_squared = {0.0, 600.0};
            break;
        case MotionProfile::Stop: {
            const Vec2d direction = direction_from_angle(angle_distribution(random));
            target.initial_velocity_px_per_second = scaled(direction, speed);
            target.maneuver_at_ms = stop_time_distribution(random);
            break;
        }
        case MotionProfile::CompoundDirectional: {
            const double initial_angle = angle_distribution(random);
            const double first_angle = initial_angle +
                sampled_sign(random) * compound_turn_distribution(random);
            const double second_angle = first_angle +
                sampled_sign(random) * compound_turn_distribution(random);
            target.initial_velocity_px_per_second =
                scaled(direction_from_angle(initial_angle), speed);
            const int first_at_ms = compound_dwell_distribution(random);
            const int second_at_ms =
                first_at_ms + compound_dwell_distribution(random);
            target.velocity_maneuvers = {
                {first_at_ms, scaled(direction_from_angle(first_angle), speed)},
                {second_at_ms, scaled(direction_from_angle(second_angle), speed)},
            };
            break;
        }
        }
        if (config.obsolete_vertical_fixture) {
            target.initial_velocity_px_per_second = {};
            target.acceleration_px_per_second_squared = {};
            target.maneuver_at_ms = -1;
        }
        if (!config.target_motion_enabled) {
            target.initial_velocity_px_per_second = {};
            target.acceleration_px_per_second_squared = {};
            target.maneuver_at_ms = -1;
            target.velocity_maneuvers.clear();
        }

        const int observation_horizon_ms = config.max_acquire_deadline_ms +
            config.tracking_window_ms;
        // Keep target kinematics paired when only Vision cadence changes.
        // Extra 200 Hz noise samples must not consume the scenario RNG and
        // silently generate different later targets.
        std::mt19937 observation_random(
            seed ^ static_cast<std::uint32_t>(
                0x91E10DA5u + target.id * 0x9E3779B9u));
        int observation_at_ms = 0;
        while (observation_at_ms <= observation_horizon_ms) {
            target.observation_at_ms.push_back(observation_at_ms);
            Vec2d observation_noise{
                noise_distribution(observation_random),
                noise_distribution(observation_random)};
            if (config.vision_disturbance ==
                    VisionDisturbanceProfile::GunKick ||
                config.vision_disturbance ==
                    VisionDisturbanceProfile::GunKickAdversarial ||
                config.vision_disturbance ==
                    VisionDisturbanceProfile::GunKickAndCameraRecoil) {
                // A deterministic 100 ms firing cadence. Each pulse produces
                // a fast upward/horizontal camera displacement followed by a
                // slower recovery. Alternating X sign represents horizontal
                // recoil without encoding a weapon-specific recoil table.
                const bool adversarial =
                    config.vision_disturbance ==
                        VisionDisturbanceProfile::GunKickAdversarial ||
                    config.vision_disturbance ==
                        VisionDisturbanceProfile::GunKickAndCameraRecoil;
                const int kFirstShotMs = adversarial
                    ? 55 + static_cast<int>((target.id * 17u) % 73u)
                    : 80;
                constexpr int kShotPeriodMs = 100;
                const int kRiseMs = adversarial ? 11 : 18;
                const int kRecoverMs = adversarial ? 72 : 55;
                if (observation_at_ms >= kFirstShotMs) {
                    const int shot_age =
                        (observation_at_ms - kFirstShotMs) % kShotPeriodMs;
                    const int shot_index =
                        (observation_at_ms - kFirstShotMs) / kShotPeriodMs;
                    double envelope = 0.0;
                    if (shot_age < kRiseMs) {
                        envelope = smoothstep(
                            static_cast<double>(shot_age) / kRiseMs);
                    } else if (shot_age < kRecoverMs) {
                        envelope = 1.0 - smoothstep(
                            static_cast<double>(shot_age - kRiseMs) /
                            (kRecoverMs - kRiseMs));
                    }
                    const double horizontal_sign =
                        (shot_index & 1) == 0 ? 1.0 : -1.0;
                    observation_noise.x += horizontal_sign *
                        (adversarial ? 8.0 : 4.0) * envelope;
                    observation_noise.y -=
                        (adversarial ? 17.0 : 8.0) * envelope;
                }
            }
            target.observation_noise_px.push_back(observation_noise);
            observation_at_ms += config.vision_interval_ms > 0
                ? config.vision_interval_ms
                : observation_interval_distribution(observation_random);
        }
        if (config.target_profile == TargetProfile::SmallVisible) {
            constexpr double small_radii[] = {8.0, 11.0, 14.0};
            target.visible_radius_px = small_radii[index % 3];
        } else {
            target.visible_radius_px = config.target_radius_px;
        }
        if (config.short_occlusion_duration_ms > 0) {
            target.vision_occlusion_bursts = {
                {early_occlusion_distribution(occlusion_random),
                 config.short_occlusion_duration_ms},
                {late_occlusion_distribution(occlusion_random),
                 config.short_occlusion_duration_ms},
            };
        }
        result.targets.push_back(std::move(target));
    }

    result.hash = script_hash(result);
    return result;
}

void advance_target(
    const TargetScript& script,
    int target_elapsed_ms,
    double dt_seconds,
    Vec2d& position_error_px,
    Vec2d& velocity_px_per_second) {
    const double dt = std::clamp(dt_seconds, 0.0, 0.1);
    if ((script.motion == MotionProfile::Accelerate ||
         script.motion == MotionProfile::JumpFall) && dt > 0.0) {
        velocity_px_per_second.x +=
            script.acceleration_px_per_second_squared.x * dt;
        velocity_px_per_second.y +=
            script.acceleration_px_per_second_squared.y * dt;
    }
    if (script.motion == MotionProfile::Reverse &&
        target_elapsed_ms == script.maneuver_at_ms) {
        if (std::fabs(velocity_px_per_second.x) >=
            std::fabs(velocity_px_per_second.y)) {
            velocity_px_per_second.x = -velocity_px_per_second.x;
        } else {
            velocity_px_per_second.y = -velocity_px_per_second.y;
        }
    }
    if (script.motion == MotionProfile::Stop &&
        target_elapsed_ms >= script.maneuver_at_ms) {
        velocity_px_per_second = {};
    }
    for (const VelocityManeuver& maneuver : script.velocity_maneuvers) {
        if (target_elapsed_ms == maneuver.at_ms) {
            velocity_px_per_second = maneuver.velocity_px_per_second;
            break;
        }
    }

    position_error_px.x += velocity_px_per_second.x * dt;
    position_error_px.y += velocity_px_per_second.y * dt;

    constexpr double min_x = -296.0;
    constexpr double max_x = 296.0;
    constexpr double min_y = -232.0;
    constexpr double max_y = 232.0;
    reflect_axis(min_x, max_x, position_error_px.x, velocity_px_per_second.x);
    reflect_axis(min_y, max_y, position_error_px.y, velocity_px_per_second.y);
}

double aim_slowdown_multiplier(
    double distance_px,
    const BenchmarkConfig& config) {
    return aim_slowdown_multiplier(
        distance_px, config.target_radius_px, config);
}

double aim_slowdown_multiplier(
    double distance_px,
    double target_radius_px,
    const BenchmarkConfig& config) {
    const double distance = std::max(0.0, distance_px);
    const double radius = std::max(
        std::numeric_limits<double>::epsilon(), target_radius_px);
    const double transition = std::max(
        std::numeric_limits<double>::epsilon(), config.slowdown_transition_px);
    if (distance >= radius + transition) return 1.0;
    if (distance >= radius) {
        const double inward = (radius + transition - distance) / transition;
        return 1.0 + (config.slowdown_edge_multiplier - 1.0) *
            smoothstep(inward);
    }
    const double penetration = 1.0 - distance / radius;
    return config.slowdown_edge_multiplier +
        (config.slowdown_center_multiplier - config.slowdown_edge_multiplier) *
        smoothstep(penetration);
}

double player_vertical_error_offset_y_px(
    const PlayerVerticalMotionScript& script,
    int elapsed_ms,
    PlayerVerticalMotionMode mode) noexcept {
    if (elapsed_ms < 0 || mode == PlayerVerticalMotionMode::Off) return 0.0;
    if (mode == PlayerVerticalMotionMode::Random) {
        mode = script.random_event == PlayerVerticalEvent::Slide
            ? PlayerVerticalMotionMode::Slide
            : PlayerVerticalMotionMode::Jump;
    }
    if (mode == PlayerVerticalMotionMode::Jump) {
        const int jump_elapsed = elapsed_ms - script.jump_onset_ms;
        if (jump_elapsed < 0 || jump_elapsed >= script.jump_duration_ms ||
            script.jump_duration_ms <= 0) {
            return 0.0;
        }
        const double phase = static_cast<double>(jump_elapsed) /
            static_cast<double>(script.jump_duration_ms);
        // Rising player camera makes the target appear lower on screen.
        return script.jump_height_px * std::sin(kPi * phase);
    }
    if (mode != PlayerVerticalMotionMode::Slide) return 0.0;

    const int slide_elapsed = elapsed_ms - script.slide_onset_ms;
    if (slide_elapsed < 0) return 0.0;
    if (slide_elapsed < script.slide_drop_ms && script.slide_drop_ms > 0) {
        const double phase = static_cast<double>(slide_elapsed) /
            static_cast<double>(script.slide_drop_ms);
        // Lowering the player camera makes the target appear higher.
        return -script.slide_depth_px * smoothstep(phase);
    }
    const int hold_end = script.slide_drop_ms + script.slide_hold_ms;
    if (slide_elapsed < hold_end) return -script.slide_depth_px;
    if (script.slide_instant_recovery || script.slide_recover_ms <= 0) {
        return 0.0;
    }
    const int recovery_elapsed = slide_elapsed - hold_end;
    if (recovery_elapsed >= script.slide_recover_ms) return 0.0;
    const double phase = static_cast<double>(recovery_elapsed) /
        static_cast<double>(script.slide_recover_ms);
    // Explicitly linear to cover the non-instant stand-up case.
    return -script.slide_depth_px * (1.0 - phase);
}

const char* to_string(MotionProfile profile) noexcept {
    switch (profile) {
    case MotionProfile::ConstantHorizontal: return "constant_horizontal";
    case MotionProfile::ConstantVertical: return "constant_vertical";
    case MotionProfile::ConstantDiagonal: return "constant_diagonal";
    case MotionProfile::Accelerate: return "accelerate";
    case MotionProfile::Reverse: return "reverse";
    case MotionProfile::JumpFall: return "jump_fall";
    case MotionProfile::Stop: return "stop";
    case MotionProfile::CompoundDirectional: return "compound_directional";
    }
    return "unknown";
}

const char* to_string(ScenarioProfile profile) noexcept {
    switch (profile) {
    case ScenarioProfile::Baseline: return "baseline";
    case ScenarioProfile::CompoundDirectional: return "compound_directional";
    }
    return "unknown";
}

}  // namespace controller_native::sustained_aimlab
