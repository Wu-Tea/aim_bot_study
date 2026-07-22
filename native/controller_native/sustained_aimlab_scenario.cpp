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
    hash_integral(hash, config.duration_ms);
    hash_integral(hash, config.target_profile);
    hash_integral(hash, config.tick_ms);
    hash_integral(hash, config.tracking_window_ms);
    hash_integral(hash, config.inter_target_gap_ms);
    hash_integral(hash, config.min_acquire_deadline_ms);
    hash_integral(hash, config.max_acquire_deadline_ms);
    hash_double(hash, config.target_radius_px);
    hash_double(hash, config.slowdown_transition_px);
    hash_double(hash, config.slowdown_edge_multiplier);
    hash_double(hash, config.slowdown_center_multiplier);
    hash_double(hash, config.camera_response_px_per_stick_second);
    hash_integral(hash, config.control_response_delay_ms);
    hash_integral(hash, config.frame_width_px);
    hash_integral(hash, config.frame_height_px);
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
        hash_integral(hash, target.acquire_deadline_ms);
        hash_double(hash, target.visible_radius_px);
        hash_integral(hash,
            static_cast<std::uint64_t>(target.observation_at_ms.size()));
        for (std::size_t index = 0; index < target.observation_at_ms.size(); ++index) {
            hash_integral(hash, target.observation_at_ms[index]);
            hash_vec(hash, target.observation_noise_px[index]);
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
        config.target_radius_px <= 0.0) {
        throw std::invalid_argument("invalid sustained AimLab benchmark config");
    }

    ScenarioScript result;
    result.seed = seed;
    result.config = config;

    std::mt19937 random(seed);
    std::uniform_real_distribution<double> angle_distribution(0.0, 2.0 * kPi);
    std::uniform_real_distribution<double> distance_distribution(48.0, 140.0);
    std::uniform_real_distribution<double> speed_distribution(80.0, 160.0);
    std::uniform_real_distribution<double> acceleration_distribution(60.0, 160.0);
    std::uniform_real_distribution<double> jump_speed_distribution(160.0, 240.0);
    std::uniform_real_distribution<double> noise_distribution(-0.75, 0.75);
    std::uniform_int_distribution<int> deadline_distribution(
        config.min_acquire_deadline_ms, config.max_acquire_deadline_ms);
    std::uniform_int_distribution<int> reverse_time_distribution(80, 180);
    std::uniform_int_distribution<int> stop_time_distribution(80, 200);
    std::uniform_int_distribution<int> observation_interval_distribution(10, 12);
    std::uniform_int_distribution<int> profile_offset_distribution(0, 6);

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
        target.motion = static_cast<MotionProfile>((profile_offset + index) % 7);
        const double angle = angle_distribution(random);
        const double distance = distance_distribution(random);
        target.initial_error_px = scaled(direction_from_angle(angle), distance);
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
        }

        const int observation_horizon_ms = config.max_acquire_deadline_ms +
            config.tracking_window_ms;
        int observation_at_ms = 0;
        while (observation_at_ms <= observation_horizon_ms) {
            target.observation_at_ms.push_back(observation_at_ms);
            target.observation_noise_px.push_back({
                noise_distribution(random), noise_distribution(random)});
            observation_at_ms += observation_interval_distribution(random);
        }
        if (config.target_profile == TargetProfile::SmallVisible) {
            constexpr double small_radii[] = {8.0, 11.0, 14.0};
            target.visible_radius_px = small_radii[index % 3];
        } else {
            target.visible_radius_px = config.target_radius_px;
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

const char* to_string(MotionProfile profile) noexcept {
    switch (profile) {
    case MotionProfile::ConstantHorizontal: return "constant_horizontal";
    case MotionProfile::ConstantVertical: return "constant_vertical";
    case MotionProfile::ConstantDiagonal: return "constant_diagonal";
    case MotionProfile::Accelerate: return "accelerate";
    case MotionProfile::Reverse: return "reverse";
    case MotionProfile::JumpFall: return "jump_fall";
    case MotionProfile::Stop: return "stop";
    }
    return "unknown";
}

}  // namespace controller_native::sustained_aimlab
