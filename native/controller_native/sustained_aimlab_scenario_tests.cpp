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
using controller_native::sustained_aimlab::PlayerVerticalMotionMode;
using controller_native::sustained_aimlab::ScenarioProfile;
using controller_native::sustained_aimlab::ScenarioScript;
using controller_native::sustained_aimlab::RuntimeManualSample;
using controller_native::sustained_aimlab::RuntimeManualSegment;
using controller_native::sustained_aimlab::RuntimeManualAimMode;
using controller_native::sustained_aimlab::RuntimeObservationSample;
using controller_native::sustained_aimlab::RuntimeTargetSample;
using controller_native::sustained_aimlab::TargetScript;
using controller_native::sustained_aimlab::TargetProfile;
using controller_native::sustained_aimlab::Vec2d;
using controller_native::sustained_aimlab::VisionDisturbanceProfile;

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
        left.player_vertical != right.player_vertical ||
        left.observation_at_ms != right.observation_at_ms ||
        left.observation_ready_at_ms != right.observation_ready_at_ms ||
        left.body_width_px != right.body_width_px ||
        left.body_height_px != right.body_height_px ||
        left.vision_occlusion_bursts.size() !=
            right.vision_occlusion_bursts.size() ||
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
    for (std::size_t index = 0;
         index < left.vision_occlusion_bursts.size(); ++index) {
        if (left.vision_occlusion_bursts[index].tracking_offset_ms !=
                right.vision_occlusion_bursts[index].tracking_offset_ms ||
            left.vision_occlusion_bursts[index].duration_ms !=
                right.vision_occlusion_bursts[index].duration_ms) {
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
    require_near(config.sensitivity_multiplier, 1.0, 1e-12,
                 "default sensitivity multiplier");
    require(config.tracking_window_ms == 1'000, "tracking window must be 1000ms");
    require(config.inter_target_gap_ms == 50, "target gap must be 50ms");
    require(config.ads_execution_timeout_ms == 575,
            "ADS execution timeout must include wait, snap and extension budgets");
    require(config.bodylock_entry_timeout_ms == 575,
            "BodyLock setup must allow the full legal ADS horizon");
    require(config.fixed_target_slot_ms == 1'575,
            "default benchmark must use a fixed 1575ms target slot");
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

    const std::size_t scheduled_targets = static_cast<std::size_t>(
        (config.duration_ms - config.initial_idle_ms -
            config.fixed_target_slot_ms) /
            (config.fixed_target_slot_ms + config.inter_target_gap_ms) +
        1);
    require(first.targets.size() == scheduled_targets,
            "generator must emit only the bounded fixed target schedule");
    require(first.targets.size() == 36,
            "default 60-second schedule must contain 36 complete slots");

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

    BenchmarkConfig historical_default = immediate;
    historical_default.initial_idle_ms = 0;
    const auto default_again =
        controller_native::sustained_aimlab::generate_script(
            1337, historical_default);
    require(first.hash == default_again.hash,
            "zero initial idle must preserve historical script identity");

    BenchmarkConfig pre_acquisition = immediate;
    pre_acquisition.initial_idle_ms = 40;
    const auto with_initial_idle =
        controller_native::sustained_aimlab::generate_script(
            1337, pre_acquisition);
    require(first.hash != with_initial_idle.hash,
            "nonzero initial idle must identify pre-acquisition fixture");
}

void test_script_hash_includes_controller_tick_and_sensitivity() {
    BenchmarkConfig baseline;
    BenchmarkConfig slower_controller = baseline;
    slower_controller.tick_ms = 4;
    BenchmarkConfig higher_sensitivity = baseline;
    higher_sensitivity.sensitivity_multiplier = 1.25;

    const auto original =
        controller_native::sustained_aimlab::generate_script(1337, baseline);
    const auto slower = controller_native::sustained_aimlab::generate_script(
        1337, slower_controller);
    const auto sensitive =
        controller_native::sustained_aimlab::generate_script(
            1337, higher_sensitivity);
    require(original.hash != slower.hash,
            "controller cadence must be part of script identity");
    require(original.hash != sensitive.hash,
            "sensitivity multiplier must be part of script identity");
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

void test_near_crosshair_profile_bounds_initial_error() {
    BenchmarkConfig config;
    config.target_profile = TargetProfile::NearCrosshair;
    const auto script =
        controller_native::sustained_aimlab::generate_script(2026072904u, config);
    for (const auto& target : script.targets) {
        const double distance = std::hypot(
            target.initial_error_px.x, target.initial_error_px.y);
        require(distance >= 8.0 && distance <= 40.0,
                "near-crosshair profile must isolate terminal positioning");
        require(target.visible_radius_px == config.target_radius_px,
                "near-crosshair profile must retain ordinary target size");
    }
}

void test_vertical_motion_schedule_is_seeded_and_bounded() {
    BenchmarkConfig config;
    config.duration_ms = 3'000;
    const ScenarioScript first =
        controller_native::sustained_aimlab::generate_script(
            2026072901u, config);
    const ScenarioScript second =
        controller_native::sustained_aimlab::generate_script(
            2026072901u, config);
    for (std::size_t index = 0; index < first.targets.size(); ++index) {
        const auto& motion = first.targets[index].player_vertical;
        require(motion == second.targets[index].player_vertical,
                "same seed must reproduce vertical player motion");
        require(motion.slide_onset_ms >= 60 &&
                    motion.slide_onset_ms <= 220,
                "slide onset must stay in the combat window");
        require(motion.slide_drop_ms >= 70 &&
                    motion.slide_drop_ms <= 140 &&
                    motion.slide_hold_ms >= 120 &&
                    motion.slide_hold_ms <= 300,
                "slide drop and hold durations must stay bounded");
        require(motion.slide_recover_ms >= 100 &&
                    motion.slide_recover_ms <= 220 &&
                    motion.slide_depth_px >= 32.0 &&
                    motion.slide_depth_px <= 72.0,
                "slide recovery and depth must stay bounded");
        require(motion.jump_onset_ms >= 60 &&
                    motion.jump_onset_ms <= 240 &&
                    motion.jump_duration_ms >= 420 &&
                    motion.jump_duration_ms <= 700 &&
                    motion.jump_height_px >= 28.0 &&
                    motion.jump_height_px <= 64.0,
                "jump timing and height must stay bounded");
    }
}

void test_vertical_motion_shapes_cover_slide_recovery_and_jump() {
    using controller_native::sustained_aimlab::
        PlayerVerticalMotionScript;
    using controller_native::sustained_aimlab::
        player_vertical_error_offset_y_px;
    PlayerVerticalMotionScript motion;
    motion.slide_onset_ms = 100;
    motion.slide_drop_ms = 100;
    motion.slide_hold_ms = 200;
    motion.slide_recover_ms = 100;
    motion.slide_depth_px = 40.0;
    motion.slide_instant_recovery = false;
    require_near(
        player_vertical_error_offset_y_px(
            motion, 99, PlayerVerticalMotionMode::Slide),
        0.0, 1e-12, "slide must wait for onset");
    require_near(
        player_vertical_error_offset_y_px(
            motion, 200, PlayerVerticalMotionMode::Slide),
        -40.0, 1e-12, "slide must reach its lowered camera height");
    require_near(
        player_vertical_error_offset_y_px(
            motion, 450, PlayerVerticalMotionMode::Slide),
        -20.0, 1e-12, "linear stand-up must recover halfway");
    motion.slide_instant_recovery = true;
    require_near(
        player_vertical_error_offset_y_px(
            motion, 400, PlayerVerticalMotionMode::Slide),
        0.0, 1e-12, "instant stand-up must restore height in one tick");

    motion.jump_onset_ms = 100;
    motion.jump_duration_ms = 600;
    motion.jump_height_px = 50.0;
    require_near(
        player_vertical_error_offset_y_px(
            motion, 400, PlayerVerticalMotionMode::Jump),
        50.0, 1e-9, "jump apex must reach configured screen displacement");
    require_near(
        player_vertical_error_offset_y_px(
            motion, 700, PlayerVerticalMotionMode::Jump),
        0.0, 1e-12, "jump landing must restore standing height");
}

void test_stationary_target_mode_preserves_paired_nonmotion_script() {
    BenchmarkConfig moving_config;
    BenchmarkConfig stationary_config;
    stationary_config.target_motion_enabled = false;
    const ScenarioScript moving =
        controller_native::sustained_aimlab::generate_script(
            2026072902u, moving_config);
    const ScenarioScript stationary =
        controller_native::sustained_aimlab::generate_script(
            2026072902u, stationary_config);
    require(moving.hash != stationary.hash,
            "target-motion semantics must change script identity");
    require(moving.targets.size() == stationary.targets.size(),
            "target-motion mode must preserve target count");
    for (std::size_t index = 0; index < moving.targets.size(); ++index) {
        const auto& active = moving.targets[index];
        const auto& still = stationary.targets[index];
        require(
            active.id == still.id &&
                same_vec(active.initial_error_px, still.initial_error_px) &&
                active.acquire_deadline_ms == still.acquire_deadline_ms &&
                active.player_strafe == still.player_strafe &&
                active.player_vertical == still.player_vertical,
            "stationary/moving pairs must share nonmotion scenario inputs");
        require_near(still.initial_velocity_px_per_second.x, 0.0, 1e-12,
                     "stationary target x velocity");
        require_near(still.initial_velocity_px_per_second.y, 0.0, 1e-12,
                     "stationary target y velocity");
        require(still.velocity_maneuvers.empty(),
                "stationary targets must not retain maneuvers");
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

void test_200hz_changes_only_observation_schedule() {
    BenchmarkConfig baseline_config;
    BenchmarkConfig high_rate_config;
    high_rate_config.vision_interval_ms = 5;
    const auto baseline =
        controller_native::sustained_aimlab::generate_script(
            2026072804, baseline_config);
    const auto high_rate =
        controller_native::sustained_aimlab::generate_script(
            2026072804, high_rate_config);
    require(baseline.targets.size() == high_rate.targets.size(),
            "Vision cadence must not change target count");
    require(baseline.hash != high_rate.hash,
            "Vision cadence must be part of script identity");
    for (std::size_t index = 0; index < baseline.targets.size(); ++index) {
        const auto& slow = baseline.targets[index];
        const auto& fast = high_rate.targets[index];
        require(
            slow.id == fast.id && slow.motion == fast.motion &&
                same_vec(slow.initial_error_px, fast.initial_error_px) &&
                same_vec(
                    slow.initial_velocity_px_per_second,
                    fast.initial_velocity_px_per_second) &&
                same_vec(
                    slow.acceleration_px_per_second_squared,
                    fast.acceleration_px_per_second_squared) &&
                slow.maneuver_at_ms == fast.maneuver_at_ms &&
                slow.acquire_deadline_ms == fast.acquire_deadline_ms &&
                slow.player_strafe == fast.player_strafe &&
                slow.player_vertical == fast.player_vertical &&
                slow.velocity_maneuvers.size() ==
                    fast.velocity_maneuvers.size(),
            "Vision cadence must not change target kinematics");
        for (std::size_t maneuver = 0;
             maneuver < slow.velocity_maneuvers.size(); ++maneuver) {
            require(
                slow.velocity_maneuvers[maneuver].at_ms ==
                        fast.velocity_maneuvers[maneuver].at_ms &&
                    same_vec(
                        slow.velocity_maneuvers[maneuver]
                            .velocity_px_per_second,
                        fast.velocity_maneuvers[maneuver]
                            .velocity_px_per_second),
                "Vision cadence must not change target maneuvers");
        }
        require(fast.observation_at_ms.size() >
                    slow.observation_at_ms.size(),
                "200Hz must produce more observations");
        for (std::size_t sample = 1;
             sample < fast.observation_at_ms.size(); ++sample) {
            require(
                fast.observation_at_ms[sample] -
                        fast.observation_at_ms[sample - 1] ==
                    5,
                "200Hz schedule must use an exact 5ms interval");
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

void test_obsolete_vertical_fixture_is_stationary_for_every_motion_label() {
    BenchmarkConfig config;
    config.obsolete_vertical_fixture = true;
    const ScenarioScript script =
        controller_native::sustained_aimlab::generate_script(2026072601, config);
    for (const TargetScript& target : script.targets) {
        require_near(target.initial_velocity_px_per_second.x, 0.0, 1e-12,
                     "V1 target x velocity must be zero");
        require_near(target.initial_velocity_px_per_second.y, 0.0, 1e-12,
                     "V1 target y velocity must be zero");
        require_near(target.acceleration_px_per_second_squared.x, 0.0, 1e-12,
                     "V1 target x acceleration must be zero");
        require_near(target.acceleration_px_per_second_squared.y, 0.0, 1e-12,
                     "V1 target y acceleration must be zero");
        require(target.maneuver_at_ms == -1,
                "V1 target maneuver must be disabled");
    }
}

void test_short_occlusion_bursts_are_tracking_relative_and_hashed() {
    BenchmarkConfig config;
    config.short_occlusion_duration_ms = 36;
    const auto first =
        controller_native::sustained_aimlab::generate_script(2026072701, config);
    const auto second =
        controller_native::sustained_aimlab::generate_script(2026072701, config);
    require(first.hash == second.hash,
            "occlusion script must be deterministic");
    for (const auto& target : first.targets) {
        require(target.vision_occlusion_bursts.size() == 2,
                "each target must receive two short bursts");
        for (const auto& burst : target.vision_occlusion_bursts) {
            require(burst.tracking_offset_ms >= 60,
                    "burst must begin after tracking starts");
            require(burst.duration_ms == 36,
                    "burst duration must match the selected cohort");
        }
        require(
            target.vision_occlusion_bursts[0].tracking_offset_ms +
                    target.vision_occlusion_bursts[0].duration_ms <
                target.vision_occlusion_bursts[1].tracking_offset_ms,
            "bursts must not overlap");
    }

    BenchmarkConfig plain;
    const auto no_occlusion =
        controller_native::sustained_aimlab::generate_script(2026072701, plain);
    require(no_occlusion.hash != first.hash,
            "occlusion semantics must change script identity");
    for (const auto& target : no_occlusion.targets) {
        require(target.vision_occlusion_bursts.empty(),
                "ordinary scenarios must remain burst-free");
    }
}

void test_runtime_profile_drives_paired_delivery_capture_age_and_target_shape() {
    BenchmarkConfig config;
    config.duration_ms = 1'000;
    config.fixed_target_slot_ms = 200;
    config.runtime_observation_pattern = {
        RuntimeObservationSample{7, 5},
        RuntimeObservationSample{8, 6},
        RuntimeObservationSample{6, 4},
    };
    config.runtime_manual_segments = {
        RuntimeManualSegment{{RuntimeManualSample{10, 0.25, -0.10}}},
        RuntimeManualSegment{{RuntimeManualSample{10, -0.20, 0.05}}},
        RuntimeManualSegment{{RuntimeManualSample{10, 0.00, 0.00}}},
        RuntimeManualSegment{{RuntimeManualSample{10, 0.10, 0.30}}},
    };
    config.runtime_target_samples = {
        RuntimeTargetSample{{19.0, -10.0}, 42.0, 96.0},
        RuntimeTargetSample{{-31.0, 7.0}, 28.0, 70.0},
    };

    const ScenarioScript script =
        controller_native::sustained_aimlab::generate_script(2026082501, config);
    require(!script.targets.empty(), "runtime profile generated no targets");
    const TargetScript& first = script.targets.front();
    require(same_vec(first.initial_error_px, {19.0, -10.0}),
            "runtime target error must replace the synthetic range");
    require_near(first.body_width_px, 42.0, 1e-12,
                 "runtime body width");
    require_near(first.body_height_px, 96.0, 1e-12,
                 "runtime body height");
    const ScenarioScript replay =
        controller_native::sustained_aimlab::generate_script(2026082501, config);
    require(script.hash == replay.hash,
            "runtime manual trace assignment must be seed reproducible");
    std::set<std::size_t> first_cycle;
    for (std::size_t index = 0; index < 4; ++index) {
        require(script.targets[index].has_runtime_ads_manual_segment &&
                    script.targets[index].has_runtime_bodylock_manual_segment,
                "legacy any-mode traces must remain available to both cohorts");
        first_cycle.insert(
            script.targets[index].runtime_bodylock_manual_segment_index);
        require(
            script.targets[index].runtime_bodylock_manual_segment_index ==
                replay.targets[index].runtime_bodylock_manual_segment_index,
            "runtime manual trace replay changed for the same seed");
    }
    require(first_cycle.size() == 4,
            "each retained user-habit trace must run once before reuse");
    const ScenarioScript different_seed =
        controller_native::sustained_aimlab::generate_script(2026082502, config);
    require(
        first.runtime_bodylock_manual_segment_index !=
            different_seed.targets.front().runtime_bodylock_manual_segment_index,
        "runtime manual trace assignment must vary across benchmark seeds");
    require(first.observation_at_ms.size() ==
                first.observation_ready_at_ms.size(),
            "runtime capture and delivery schedules must stay paired");
    require(first.observation_at_ms.size() > 4,
            "runtime observation pattern must cover the target horizon");
    for (std::size_t index = 0; index < 4; ++index) {
        const auto& sample = config.runtime_observation_pattern[index % 3];
        require(first.observation_ready_at_ms[index] -
                    first.observation_at_ms[index] == sample.capture_age_ms,
                "runtime capture age must be preserved");
        if (index > 0) {
            require(first.observation_ready_at_ms[index] -
                        first.observation_ready_at_ms[index - 1] ==
                    sample.delivery_interval_ms,
                    "runtime delivery interval must be preserved");
        }
    }

    BenchmarkConfig synthetic;
    synthetic.duration_ms = config.duration_ms;
    const auto synthetic_script =
        controller_native::sustained_aimlab::generate_script(2026082501, synthetic);
    require(script.hash != synthetic_script.hash,
            "runtime covariates must change scenario identity");
}

void test_runtime_manual_library_preserves_aim_mode_scope() {
    BenchmarkConfig config;
    config.duration_ms = config.fixed_target_slot_ms;
    config.runtime_manual_segments = {
        RuntimeManualSegment{{RuntimeManualSample{10, 0.20, 0.0}}},
        RuntimeManualSegment{{RuntimeManualSample{10, -0.20, 0.0}}},
    };
    config.runtime_manual_segments[0].aim_mode = RuntimeManualAimMode::Ads;
    config.runtime_manual_segments[1].aim_mode = RuntimeManualAimMode::BodyLock;

    const ScenarioScript script =
        controller_native::sustained_aimlab::generate_script(2026082701, config);
    require(!script.targets.empty(), "mode-scoped runtime profile has no targets");
    for (const auto& target : script.targets) {
        require(target.has_runtime_ads_manual_segment &&
                    target.runtime_ads_manual_segment_index == 0,
                "ADS cohort must receive only ADS user-habit traces");
        require(target.has_runtime_bodylock_manual_segment &&
                    target.runtime_bodylock_manual_segment_index == 1,
                "BodyLock cohort must receive only BodyLock user-habit traces");
    }
}

void test_default_fixed_schedule_has_stable_script_identity() {
    BenchmarkConfig config;
    config.duration_ms = config.fixed_target_slot_ms;
    require(config.runtime_observation_pattern.empty(),
            "default runtime observation pattern is not empty");
    require(config.runtime_manual_segments.empty(),
            "default runtime manual segments are not empty");
    require(config.runtime_target_samples.empty(),
            "default runtime target samples are not empty");
    const ScenarioScript script =
        controller_native::sustained_aimlab::generate_script(2026072301, config);
    require(script.hash == 18'044'272'266'236'773'195ull,
            "default fixed target schedule changed script identity: " +
                std::to_string(script.hash));
}

void test_gun_kick_disturbance_is_deterministic_and_observation_only() {
    BenchmarkConfig plain_config;
    plain_config.vision_interval_ms = 10;
    BenchmarkConfig kick_config = plain_config;
    kick_config.vision_disturbance = VisionDisturbanceProfile::GunKick;
    const auto plain =
        controller_native::sustained_aimlab::generate_script(
            2026073007, plain_config);
    const auto first =
        controller_native::sustained_aimlab::generate_script(
            2026073007, kick_config);
    const auto second =
        controller_native::sustained_aimlab::generate_script(
            2026073007, kick_config);

    require(first.hash == second.hash,
            "gun-kick disturbance must be deterministic");
    require(first.hash != plain.hash,
            "gun-kick disturbance must change script identity");
    require(first.targets.size() == plain.targets.size(),
            "gun-kick disturbance must preserve target count");
    bool saw_large_disturbance = false;
    for (std::size_t target_index = 0;
         target_index < first.targets.size(); ++target_index) {
        const auto& base = plain.targets[target_index];
        const auto& kicked = first.targets[target_index];
        require(
            base.id == kicked.id &&
                base.motion == kicked.motion &&
                same_vec(base.initial_error_px, kicked.initial_error_px) &&
                same_vec(
                    base.initial_velocity_px_per_second,
                    kicked.initial_velocity_px_per_second) &&
                base.observation_at_ms == kicked.observation_at_ms,
            "gun-kick must not alter target motion or Vision cadence");
        require(
            kicked.observation_noise_px.size() ==
                base.observation_noise_px.size(),
            "gun-kick observations must remain paired");
        for (std::size_t sample = 0;
             sample < kicked.observation_noise_px.size(); ++sample) {
            const Vec2d delta{
                kicked.observation_noise_px[sample].x -
                    base.observation_noise_px[sample].x,
                kicked.observation_noise_px[sample].y -
                    base.observation_noise_px[sample].y};
            saw_large_disturbance =
                saw_large_disturbance ||
                std::fabs(delta.x) > 2.0 ||
                std::fabs(delta.y) > 4.0;
            require(
                std::fabs(delta.x) <= 4.0 + 1e-9 &&
                    std::fabs(delta.y) <= 8.0 + 1e-9,
                "gun-kick disturbance must stay bounded");
        }
    }
    require(saw_large_disturbance,
            "gun-kick fixture must contain a meaningful camera pulse");
}

void test_adversarial_gun_kick_is_log_scale_and_phase_swept() {
    BenchmarkConfig plain_config;
    plain_config.vision_interval_ms = 10;
    BenchmarkConfig attack_config = plain_config;
    attack_config.vision_disturbance =
        VisionDisturbanceProfile::GunKickAdversarial;
    const auto plain = generate_script(2026073101, plain_config);
    const auto first = generate_script(2026073101, attack_config);
    const auto second = generate_script(2026073101, attack_config);
    require(first.hash == second.hash,
            "adversarial gun-kick must remain deterministic");
    bool saw_log_scale_vertical_pulse = false;
    for (std::size_t target_index = 0;
         target_index < first.targets.size(); ++target_index) {
        const auto& base = plain.targets[target_index];
        const auto& attacked = first.targets[target_index];
        require(base.observation_at_ms == attacked.observation_at_ms,
                "phase sweep must preserve Vision cadence");
        for (std::size_t sample = 0;
             sample < attacked.observation_noise_px.size(); ++sample) {
            const Vec2d delta{
                attacked.observation_noise_px[sample].x -
                    base.observation_noise_px[sample].x,
                attacked.observation_noise_px[sample].y -
                    base.observation_noise_px[sample].y};
            saw_log_scale_vertical_pulse =
                saw_log_scale_vertical_pulse ||
                std::fabs(delta.y) > 8.0;
            require(std::fabs(delta.x) <= 8.0 + 1e-9 &&
                        std::fabs(delta.y) <= 17.0 + 1e-9,
                    "adversarial disturbance must stay bounded");
        }
    }
    require(saw_log_scale_vertical_pulse,
            "adversarial fixture must exceed the old 8px vertical pulse");
}

void test_dropout_decoy_fixture_is_stationary_and_deterministic() {
    BenchmarkConfig config;
    config.duration_ms = 2'000;
    config.vision_interval_ms = 6;
    config.vision_disturbance =
        VisionDisturbanceProfile::TargetDropoutDecoy;
    const auto first = generate_script(2026080101, config);
    const auto second = generate_script(2026080101, config);
    require(first.hash == second.hash,
            "dropout-decoy fixture must be deterministic");
    require(!first.targets.empty(), "dropout-decoy fixture generated no target");
    for (const auto& target : first.targets) {
        require(target.initial_velocity_px_per_second.x == 0.0 &&
                    target.initial_velocity_px_per_second.y == 0.0 &&
                    target.velocity_maneuvers.empty(),
                "dropout-decoy fixture must isolate controller-induced motion");
        require(target.vision_occlusion_bursts.size() == 2,
                "dropout-decoy fixture must contain two occlusion windows");
        require(target.vision_occlusion_bursts[0].tracking_offset_ms == 110 &&
                    target.vision_occlusion_bursts[0].duration_ms == 70 &&
                    target.vision_occlusion_bursts[1].tracking_offset_ms == 360 &&
                    target.vision_occlusion_bursts[1].duration_ms == 50,
                "dropout-decoy windows changed without updating evidence contract");
    }
}

}  // namespace

int main() {
    try {
        test_defaults_and_slowdown_anchor_points();
        test_seeded_generation_is_reproducible_and_complete();
        test_script_hash_includes_control_response_delay();
        test_script_hash_includes_controller_tick_and_sensitivity();
        test_near_crosshair_profile_bounds_initial_error();
        test_full_speed_strafe_schedule_is_seeded_and_bounded();
        test_vertical_motion_schedule_is_seeded_and_bounded();
        test_vertical_motion_shapes_cover_slide_recovery_and_jump();
        test_stationary_target_mode_preserves_paired_nonmotion_script();
        test_generated_ranges_and_observation_schedule();
        test_200hz_changes_only_observation_schedule();
        test_runtime_profile_drives_paired_delivery_capture_age_and_target_shape();
        test_runtime_manual_library_preserves_aim_mode_scope();
        test_default_fixed_schedule_has_stable_script_identity();
        test_motion_profiles_and_boundary_reflection();
        test_motion_profile_names_are_stable();
        test_bodylock_stress_profiles_are_isolated_and_deterministic();
        test_small_target_profile_reuses_motion_and_cycles_visible_radius();
        test_obsolete_vertical_fixture_is_stationary_for_every_motion_label();
        test_short_occlusion_bursts_are_tracking_relative_and_hashed();
        test_gun_kick_disturbance_is_deterministic_and_observation_only();
        test_adversarial_gun_kick_is_log_scale_and_phase_swept();
        test_dropout_decoy_fixture_is_stationary_and_deterministic();
        std::cout << "cod_native_sustained_aimlab_scenario_tests PASS\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "cod_native_sustained_aimlab_scenario_tests FAIL: "
                  << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
