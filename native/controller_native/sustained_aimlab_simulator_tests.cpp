#include "sustained_aimlab_simulator.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
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
    const ScenarioScript script = stationary_script(80, {10.0, 0.0});
    ControllerStep controller = [](const ControllerObservation& input) {
        ControllerStepResult output;
        output.target_observed = input.target_present;
        output.tracker_reliable = input.target_present;
        output.bodylock_mode = input.target_present && input.now_ms >= 5;
        output.predicted_terminal_error_px = {-4.0, 0.0};
        output.radial_closing_velocity_px_per_sec = 75.0;
        return output;
    };
    const BenchmarkResult result = run_simulation(
        script, ManualProfile::Pure, controller);
    require(result.handoff_count == 1,
            "ADS-to-BodyLock transition must be counted once");
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

}  // namespace

int main() {
    try {
        test_runner_executes_exact_duration_and_preserves_identity();
        test_miss_respects_deadline_and_tracking_is_exactly_1000ms();
        test_virtual_camera_x_and_y_signs_close_error();
        test_virtual_camera_can_delay_delivered_control_without_delaying_controller();
        test_closed_loop_score_ordering();
        test_same_script_is_reused_for_pure_and_mixed_runs();
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
        std::cout << "cod_native_sustained_aimlab_simulator_tests PASS\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "cod_native_sustained_aimlab_simulator_tests FAIL: "
                  << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
