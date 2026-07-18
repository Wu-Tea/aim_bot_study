#include "sustained_aimlab_scenario.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

namespace {

using controller_native::sustained_aimlab::BenchmarkConfig;
using controller_native::sustained_aimlab::MotionProfile;
using controller_native::sustained_aimlab::ScenarioScript;
using controller_native::sustained_aimlab::TargetScript;
using controller_native::sustained_aimlab::Vec2d;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void require_near(double actual, double expected, double tolerance,
                  const std::string& message) {
    if (std::fabs(actual - expected) > tolerance) {
        throw std::runtime_error(
            message + ": expected=" + std::to_string(expected) +
            " actual=" + std::to_string(actual));
    }
}

bool same_vec(Vec2d left, Vec2d right) {
    return left.x == right.x && left.y == right.y;
}

bool same_target(const TargetScript& left, const TargetScript& right) {
    if (left.id != right.id || left.motion != right.motion ||
        !same_vec(left.initial_error_px, right.initial_error_px) ||
        !same_vec(left.initial_velocity_px_per_second,
                  right.initial_velocity_px_per_second) ||
        !same_vec(left.acceleration_px_per_second_squared,
                  right.acceleration_px_per_second_squared) ||
        left.maneuver_at_ms != right.maneuver_at_ms ||
        left.acquire_deadline_ms != right.acquire_deadline_ms ||
        left.observation_at_ms != right.observation_at_ms ||
        left.observation_noise_px.size() != right.observation_noise_px.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.observation_noise_px.size(); ++index) {
        if (!same_vec(left.observation_noise_px[index],
                      right.observation_noise_px[index])) {
            return false;
        }
    }
    return true;
}

void test_defaults_and_slowdown_anchor_points() {
    const BenchmarkConfig config;
    require(config.duration_ms == 60'000, "default duration must be one minute");
    require(config.tick_ms == 1, "controller tick must be 1ms");
    require(config.tracking_window_ms == 1'000, "tracking window must be 1000ms");
    require(config.inter_target_gap_ms == 50, "target gap must be 50ms");
    require_near(config.target_radius_px, 24.0, 1e-12, "target radius");

    using controller_native::sustained_aimlab::aim_slowdown_multiplier;
    require_near(aim_slowdown_multiplier(27.0, config), 1.0, 1e-6,
                 "outside transition multiplier");
    require_near(aim_slowdown_multiplier(24.0, config), 0.5, 1e-6,
                 "circle edge multiplier");
    require_near(aim_slowdown_multiplier(0.0, config), 0.4, 1e-6,
                 "circle center multiplier");
    const double transition = aim_slowdown_multiplier(25.5, config);
    require(transition > 0.5 && transition < 1.0,
            "outer 3px transition must be smooth");
}

void test_seeded_generation_is_reproducible_and_complete() {
    const BenchmarkConfig config;
    const ScenarioScript first =
        controller_native::sustained_aimlab::generate_script(1337, config);
    const ScenarioScript second =
        controller_native::sustained_aimlab::generate_script(1337, config);
    const ScenarioScript different =
        controller_native::sustained_aimlab::generate_script(1338, config);

    require(first.hash != 0, "script hash must be populated");
    require(first.hash == second.hash, "same seed hash must match");
    require(first.hash != different.hash, "different seed hash must differ");
    require(first.targets.size() == second.targets.size(),
            "same seed target count must match");
    for (std::size_t index = 0; index < first.targets.size(); ++index) {
        require(same_target(first.targets[index], second.targets[index]),
                "same seed target scripts must match");
    }

    const std::size_t worst_case_targets = static_cast<std::size_t>(
        std::ceil(static_cast<double>(config.duration_ms) /
                  (config.min_acquire_deadline_ms + config.inter_target_gap_ms)));
    require(first.targets.size() >= worst_case_targets,
            "generator must cover worst-case one-minute misses");

    std::set<MotionProfile> profiles;
    for (const TargetScript& target : first.targets) profiles.insert(target.motion);
    require(profiles.size() == 7, "all motion profiles must be present");
}

void test_generated_ranges_and_observation_schedule() {
    const BenchmarkConfig config;
    const ScenarioScript script =
        controller_native::sustained_aimlab::generate_script(20260718, config);

    for (const TargetScript& target : script.targets) {
        const double radius = std::hypot(
            target.initial_error_px.x, target.initial_error_px.y);
        require(radius >= 48.0 - 1e-9 && radius <= 140.0 + 1e-9,
                "initial radial error must stay in [48,140]");
        require(target.initial_error_px.x >= -296.0 &&
                    target.initial_error_px.x <= 296.0 &&
                    target.initial_error_px.y >= -232.0 &&
                    target.initial_error_px.y <= 232.0,
                "initial target circle must stay on screen");
        require(target.acquire_deadline_ms >= 250 &&
                    target.acquire_deadline_ms <= 330,
                "acquisition deadline must stay in [250,330]");
        require(target.observation_at_ms.size() ==
                    target.observation_noise_px.size(),
                "observation times and noise must stay paired");
        require(!target.observation_at_ms.empty(),
                "every target needs observations");
        for (std::size_t index = 0; index < target.observation_at_ms.size(); ++index) {
            if (index > 0) {
                const int interval = target.observation_at_ms[index] -
                    target.observation_at_ms[index - 1];
                require(interval >= 10 && interval <= 13,
                        "observation interval must represent 80-100Hz");
            }
            const Vec2d noise = target.observation_noise_px[index];
            require(std::fabs(noise.x) <= 0.75 && std::fabs(noise.y) <= 0.75,
                    "observation noise must stay bounded");
        }
    }
}

void test_motion_profiles_and_boundary_reflection() {
    using controller_native::sustained_aimlab::advance_target;

    TargetScript reverse{};
    reverse.motion = MotionProfile::Reverse;
    reverse.initial_velocity_px_per_second = {100.0, 0.0};
    reverse.maneuver_at_ms = 100;
    Vec2d position{};
    Vec2d velocity = reverse.initial_velocity_px_per_second;
    advance_target(reverse, 99, 0.001, position, velocity);
    require(velocity.x > 0.0, "reverse must wait for maneuver time");
    advance_target(reverse, 100, 0.001, position, velocity);
    require(velocity.x < 0.0, "reverse must flip at maneuver time");

    TargetScript jump{};
    jump.motion = MotionProfile::JumpFall;
    jump.acceleration_px_per_second_squared = {0.0, 600.0};
    Vec2d jump_position{};
    Vec2d jump_velocity{0.0, -200.0};
    advance_target(jump, 1, 0.100, jump_position, jump_velocity);
    require(jump_velocity.y > -200.0,
            "jump profile must accelerate downward");

    TargetScript bounded{};
    bounded.motion = MotionProfile::ConstantHorizontal;
    Vec2d edge_position{295.9, 0.0};
    Vec2d edge_velocity{160.0, 0.0};
    advance_target(bounded, 1, 0.010, edge_position, edge_velocity);
    require(edge_position.x <= 296.0 && edge_velocity.x < 0.0,
            "right screen bound must reflect horizontal velocity");
}

void test_motion_profile_names_are_stable() {
    using controller_native::sustained_aimlab::to_string;
    require(std::string(to_string(MotionProfile::ConstantHorizontal)) ==
                "constant_horizontal", "horizontal name");
    require(std::string(to_string(MotionProfile::ConstantVertical)) ==
                "constant_vertical", "vertical name");
    require(std::string(to_string(MotionProfile::ConstantDiagonal)) ==
                "constant_diagonal", "diagonal name");
    require(std::string(to_string(MotionProfile::Accelerate)) ==
                "accelerate", "accelerate name");
    require(std::string(to_string(MotionProfile::Reverse)) ==
                "reverse", "reverse name");
    require(std::string(to_string(MotionProfile::JumpFall)) ==
                "jump_fall", "jump name");
    require(std::string(to_string(MotionProfile::Stop)) ==
                "stop", "stop name");
}

}  // namespace

int main() {
    try {
        test_defaults_and_slowdown_anchor_points();
        test_seeded_generation_is_reproducible_and_complete();
        test_generated_ranges_and_observation_schedule();
        test_motion_profiles_and_boundary_reflection();
        test_motion_profile_names_are_stable();
        std::cout << "cod_native_sustained_aimlab_scenario_tests PASS\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "cod_native_sustained_aimlab_scenario_tests FAIL: "
                  << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
