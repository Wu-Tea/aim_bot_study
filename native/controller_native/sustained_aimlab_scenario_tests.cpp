#include "sustained_aimlab_scenario.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

namespace {

using controller_native::sustained_aimlab::BenchmarkConfig;
using controller_native::sustained_aimlab::MotionProfile;
using controller_native::sustained_aimlab::ScenarioProfile;
using controller_native::sustained_aimlab::ScenarioScript;
using controller_native::sustained_aimlab::TargetScript;
using controller_native::sustained_aimlab::TargetProfile;
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
        left.velocity_maneuvers.size() != right.velocity_maneuvers.size() ||
        left.acquire_deadline_ms != right.acquire_deadline_ms ||
        left.player_strafe != right.player_strafe ||
        left.observation_at_ms != right.observation_at_ms ||
        left.observation_noise_px.size() != right.observation_noise_px.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.velocity_maneuvers.size(); ++index) {
        if (left.velocity_maneuvers[index].at_ms !=
                right.velocity_maneuvers[index].at_ms ||
            !same_vec(left.velocity_maneuvers[index].velocity_px_per_second,
                      right.velocity_maneuvers[index].velocity_px_per_second)) {
            return false;
        }
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

void test_script_hash_includes_control_response_delay() {
    BenchmarkConfig immediate;
    BenchmarkConfig delayed;
    delayed.control_response_delay_ms = 45;
    const auto first = controller_native::sustained_aimlab::generate_script(
        1337, immediate);
    const auto second = controller_native::sustained_aimlab::generate_script(
        1337, delayed);
    require(first.hash != second.hash,
            "script identity must include delayed plant semantics");
}

void test_full_speed_strafe_schedule_is_seeded_and_bounded() {
    BenchmarkConfig config;
    config.duration_ms = 3'000;
    const ScenarioScript first =
        controller_native::sustained_aimlab::generate_script(2026072301u, config);
    const ScenarioScript second =
        controller_native::sustained_aimlab::generate_script(2026072301u, config);
    require(first.targets.size() == second.targets.size(),
            "same seed must preserve player strafe count");
    for (std::size_t index = 0; index < first.targets.size(); ++index) {
        const auto& strafe = first.targets[index].player_strafe;
        require(strafe == second.targets[index].player_strafe,
                "same seed must reproduce player strafe");
        require(strafe.initial_direction == -1 ||
                    strafe.initial_direction == 1,
                "active strafe direction must be full scale");
        require(0 <= strafe.onset_ms &&
                    strafe.onset_ms < strafe.reverse_ms &&
                    strafe.reverse_ms < strafe.release_ms,
                "strafe phases must be ordered");
        require(strafe.top_speed_px_per_second >= 125.0 &&
                    strafe.top_speed_px_per_second <= 232.0,
                "player speed must cover approved weapon mobility range");
        require(strafe.time_constant_ms >= 100.0 &&
                    strafe.time_constant_ms <= 180.0,
                "player inertia must remain in approved range");
    }
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

    TargetScript compound{};
    compound.motion = MotionProfile::CompoundDirectional;
    compound.velocity_maneuvers = {
        {100, {-120.0, 0.0}},
        {220, {120.0, 0.0}},
    };
    Vec2d compound_position{};
    Vec2d compound_velocity{120.0, 0.0};
    advance_target(compound, 100, 0.001, compound_position, compound_velocity);
    require(compound_velocity.x == -120.0,
            "compound motion must apply its first direction change");
    advance_target(compound, 220, 0.001, compound_position, compound_velocity);
    require(compound_velocity.x == 120.0,
            "compound motion must apply its second direction change");
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
    require(std::string(to_string(MotionProfile::CompoundDirectional)) ==
                "compound_directional", "compound motion name");
    require(std::string(to_string(ScenarioProfile::Baseline)) ==
                "baseline", "baseline scenario name");
    require(std::string(to_string(ScenarioProfile::CompoundDirectional)) ==
                "compound_directional", "compound scenario name");
}

void test_bodylock_stress_profiles_are_isolated_and_deterministic() {
    BenchmarkConfig compound_config;
    compound_config.scenario_profile = ScenarioProfile::CompoundDirectional;
    const ScenarioScript compound =
        controller_native::sustained_aimlab::generate_script(
            20260728, compound_config);
    const ScenarioScript compound_again =
        controller_native::sustained_aimlab::generate_script(
            20260728, compound_config);
    BenchmarkConfig baseline_config;
    const ScenarioScript baseline =
        controller_native::sustained_aimlab::generate_script(
            20260728, baseline_config);
    require(compound.hash == compound_again.hash,
            "compound directional script must be deterministic");
    require(compound.hash != baseline.hash,
            "stress profiles must have distinct script identities");
    unsigned direction_quadrants = 0;
    auto record_direction = [&](Vec2d velocity) {
        const unsigned x_bit = velocity.x >= 0.0 ? 1u : 0u;
        const unsigned y_bit = velocity.y >= 0.0 ? 1u : 0u;
        direction_quadrants |= 1u << (x_bit + 2u * y_bit);
    };
    for (const TargetScript& target : compound.targets) {
        require(target.motion == MotionProfile::CompoundDirectional,
                "compound cohort must isolate repeated two-dimensional turns");
        require(target.velocity_maneuvers.size() == 2,
                "compound target must turn twice");
        const int first_dwell = target.velocity_maneuvers[0].at_ms;
        const int second_dwell =
            target.velocity_maneuvers[1].at_ms - first_dwell;
        require(first_dwell >= 80 && first_dwell <= 180 &&
                    second_dwell >= 80 && second_dwell <= 180,
                "compound turn dwell must stay in [80,180]ms");
        Vec2d previous = target.initial_velocity_px_per_second;
        record_direction(previous);
        for (const auto& maneuver : target.velocity_maneuvers) {
            const Vec2d next = maneuver.velocity_px_per_second;
            const double previous_speed = std::hypot(previous.x, previous.y);
            const double next_speed = std::hypot(next.x, next.y);
            require_near(next_speed, previous_speed, 1e-9,
                         "compound turn must preserve target speed");
            const double cosine = std::clamp(
                (previous.x * next.x + previous.y * next.y) /
                    (previous_speed * next_speed),
                -1.0, 1.0);
            const double turn_degrees =
                std::acos(cosine) * 180.0 / 3.14159265358979323846;
            require(turn_degrees >= 70.0 - 1e-9 &&
                        turn_degrees <= 150.0 + 1e-9,
                    "compound motion must make a substantial 2D turn");
            record_direction(next);
            previous = next;
        }
    }
    require(direction_quadrants == 0b1111u,
            "compound motion set must cover all four screen quadrants");
}

void test_small_target_profile_reuses_motion_and_cycles_visible_radius() {
    BenchmarkConfig ordinary_config;
    BenchmarkConfig small_config;
    small_config.target_profile = TargetProfile::SmallVisible;
    const ScenarioScript ordinary =
        controller_native::sustained_aimlab::generate_script(1337, ordinary_config);
    const ScenarioScript small =
        controller_native::sustained_aimlab::generate_script(1337, small_config);
    require(ordinary.targets.size() == small.targets.size(),
            "target profile must not change script length");
    for (std::size_t index = 0; index < ordinary.targets.size(); ++index) {
        const TargetScript& large = ordinary.targets[index];
        const TargetScript& tiny = small.targets[index];
        require(same_target(large, tiny),
                "small target profile must reuse identical motion and noise");
        require_near(large.visible_radius_px, 24.0, 1e-12,
                     "ordinary visible radius");
        require(tiny.visible_radius_px == 8.0 ||
                    tiny.visible_radius_px == 11.0 ||
                    tiny.visible_radius_px == 14.0,
                "small target radius must use the approved 8/11/14px set");
    }
}

}  // namespace

int main() {
    try {
        test_defaults_and_slowdown_anchor_points();
        test_seeded_generation_is_reproducible_and_complete();
        test_script_hash_includes_control_response_delay();
        test_full_speed_strafe_schedule_is_seeded_and_bounded();
        test_generated_ranges_and_observation_schedule();
        test_motion_profiles_and_boundary_reflection();
        test_motion_profile_names_are_stable();
        test_bodylock_stress_profiles_are_isolated_and_deterministic();
        test_small_target_profile_reuses_motion_and_cycles_visible_radius();
        std::cout << "cod_native_sustained_aimlab_scenario_tests PASS\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "cod_native_sustained_aimlab_scenario_tests FAIL: "
                  << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
