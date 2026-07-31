#include "sustained_aimlab_simulator.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace controller_native::sustained_aimlab;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

ScenarioScript stationary_script(int duration_ms, Vec2d error,
                                 int deadline_ms = 300) {
    ScenarioScript script;
    script.seed = 7;
    script.hash = 7007;
    script.config.duration_ms = duration_ms;
    TargetScript target;
    target.id = 1;
    target.motion = MotionProfile::ConstantHorizontal;
    target.initial_error_px = error;
    target.acquire_deadline_ms = deadline_ms;
    for (int ms = 0; ms <= 1'400; ++ms) {
        target.observation_at_ms.push_back(ms);
        target.observation_noise_px.push_back({});
    }
    script.targets.push_back(target);
    return script;
}

ControllerStep proportional_controller(int delay_ms = 0, double scale = 0.025) {
    return [=](const ControllerObservation& input) {
        ControllerStepResult output;
        output.target_observed = input.target_present;
        output.tracker_reliable = input.target_present;
        output.bodylock_mode = input.target_present;
        if (input.target_present && input.now_ms >= delay_ms) {
            output.final_stick.x = std::clamp(
                input.observed_error_px.x * scale, -1.0, 1.0);
            output.final_stick.y = std::clamp(
                -input.observed_error_px.y * scale, -1.0, 1.0);
            output.requested_assist_stick = output.final_stick;
            output.shaped_assist_stick = output.final_stick;
        }
        return output;
    };
}

void test_runner_executes_exact_duration_and_preserves_identity() {
    BenchmarkConfig config;
    config.duration_ms = 60'000;
    const ScenarioScript script = generate_script(1337, config);
    int calls = 0;
    ControllerStep zero = [&](const ControllerObservation&) {
        ++calls;
        return ControllerStepResult{};
    };
    const BenchmarkResult result =
        run_simulation(script, ManualProfile::Pure, zero);
    require(calls == 60'000, "runner must execute exactly 60,000 ticks");
    require(result.ticks == 60'000, "result must record exact tick count");
    require(result.seed == script.seed && result.script_hash == script.hash,
            "runner must preserve scenario identity");
}

void test_miss_respects_deadline_and_tracking_is_exactly_1000ms() {
    const ScenarioScript missed_script = stationary_script(400, {100.0, 0.0}, 250);
    const BenchmarkResult missed = run_simulation(
        missed_script, ManualProfile::Pure,
        [](const ControllerObservation&) { return ControllerStepResult{}; });
    require(missed.targets_spawned == 1 && missed.targets_missed == 1,
            "unacquired target must miss at deadline");
    require(missed.targets.front().first_entry_ms == -1,
            "miss must not create an entry time");

    const ScenarioScript tracked_script = stationary_script(1'300, {20.0, 0.0});
    const BenchmarkResult tracked = run_simulation(
        tracked_script, ManualProfile::Pure, proportional_controller());
    require(tracked.targets_acquired == 1, "controller must acquire target");
    require(tracked.targets.front().tracking_errors_px.size() == 1'000,
            "tracking window must contain exactly 1000 ticks");
}

void test_virtual_camera_x_and_y_signs_close_error() {
    const ScenarioScript script = stationary_script(30, {20.0, 20.0});
    std::vector<Vec2d> observations;
    ControllerStep controller = [&](const ControllerObservation& input) {
        if (input.fresh_vision) observations.push_back(input.observed_error_px);
        ControllerStepResult output;
        output.final_stick = {1.0, -1.0};
        output.target_observed = true;
        output.tracker_reliable = true;
        output.bodylock_mode = true;
        return output;
    };
    (void)run_simulation(script, ManualProfile::Pure, controller);
    require(observations.size() > 2, "fixture must receive observations");
    require(observations.back().x < observations.front().x,
            "positive X stick must close positive screen X error");
    require(observations.back().y < observations.front().y,
            "negative Y stick must close positive screen Y error");
}

void test_virtual_camera_can_delay_delivered_control_without_delaying_controller() {
    ScenarioScript script = stationary_script(30, {20.0, 0.0});
    script.config.control_response_delay_ms = 10;
    std::vector<SimulationTraceFrame> trace;
    ControllerStep controller = [](const ControllerObservation& input) {
        ControllerStepResult output;
        output.final_stick = input.target_present ? Vec2d{1.0, 0.0} : Vec2d{};
        output.target_observed = input.target_present;
        output.tracker_reliable = input.target_present;
        output.bodylock_mode = input.target_present;
        return output;
    };
    (void)run_simulation(
        script, ManualProfile::Pure, controller, BenchmarkCohort::AdsAcquire,
        [&](const SimulationTraceFrame& frame) { trace.push_back(frame); });
    require(trace.size() == 30, "delay fixture must retain every tick");
    require(std::fabs(trace[9].true_error_after_px.x - 20.0) < 1e-9,
            "camera must not react before the configured delivery delay");
    require(trace[10].true_error_after_px.x < 20.0,
            "camera must apply the queued stick at the configured delay");
}

void test_full_speed_left_strafe_moves_relative_error_with_inertia() {
    ScenarioScript script = stationary_script(320, {100.0, 0.0}, 320);
    auto& strafe = script.targets.front().player_strafe;
    strafe.initial_direction = 1;
    strafe.onset_ms = 10;
    strafe.reverse_ms = 120;
    strafe.release_ms = 240;
    strafe.top_speed_px_per_second = 200.0;
    strafe.time_constant_ms = 100.0;

    std::vector<SimulationTraceFrame> off_trace;
    const BenchmarkResult off = run_simulation(
        script, ManualProfile::Pure,
        [](const ControllerObservation&) { return ControllerStepResult{}; },
        BenchmarkCohort::AdsAcquire,
        [&](const SimulationTraceFrame& frame) { off_trace.push_back(frame); },
        PlayerStrafeMode::Off);
    std::vector<SimulationTraceFrame> on_trace;
    const BenchmarkResult on = run_simulation(
        script, ManualProfile::Pure,
        [](const ControllerObservation&) { return ControllerStepResult{}; },
        BenchmarkCohort::AdsAcquire,
        [&](const SimulationTraceFrame& frame) { on_trace.push_back(frame); },
        PlayerStrafeMode::FullReversal);

    require(off.script_hash == on.script_hash,
            "paired strafe variants must share target identity");
    require(on_trace[10].input.left_x == 1.0 &&
                on_trace[120].input.left_x == -1.0 &&
                on_trace[240].input.left_x == 0.0,
            "strafe schedule must deliver full-scale onset, reversal, release");
    require(on_trace[60].true_error_after_px.x <
                off_trace[60].true_error_after_px.x,
            "positive player motion must shift target left");
    require(on_trace[10].player_velocity_x_px_per_second > 0.0 &&
                on_trace[10].player_velocity_x_px_per_second < 200.0,
            "player velocity must ramp instead of jumping");
    require(std::fabs(on_trace[241].player_velocity_x_px_per_second) <
                std::fabs(on_trace[239].player_velocity_x_px_per_second),
            "release must decay velocity instead of stopping instantly");
    require(std::fabs(on_trace[241].player_velocity_x_px_per_second) > 0.0,
            "release must retain bounded inertia");
    require(off.left_strafe_active_ms == 0 &&
                off.max_abs_player_speed_px_per_second == 0.0,
            "off mode must leave player plant neutral");
    require(on.left_strafe_active_ms == 230 &&
                on.left_strafe_reversals == 1 &&
                on.max_abs_left_x == 1.0 &&
                on.max_abs_player_speed_px_per_second > 0.0,
            "strafe run must retain audit metrics");
}

void test_slide_and_jump_shift_vertical_error_and_emit_audit_metrics() {
    ScenarioScript script = stationary_script(900, {0.0, 0.0}, 300);
    auto& motion = script.targets.front().player_vertical;
    motion.slide_onset_ms = 100;
    motion.slide_drop_ms = 100;
    motion.slide_hold_ms = 100;
    motion.slide_recover_ms = 100;
    motion.slide_depth_px = 40.0;
    motion.slide_instant_recovery = false;
    motion.jump_onset_ms = 100;
    motion.jump_duration_ms = 600;
    motion.jump_height_px = 50.0;

    auto zero_controller = [](const ControllerObservation& input) {
        ControllerStepResult output;
        output.target_observed = input.target_present;
        output.tracker_reliable = input.target_present;
        output.bodylock_mode = input.target_present;
        return output;
    };
    std::vector<SimulationTraceFrame> slide_trace;
    const BenchmarkResult slide = run_simulation(
        script, ManualProfile::Pure, zero_controller,
        BenchmarkCohort::AdsAcquire,
        [&](const SimulationTraceFrame& frame) {
            slide_trace.push_back(frame);
        },
        PlayerStrafeMode::Off, PlayerVerticalMotionMode::Slide);
    std::vector<SimulationTraceFrame> jump_trace;
    const BenchmarkResult jump = run_simulation(
        script, ManualProfile::Pure, zero_controller,
        BenchmarkCohort::AdsAcquire,
        [&](const SimulationTraceFrame& frame) {
            jump_trace.push_back(frame);
        },
        PlayerStrafeMode::Off, PlayerVerticalMotionMode::Jump);

    require(slide_trace[200].true_error_after_px.y < -39.0,
            "slide camera drop must move the target upward on screen");
    require(std::fabs(slide_trace[400].true_error_after_px.y) < 1e-9,
            "linear slide recovery must return to standing height");
    require(jump_trace[400].true_error_after_px.y > 49.0,
            "jump apex must move the target downward on screen");
    require(std::fabs(jump_trace[700].true_error_after_px.y) < 1e-9,
            "jump landing must restore standing height");
    require(slide.player_slide_events == 1 &&
                slide.player_jump_events == 0 &&
                slide.player_vertical_active_ms > 0 &&
                slide.max_abs_player_vertical_offset_px >= 40.0 &&
                slide.max_abs_player_vertical_speed_px_per_second > 0.0,
            "slide run must retain vertical-motion audit metrics");
    require(jump.player_slide_events == 0 &&
                jump.player_jump_events == 1 &&
                jump.player_vertical_active_ms > 0 &&
                jump.max_abs_player_vertical_offset_px >= 50.0,
            "jump run must retain vertical-motion audit metrics");
}

void test_default_simulation_mode_is_explicit_off() {
    const ScenarioScript script = stationary_script(80, {20.0, 0.0});
    const BenchmarkResult implicit = run_simulation(
        script, ManualProfile::Pure, proportional_controller());
    const BenchmarkResult explicit_off = run_simulation(
        script, ManualProfile::Pure, proportional_controller(),
        BenchmarkCohort::AdsAcquire, {}, PlayerStrafeMode::Off);
    require(implicit.acquire_points == explicit_off.acquire_points &&
                implicit.tracking_points == explicit_off.tracking_points &&
                implicit.mean_error_px == explicit_off.mean_error_px,
            "default simulation must preserve pre-feature no-strafe result");
}

void test_closed_loop_score_ordering() {
    const ScenarioScript script = stationary_script(1'350, {100.0, 0.0});
    const BenchmarkResult fast = run_simulation(
        script, ManualProfile::Pure, proportional_controller(0, 0.035));
    const BenchmarkResult delayed = run_simulation(
        script, ManualProfile::Pure, proportional_controller(120, 0.035));
    const BenchmarkResult stopped = run_simulation(
        script, ManualProfile::Pure,
        [](const ControllerObservation&) { return ControllerStepResult{}; });

    require(fast.acquire_points > delayed.acquire_points,
            "fast controller must earn more acquisition points");
    require(delayed.acquire_points > stopped.acquire_points,
            "delayed controller must beat stopped controller");
    require(fast.tracking_points >= delayed.tracking_points,
            "fast controller must not lose tracking score");
}

void test_same_script_is_reused_for_pure_and_mixed_runs() {
    BenchmarkConfig config;
    config.duration_ms = 3'000;
    const ScenarioScript script = generate_script(424242, config);
    const BenchmarkResult pure = run_simulation(
        script, ManualProfile::Pure, proportional_controller());
    const BenchmarkResult mixed = run_simulation(
        script, ManualProfile::Mixed, proportional_controller());
    require(pure.script_hash == mixed.script_hash &&
                pure.script_hash == script.hash,
            "manual profiles must consume the identical scenario script");
    require(pure.manual_profile == ManualProfile::Pure &&
                mixed.manual_profile == ManualProfile::Mixed,
            "result must identify manual profile");
}

void test_scripted_manual_input_is_controller_independent() {
    const ScenarioScript script = stationary_script(200, {80.0, 40.0}, 300);
    auto capture = [](std::vector<Vec2d>& values, ControllerStep inner) {
        return [&values, inner = std::move(inner)](
                   const ControllerObservation& input) mutable {
            if (input.target_present) values.push_back(input.manual_stick);
            return inner(input);
        };
    };
    for (const ManualProfile profile : {
             ManualProfile::Scripted,
             ManualProfile::WrongThenCorrect,
             ManualProfile::ArcRecovery}) {
        std::vector<Vec2d> zero_inputs;
        std::vector<Vec2d> active_inputs;
        (void)run_simulation(
            script, profile,
            capture(zero_inputs, [](const ControllerObservation&) {
                return ControllerStepResult{};
            }));
        (void)run_simulation(
            script, profile,
            capture(active_inputs, proportional_controller(0, 0.01)));

        require(zero_inputs.size() == active_inputs.size() &&
                    !zero_inputs.empty(),
                "fixed replay must expose matching manual sample counts");
        for (std::size_t index = 0; index < zero_inputs.size(); ++index) {
            require(zero_inputs[index].x == active_inputs[index].x &&
                        zero_inputs[index].y == active_inputs[index].y,
                    "fixed manual samples must not depend on controller output");
        }
    }
}

void test_manual_recovery_profiles_expose_expected_trajectory() {
    const ScenarioScript script = stationary_script(400, {80.0, 0.0}, 400);
    auto samples_for = [&](ManualProfile profile) {
        std::vector<Vec2d> values;
        (void)run_simulation(
            script, profile,
            [&](const ControllerObservation& input) {
                if (input.target_present) values.push_back(input.manual_stick);
                return ControllerStepResult{};
            });
        return values;
    };

    const auto recover = samples_for(ManualProfile::WrongThenCorrect);
    require(recover.size() == 400,
            "recover fixture must retain the full target lifetime");
    require(recover[20].x < -0.6 && std::fabs(recover[20].y) < 1e-9,
            "recover profile must begin with a strong wrong-way command");
    require(recover[120].x > 0.6 && std::fabs(recover[120].y) < 1e-9,
            "recover profile must reverse into a strong correction");
    require(length(recover[320]) < 1e-9,
            "recover profile must release after its correction");

    const auto arc = samples_for(ManualProfile::ArcRecovery);
    require(arc.size() == 400,
            "arc fixture must retain the full target lifetime");
    require(std::fabs(arc.front().x) < 1e-9 &&
                std::fabs(arc.front().y) > 0.6,
            "arc profile must begin with a strong tangential error");
    require(arc[70].x > 0.3 && std::fabs(arc[70].y) > 0.3,
            "arc profile must pass through a strong tangential command");
    require(arc[180].x > 0.6,
            "arc profile must finish in the corrective direction");
    require(length(arc[320]) < 1e-9,
            "arc profile must release after its corrective hold");
}

void test_fixed_target_slot_preserves_wall_clock_spawn_times() {
    BenchmarkConfig config;
    config.duration_ms = 1'000;
    config.fixed_target_slot_ms = 400;
    config.inter_target_gap_ms = 50;
    const ScenarioScript script = generate_script(919191, config);
    auto starts_for = [&](ControllerStep inner) {
        std::vector<std::pair<std::uint64_t, int>> starts;
        std::uint64_t previous = 0;
        (void)run_simulation(
            script, ManualProfile::Pure,
            [&](const ControllerObservation& input) {
                if (input.target_present && input.target_id != previous) {
                    starts.emplace_back(input.target_id, input.now_ms);
                    previous = input.target_id;
                } else if (!input.target_present) {
                    previous = 0;
                }
                return inner(input);
            });
        return starts;
    };
    const auto stopped = starts_for([](const ControllerObservation&) {
        return ControllerStepResult{};
    });
    const auto active = starts_for(proportional_controller(0, 0.04));
    require(stopped == active && stopped.size() >= 2,
            "fixed slots must preserve target spawn times across controllers");
    require(stopped[0].second == 0 && stopped[1].second == 450,
            "fixed slot plus gap must define the wall-clock schedule");
}

void test_despawn_publishes_a_fresh_empty_observation() {
    const ScenarioScript script = stationary_script(400, {100.0, 0.0}, 250);
    bool saw_fresh_miss = false;
    ControllerStep controller = [&](const ControllerObservation& input) {
        if (!input.target_present && input.fresh_vision) saw_fresh_miss = true;
        return ControllerStepResult{};
    };
    (void)run_simulation(script, ManualProfile::Pure, controller);
    require(saw_fresh_miss,
            "despawn must send a fresh empty observation to clear controller hold");
}

void test_run_end_does_not_turn_partial_acquisition_into_a_miss() {
    const ScenarioScript script = stationary_script(100, {100.0, 0.0}, 250);
    const BenchmarkResult result = run_simulation(
        script, ManualProfile::Pure,
        [](const ControllerObservation&) { return ControllerStepResult{}; });
    require(result.targets_spawned == 1, "partial target must remain recorded");
    require(result.targets_missed == 0,
            "run end before deadline must not count a target miss");
    require(!result.targets.front().acquisition_timed_out,
            "partial target must not be marked timed out");
}

void test_bodylock_cohort_scores_only_after_confirmed_mode_entry() {
    ScenarioScript script = stationary_script(1'200, {10.0, 0.0});
    script.config.bodylock_entry_timeout_ms = 120;
    ControllerStep delayed_bodylock = [](const ControllerObservation& input) {
        ControllerStepResult output;
        output.target_observed = input.target_present;
        output.tracker_reliable = input.target_present;
        output.bodylock_mode = input.target_present && input.now_ms >= 50;
        return output;
    };
    const BenchmarkResult result = run_simulation(
        script, ManualProfile::Pure, delayed_bodylock,
        BenchmarkCohort::BodyLockFollow);
    require(result.cohort == BenchmarkCohort::BodyLockFollow,
            "result must identify isolated BodyLock cohort");
    require(result.targets.size() == 1,
            "fixture must score one BodyLock target");
    require(result.targets.front().bodylock_entry_ms == 50,
            "warm-up time must be recorded but not scored");
    require(result.targets.front().tracking_errors_px.size() == 1'000,
            "BodyLock must receive exactly 1000 scored milliseconds");
}

void test_bodylock_cohort_fails_when_mode_never_enters() {
    ScenarioScript script = stationary_script(300, {10.0, 0.0});
    script.config.bodylock_entry_timeout_ms = 100;
    const BenchmarkResult result = run_simulation(
        script, ManualProfile::Pure,
        [](const ControllerObservation& input) {
            ControllerStepResult output;
            output.target_observed = input.target_present;
            output.tracker_reliable = input.target_present;
            return output;
        },
        BenchmarkCohort::BodyLockFollow);
    require(result.bodylock_entry_failures == 1,
            "never entering BodyLock must fail the cohort");
    require(result.targets.front().tracking_errors_px.empty(),
            "ADS warm-up frames must not leak into BodyLock tracking score");
}

void test_bodylock_warmup_is_stationary_and_manual_neutral() {
    ScenarioScript script = stationary_script(1'200, {10.0, 0.0});
    script.targets.front().initial_velocity_px_per_second = {300.0, 0.0};
    std::vector<double> warmup_errors;
    ControllerStep delayed_bodylock = [&](const ControllerObservation& input) {
        ControllerStepResult output;
        output.target_observed = input.target_present;
        output.tracker_reliable = input.target_present;
        output.bodylock_mode = input.target_present && input.now_ms >= 50;
        if (input.target_present && input.now_ms < 50) {
            warmup_errors.push_back(input.observed_error_px.x);
            require(std::hypot(input.manual_stick.x, input.manual_stick.y) == 0.0,
                    "BodyLock warm-up must not inject mixed manual errors");
        }
        return output;
    };
    (void)run_simulation(script, ManualProfile::Mixed, delayed_bodylock,
                         BenchmarkCohort::BodyLockFollow);
    require(!warmup_errors.empty(), "fixture must observe BodyLock warm-up");
    require(std::fabs(warmup_errors.back() - warmup_errors.front()) < 1e-9,
            "BodyLock target motion must begin only after confirmed mode entry");
}

void test_compound_bodylock_motion_turns_twice_after_clean_entry() {
    ScenarioScript script = stationary_script(420, {60.0, 0.0});
    script.config.scenario_profile = ScenarioProfile::CompoundDirectional;
    script.targets.front().motion = MotionProfile::CompoundDirectional;
    script.targets.front().initial_velocity_px_per_second = {84.8528, 84.8528};
    script.targets.front().velocity_maneuvers = {
        {80, {-84.8528, 84.8528}},
        {180, {-84.8528, -84.8528}},
    };
    std::vector<SimulationTraceFrame> trace;
    ControllerStep immediate_bodylock = [](const ControllerObservation& input) {
        ControllerStepResult output;
        output.target_observed = input.target_present;
        output.tracker_reliable = input.target_present;
        output.bodylock_mode = input.target_present;
        return output;
    };
    (void)run_simulation(
        script, ManualProfile::Pure, immediate_bodylock,
        BenchmarkCohort::BodyLockFollow,
        [&](const SimulationTraceFrame& frame) { trace.push_back(frame); });
    require(std::fabs(trace.front().true_error_before_px.x - 8.0) < 1e-9,
            "compound tracking stress must retain the clean 8px entry");
    bool saw_upper_left = false;
    bool saw_lower_left_after_upper_left = false;
    for (const SimulationTraceFrame& frame : trace) {
        if (!frame.target_active) continue;
        if (frame.target_velocity_px_per_second.x < 0.0 &&
            frame.target_velocity_px_per_second.y > 0.0) {
            saw_upper_left = true;
        } else if (saw_upper_left &&
                   frame.target_velocity_px_per_second.x < 0.0 &&
                   frame.target_velocity_px_per_second.y < 0.0) {
            saw_lower_left_after_upper_left = true;
        }
    }
    require(saw_upper_left && saw_lower_left_after_upper_left,
            "compound BodyLock tracking must execute two 2D direction changes");
}

void test_plan_diagnostics_and_ads_handoff_reach_the_scorer() {
    const ScenarioScript script = stationary_script(200, {30.0, 0.0});
    ControllerStep controller = [](const ControllerObservation& input) {
        ControllerStepResult output;
        output.target_observed = input.target_present;
        output.tracker_reliable = input.target_present;
        output.bodylock_mode = input.target_present && input.now_ms >= 5;
        output.final_stick = input.target_present
            ? Vec2d{0.50, 0.0}
            : Vec2d{};
        output.predicted_terminal_error_px = {-4.0, 0.0};
        output.radial_closing_velocity_px_per_sec = 75.0;
        return output;
    };
    const BenchmarkResult result = run_simulation(
        script, ManualProfile::Pure, controller);
    require(result.handoff_count == 1,
            "ADS-to-BodyLock transition must be counted once");
    require(result.handoff_episodes == 1,
            "ADS-to-BodyLock transition must create one scored episode");
    require(result.post_handoff_local_error_area_px_ms > 0.0,
            "handoff local burden must be populated");
    require(result.max_handoff_residual_px > 0.0,
            "handoff must retain the transition-frame residual");
    require(std::fabs(result.max_abs_handoff_closing_speed_px_per_sec - 75.0) < 1e-9,
            "tracker radial closing speed must reach the scorer unchanged");
}

void test_ads_handoff_on_acquisition_boundary_is_not_lost() {
    const ScenarioScript script = stationary_script(40, {10.0, 0.0});
    ControllerStep controller = [](const ControllerObservation& input) {
        ControllerStepResult output;
        output.target_observed = input.target_present;
        output.tracker_reliable = input.target_present;
        output.bodylock_mode = input.target_present;
        output.radial_closing_velocity_px_per_sec = 25.0;
        return output;
    };
    const BenchmarkResult result = run_simulation(
        script, ManualProfile::Pure, controller, BenchmarkCohort::AdsAcquire);
    require(result.targets.front().acquired,
            "boundary fixture must complete ADS acquisition");
    require(result.targets.front().bodylock_entry_ms == 0,
            "BodyLock entry on the acquisition tick must retain its ADS time");
    require(result.handoff_count == 1,
            "ADS-to-BodyLock transition on the acquisition tick must be counted");
    require(result.max_handoff_residual_px > 0.0,
            "boundary handoff must retain a measurable residual");
}

Vec2d first_mixed_manual_for_target(std::uint64_t id) {
    ScenarioScript script = stationary_script(20, {30.0, 40.0});
    script.targets.front().id = id;
    Vec2d captured{};
    bool saw_manual = false;
    (void)run_simulation(
        script, ManualProfile::Mixed,
        [&](const ControllerObservation& input) {
            if (!saw_manual && input.target_present) {
                captured = input.manual_stick;
                saw_manual = true;
            }
            return ControllerStepResult{};
        });
    require(saw_manual, "fixture must capture mixed manual input");
    return captured;
}

void test_mixed_profile_contains_polar_component_errors() {
    const Vec2d radial{0.6, -0.8};
    const Vec2d tangent{0.8, 0.6};
    const auto radial_wrong = first_mixed_manual_for_target(7);
    require(radial_wrong.x * radial.x + radial_wrong.y * radial.y < -0.28,
            "target 7 must contain wrong radial manual input");
    require(radial_wrong.x * tangent.x + radial_wrong.y * tangent.y > 0.22,
            "target 7 must preserve helpful tangential manual input");

    const auto tangent_wrong = first_mixed_manual_for_target(8);
    require(tangent_wrong.x * radial.x + tangent_wrong.y * radial.y > 0.22,
            "target 8 must preserve helpful radial manual input");
    require(tangent_wrong.x * tangent.x + tangent_wrong.y * tangent.y < -0.28,
            "target 8 must contain wrong tangential manual input");
}

void test_trace_is_deterministic_and_observational() {
    const ScenarioScript script = stationary_script(40, {20.0, 10.0});
    const BenchmarkResult plain = run_simulation(
        script, ManualProfile::Pure, proportional_controller());
    std::vector<SimulationTraceFrame> trace;
    const BenchmarkResult observed = run_simulation(
        script, ManualProfile::Pure, proportional_controller(),
        BenchmarkCohort::AdsAcquire,
        [&](const SimulationTraceFrame& frame) { trace.push_back(frame); });

    require(plain.acquire_points == observed.acquire_points,
            "trace observer must not change acquisition score");
    require(plain.tracking_points == observed.tracking_points,
            "trace observer must not change tracking score");
    require(trace.size() == 40, "trace must contain one frame per tick");
    require(trace.front().true_error_before_px.x == 20.0,
            "trace must expose pre-control ground truth");
    require(trace.front().absolute_ms == 0,
            "trace timestamps must begin at zero");
}

void test_obsolete_manual_profile_persists_after_center_crossing() {
    ScenarioScript script = stationary_script(260, {0.0, -40.0});
    script.targets.front().id = 11;
    std::vector<Vec2d> manual;
    const BenchmarkResult result = run_simulation(
        script, ManualProfile::ObsoleteAfterCrossing,
        [&](const ControllerObservation& input) {
            if (input.target_present) manual.push_back(input.manual_stick);
            ControllerStepResult output;
            output.final_stick = {0.0, 1.0};
            output.target_observed = input.target_present;
            output.tracker_reliable = input.target_present;
            return output;
        });
    require(manual.size() == 260, "fixture must expose every active tick");
    require(manual[0].y > 0.60,
            "obsolete profile must begin with realistic upward manual strength");
    require(manual[90].y > 0.60,
            "manual direction must persist after the counterfactual crossing");
    require(std::fabs(manual[250].y) < 1e-9,
            "manual persistence must end within the bounded 80-140ms window");
    require(result.maximum_vertical_overshoot_px > 0.0,
            "V1 must measure crossing without relying on brake arming");
    require(result.post_cross_wrong_way_output_integral > 0.0,
            "V1 must measure obsolete delivered output after crossing");
}

void test_short_occlusion_withholds_publication_without_fresh_miss() {
    ScenarioScript script = stationary_script(90, {8.0, 0.0});
    script.targets.front().vision_occlusion_bursts.push_back({20, 24});
    std::vector<SimulationTraceFrame> trace;
    (void)run_simulation(
        script, ManualProfile::Pure, proportional_controller(),
        BenchmarkCohort::BodyLockFollow,
        [&](const SimulationTraceFrame& frame) { trace.push_back(frame); });

    require(trace.size() == 90, "occlusion fixture must retain every tick");
    const std::uint64_t frame_before_burst = trace[19].input.frame_id;
    require(trace[19].fresh_vision,
            "fixture must publish immediately before the burst");
    for (int tick = 20; tick < 44; ++tick) {
        require(trace[tick].vision_occluded,
                "burst ticks must be marked occluded");
        require(!trace[tick].fresh_vision,
                "occlusion must withhold scheduled publication");
        require(trace[tick].input.target_present,
                "occlusion must not become a confirmed target miss");
        require(trace[tick].input.frame_id == frame_before_burst,
                "withheld frames must not advance Vision identity");
    }
    require(!trace[44].vision_occluded && trace[44].fresh_vision,
            "first tick after the burst must reveal a fresh observation");
    require(trace[44].input.frame_id == frame_before_burst + 1,
            "reveal must advance Vision identity exactly once");
}

void test_camera_recoil_moves_true_error_but_visual_kick_does_not() {
    auto run_profile = [](VisionDisturbanceProfile profile) {
        ScenarioScript script = stationary_script(320, {0.0, 0.0});
        script.config.vision_disturbance = profile;
        std::vector<SimulationTraceFrame> trace;
        ControllerStep passive = [](const ControllerObservation& input) {
            ControllerStepResult output;
            output.bodylock_mode = input.target_present;
            output.target_observed = input.target_present;
            output.tracker_reliable = input.target_present;
            return output;
        };
        (void)run_simulation(
            script, ManualProfile::Pure, passive,
            BenchmarkCohort::BodyLockFollow,
            [&](const SimulationTraceFrame& frame) {
                trace.push_back(frame);
            });
        return trace;
    };
    const auto visual =
        run_profile(VisionDisturbanceProfile::GunKickAdversarial);
    const auto physical =
        run_profile(VisionDisturbanceProfile::CameraRecoil);
    double visual_true_excursion = 0.0;
    double physical_true_excursion = 0.0;
    for (std::size_t index = 0; index < visual.size(); ++index) {
        visual_true_excursion = std::max(
            visual_true_excursion,
            length(visual[index].true_error_after_px));
        physical_true_excursion = std::max(
            physical_true_excursion,
            length(physical[index].true_error_after_px));
    }
    require(visual_true_excursion < 1e-9,
            "observation-only gun kick must not move the true camera");
    require(physical_true_excursion > 3.0,
            "camera recoil fixture must move the true relative error");
}

void test_horizontal_aim_bias_is_observation_only_and_recovers() {
    ScenarioScript script = stationary_script(360, {0.0, 0.0});
    script.config.vision_disturbance =
        VisionDisturbanceProfile::HorizontalAimBiasRecovery;
    std::vector<SimulationTraceFrame> trace;
    ControllerStep passive = [](const ControllerObservation& input) {
        ControllerStepResult output;
        output.bodylock_mode = input.target_present;
        output.target_observed = input.target_present;
        output.tracker_reliable = input.target_present;
        return output;
    };
    (void)run_simulation(
        script, ManualProfile::Pure, passive,
        BenchmarkCohort::BodyLockFollow,
        [&](const SimulationTraceFrame& frame) {
            trace.push_back(frame);
        });
    double maximum_observed_bias = 0.0;
    double maximum_true_error = 0.0;
    double final_observed_error = 1000.0;
    for (const auto& frame : trace) {
        if (frame.fresh_vision) {
            maximum_observed_bias = std::max(
                maximum_observed_bias,
                std::fabs(frame.input.observed_error_px.x));
            final_observed_error =
                std::fabs(frame.input.observed_error_px.x);
        }
        maximum_true_error = std::max(
            maximum_true_error,
            std::fabs(frame.true_error_after_px.x));
    }
    require(maximum_observed_bias > 20.0,
            "fixture must begin with a materially wrong horizontal aim point");
    require(maximum_true_error < 1e-9,
            "wrong target localization must not move the physical camera");
    require(final_observed_error < 0.5,
            "fresh target localization must recover to the true point");
}

}  // namespace

int main() {
    try {
        test_runner_executes_exact_duration_and_preserves_identity();
        test_miss_respects_deadline_and_tracking_is_exactly_1000ms();
        test_virtual_camera_x_and_y_signs_close_error();
        test_virtual_camera_can_delay_delivered_control_without_delaying_controller();
        test_full_speed_left_strafe_moves_relative_error_with_inertia();
        test_slide_and_jump_shift_vertical_error_and_emit_audit_metrics();
        test_default_simulation_mode_is_explicit_off();
        test_closed_loop_score_ordering();
        test_same_script_is_reused_for_pure_and_mixed_runs();
        test_scripted_manual_input_is_controller_independent();
        test_manual_recovery_profiles_expose_expected_trajectory();
        test_fixed_target_slot_preserves_wall_clock_spawn_times();
        test_mixed_profile_contains_polar_component_errors();
        test_despawn_publishes_a_fresh_empty_observation();
        test_run_end_does_not_turn_partial_acquisition_into_a_miss();
        test_bodylock_cohort_scores_only_after_confirmed_mode_entry();
        test_bodylock_cohort_fails_when_mode_never_enters();
        test_bodylock_warmup_is_stationary_and_manual_neutral();
        test_compound_bodylock_motion_turns_twice_after_clean_entry();
        test_plan_diagnostics_and_ads_handoff_reach_the_scorer();
        test_ads_handoff_on_acquisition_boundary_is_not_lost();
        test_trace_is_deterministic_and_observational();
        test_obsolete_manual_profile_persists_after_center_crossing();
        test_short_occlusion_withholds_publication_without_fresh_miss();
        test_camera_recoil_moves_true_error_but_visual_kick_does_not();
        test_horizontal_aim_bias_is_observation_only_and_recovers();
        std::cout << "cod_native_sustained_aimlab_simulator_tests PASS\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "cod_native_sustained_aimlab_simulator_tests FAIL: "
                  << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
